// polling.cpp — mede o polling rate do mouse pelo Raw Input do Windows.
#include "polling.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <map>

namespace polling {

// ---------------------------------------------------------------------------
// As contas
// ---------------------------------------------------------------------------
static constexpr double PAUSA_MS = 40;          // mais que isso sem aviso = o mouse parou (fecha o trecho)
static constexpr double TRECHO_MAX_MS = 500;    // trecho longo vira uma medida a cada meio segundo
static constexpr double TRECHO_MIN_MS = 100;    // trecho curto demais não é confiável
static constexpr int TRECHO_MIN_AVISOS = 25;
static constexpr size_t TRECHOS_GUARDADOS = 24;

void Medidor::Zerar() {
    n_ = 0;
    ultimo_ = -1;
    taxas_.clear();
}

// Fecha o trecho de movimento: taxa = avisos por segundo dentro dele.
bool Medidor::FecharTrecho() {
    bool valeu = false;
    double dur = ultimo_ - inicio_;
    if (n_ >= TRECHO_MIN_AVISOS && dur >= TRECHO_MIN_MS) {
        taxas_.push_back((n_ - 1) * 1000.0 / dur);
        if (taxas_.size() > TRECHOS_GUARDADOS) taxas_.erase(taxas_.begin());
        valeu = true;
    }
    n_ = 0;
    return valeu;
}

bool Medidor::Evento(double ms) {
    bool novo = false;
    if (ultimo_ >= 0 && ms - ultimo_ > PAUSA_MS) novo = FecharTrecho();   // parou e voltou a mexer
    if (n_ == 0) inicio_ = ms;
    ++n_;
    ultimo_ = ms;
    if (ultimo_ - inicio_ >= TRECHO_MAX_MS) novo = FecharTrecho() || novo;
    return novo;
}

int AjustarParaTaxaComum(double hz) {
    static const int comuns[] = {125, 250, 500, 1000, 2000, 4000, 8000};
    if (hz < 100 || hz > 9000) return (int)std::lround(hz);
    int melhor = comuns[0];
    double menor = 1e9;
    for (int c : comuns) {
        double distancia = std::fabs(std::log(hz / c));            // distância "em proporção": 700 fica mais perto de 500 que de 1000
        if (distancia < menor) {
            menor = distancia;
            melhor = c;
        }
    }
    return melhor;
}

int Medidor::Hz() const {
    if (taxas_.size() < 2) return 0;
    // Quando o mouse se mexe devagar, alguns avisos "faltam" (não houve movimento naquele instante), então
    // as medidas erram pra BAIXO. Por isso vale um valor alto do conjunto (percentil 75), não a média.
    std::vector<double> ordenadas = taxas_;
    std::sort(ordenadas.begin(), ordenadas.end());
    double alto = ordenadas[ordenadas.size() * 3 / 4];
    return AjustarParaTaxaComum(alto);
}

// ---------------------------------------------------------------------------
// A parte do Windows
// ---------------------------------------------------------------------------
static Trava gTrava;
static Resultado gResultado;
static std::wstring gArquivo;
static HWND gJanela = nullptr;
static HANDLE gThread = nullptr;
static HINSTANCE gInst = nullptr;
static volatile LONG gEventos = 0;               // total de avisos de mouse recebidos (diagnóstico)
static volatile LONG gTrechos = 0;

static double AgoraMs() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)freq.QuadPart;
}

// Cada mouse (receptor, mouse com fio...) tem o seu Medidor. O Attack Shark (VID 1D57) tem preferência.
struct Dispositivo {
    Medidor medidor;
    bool nosso = false;
};

static std::map<HANDLE, Dispositivo> gDispositivos;
static bool gViuNosso = false;

static bool EhAttackShark(HANDLE dispositivo) {
    UINT n = 0;
    GetRawInputDeviceInfoW(dispositivo, RIDI_DEVICENAME, nullptr, &n);
    if (n == 0 || n > 1024) return false;
    std::wstring nome(n, L'\0');
    if (GetRawInputDeviceInfoW(dispositivo, RIDI_DEVICENAME, nome.data(), &n) == (UINT)-1) return false;
    for (auto& c : nome) c = (wchar_t)towupper(c);
    return nome.find(L"VID_1D57") != std::wstring::npos;
}

static void Publicar(int hz) {
    double agora = Agora();
    bool mudou;
    {
        Segurando t(gTrava);
        mudou = gResultado.hz != hz;
        gResultado.hz = hz;
        gResultado.aoVivo = true;
        gResultado.momento = agora;
    }
    if (mudou && !gArquivo.empty())                          // guarda só quando muda (o polling quase nunca muda)
        if (FILE* f = _wfopen(gArquivo.c_str(), L"a")) {
            fprintf(f, "%.0f;%d\n", agora, hz);
            fclose(f);
        }
}

static void AoMover(HANDLE dispositivo, double ms) {
    auto it = gDispositivos.find(dispositivo);
    if (it == gDispositivos.end()) {
        Dispositivo d;
        d.nosso = EhAttackShark(dispositivo);
        it = gDispositivos.emplace(dispositivo, std::move(d)).first;
    }
    Dispositivo& d = it->second;
    if (d.nosso) gViuNosso = true;
    bool novo = d.medidor.Evento(ms);
    if (novo) InterlockedIncrement(&gTrechos);
    if (!novo || (!d.nosso && gViuNosso)) return;            // já achamos o Attack Shark: ignora os outros mouses
    int hz = d.medidor.Hz();
    if (hz > 0) Publicar(hz);
}

static LRESULT CALLBACK ProcPolling(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INPUT) {
        alignas(8) BYTE buf[256];
        UINT tam = sizeof(buf);
        UINT lidos = GetRawInputData((HRAWINPUT)lp, RIM_INPUT, buf, &tam, sizeof(RAWINPUTHEADER));
        if (lidos != (UINT)-1) {
            const RAWINPUT* ri = reinterpret_cast<const RAWINPUT*>(buf);
            if (ri->header.dwType == RIM_TYPEMOUSE) {
                InterlockedIncrement(&gEventos);
                if (!(ri->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) &&
                    (ri->data.mouse.lLastX != 0 || ri->data.mouse.lLastY != 0))   // só movimento (não clique/rodinha)
                    AoMover(ri->header.hDevice, AgoraMs());
            }
        }
        return DefWindowProcW(h, msg, wp, lp);               // o Windows pede pra repassar (ele limpa a fila)
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(h);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static DWORD WINAPI ThreadPolling(LPVOID) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = ProcPolling;
    wc.hInstance = gInst;
    wc.lpszClassName = L"SharkInfoPolling";
    RegisterClassW(&wc);
    // Janela comum, mas invisível e sem tamanho: existe apenas pra receber os avisos do Raw Input.
    // (Não uso a "só de mensagens" porque a escuta do mouse é mais garantida numa janela normal.)
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"SharkInfo polling", WS_POPUP, 0, 0, 0, 0, nullptr,
                             nullptr, gInst, nullptr);
    if (!h) return 1;
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;                                  // "controles genéricos"
    rid.usUsage = 0x02;                                      // ... tipo mouse
    rid.dwFlags = RIDEV_INPUTSINK;                           // recebe mesmo com outra janela na frente (jogo)
    rid.hwndTarget = h;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        DestroyWindow(h);
        return 1;
    }
    gJanela = h;
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    gJanela = nullptr;
    return 0;
}

bool Iniciar(HINSTANCE inst, const std::wstring& pasta) {
    if (gThread) return true;
    gInst = inst;
    gArquivo = pasta + L"\\polling.csv";
    if (FILE* f = _wfopen(gArquivo.c_str(), L"r")) {          // última medição de outra vez que o app abriu
        double m;
        int hz;
        while (fscanf(f, "%lf;%d", &m, &hz) == 2) {
            Segurando t(gTrava);
            gResultado.hz = hz;
            gResultado.momento = m;
            gResultado.aoVivo = false;
        }
        fclose(f);
    }
    gThread = CreateThread(nullptr, 0, ThreadPolling, nullptr, 0, nullptr);
    return gThread != nullptr;
}

void Parar() {
    if (!gThread) return;
    if (gJanela) PostMessageW(gJanela, WM_CLOSE, 0, 0);
    WaitForSingleObject(gThread, 2000);
    CloseHandle(gThread);
    gThread = nullptr;
}

Resultado Atual() {
    Segurando t(gTrava);
    Resultado r = gResultado;
    r.eventos = (unsigned)gEventos;
    r.trechos = (int)gTrechos;
    return r;
}

}  // namespace polling
