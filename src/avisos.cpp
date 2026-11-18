// avisos.cpp
#include "avisos.h"

#include <winhttp.h>   // HTTP do próprio Windows (pra falar com o ntfy)

// ===========================================================================
// Celular (ntfy)
// ===========================================================================
// Coloca "\" antes de aspas e barras, pra o texto caber dentro de um JSON.
static std::string EscaparJson(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '"' || c == '\\') r += '\\';
        if (c == '\n') { r += "\\n"; continue; }
        r += c;
    }
    return r;
}

struct PedidoCelular {
    std::wstring servidor;
    std::string corpo;
};

static DWORD WINAPI ThreadCelular(LPVOID p) {
    PedidoCelular* ped = static_cast<PedidoCelular*>(p);
    // Quebra "https://ntfy.sh" em partes: protocolo, servidor, porta, caminho.
    URL_COMPONENTS url{};
    url.dwStructSize = sizeof(url);
    wchar_t host[256] = {}, caminho[512] = {};
    url.lpszHostName = host;
    url.dwHostNameLength = 256;
    url.lpszUrlPath = caminho;
    url.dwUrlPathLength = 512;
    if (WinHttpCrackUrl(ped->servidor.c_str(), 0, 0, &url)) {
        HINTERNET sessao = WinHttpOpen(L"SharkInfo/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        HINTERNET con = sessao ? WinHttpConnect(sessao, host, url.nPort, 0) : nullptr;
        bool https = url.nScheme == INTERNET_SCHEME_HTTPS;
        HINTERNET req = con ? WinHttpOpenRequest(con, L"POST", caminho[0] ? caminho : L"/", nullptr,
                                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 https ? WINHTTP_FLAG_SECURE : 0)
                            : nullptr;
        if (req) {
            const wchar_t* cab = L"Content-Type: application/json\r\n";
            if (WinHttpSendRequest(req, cab, (DWORD)-1, (LPVOID)ped->corpo.data(), (DWORD)ped->corpo.size(),
                                   (DWORD)ped->corpo.size(), 0))
                WinHttpReceiveResponse(req, nullptr);   // espera o servidor confirmar
            WinHttpCloseHandle(req);
        }
        if (con) WinHttpCloseHandle(con);
        if (sessao) WinHttpCloseHandle(sessao);
    }
    delete ped;
    return 0;
}

void EnviarCelular(const Config& cfg, const std::wstring& titulo, const std::wstring& msg, int prioridade) {
    auto* ped = new PedidoCelular;
    ped->servidor = cfg.celularServidor;
    // O ntfy aceita JSON no endereço raiz; assim os acentos chegam certinhos (UTF-8).
    ped->corpo = "{\"topic\":\"" + EscaparJson(ParaUtf8(cfg.celularTopico)) +
                 "\",\"title\":\"" + EscaparJson(ParaUtf8(titulo)) +
                 "\",\"message\":\"" + EscaparJson(ParaUtf8(msg)) +
                 "\",\"priority\":" + std::to_string(prioridade) +
                 ",\"tags\":[\"mouse\",\"battery\"]}";
    HANDLE t = CreateThread(nullptr, 0, ThreadCelular, ped, 0, nullptr);
    if (t) CloseHandle(t);   // não precisamos esperar: a thread se vira sozinha
    else delete ped;
}

// ===========================================================================
// Faixa por cima do jogo
// ===========================================================================
// Estilos especiais da janela:
//   TOPMOST      -> sempre por cima
//   LAYERED      -> permite transparência
//   TRANSPARENT  -> cliques "atravessam" a faixa e vão pro jogo
//   TOOLWINDOW   -> não aparece na barra de tarefas nem no Alt+Tab
//   NOACTIVATE   -> nunca pega o foco (senão o jogo em tela cheia minimiza!)
// Funciona em jogos no modo "janela sem bordas". Em "tela cheia exclusiva",
// nada desenha por cima: aí valem a notificação e o celular.
constexpr UINT_PTR TIMER_ESCONDER = 1;

bool Faixa::Criar(HINSTANCE inst) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"SharkInfoFaixa";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
                                WS_EX_NOACTIVATE,
                            wc.lpszClassName, L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    SetLayeredWindowAttributes(hwnd_, 0, 235, LWA_ALPHA);     // 235/255 = levemente transparente
    return true;
}

void Faixa::Mostrar(const std::wstring& texto, COLORREF cor, int segundos, bool topo) {
    if (!hwnd_) return;
    texto_ = texto;
    cor_ = cor;
    UINT dpi = GetDpiForWindow(hwnd_);                         // escala pra telas com zoom (125%, 150%...)
    int w = MulDiv(560, dpi, 96), h = MulDiv(64, dpi, 96);
    int telaW = GetSystemMetrics(SM_CXSCREEN), telaH = GetSystemMetrics(SM_CYSCREEN);
    int x = (telaW - w) / 2;
    int y = topo ? MulDiv(40, dpi, 96) : telaH - h - MulDiv(90, dpi, 96);
    SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, w + 1, h + 1, h / 2, h / 2), FALSE);  // cantos arredondados
    // SWP_NOACTIVATE: aparece SEM roubar o foco do jogo.
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, nullptr, TRUE);                       // pede pra redesenhar
    SetTimer(hwnd_, TIMER_ESCONDER, segundos * 1000, nullptr);  // some sozinha depois
}

LRESULT CALLBACK Faixa::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {                                   // guarda o "this" dentro da janela
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* eu = reinterpret_cast<Faixa*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;                                   // reforço: clique nunca ativa
    case WM_TIMER:
        KillTimer(h, TIMER_ESCONDER);
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        HBRUSH pincel = CreateSolidBrush(eu ? eu->cor_ : RGB(0, 0, 0));
        FillRect(dc, &r, pincel);
        DeleteObject(pincel);
        int alturaFonte = -MulDiv(15, GetDpiForWindow(h), 72);  // 15 pt
        HFONT fonte = CreateFontW(alturaFonte, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                  CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HGDIOBJ antiga = SelectObject(dc, fonte);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        if (eu) DrawTextW(dc, eu->texto_.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, antiga);
        DeleteObject(fonte);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ===========================================================================
// O porteiro
// ===========================================================================
void GerenciadorAvisos::Disparar(const std::wstring& titulo, const std::wstring& msg, COLORREF cor,
                                 int prioridade) {
    if (cfg_.notificacaoWindows) notificar_(titulo, msg, prioridade >= 4);
    if (cfg_.avisoNaTela) tela_(msg, cor);
    if (cfg_.celularAtivo) EnviarCelular(cfg_, titulo, msg, prioridade);
}

void GerenciadorAvisos::Processar(const Leitura& l, const std::wstring& textoRestante) {
    if (l.dormindo || (l.percentual < 0 && !l.carregando)) return;   // sem informação confiável
    if (l.carregando) {
        avisouAviso_ = avisouCritico_ = false;                         // plugou: zera os avisos de bateria baixa
        if (l.percentual == 100 && !avisouCheio_ && cfg_.avisarCargaCompleta) {
            avisouCheio_ = true;
            Disparar(L"Mouse carregado", L"Bateria do mouse em 100%, pode tirar do cabo", RGB(46, 125, 50), 3);
        }
        return;
    }
    avisouCheio_ = false;
    int p = l.percentual;
    std::wstring extra = textoRestante.empty() ? L"" : L" (~" + textoRestante + L" restantes)";
    wchar_t msg[160];
    if (p <= cfg_.criticoPercentual && !avisouCritico_) {
        avisouCritico_ = avisouAviso_ = true;
        swprintf(msg, 160, L"Mouse com %d%%%ls, carregue agora!", p, extra.c_str());
        Disparar(L"Bateria do mouse CRÍTICA", msg, RGB(198, 40, 40), 5);
    } else if (p <= cfg_.avisoPercentual && !avisouAviso_) {
        avisouAviso_ = true;
        swprintf(msg, 160, L"Mouse com %d%%%ls", p, extra.c_str());
        Disparar(L"Bateria do mouse baixa", msg, RGB(230, 81, 0), 4);
    }
    // "Folga" de 5% pra não reavisar se o valor ficar oscilando no limite.
    if (p >= cfg_.avisoPercentual + 5) avisouAviso_ = false;
    if (p >= cfg_.criticoPercentual + 5) avisouCritico_ = false;
}
