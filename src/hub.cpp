// hub.cpp — desenha e controla a janela do hub. Tudo é GDI+ "na mão", sem botões nem controles do Windows:
// a cada pintura a gente refaz a lista de "zonas clicáveis" (retângulos com um número de identificação),
// e o clique do mouse só procura em qual zona caiu. É o mesmo truque dos menus de jogos.
#include "hub.h"
#include "gdiplus_inc.h"
#include "icone.h"
#include "polling.h"

#include <ctime>

using namespace Gdiplus;

// ---------------------------------------------------------------------------
// Paleta (ARGB). Tema escuro com acento ciano.
// ---------------------------------------------------------------------------
namespace cor {
constexpr unsigned FUNDO = 0xFF14171A, LATERAL = 0xFF0E1013, CARTAO = 0xFF1E2227, CARTAO_DEST = 0xFF252B31,
                   BORDA = 0xFF2D343B, TEXTO = 0xFFECEFF1, SUAVE = 0xFF8A949E, ACENTO = 0xFF00C2FF,
                   ACENTO_ESC = 0xFF06232D, HOVER = 0xFF2A3037, PERIGO = 0xFFFF5A52, AZUL = 0xFF0A84FF;
}

constexpr int ID_NAV = 1000;                      // 1000..1004 = trocar de página
constexpr int ID_24H = 2000, ID_7D = 2001;        // período do gráfico

static Color C(unsigned argb) { return Color(argb); }
static Color Alfa(unsigned argb, int a) {
    return Color((BYTE)a, (BYTE)((argb >> 16) & 0xFF), (BYTE)((argb >> 8) & 0xFF), (BYTE)(argb & 0xFF));
}
static unsigned DeColorref(COLORREF c) {
    return 0xFF000000u | ((unsigned)GetRValue(c) << 16) | ((unsigned)GetGValue(c) << 8) | GetBValue(c);
}
static Graphics& G(void* p) { return *static_cast<Graphics*>(p); }

static void CaminhoArredondado(GraphicsPath& p, float x, float y, float w, float h, float r) {
    float d = std::min(r * 2, std::min(w, h));
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

static std::wstring DataCurta(double momento) {
    time_t t = (time_t)momento;
    tm local{};
    localtime_s(&local, &t);
    wchar_t buf[32];
    wcsftime(buf, 32, L"%d/%m %H:%M", &local);
    return buf;
}

// ---------------------------------------------------------------------------
// Ciclo de vida
// ---------------------------------------------------------------------------
Hub::~Hub() {
    delete cacheMouse_;
}

void Hub::Abrir(HINSTANCE inst, std::function<DadosHub()> dados, Historico* hist, std::function<void(int)> acionar,
                std::function<double()> agora, Pagina pagina) {
    inst_ = inst;
    dados_ = std::move(dados);
    hist_ = hist;
    acionar_ = std::move(acionar);
    agora_ = std::move(agora);
    pagina_ = pagina;
    if (hwnd_) {                                           // já aberto: só traz pra frente
        ShowWindow(hwnd_, SW_RESTORE);
        SetForegroundWindow(hwnd_);
        Atualizar();
        return;
    }
    static bool registrada = false;
    if (!registrada) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = Proc;
        wc.hInstance = inst;
        wc.lpszClassName = L"SharkInfoHub";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        RegisterClassW(&wc);
        registrada = true;
    }
    UINT dpi = GetDpiForSystem();
    hwnd_ = CreateWindowExW(0, L"SharkInfoHub", L"SharkInfo", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            MulDiv(1060, dpi, 96), MulDiv(720, dpi, 96), nullptr, nullptr, inst, this);
    if (!hwnd_) return;
    // Barra de título escura (Windows 10 2004+ / 11). Carrega a função na hora pra não depender da dwmapi.lib.
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        if (auto set = reinterpret_cast<Fn>(GetProcAddress(dwm, "DwmSetWindowAttribute"))) {
            BOOL escuro = TRUE;
            set(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &escuro, sizeof(escuro));
            COLORREF barra = RGB(0x0E, 0x10, 0x13);
            set(hwnd_, 35 /*DWMWA_CAPTION_COLOR (Windows 11)*/, &barra, sizeof(barra));
        }
        FreeLibrary(dwm);
    }
    SetTimer(hwnd_, 1, 1000, nullptr);                     // repinta 1x por segundo (polling ao vivo, relógio)
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void Hub::Atualizar() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Hub::Fechar() {
    if (hwnd_) DestroyWindow(hwnd_);
}

LRESULT CALLBACK Hub::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    auto* eu = reinterpret_cast<Hub*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!eu) return DefWindowProcW(h, msg, wp, lp);
    auto zonaEm = [&](int px, int py) {
        for (auto it = eu->zonas_.rbegin(); it != eu->zonas_.rend(); ++it)   // a de cima (última pintada) vence
            if (px >= it->x && px < it->x + it->w && py >= it->y && py < it->y + it->h) return it->id;
        return 0;
    };
    switch (msg) {
    case WM_GETMINMAXINFO: {
        auto* m = reinterpret_cast<MINMAXINFO*>(lp);
        UINT dpi = GetDpiForWindow(h);
        m->ptMinTrackSize.x = MulDiv(900, dpi, 96);
        m->ptMinTrackSize.y = MulDiv(640, dpi, 96);
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        if (!eu->seguindoMouse_) {                         // pede o aviso de "o mouse saiu da janela"
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            eu->seguindoMouse_ = true;
        }
        int id = zonaEm((short)LOWORD(lp), (short)HIWORD(lp));
        if (id != eu->hover_) {
            eu->hover_ = id;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        eu->seguindoMouse_ = false;
        if (eu->hover_) {
            eu->hover_ = 0;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && eu->hover_) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_LBUTTONUP: {
        int id = zonaEm((short)LOWORD(lp), (short)HIWORD(lp));
        if (id >= ID_NAV && id < ID_NAV + TOTAL_PAGINAS) {
            eu->pagina_ = (Pagina)(id - ID_NAV);
        } else if (id == ID_24H || id == ID_7D) {
            eu->seteDias_ = id == ID_7D;
        } else if (id > 0 && eu->acionar_) {
            eu->acionar_(id);
        }
        if (id > 0) InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;                                          // nós pintamos tudo (evita "piscar")
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        HDC mem = CreateCompatibleDC(dc);                  // desenha escondido e copia de uma vez (sem piscar)
        HBITMAP bmp = CreateCompatibleBitmap(dc, r.right, r.bottom);
        HGDIOBJ antigo = SelectObject(mem, bmp);
        eu->Pintar(mem, r);
        BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, antigo);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, 1);
        eu->hwnd_ = nullptr;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Peças de desenho
// ---------------------------------------------------------------------------
// estilo: 0 normal | 1 semibold | 2 fino (light).  alinhaH/alinhaV: 0 começo | 1 meio | 2 fim
void Hub::Texto(const std::wstring& t, float px, int estilo, unsigned cor, float x, float y, float w, float h,
                int alinhaH, int alinhaV) {
    FontFamily fam(estilo == 1 ? L"Segoe UI Semibold" : estilo == 2 ? L"Segoe UI Light" : L"Segoe UI");
    Font fonte(&fam, px * e_, FontStyleRegular, UnitPixel);
    StringFormat fmt;
    fmt.SetAlignment(alinhaH == 0 ? StringAlignmentNear : alinhaH == 1 ? StringAlignmentCenter : StringAlignmentFar);
    fmt.SetLineAlignment(alinhaV == 0 ? StringAlignmentNear : alinhaV == 1 ? StringAlignmentCenter : StringAlignmentFar);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush pincel(C(cor));
    G(g_).DrawString(t.c_str(), -1, &fonte, RectF(x, y, w, h), &fmt, &pincel);
}

void Hub::Cartao(float x, float y, float w, float h, bool destaque) {
    GraphicsPath p;
    CaminhoArredondado(p, x, y, w, h, P(14));
    SolidBrush fundo(C(destaque ? cor::CARTAO_DEST : cor::CARTAO));
    G(g_).FillPath(&fundo, &p);
    Pen borda(C(cor::BORDA), 1.0f);
    G(g_).DrawPath(&borda, &p);
}

void Hub::Pilula(float x, float y, float w, float h, unsigned corFundo) {
    GraphicsPath p;
    CaminhoArredondado(p, x, y, w, h, h / 2);
    SolidBrush b(C(corFundo));
    G(g_).FillPath(&b, &p);
}

void Hub::Chave(float x, float y, bool ligada, int id) {
    float w = P(44), h = P(24);
    bool sobre = hover_ == id;
    Pilula(x, y, w, h, ligada ? cor::ACENTO : (sobre ? 0xFF3A424A : 0xFF2F363D));
    float k = h - P(8);
    SolidBrush bola(C(ligada ? cor::ACENTO_ESC : 0xFFB8C0C7));
    G(g_).FillEllipse(&bola, ligada ? x + w - k - P(4) : x + P(4), y + P(4), k, k);
    AddZona(x, y, w, h, id);
}

void Hub::Botao(float x, float y, float w, const std::wstring& texto, int id, bool principal) {
    float h = P(34);
    bool sobre = hover_ == id;
    unsigned fundo = principal ? (sobre ? 0xFF33D0FF : cor::ACENTO) : (sobre ? cor::HOVER : 0xFF282E34);
    Pilula(x, y, w, h, fundo);
    Texto(texto, 13, 1, principal ? cor::ACENTO_ESC : cor::TEXTO, x, y, w, h, 1, 1);
    AddZona(x, y, w, h, id);
}

// "−  20%  +"
void Hub::Passo(float x, float y, int valor, int idMenos, int idMais) {
    float b = P(30);
    for (int i = 0; i < 2; ++i) {
        float bx = i == 0 ? x : x + P(110);
        int id = i == 0 ? idMenos : idMais;
        Pilula(bx, y, b, b, hover_ == id ? cor::HOVER : 0xFF282E34);
        Texto(i == 0 ? L"−" : L"+", 18, 1, cor::TEXTO, bx, y, b, b - P(2), 1, 1);
        AddZona(bx, y, b, b, id);
    }
    Texto(std::to_wstring(valor) + L"%", 15, 1, cor::TEXTO, x + b, y, P(110) - b, b, 1, 1);
}

// Ícones feitos de linhas simples. tipo: 0 mouse | 1 gráfico | 2 sino | 3 ajustes | 4 info
void Hub::Icone(int tipo, float cx, float cy, float tam, unsigned corIcone, float espessura) {
    Graphics& g = G(g_);
    Pen caneta(C(corIcone), espessura);
    caneta.SetLineJoin(LineJoinRound);
    caneta.SetStartCap(LineCapRound);
    caneta.SetEndCap(LineCapRound);
    float r = tam / 2;
    switch (tipo) {
    case 0: {                                              // mouse: cápsula com a divisória dos botões
        float w = tam * 0.58f, h = tam * 0.92f, x = cx - w / 2, y = cy - h / 2;
        GraphicsPath p;
        CaminhoArredondado(p, x, y, w, h, w / 2);
        g.DrawPath(&caneta, &p);
        g.DrawLine(&caneta, cx, y, cx, y + h * 0.36f);
        break;
    }
    case 1: {                                              // gráfico: eixos e uma linha subindo e descendo
        g.DrawLine(&caneta, cx - r, cy - r, cx - r, cy + r);
        g.DrawLine(&caneta, cx - r, cy + r, cx + r, cy + r);
        PointF linha[] = {{cx - r * 0.7f, cy + r * 0.2f}, {cx - r * 0.2f, cy - r * 0.35f}, {cx + r * 0.25f, cy + r * 0.05f},
                          {cx + r * 0.8f, cy - r * 0.7f}};
        g.DrawLines(&caneta, linha, 4);
        break;
    }
    case 2: {                                              // sino
        g.DrawArc(&caneta, cx - r * 0.65f, cy - r * 0.85f, r * 1.3f, r * 1.3f, 180, 180);
        g.DrawLine(&caneta, cx - r * 0.65f, cy - r * 0.2f, cx - r * 0.85f, cy + r * 0.5f);
        g.DrawLine(&caneta, cx + r * 0.65f, cy - r * 0.2f, cx + r * 0.85f, cy + r * 0.5f);
        g.DrawLine(&caneta, cx - r * 0.85f, cy + r * 0.5f, cx + r * 0.85f, cy + r * 0.5f);
        g.DrawArc(&caneta, cx - r * 0.25f, cy + r * 0.55f, r * 0.5f, r * 0.4f, 0, 180);
        break;
    }
    case 3: {                                              // ajustes: três linhas com bolinhas
        float ys[] = {cy - r * 0.6f, cy, cy + r * 0.6f};
        float xs[] = {cx - r * 0.3f, cx + r * 0.4f, cx - r * 0.1f};
        for (int i = 0; i < 3; ++i) {
            g.DrawLine(&caneta, cx - r, ys[i], cx + r, ys[i]);
            SolidBrush miolo(C(cor::LATERAL));
            g.FillEllipse(&miolo, xs[i] - r * 0.22f, ys[i] - r * 0.22f, r * 0.44f, r * 0.44f);
            g.DrawEllipse(&caneta, xs[i] - r * 0.22f, ys[i] - r * 0.22f, r * 0.44f, r * 0.44f);
        }
        break;
    }
    default: {                                             // info: círculo com um "i"
        g.DrawEllipse(&caneta, cx - r, cy - r, tam, tam);
        g.DrawLine(&caneta, cx, cy - r * 0.05f, cx, cy + r * 0.5f);
        SolidBrush ponto(C(corIcone));
        g.FillEllipse(&ponto, cx - espessura * 0.8f, cy - r * 0.5f - espessura * 0.8f, espessura * 1.6f, espessura * 1.6f);
        break;
    }
    }
}

// Linha de uma lista de opções: título e descrição à esquerda. Devolve o "y" do centro da linha.
float Hub::Linha(float x, float y, float w, const std::wstring& titulo, const std::wstring& desc, bool separador) {
    float h = P(62);
    Texto(titulo, 15, 1, cor::TEXTO, x + P(22), y + P(10), w - P(300), P(22));
    Texto(desc, 12, 0, cor::SUAVE, x + P(22), y + P(33), w - P(300), P(20));
    if (separador) {
        Pen p(C(cor::BORDA), 1.0f);
        G(g_).DrawLine(&p, x + P(22), y + h, x + w - P(22), y + h);
    }
    return y + h / 2;
}

// ---------------------------------------------------------------------------
// Gráfico (versão escura). eixos=false: miniatura, só a linha.
// ---------------------------------------------------------------------------
void Hub::Grafico(float x, float y, float w, float h, double periodo, bool eixos) {
    Graphics& g = G(g_);
    float esq = x + (eixos ? P(44) : P(6)), dir = x + w - P(10);
    float topo = y + P(8), base = y + h - (eixos ? P(28) : P(6));
    if (dir - esq < 50 || base - topo < 30) return;
    double agora = agora_ ? agora_() : Agora();
    double t0 = agora - periodo;
    auto X = [&](double t) { return esq + (float)((t - t0) / periodo) * (dir - esq); };
    auto Y = [&](int pct) { return base - pct / 100.0f * (base - topo); };

    Pen grade(C(0xFF262C32), 1.0f);
    int passo = eixos ? 20 : 50;
    for (int p = 0; p <= 100; p += passo) {
        g.DrawLine(&grade, esq, Y(p), dir, Y(p));
        if (eixos) Texto(std::to_wstring(p) + L"%", 11, 0, cor::SUAVE, x, Y(p) - P(9), esq - x - P(8), P(18), 2, 1);
    }
    if (eixos)
        for (int i = 0; i <= 4; ++i) {                     // 5 datas embaixo; as pontas ficam alinhadas pra dentro
            double t = t0 + periodo * i / 4;
            float cx = X(t);
            float lx = i == 0 ? cx : i == 4 ? cx - P(110) : cx - P(55);
            Texto(DataCurta(t), 11, 0, cor::SUAVE, lx, base + P(6), P(110), P(18), i == 0 ? 0 : i == 4 ? 2 : 1);
        }

    auto pontos = hist_ ? hist_->LeiturasDesde(periodo, agora) : std::vector<PontoHistorico>{};
    if (pontos.empty()) {
        Texto(L"Ainda sem dados. Deixe o app aberto um tempo.", 13, 0, cor::SUAVE, esq, topo, dir - esq, base - topo, 1, 1);
        return;
    }
    SolidBrush azul(Alfa(cor::AZUL, 55));                  // trechos no cabo
    double inicioCabo = -1;
    for (size_t i = 0; i < pontos.size(); ++i) {
        if (pontos[i].carregando && inicioCabo < 0) inicioCabo = pontos[i].momento;
        bool fim = !pontos[i].carregando || i + 1 == pontos.size();
        if (inicioCabo >= 0 && fim) {
            g.FillRectangle(&azul, X(inicioCabo), topo, std::max(2.0f, X(pontos[i].momento) - X(inicioCabo)), base - topo);
            inicioCabo = -1;
        }
    }
    std::vector<PointF> linha;
    for (const auto& p : pontos) linha.push_back(PointF(X(p.momento), Y(p.percentual)));
    if (linha.size() >= 2) {
        std::vector<PointF> area = linha;                  // preenchimento suave embaixo da linha
        area.push_back(PointF(linha.back().X, base));
        area.push_back(PointF(linha.front().X, base));
        LinearGradientBrush degrade(PointF(0, topo), PointF(0, base), Alfa(cor::ACENTO, 70), Alfa(cor::ACENTO, 0));
        g.FillPolygon(&degrade, area.data(), (INT)area.size());
        Pen traco(C(cor::ACENTO), P(2.2f));
        traco.SetLineJoin(LineJoinRound);
        g.DrawLines(&traco, linha.data(), (INT)linha.size());
    } else {
        SolidBrush ponto(C(cor::ACENTO));
        g.FillEllipse(&ponto, linha[0].X - P(3), linha[0].Y - P(3), P(6), P(6));
    }
}

// ---------------------------------------------------------------------------
// Textos compartilhados
// ---------------------------------------------------------------------------
static void TextosRestante(const DadosHub& d, std::wstring& titulo, std::wstring& valor, std::wstring& sub) {
    titulo = L"TEMPO RESTANTE";
    if (!d.temLeitura) {
        valor = L"—";
        sub = L"Sem leitura do mouse";
    } else if (d.l.carregando) {
        titulo = L"CARGA";
        if (d.l.percentual == 100) {
            valor = L"Completa";
            sub = L"Pode tirar do cabo";
        } else if (d.carga.valida) {
            valor = L"~" + FormatarMinutos(d.carga.minutos);
            sub = d.carga.aprendido ? L"para encher (aprendido nas suas cargas)" : L"para encher (ainda aprendendo)";
        } else {
            valor = L"calculando…";
            sub = L"Anotando quando o cabo foi plugado";
        }
    } else if (d.descarga.minutos < 0) {
        valor = L"calculando…";
        sub = L"Precisa ver a bateria cair um degrau (10%)";
    } else if (d.descarga.fonte == EstimativaDescarga::MINIMO) {
        valor = L"≥ " + FormatarMinutos(d.descarga.minutos);
        sub = L"Pelo menos isso. Ainda medindo, só cresce";
    } else {
        valor = L"~" + FormatarMinutos(d.descarga.minutos);
        sub = d.descarga.fonte == EstimativaDescarga::SESSAO ? L"Medido na descarga de agora"
                                                              : L"Pelas suas descargas anteriores";
    }
}

static int PercentualMostrado(const DadosHub& d) {
    if (!d.temLeitura) return -1;
    int pct = d.l.percentual;
    if (d.l.carregando && d.carga.valida && pct != 100) pct = std::max(pct, d.carga.percentual);   // palpite no cabo
    return pct;
}

// ---------------------------------------------------------------------------
// Pintura geral: barra lateral + a página escolhida
// ---------------------------------------------------------------------------
void Hub::Pintar(HDC dc, const RECT& r) {
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g_ = &g;
    e_ = GetDpiForWindow(hwnd_) / 96.0f;
    d_ = dados_ ? dados_() : DadosHub{};
    zonas_.clear();
    g.Clear(C(cor::FUNDO));

    float W = (float)r.right, H = (float)r.bottom;
    float lat = P(88);
    SolidBrush fundoLat(C(cor::LATERAL));
    g.FillRectangle(&fundoLat, 0.0f, 0.0f, lat, H);

    // Marca no topo da barra lateral
    Pilula(lat / 2 - P(22), P(22), P(44), P(44), cor::ACENTO_ESC);
    Icone(0, lat / 2, P(44), P(24), cor::ACENTO, P(2.2f));

    static const wchar_t* nomes[] = {L"Início", L"Histórico", L"Avisos", L"Ajustes", L"Sobre"};
    static const int icones[] = {0, 1, 2, 3, 4};
    for (int i = 0; i < TOTAL_PAGINAS; ++i) {
        float iy = P(100) + i * P(76);
        bool ativa = pagina_ == i, sobre = hover_ == ID_NAV + i;
        if (ativa || sobre) {
            GraphicsPath p;
            CaminhoArredondado(p, P(10), iy, lat - P(20), P(66), P(12));
            SolidBrush b(C(ativa ? 0xFF1B2127 : 0xFF161A1E));
            g.FillPath(&b, &p);
        }
        if (ativa) {
            SolidBrush barra(C(cor::ACENTO));
            g.FillRectangle(&barra, 0.0f, iy + P(14), P(3), P(38));
        }
        unsigned c = ativa ? cor::ACENTO : (sobre ? cor::TEXTO : cor::SUAVE);
        Icone(icones[i], lat / 2, iy + P(25), P(22), c, P(1.9f));
        Texto(nomes[i], 11.5f, 0, c, 0, iy + P(41), lat, P(20), 1);
        AddZona(P(10), iy, lat - P(20), P(66), ID_NAV + i);
    }

    float x = lat + P(32), y = P(26), w = W - x - P(32), h = H - y - P(26);
    switch (pagina_) {
    case INICIO: PaginaInicio(x, y, w, h); break;
    case HISTORICO: PaginaHistorico(x, y, w, h); break;
    case AVISOS: PaginaAvisos(x, y, w, h); break;
    case AJUSTES: PaginaAjustes(x, y, w, h); break;
    default: PaginaSobre(x, y, w, h); break;
    }
    g_ = nullptr;
}

// ---------------------------------------------------------------------------
// Início
// ---------------------------------------------------------------------------
void Hub::PaginaInicio(float x, float y, float w, float h) {
    Graphics& g = G(g_);
    const DadosHub& d = d_;
    std::wstring nome = d.temLeitor ? d.l.modelo : L"Mouse não encontrado";
    Texto(nome, 28, 1, cor::TEXTO, x, y - P(2), w, P(40));

    int pct = PercentualMostrado(d);
    bool carregando = d.temLeitura && d.l.carregando, dormindo = !d.temLeitura || d.l.dormindo;
    unsigned corNivel = DeColorref(CorDoNivel(pct, carregando, dormindo, d.cfg.avisoPercentual, d.cfg.criticoPercentual));

    std::wstring status;
    unsigned corStatus = corNivel;
    if (!d.temLeitor) {
        status = L"Receptor não encontrado. Plugue o USB do mouse";
        corStatus = cor::PERIGO;
    } else if (!d.temLeitura) {
        status = L"Aguardando o mouse…";
        corStatus = cor::SUAVE;
    } else if (carregando) {
        status = L"Carregando no cabo";
    } else if (d.l.dormindo) {
        status = L"Dormindo (mexa o mouse pra acordar)";
        corStatus = cor::SUAVE;
    } else {
        status = L"Conectado pelo " + d.l.conexao;
    }
    SolidBrush ponto(C(corStatus));
    g.FillEllipse(&ponto, x + P(2), y + P(51), P(10), P(10));
    Texto(status, 14, 0, cor::SUAVE, x + P(20), y + P(44), w - P(20), P(24));

    // ---- Coluna da esquerda: o mouse grande com a bateria ----
    float hy = y + P(80), hh = h - P(80);
    float hw = std::min(w * 0.40f, P(400));
    Cartao(x, hy, hw, hh, true);

    float tam = std::min(hw * 0.62f, hh * 0.52f);
    float mx = x + hw / 2 - tam * 0.44f, my = hy + P(26);
    {                                                      // brilho atrás do mouse, na cor do nível
        GraphicsPath brilho;
        float rb = hw * 0.46f;
        brilho.AddEllipse(x + hw / 2 - rb, my + tam * 0.45f - rb, rb * 2, rb * 2);
        PathGradientBrush pg(&brilho);
        pg.SetCenterColor(Alfa(corNivel, 60));
        Color borda = Alfa(corNivel, 0);
        int n = 1;
        pg.SetSurroundColors(&borda, &n);
        g.FillPath(&pg, &brilho);
    }
    int chave = (pct + 2) * 1000 + (carregando ? 100 : 0) + (dormindo ? 10 : 0) + (int)(tam / 8);
    if (!cacheMouse_ || cacheChave_ != chave) {
        delete cacheMouse_;
        cacheMouse_ = CriarBitmapMouse(pct, carregando, dormindo, d.cfg.avisoPercentual, d.cfg.criticoPercentual,
                                       (int)tam, false);
        cacheChave_ = chave;
    }
    if (cacheMouse_) g.DrawImage(cacheMouse_, mx, my, tam, tam);

    float py = my + tam + P(6);
    std::wstring pctTxt = pct >= 0 ? std::to_wstring(pct) + L"%" : L"—";
    Texto(pctTxt, 56, 2, cor::TEXTO, x, py, hw, P(76), 1, 1);
    std::wstring rotulo = !d.temLeitura ? L"sem leitura" : carregando ? (d.l.percentual == 100 ? L"carga completa" : L"carregando (palpite)")
                          : d.l.dormindo ? L"último valor" : L"de bateria";
    Texto(rotulo, 14, 0, cor::SUAVE, x, py + P(70), hw, P(22), 1);
    float bx = x + P(36), bw = hw - P(72), by = py + P(104);
    Pilula(bx, by, bw, P(8), 0xFF2B3138);
    if (pct > 0) Pilula(bx, by, std::max(P(8), bw * pct / 100.0f), P(8), corNivel);

    // ---- Coluna da direita: os cartões ----
    float rx = x + hw + P(20), rw = w - hw - P(20);
    float cw = (rw - P(16)) / 2, ch = P(132);
    auto cartao = [&](int col, int lin, const std::wstring& titulo, const std::wstring& valor, const std::wstring& sub,
                      unsigned corValor) {
        float cx = rx + col * (cw + P(16)), cy = hy + lin * (ch + P(16));
        Cartao(cx, cy, cw, ch);
        Texto(titulo, 11.5f, 1, cor::SUAVE, cx + P(18), cy + P(14), cw - P(36), P(18));
        Texto(valor, 30, 1, corValor, cx + P(18), cy + P(36), cw - P(36), P(44), 0, 1);
        Texto(sub, 12, 0, cor::SUAVE, cx + P(18), cy + P(84), cw - P(36), P(20));
        return std::pair<float, float>(cx, cy);
    };

    std::wstring t1, v1, s1;
    TextosRestante(d, t1, v1, s1);
    cartao(0, 0, t1, v1, s1, cor::TEXTO);

    // Polling rate (com a régua das taxas comuns)
    std::wstring pv = d.pollingHz > 0 ? std::to_wstring(d.pollingHz) + L" Hz" : L"—";
    std::wstring ps = d.pollingHz > 0 ? (d.pollingAoVivo ? L"Medido agora, pelo movimento do mouse" : L"Última medição (mexa o mouse pra atualizar)")
                                      : L"Mexa o mouse um pouco pra medir";
    auto pos = cartao(1, 0, L"POLLING RATE", pv, ps, d.pollingHz > 0 ? cor::ACENTO : cor::SUAVE);
    {
        static const int taxas[] = {125, 250, 500, 1000, 2000};
        float pw = (cw - P(36) - 4 * P(6)) / 5, pyy = pos.second + ch - P(28);
        for (int i = 0; i < 5; ++i) {
            bool acesa = d.pollingHz > 0 && (i == 4 ? d.pollingHz >= 2000 : d.pollingHz == taxas[i]);
            float pxx = pos.first + P(18) + i * (pw + P(6));
            Pilula(pxx, pyy, pw, P(16), acesa ? cor::ACENTO : 0xFF2B3138);
            Texto(i == 4 ? L"2000+" : std::to_wstring(taxas[i]), 10.5f, 1, acesa ? cor::ACENTO_ESC : cor::SUAVE, pxx, pyy, pw, P(16), 1, 1);
        }
    }

    cartao(0, 1, L"CARGA COMPLETA", FormatarMinutos(d.minutosCargaCompleta),
           d.cargaAprendida ? L"De 0 a 100%, aprendido nas suas cargas" : L"De 0 a 100%, valor padrão (aprendendo)", cor::TEXTO);
    cartao(1, 1, L"AVISOS", std::to_wstring(d.cfg.avisoPercentual) + L"%  ·  " + std::to_wstring(d.cfg.criticoPercentual) + L"%",
           L"Primeiro aviso e aviso urgente", cor::TEXTO);

    // Últimas 24 h
    float gy = hy + 2 * (ch + P(16)), gh = hy + hh - gy;
    if (gh > P(110)) {
        Cartao(rx, gy, rw, gh);
        Texto(L"ÚLTIMAS 24 HORAS", 11.5f, 1, cor::SUAVE, rx + P(18), gy + P(12), rw - P(36), P(18));
        Grafico(rx + P(10), gy + P(30), rw - P(20), gh - P(40), 86400.0, false);
    }
}

// ---------------------------------------------------------------------------
// Histórico
// ---------------------------------------------------------------------------
void Hub::PaginaHistorico(float x, float y, float w, float h) {
    const DadosHub& d = d_;
    Texto(L"Histórico da bateria", 28, 1, cor::TEXTO, x, y - P(2), w, P(40));
    for (int i = 0; i < 2; ++i) {                          // "24 horas" | "7 dias"
        int id = i == 0 ? ID_24H : ID_7D;
        bool ativo = (i == 1) == seteDias_;
        float pw = P(96), px = x + w - (2 - i) * (pw + P(8)) + P(8);
        Pilula(px, y + P(4), pw, P(32), ativo ? cor::ACENTO : (hover_ == id ? cor::HOVER : 0xFF282E34));
        Texto(i == 0 ? L"24 horas" : L"7 dias", 13, 1, ativo ? cor::ACENTO_ESC : cor::TEXTO, px, y + P(4), pw, P(32), 1, 1);
        AddZona(px, y + P(4), pw, P(32), id);
    }
    int pct = PercentualMostrado(d);
    std::wstring t1, v1, s1;
    TextosRestante(d, t1, v1, s1);
    Texto(L"Bateria agora: " + (pct >= 0 ? std::to_wstring(pct) + L"%" : std::wstring(L"—")) + L"   ·   " + v1 + L"   ·   " + s1, 14, 0,
          cor::SUAVE, x, y + P(46), w, P(24));
    float gy = y + P(84);
    Cartao(x, gy, w, h - P(84));
    Grafico(x + P(12), gy + P(14), w - P(24), h - P(84) - P(26), seteDias_ ? 7 * 86400.0 : 86400.0, true);
}

// ---------------------------------------------------------------------------
// Avisos
// ---------------------------------------------------------------------------
void Hub::PaginaAvisos(float x, float y, float w, float h) {
    const DadosHub& d = d_;
    const Config& c = d.cfg;
    Texto(L"Avisos", 28, 1, cor::TEXTO, x, y - P(2), w, P(40));
    Texto(L"Como o SharkInfo avisa que o mouse está descarregando.", 14, 0, cor::SUAVE, x, y + P(40), w, P(24));
    float cy = y + P(80), ch = P(62) * 6 + P(10);
    Cartao(x, cy, w, std::min(ch, h - P(80)));
    float ctrl = x + w - P(22) - P(44);                    // onde começam os controles (alinhados à direita)

    struct Op { const wchar_t* t; const wchar_t* d; bool v; int id; };
    Op ops[] = {
        {L"Faixa por cima do jogo", L"Aparece sobre o jogo sem tirar o foco (funciona em tela cheia sem bordas)", c.avisoNaTela, HA_FAIXA},
        {L"Notificação do Windows", L"Balão com o ícone colorido do mouse", c.notificacaoWindows, HA_NOTIF},
        {L"Avisar quando terminar de carregar", L"Diz quando o mouse chegou a 100% no cabo", c.avisarCargaCompleta, HA_CHEIO},
        {L"Enviar avisos pro celular", L"Pelo app ntfy, no tópico abaixo", c.celularAtivo, HA_CELULAR},
    };
    float ly = cy;
    for (const Op& o : ops) {
        float meio = Linha(x, ly, w, o.t, o.d, true);
        Chave(ctrl, meio - P(12), o.v, o.id);
        ly += P(62);
    }
    float meio = Linha(x, ly, w, L"Primeiro aviso em", L"Notificação amarela. Vai de 10% a 50%", true);
    Passo(x + w - P(22) - P(140), meio - P(15), c.avisoPercentual, HA_AVISO_MENOS, HA_AVISO_MAIS);
    ly += P(62);
    meio = Linha(x, ly, w, L"Aviso urgente em", L"Notificação vermelha. Sempre abaixo do primeiro aviso", false);
    Passo(x + w - P(22) - P(140), meio - P(15), c.criticoPercentual, HA_CRITICO_MENOS, HA_CRITICO_MAIS);
    ly += P(62) + P(8);

    float by = cy + std::min(ch, h - P(80)) + P(18);
    Botao(x, by, P(170), L"Testar a faixa", HA_TESTE_FAIXA, true);
    Botao(x + P(182), by, P(210), L"Copiar tópico do celular", HA_COPIAR_TOPICO);
    Botao(x + P(404), by, P(210), L"Enviar teste pro celular", HA_TESTE_CELULAR);
    Texto(L"Tópico: " + c.celularTopico, 12, 0, cor::SUAVE, x, by + P(44), w, P(20));
}

// ---------------------------------------------------------------------------
// Ajustes
// ---------------------------------------------------------------------------
void Hub::PaginaAjustes(float x, float y, float w, float h) {
    (void)h;
    const DadosHub& d = d_;
    const Config& c = d.cfg;
    Texto(L"Ajustes", 28, 1, cor::TEXTO, x, y - P(2), w, P(40));
    Texto(L"O menu do botão direito no ícone da bandeja continua igual.", 14, 0, cor::SUAVE, x, y + P(40), w, P(24));
    float cy = y + P(80);
    Cartao(x, cy, w, P(62) * 3 + P(4));
    float ctrl = x + w - P(22) - P(44);
    struct Op { const wchar_t* t; const wchar_t* d; bool v; int id; };
    Op ops[] = {
        {L"Mostrar a % na barra de tarefas", L"O mouse com a porcentagem ao lado do relógio", c.widgetBarra, HA_WIDGET},
        {L"Número no ícone da bandeja", L"Troca o desenho do mouse por um quadrado colorido com a %", c.iconeNumero, HA_NUMERO},
        {L"Iniciar com o Windows", L"Abre o SharkInfo sozinho quando você liga o PC", d.iniciaComWindows, HA_INICIO},
    };
    float ly = cy;
    for (int i = 0; i < 3; ++i) {
        float meio = Linha(x, ly, w, ops[i].t, ops[i].d, i < 2);
        Chave(ctrl, meio - P(12), ops[i].v, ops[i].id);
        ly += P(62);
    }
    float by = cy + P(62) * 3 + P(28);
    Botao(x, by, P(200), L"Abrir configurações", HA_CONFIG, true);
    Botao(x + P(212), by, P(220), L"Recarregar configurações", HA_RECARREGAR);
    Botao(x + P(444), by, P(200), L"Diagnóstico do mouse", HA_DIAGNOSTICO);
    Texto(L"As configurações ficam no config.ini, em %APPDATA%\\SharkInfo (junto com o histórico).", 12, 0, cor::SUAVE, x,
          by + P(46), w, P(20));
}

// ---------------------------------------------------------------------------
// Sobre
// ---------------------------------------------------------------------------
void Hub::PaginaSobre(float x, float y, float w, float h) {
    (void)h;
    const DadosHub& d = d_;
    Texto(L"Sobre", 28, 1, cor::TEXTO, x, y - P(2), w, P(40));
    Texto(L"SharkInfo " + d.versao, 14, 0, cor::SUAVE, x, y + P(40), w, P(24));
    float cy = y + P(80);
    Cartao(x, cy, w, P(62) * 2 + P(4));
    std::wstring titulo, desc, botao;
    int acao = 0;
    switch (d.atualizacao) {
    case 2:
        titulo = L"Nova versão " + d.versaoNova + L" pronta";
        desc = L"Já foi baixada e conferida. Reinicia em 1 segundo.";
        botao = L"Instalar agora";
        acao = HA_INSTALAR_ATUALIZACAO;
        break;
    case 1:
        titulo = L"Procurando atualização…";
        desc = L"Isso leva alguns segundos.";
        break;
    case 3:
        titulo = L"Versão de desenvolvimento";
        desc = L"Esta cópia foi compilada à mão; o atualizador fica desligado.";
        break;
    default:
        titulo = L"Você está na versão " + d.versao;
        desc = L"O app procura versões novas sozinho a cada 30 minutos.";
        botao = L"Procurar agora";
        acao = HA_PROCURAR_ATUALIZACAO;
        break;
    }
    float meio = Linha(x, cy, w, titulo, desc, true);
    if (acao) Botao(x + w - P(22) - P(170), meio - P(17), P(170), botao, acao, d.atualizacao == 2);
    meio = Linha(x, cy + P(62), w, L"Atualizar automaticamente", L"Baixa em segundo plano e só avisa fora do jogo", false);
    Chave(x + w - P(22) - P(44), meio - P(12), d.cfg.atualizarAutomaticamente, HA_AUTO_ATUALIZAR);

    float by = cy + P(62) * 2 + P(28);
    Botao(x, by, P(150), L"Sair do SharkInfo", HA_SAIR);
    Texto(L"github.com/Pedr030/sharkinfo", 12, 0, cor::SUAVE, x, by + P(48), w, P(20));
}
