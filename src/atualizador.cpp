// atualizador.cpp
#include "atualizador.h"
#include "versao.h"

#include <bcrypt.h>     // SHA-256 do próprio Windows
#include <shellapi.h>   // SHQueryUserNotificationState (está jogando em tela cheia?)
#include <winhttp.h>    // HTTPS do próprio Windows

#include <cwctype>

namespace atualizador {

// ===========================================================================
// Funções puras
// ===========================================================================
// "v1.2.10" -> {1, 2, 10}. O que vem depois de "-" (ex.: "-dev", "-beta") conta como "menor".
static std::vector<int> Partes(const std::wstring& v, bool& temSufixo) {
    std::vector<int> r;
    size_t i = (!v.empty() && (v[0] == L'v' || v[0] == L'V')) ? 1 : 0;
    temSufixo = false;
    int atual = 0;
    bool temNumero = false;
    for (; i < v.size(); ++i) {
        wchar_t c = v[i];
        if (c >= L'0' && c <= L'9') {
            atual = atual * 10 + (c - L'0');
            temNumero = true;
        } else if (c == L'.') {
            r.push_back(atual);
            atual = 0;
            temNumero = false;
        } else {                                  // "-dev", "+build"...
            temSufixo = true;
            break;
        }
    }
    if (temNumero || !r.empty()) r.push_back(atual);
    return r;
}

int CompararVersoes(const std::wstring& a, const std::wstring& b) {
    bool sa, sb;
    auto pa = Partes(a, sa), pb = Partes(b, sb);
    size_t n = std::max(pa.size(), pb.size());
    for (size_t i = 0; i < n; ++i) {             // compara número por número; faltando = 0
        int x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    if (sa != sb) return sa ? -1 : 1;            // 1.0.0-dev < 1.0.0
    return 0;
}

// Lê uma string JSON começando logo depois da aspa de abertura. Entende \n, \", \uXXXX etc.
static bool LerStringJson(const std::string& js, size_t& i, std::string& saida) {
    saida.clear();
    auto utf8 = [&saida](unsigned cp) {          // escreve um caractere Unicode em UTF-8
        if (cp < 0x80) saida += (char)cp;
        else if (cp < 0x800) { saida += (char)(0xC0 | (cp >> 6)); saida += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            saida += (char)(0xE0 | (cp >> 12));
            saida += (char)(0x80 | ((cp >> 6) & 0x3F));
            saida += (char)(0x80 | (cp & 0x3F));
        } else {
            saida += (char)(0xF0 | (cp >> 18));
            saida += (char)(0x80 | ((cp >> 12) & 0x3F));
            saida += (char)(0x80 | ((cp >> 6) & 0x3F));
            saida += (char)(0x80 | (cp & 0x3F));
        }
    };
    auto hex4 = [&js](size_t p, unsigned& v) {
        if (p + 4 > js.size()) return false;
        v = (unsigned)strtoul(js.substr(p, 4).c_str(), nullptr, 16);
        return true;
    };
    while (i < js.size()) {
        char c = js[i++];
        if (c == '"') return true;               // fim da string
        if (c != '\\') { saida += c; continue; }
        if (i >= js.size()) return false;
        char e = js[i++];
        switch (e) {
        case 'n': saida += '\n'; break;
        case 'r': break;                          // ignora \r (o Windows mostra bem só com \n)
        case 't': saida += '\t'; break;
        case 'b': case 'f': break;
        case 'u': {
            unsigned cp;
            if (!hex4(i, cp)) return false;
            i += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= js.size() && js[i] == '\\' && js[i + 1] == 'u') {
                unsigned baixo;                   // emoji etc.: vem em dois pedaços ("par substituto")
                if (hex4(i + 2, baixo) && baixo >= 0xDC00 && baixo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (baixo - 0xDC00);
                    i += 6;
                }
            }
            utf8(cp);
            break;
        }
        default: saida += e;                      // \" \\ \/
        }
    }
    return false;
}

// Acha "chave": "valor" a partir da posição 'inicio'. Devolve a posição depois do valor (ou npos).
static size_t AcharTexto(const std::string& js, const std::string& chave, std::string& valor, size_t inicio = 0) {
    std::string alvo = "\"" + chave + "\"";
    for (size_t p = js.find(alvo, inicio); p != std::string::npos; p = js.find(alvo, p + 1)) {
        size_t i = p + alvo.size();
        while (i < js.size() && (js[i] == ' ' || js[i] == '\n' || js[i] == '\r' || js[i] == '\t')) ++i;
        if (i >= js.size() || js[i] != ':') continue;
        ++i;
        while (i < js.size() && (js[i] == ' ' || js[i] == '\n' || js[i] == '\r' || js[i] == '\t')) ++i;
        if (i >= js.size() || js[i] != '"') continue;   // valor não é texto (null, número...)
        ++i;
        if (LerStringJson(js, i, valor)) return i;
    }
    return std::string::npos;
}

static std::wstring ParaLargo(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static bool TerminaCom(const std::string& s, const std::string& fim) {
    return s.size() >= fim.size() && s.compare(s.size() - fim.size(), fim.size(), fim) == 0;
}

bool InterpretarRelease(const std::string& json, InfoRelease& r) {
    r = InfoRelease{};
    std::string tag, corpo, url;
    if (AcharTexto(json, "tag_name", tag) == std::string::npos) return false;
    r.versao = ParaLargo(tag[0] == 'v' || tag[0] == 'V' ? tag.substr(1) : tag);
    if (AcharTexto(json, "body", corpo) != std::string::npos) r.notas = ParaLargo(corpo);
    // Os anexos da release: procura os links que terminam no nome certo.
    for (size_t p = 0; (p = AcharTexto(json, "browser_download_url", url, p)) != std::string::npos;) {
        if (TerminaCom(url, "/SharkInfo.exe")) r.urlExe = url;
        else if (TerminaCom(url, "/SharkInfo.exe.sha256")) r.urlSha = url;
    }
    return !r.versao.empty() && !r.urlExe.empty() && !r.urlSha.empty();
}

std::string LerHashDoArquivoSha(const std::string& conteudo) {
    std::string h;
    for (char c : conteudo) {
        if (isxdigit((unsigned char)c)) h += (char)tolower((unsigned char)c);
        else if (!h.empty()) break;               // terminou o bloco de hexadecimal
        if (h.size() == 64) break;
    }
    return h.size() == 64 ? h : "";
}

std::string Sha256Hex(const std::wstring& arquivo) {
    HANDLE f = CreateFileW(arquivo.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return "";
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string resultado;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<BYTE> buf(1 << 16);
        DWORD lidos = 0;
        bool ok = true;
        while (ReadFile(f, buf.data(), (DWORD)buf.size(), &lidos, nullptr) && lidos > 0)
            if (BCryptHashData(hash, buf.data(), lidos, 0) != 0) { ok = false; break; }   // "mói" o arquivo aos pedaços
        BYTE dig[32];
        if (ok && BCryptFinishHash(hash, dig, sizeof(dig), 0) == 0) {
            char hex[3];
            for (BYTE b : dig) {
                snprintf(hex, sizeof(hex), "%02x", b);
                resultado += hex;
            }
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return resultado;
}

// ===========================================================================
// Internet (WinHTTP)
// ===========================================================================
// Baixa uma URL. Se 'arquivo' não for vazio, grava lá; senão devolve em 'corpo'.
static bool Baixar(const std::string& urlUtf8, std::string* corpo, const std::wstring& arquivo) {
    std::wstring url = ParaLargo(urlUtf8);
    URL_COMPONENTS u{};
    u.dwStructSize = sizeof(u);
    wchar_t host[256] = {}, caminho[2048] = {};
    u.lpszHostName = host;
    u.dwHostNameLength = 256;
    u.lpszUrlPath = caminho;
    u.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &u)) return false;
    // O GitHub exige um "User-Agent" (nome de quem está pedindo).
    std::wstring agente = L"SharkInfo/" SHARKINFO_LARGO(SHARKINFO_VERSAO);
    HINTERNET s = WinHttpOpen(agente.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 10000, 10000, 15000, 60000);
    HINTERNET c = WinHttpConnect(s, host, u.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", caminho, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                    : nullptr;
    bool ok = false;
    HANDLE f = INVALID_HANDLE_VALUE;
    // Só a API do GitHub precisa do cabeçalho "quero JSON"; o download do arquivo vai sem nada.
    bool api = urlUtf8.find("api.github.com") != std::string::npos;
    const wchar_t* cab = api ? L"Accept: application/vnd.github+json\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS;
    if (r && WinHttpSendRequest(r, cab, api ? (DWORD)-1 : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, nullptr)) {      // o WinHTTP segue redirecionamentos sozinho (o GitHub usa)
        DWORD status = 0, tam = sizeof(status);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &tam, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            if (!arquivo.empty())
                f = CreateFileW(arquivo.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            ok = arquivo.empty() || f != INVALID_HANDLE_VALUE;
            std::vector<char> buf(1 << 16);
            DWORD lidos = 0;
            while (ok && WinHttpReadData(r, buf.data(), (DWORD)buf.size(), &lidos) && lidos > 0) {
                if (corpo) corpo->append(buf.data(), lidos);
                DWORD escritos = 0;
                if (f != INVALID_HANDLE_VALUE && !WriteFile(f, buf.data(), lidos, &escritos, nullptr)) ok = false;
            }
        }
    }
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    if (!ok && !arquivo.empty()) DeleteFileW(arquivo.c_str());
    return ok;
}

// ===========================================================================
// O fluxo
// ===========================================================================
static Trava gTrava;
static HWND gJanela = nullptr;
static UINT gMsg = 0;
static Estado gEstado = Estado::Ocioso;
static InfoRelease gPendente;
static std::wstring gArquivoNovo;             // o .exe novo já baixado e conferido

std::wstring VersaoAtual() { return SHARKINFO_LARGO(SHARKINFO_VERSAO); }
bool EhVersaoDev() { return VersaoAtual().find(L"-dev") != std::wstring::npos; }

void Configurar(HWND janela, UINT msgAviso) {
    gJanela = janela;
    gMsg = msgAviso;
    if (EhVersaoDev()) gEstado = Estado::Dev;
}

Estado EstadoAtual() {
    Segurando t(gTrava);
    return gEstado;
}

InfoRelease Pendente() {
    Segurando t(gTrava);
    return gPendente;
}

static void Mudar(Estado e, bool manual) {
    {
        Segurando t(gTrava);
        gEstado = e;
    }
    PostMessageW(gJanela, gMsg, (WPARAM)e, manual ? 1 : 0);
}

struct Pedido { bool manual; };

static DWORD WINAPI ThreadProcurar(LPVOID p) {
    bool manual = static_cast<Pedido*>(p)->manual;
    delete static_cast<Pedido*>(p);
    std::string json;
    InfoRelease info;
    if (!Baixar("https://api.github.com/repos/" SHARKINFO_REPO "/releases/latest", &json, L"") ||
        !InterpretarRelease(json, info)) {
        Mudar(gArquivoNovo.empty() ? Estado::Erro : Estado::Pronta, manual);   // uma já baixada continua valendo
        return 0;
    }
    if (CompararVersoes(info.versao, VersaoAtual()) <= 0) {   // já estamos na mais nova
        Mudar(Estado::EmDia, manual);
        return 0;
    }
    std::wstring pasta = PastaDados() + L"\\atualizacao";
    CreateDirectoryW(pasta.c_str(), nullptr);
    std::wstring destino = pasta + L"\\SharkInfo-" + info.versao + L".exe";
    std::string shaTexto;
    if (!Baixar(info.urlSha, &shaTexto, L"")) {
        Mudar(Estado::Erro, manual);
        return 0;
    }
    std::string esperado = LerHashDoArquivoSha(shaTexto);
    // Já baixou essa versão antes (e está íntegra)? Não baixa de novo.
    bool jaTem = !esperado.empty() && Sha256Hex(destino) == esperado;
    if (!jaTem) {
        Mudar(Estado::Baixando, false);
        std::wstring parcial = destino + L".parcial";
        if (!Baixar(info.urlExe, nullptr, parcial)) {
            Mudar(Estado::Erro, manual);
            return 0;
        }
        if (esperado.empty() || Sha256Hex(parcial) != esperado) {   // impressão digital não bate: descarta
            DeleteFileW(parcial.c_str());
            Mudar(Estado::Erro, manual);
            return 0;
        }
        MoveFileExW(parcial.c_str(), destino.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    {
        Segurando t(gTrava);
        gPendente = info;
        gArquivoNovo = destino;
    }
    Mudar(Estado::Pronta, manual);
    return 0;
}

void Procurar(bool manual) {
    {
        Segurando t(gTrava);
        if (gEstado == Estado::Dev) {             // compilação de teste: não se atualiza
            if (manual) PostMessageW(gJanela, gMsg, (WPARAM)Estado::Dev, 1);
            return;
        }
        if (gEstado == Estado::Procurando || gEstado == Estado::Baixando) return;   // já está trabalhando
        if (gEstado == Estado::Pronta && !manual) return;   // já tem uma pronta esperando
        gEstado = Estado::Procurando;
    }
    HANDLE t = CreateThread(nullptr, 0, ThreadProcurar, new Pedido{manual}, 0, nullptr);
    if (t) CloseHandle(t);
}

bool Instalar(bool reiniciar) {
    std::wstring novo;
    {
        Segurando t(gTrava);
        if (gEstado != Estado::Pronta) return false;
        novo = gArquivoNovo;
    }
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring velho = std::wstring(exe) + L".old";
    DeleteFileW(velho.c_str());
    if (!MoveFileExW(exe, velho.c_str(), MOVEFILE_REPLACE_EXISTING)) return false;   // renomeia o que está aberto
    if (!MoveFileExW(novo.c_str(), exe, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        MoveFileExW(velho.c_str(), exe, 0);       // deu errado: desfaz
        return false;
    }
    {
        Segurando t(gTrava);
        gEstado = Estado::Ocioso;
        gArquivoNovo.clear();
    }
    if (reiniciar) {
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --pos-atualizacao " + std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    return true;
}

void LimparPosAtualizacao(DWORD pidAntigo) {
    if (pidAntigo) {                              // espera a versão antiga terminar de fechar (até 10 s)
        HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pidAntigo);
        if (p) {
            WaitForSingleObject(p, 10000);
            CloseHandle(p);
        }
    }
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring velho = std::wstring(exe) + L".old";
    for (int i = 0; i < 10 && GetFileAttributesW(velho.c_str()) != INVALID_FILE_ATTRIBUTES; ++i) {
        if (DeleteFileW(velho.c_str())) break;
        Sleep(300);
    }
}

bool UsuarioOcupado() {
    QUERY_USER_NOTIFICATION_STATE st;
    if (SUCCEEDED(SHQueryUserNotificationState(&st)) &&
        (st == QUNS_BUSY || st == QUNS_RUNNING_D3D_FULL_SCREEN || st == QUNS_PRESENTATION_MODE))
        return true;                              // tela cheia "de verdade" ou modo apresentação
    // Jogo em "janela sem bordas": a janela da frente cobre o monitor inteiro.
    HWND frente = GetForegroundWindow();
    if (!frente) return false;
    wchar_t classe[64] = {};
    GetClassNameW(frente, classe, 64);
    if (wcscmp(classe, L"Progman") == 0 || wcscmp(classe, L"WorkerW") == 0) return false;   // é a área de trabalho
    RECT jr;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetWindowRect(frente, &jr) || !GetMonitorInfoW(MonitorFromWindow(frente, MONITOR_DEFAULTTONEAREST), &mi))
        return false;
    return jr.left <= mi.rcMonitor.left && jr.top <= mi.rcMonitor.top && jr.right >= mi.rcMonitor.right &&
           jr.bottom >= mi.rcMonitor.bottom;
}

}  // namespace atualizador
