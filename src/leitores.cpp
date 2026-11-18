// leitores.cpp — implementação dos leitores. Usa só a API HID que já vem no Windows
// (hid.dll + setupapi.dll), sem bibliotecas de terceiros.
#include "leitores.h"

extern "C" {
#include <hidsdi.h>       // HidD_*: atributos, feature reports
#include <hidpi.h>        // HidP_GetCaps: "ficha técnica" de cada canal HID
}
#include <setupapi.h>     // SetupDi*: listar dispositivos plugados

#include <algorithm>
#include <cwctype>
#include <set>
#include <utility>

// ===========================================================================
// Listagem de dispositivos HID
// ===========================================================================
// Um receptor USB aparece no Windows como VÁRIOS "canais" (coleções HID):
// um pro movimento do mouse, um pra teclas extras, um de status do fabricante...
// Cada canal tem um "usage page" que diz pra que ele serve. A bateria fica
// num canal de fabricante (0x000A no Xenta, 0xFFFF no Rongyuan).
struct InfoHid {
    std::wstring caminho;        // "endereço" do canal, usado pra abrir
    USHORT vid = 0, pid = 0;     // Vendor ID e Product ID
    USHORT usagePage = 0, usage = 0;
    USHORT tamEntrada = 0;       // tamanho das mensagens que o canal manda (com o byte do ID)
    USHORT tamFeature = 0;       // tamanho dos "feature reports" (pergunta/resposta)
    int interfaceNum = -1;       // número da interface USB (o "mi_02" no caminho)
    std::wstring produto;        // nome que o próprio receptor informa
};

// Acha "mi_XX" no caminho do dispositivo e devolve XX (em hexadecimal).
static int InterfaceDoCaminho(const std::wstring& caminho) {
    std::wstring baixo = caminho;
    for (auto& c : baixo) c = (wchar_t)towlower(c);
    size_t p = baixo.find(L"mi_");
    if (p == std::wstring::npos || p + 5 > baixo.size()) return -1;
    return (int)wcstol(baixo.substr(p + 3, 2).c_str(), nullptr, 16);
}

static std::vector<InfoHid> ListarHid() {
    std::vector<InfoHid> lista;
    GUID guidHid;
    HidD_GetHidGuid(&guidHid);                                        // "categoria HID" do Windows
    HDEVINFO conjunto = SetupDiGetClassDevsW(&guidHid, nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);  // só os plugados
    if (conjunto == INVALID_HANDLE_VALUE) return lista;

    SP_DEVICE_INTERFACE_DATA iface{};
    iface.cbSize = sizeof(iface);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(conjunto, nullptr, &guidHid, i, &iface); ++i) {
        DWORD precisa = 0;
        SetupDiGetDeviceInterfaceDetailW(conjunto, &iface, nullptr, 0, &precisa, nullptr);  // pergunta o tamanho
        std::vector<BYTE> buf(precisa);
        auto* det = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buf.data());
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(conjunto, &iface, det, precisa, nullptr, nullptr)) continue;

        InfoHid info;
        info.caminho = det->DevicePath;
        info.interfaceNum = InterfaceDoCaminho(info.caminho);
        // Acesso 0 = "só quero olhar a ficha": funciona até em canais que o Windows trava (teclado/mouse).
        HANDLE h = CreateFileW(det->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        HIDD_ATTRIBUTES attr{};
        attr.Size = sizeof(attr);
        if (HidD_GetAttributes(h, &attr)) {
            info.vid = attr.VendorID;
            info.pid = attr.ProductID;
        }
        PHIDP_PREPARSED_DATA pp = nullptr;
        if (HidD_GetPreparsedData(h, &pp)) {
            HIDP_CAPS caps{};
            if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
                info.usagePage = caps.UsagePage;
                info.usage = caps.Usage;
                info.tamEntrada = caps.InputReportByteLength;
                info.tamFeature = caps.FeatureReportByteLength;
            }
            HidD_FreePreparsedData(pp);
        }
        wchar_t nome[128] = {};
        if (HidD_GetProductString(h, nome, sizeof(nome))) info.produto = nome;
        CloseHandle(h);
        lista.push_back(info);
    }
    SetupDiDestroyDeviceInfoList(conjunto);
    return lista;
}

// ===========================================================================
// 1) Xenta — o receptor TRANSMITE: [03][modelo][40/41][estado][nível]
// ===========================================================================
// estado: 1 = na bateria, 2 = cheio, 3 = carregando (nível congelado)
constexpr USHORT XENTA_VID = 0x1D57;
constexpr USHORT XENTA_PAGINA_STATUS = 0x000A;
static const std::set<USHORT> XENTA_PIDS_CABO = {0xFA55, 0xFA56, 0xFA61, 0xFA50, 0x2155};

struct ModeloXenta { int codigo; const wchar_t* nome; bool emDezenas; };
static const ModeloXenta XENTA_MODELOS[] = {
    {0x55, L"Attack Shark X11", false},    {0x07, L"Attack Shark X11 SE", false},
    {0xBE, L"Attack Shark X11 Pro", false}, {0x4D, L"Attack Shark X3", false},
    {0x4E, L"Attack Shark X3 Max", false}, {0x4F, L"Attack Shark X3 Pro", false},
    {0xE5, L"Attack Shark V3 Pro", false}, {0x24, L"Attack Shark V6", false},
    {0x85, L"Attack Shark X6", false},     {0x10, L"Attack Shark X6 / R1", true},  // manda 0..10
};

static const ModeloXenta* AcharModelo(int codigo) {
    for (const auto& m : XENTA_MODELOS)
        if (m.codigo == codigo) return &m;
    return nullptr;
}

bool InterpretarQuadroXenta(const uint8_t* d, size_t n, int& modelo, int& estado, int& nivel) {
    if (n < 5 || d[0] != 0x03) return false;              // não é o relatório 3
    if (d[2] != 0x40 && d[2] != 0x41) return false;       // não é evento de bateria
    modelo = d[1];
    estado = d[3];
    nivel = d[4];
    return true;
}

int ConverterNivelXenta(int modelo, int nivel, int forcarDezenas) {
    const ModeloXenta* m = AcharModelo(modelo);
    bool dezenas = m ? m->emDezenas : false;
    if (forcarDezenas == 0 || forcarDezenas == 1) dezenas = forcarDezenas == 1;  // escolha do usuário vence
    if (dezenas) return (nivel >= 0 && nivel <= 10) ? nivel * 10 : -1;
    return (nivel >= 0 && nivel <= 100) ? nivel : -1;
}

class LeitorXenta : public Leitor {
public:
    LeitorXenta(InfoHid info, int forcarDezenas) : info_(std::move(info)), forcar_(forcarDezenas) {
        parar_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);             // "sinal de parar", começa desligado
        thread_ = CreateThread(nullptr, 0, &LeitorXenta::Ponte, this, 0, nullptr);  // começa a escutar
    }
    ~LeitorXenta() override {
        SetEvent(parar_);                                  // pede pra thread terminar
        WaitForSingleObject(thread_, 3000);                // espera até 3 s
        CloseHandle(thread_);
        CloseHandle(parar_);
    }

    bool Ler(Leitura& s) override {
        std::vector<uint8_t> ultimo, penultimo;
        double quando;
        {
            Segurando t(trava_);                           // copia os dados com segurança
            if (perdido_) return false;                    // receptor sumiu
            ultimo = ultimo_;
            penultimo = penultimo_;
            quando = ultimoEm_;
        }
        bool cabo = XENTA_PIDS_CABO.count(info_.pid) > 0;
        s = Leitura{};
        s.momento = Agora();
        s.conexao = cabo ? L"cabo USB" : L"receptor 2.4G";
        int modelo, estado, nivel;
        if (ultimo.empty() || !InterpretarQuadroXenta(ultimo.data(), ultimo.size(), modelo, estado, nivel)) {
            s.carregando = cabo;                           // nada chegou ainda
            s.dormindo = !cabo;
            return true;
        }
        const ModeloXenta* m = AcharModelo(modelo);
        if (m) s.modelo = m->nome;
        else {
            wchar_t nome[64];
            swprintf(nome, 64, L"Mouse Xenta (modelo 0x%02X)", modelo);
            s.modelo = nome;
        }
        if (s.momento - quando > 15.0) {                   // 15 s sem mensagem: dormiu/desligou
            s.percentual = ultimoPct_;
            s.dormindo = true;
            return true;
        }
        if (cabo || estado == 2 || estado == 3) {          // no cabo: o nível fica congelado
            s.carregando = true;
            if (estado == 2) ultimoPct_ = 100;
            s.percentual = (estado == 2) ? 100 : ultimoPct_;
            return true;
        }
        // Ao reconectar, a 1ª mensagem costuma dizer 100% (mentira).
        // Só confiamos quando duas mensagens seguidas são iguais.
        if (ultimo != penultimo) {
            s.percentual = ultimoPct_;
            return true;
        }
        int pct = ConverterNivelXenta(modelo, nivel, forcar_);
        if (pct >= 0) ultimoPct_ = pct;
        s.percentual = pct >= 0 ? pct : ultimoPct_;
        return true;
    }

private:
    static DWORD WINAPI Ponte(LPVOID eu) {                 // o Windows chama uma função "solta"; ela chama o método
        static_cast<LeitorXenta*>(eu)->Escutar();
        return 0;
    }

    void Escutar() {
        int falhasAbrir = 0;
        while (WaitForSingleObject(parar_, 0) == WAIT_TIMEOUT) {          // enquanto não mandaram parar
            HANDLE h = CreateFileW(info_.caminho.c_str(), GENERIC_READ,     // só leitura: não briga com o app oficial
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_OVERLAPPED, nullptr);          // "overlapped" = leitura que não trava
            if (h == INVALID_HANDLE_VALUE) {
                if (++falhasAbrir >= 3) {                                   // 3 falhas seguidas: receptor sumiu
                    Segurando t(trava_);
                    perdido_ = true;
                    return;
                }
                WaitForSingleObject(parar_, 2000);
                continue;
            }
            falhasAbrir = 0;
            OVERLAPPED ov{};
            ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);        // acende quando chegar mensagem
            std::vector<uint8_t> buf(std::max<USHORT>(info_.tamEntrada, 64));
            bool parando = false;
            for (;;) {
                ResetEvent(ov.hEvent);
                DWORD lidos = 0;
                if (!ReadFile(h, buf.data(), info_.tamEntrada, nullptr, &ov) &&
                    GetLastError() != ERROR_IO_PENDING) break;              // erro de verdade (desplugou)
                HANDLE espera[2] = {ov.hEvent, parar_};
                DWORD r = WaitForMultipleObjects(2, espera, FALSE, INFINITE);  // dorme até mensagem OU "parar"
                if (r == WAIT_OBJECT_0 + 1) {                               // mandaram parar
                    CancelIo(h);
                    GetOverlappedResult(h, &ov, &lidos, TRUE);
                    parando = true;
                    break;
                }
                if (!GetOverlappedResult(h, &ov, &lidos, FALSE)) break;     // leitura falhou (desplugou)
                int modelo, estado, nivel;
                if (!InterpretarQuadroXenta(buf.data(), lidos, modelo, estado, nivel)) continue;
                Segurando t(trava_);                                         // guarda a mensagem
                penultimo_ = ultimo_;
                ultimo_.assign(buf.begin(), buf.begin() + 5);
                ultimoEm_ = Agora();
            }
            CloseHandle(ov.hEvent);
            CloseHandle(h);
            if (parando) return;
            WaitForSingleObject(parar_, 2000);                               // espera e tenta reabrir
        }
    }

    InfoHid info_;
    int forcar_;
    Trava trava_;
    std::vector<uint8_t> ultimo_, penultimo_;   // as duas mensagens mais recentes
    double ultimoEm_ = 0;
    int ultimoPct_ = -1;                        // último % confirmado (mostrado quando dorme)
    bool perdido_ = false;
    HANDLE parar_ = nullptr, thread_ = nullptr;
};

// ===========================================================================
// 2) Rongyuan — PERGUNTA [00][F7][00...], RESPOSTA [id][ok][% teclado][% mouse][tecl. off][mouse off]
// ===========================================================================
static const std::set<uint32_t> RONGYUAN_RECEPTORES = {   // (VID << 16) | PID
    0x0DB0197E, 0x0DB08E9A, 0x145F0376, 0x145F038D, 0x14A5C045, 0x25AA2008, 0x25AA4008, 0x25AA4018,
    0x31514011, 0x31514014, 0x31514017, 0x31514021, 0x31514027, 0x3151402D, 0x31514038, 0x31515006,
    0x31515007, 0x31515008, 0x31515020, 0x31515026, 0x3151502B, 0x31515031, 0x31515037, 0x31515038,
    0x3151503D, 0x3151503F, 0x31515040, 0x31515042, 0x31515045, 0x3151504B, 0x3151504E, 0x31515050,
    0x31515052, 0x31515055, 0x3151505A, 0x342DE3E0, 0x342DE3E1, 0x342DE3E2, 0x342DE3E6, 0x342DE3E7,
    0x342DE3E8, 0x342DE3F5, 0x342DE3F6, 0x342DE3F7, 0x342DE3FB, 0x342DE3FC, 0x342DE3FD, 0x347AA302,
    0x347AA305, 0x374AA205, 0x374AA217, 0x374AA220, 0x374AA223, 0x374AA226, 0x374AA229, 0x374AA234,
    0x374AA237, 0x374AA251, 0x374AA263, 0x374AA268, 0x374AA311, 0x374AA314, 0x374AA319, 0x374AA322,
    0x374AA329, 0x374AA355, 0x379A1804, 0x379A1807, 0x38A90002, 0x38EE0000, 0x38EE0002, 0x38EE0039,
    0x391D1A04, 0x391D1A08, 0x391D1A0E, 0x391D1A13, 0x391D1A25, 0x391D1A2B, 0x391D1A34, 0x391D2017,
    0x391D2A05,
};

int InterpretarRespostaRongyuan(const uint8_t* r, size_t n) {
    if (n < 7) return -1;
    if ((r[2] | r[3] | r[4] | r[5] | r[6]) == 0) return -1;   // resposta "vazia": tentar de novo
    if (r[5] != 0) return -1;                                   // mouse desconectado
    return (r[3] >= 1 && r[3] <= 100) ? r[3] : -1;              // byte 3 = % do mouse
}

class LeitorRongyuan : public Leitor {
public:
    explicit LeitorRongyuan(InfoHid info) : info_(std::move(info)) {
        bool generico = info_.produto.empty() || info_.produto.find(L"Receiver") != std::wstring::npos ||
                        info_.produto.find(L"2.4G") != std::wstring::npos;
        if (info_.vid == 0x3151 && info_.pid == 0x402D) modelo_ = L"Attack Shark R3";
        else modelo_ = generico ? L"Mouse sem fio" : info_.produto;
    }

    bool Ler(Leitura& s) override {
        s = Leitura{};
        s.momento = Agora();
        s.modelo = modelo_;
        HANDLE h = CreateFileW(info_.caminho.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)               // sem permissão de escrita? feature reports funcionam com acesso 0
            h = CreateFileW(info_.caminho.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false; // receptor sumiu
        // Vantagem do C++ aqui: o Windows informa o tamanho EXATO do relatório (tamFeature),
        // então não precisamos ficar chutando tamanhos como na versão Python.
        std::vector<uint8_t> buf(info_.tamFeature);
        bool desconectado = false;
        for (int tentativa = 0; tentativa < 3; ++tentativa) {
            if (tentativa) Sleep(100);
            std::fill(buf.begin(), buf.end(), 0);
            buf[1] = 0xF7;                                       // o comando "como está a bateria?"
            if (!HidD_SetFeature(h, buf.data(), (ULONG)buf.size())) break;
            Sleep(20);                                           // 20 ms pro receptor responder
            std::fill(buf.begin(), buf.end(), 0);                // buf[0] = 0 = ID do relatório que queremos
            if (!HidD_GetFeature(h, buf.data(), (ULONG)buf.size())) break;
            int pct = InterpretarRespostaRongyuan(buf.data(), buf.size());
            if (pct >= 0) {
                ultimoPct_ = pct;
                s.percentual = pct;
                CloseHandle(h);
                return true;
            }
            if (buf.size() >= 6 && buf[5] != 0) { desconectado = true; break; }
        }
        CloseHandle(h);
        s.percentual = ultimoPct_;
        s.dormindo = true;                                       // não respondeu: mouse dormindo/desligado
        (void)desconectado;
        return true;
    }

private:
    InfoHid info_;
    std::wstring modelo_;
    int ultimoPct_ = -1;
};

// ===========================================================================
// 3) Mouse de mentira: descarrega até 5%, vai pro cabo, enche. 1 s real = 1 min "fingido".
// ===========================================================================
class LeitorDemo : public Leitor {
public:
    LeitorDemo() { comeco_ = t0_ = Agora(); }
    bool Ler(Leitura& s) override {
        double agora = Agora();
        double passados = agora - comeco_;
        s = Leitura{};
        s.modelo = L"Mouse de demonstração";
        s.momento = t0_ + (agora - t0_) * 60.0;              // relógio acelerado (nunca volta)
        double fimDescarga = (inicio_ - 5) * velocidade_;
        if (passados < fimDescarga) {                         // fase 1: descarregando
            s.percentual = inicio_ - (int)(passados / velocidade_);
            s.conexao = L"simulado";
        } else if (passados < fimDescarga + segundosCarga_) { // fase 2: no cabo (% congelado)
            s.percentual = 5;
            s.carregando = true;
            s.conexao = L"cabo (simulado)";
        } else if (passados < fimDescarga + segundosCarga_ + 5) {  // fase 3: cheio
            s.percentual = 100;
            s.carregando = true;
            s.conexao = L"cabo (simulado)";
        } else {                                              // recomeça
            comeco_ = agora;
            s.percentual = inicio_;
            s.conexao = L"simulado";
        }
        return true;
    }
private:
    double comeco_, t0_;
    int inicio_ = 30;
    double velocidade_ = 2.0;       // segundos reais por 1%
    double segundosCarga_ = 45.0;   // segundos reais no cabo até encher
};

// ===========================================================================
// Detecção e diagnóstico
// ===========================================================================
std::unique_ptr<Leitor> DetectarLeitor(int forcarDezenas) {
    auto lista = ListarHid();
    std::vector<InfoHid> xenta;
    for (const auto& d : lista)
        if (d.vid == XENTA_VID && d.usagePage == XENTA_PAGINA_STATUS && d.tamEntrada >= 5) xenta.push_back(d);
    std::stable_sort(xenta.begin(), xenta.end(), [](const InfoHid& a, const InfoHid& b) {
        return XENTA_PIDS_CABO.count(a.pid) < XENTA_PIDS_CABO.count(b.pid);   // receptor 2.4G antes do cabo
    });
    if (!xenta.empty()) return std::make_unique<LeitorXenta>(xenta.front(), forcarDezenas);

    for (const auto& d : lista) {
        uint32_t chave = ((uint32_t)d.vid << 16) | d.pid;
        if (RONGYUAN_RECEPTORES.count(chave) && d.interfaceNum == 2 && d.usagePage == 0xFFFF &&
            d.usage == 0x0002 && d.tamFeature >= 7)
            return std::make_unique<LeitorRongyuan>(d);
    }
    return nullptr;
}

std::unique_ptr<Leitor> CriarLeitorDemo() { return std::make_unique<LeitorDemo>(); }

std::wstring RelatorioDiagnostico() {
    std::wstring out = L"=== Dispositivos HID encontrados ===\r\n";
    auto lista = ListarHid();
    wchar_t linha[512];
    for (const auto& d : lista) {
        swprintf(linha, 512, L"%04X:%04X  iface=%d  page=%04X usage=%04X  in=%u feat=%u  %ls\r\n",
                 d.vid, d.pid, d.interfaceNum, d.usagePage, d.usage, d.tamEntrada, d.tamFeature,
                 d.produto.c_str());
        out += linha;
    }
    bool algum = false;
    for (const auto& d : lista) {
        if (d.vid != XENTA_VID || d.usagePage != XENTA_PAGINA_STATUS) continue;
        algum = true;
        swprintf(linha, 512, L"\r\n=== Escutando %04X:%04X por 8 s ===\r\n", d.vid, d.pid);
        out += linha;
        HANDLE h = CreateFileW(d.caminho.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            swprintf(linha, 512, L"erro ao abrir: %lu\r\n", GetLastError());
            out += linha;
            continue;
        }
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::vector<uint8_t> buf(std::max<USHORT>(d.tamEntrada, 64));
        ULONGLONG fim = GetTickCount64() + 8000;
        while (GetTickCount64() < fim) {
            ResetEvent(ov.hEvent);
            DWORD lidos = 0;
            if (!ReadFile(h, buf.data(), d.tamEntrada, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) break;
            DWORD resto = (DWORD)(fim - GetTickCount64());
            if (WaitForSingleObject(ov.hEvent, resto) != WAIT_OBJECT_0) {    // acabou o tempo
                CancelIo(h);
                GetOverlappedResult(h, &ov, &lidos, TRUE);
                break;
            }
            if (!GetOverlappedResult(h, &ov, &lidos, FALSE)) break;
            for (DWORD i = 0; i < lidos; ++i) {
                swprintf(linha, 16, L"%02X ", buf[i]);
                out += linha;
            }
            out += L"\r\n";
        }
        CloseHandle(ov.hEvent);
        CloseHandle(h);
    }
    if (!algum) out += L"\r\nNenhum receptor Xenta (1D57) com canal de status encontrado.\r\n";
    auto leitor = DetectarLeitor(-1);
    out += L"\r\nLeitor escolhido: ";
    out += leitor ? L"encontrado" : L"nenhum";
    out += L"\r\n";
    return out;
}
