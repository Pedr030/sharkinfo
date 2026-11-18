// grafico.cpp — desenha o gráfico "na mão" com GDI+ (no Python era o matplotlib).
#include "grafico.h"
#include "gdiplus_inc.h"

#include <ctime>

using namespace Gdiplus;

constexpr int ID_24H = 101, ID_7D = 102;

void JanelaGrafico::Abrir(HINSTANCE inst, Historico* hist, std::function<std::wstring()> resumo,
                          std::function<double()> agora) {
    hist_ = hist;
    resumo_ = std::move(resumo);
    agora_ = std::move(agora);
    if (hwnd_) {                                  // já aberta: só traz pra frente
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
        wc.lpszClassName = L"SharkInfoGrafico";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        RegisterClassW(&wc);
        registrada = true;
    }
    UINT dpi = GetDpiForSystem();
    hwnd_ = CreateWindowExW(0, L"SharkInfoGrafico", L"SharkInfo — Histórico",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(880, dpi, 96),
                            MulDiv(560, dpi, 96), nullptr, nullptr, inst, this);
    HFONT fonteUi = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                CLEARTYPE_QUALITY, 0, L"Segoe UI");
    // Dois botões tipo "rádio" pra escolher o período.
    botao24h_ = CreateWindowW(L"BUTTON", L"24 horas", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                              0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)ID_24H, inst, nullptr);
    botao7d_ = CreateWindowW(L"BUTTON", L"7 dias", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 0, 0, 10, 10,
                             hwnd_, (HMENU)(INT_PTR)ID_7D, inst, nullptr);
    SendMessageW(botao24h_, WM_SETFONT, (WPARAM)fonteUi, TRUE);
    SendMessageW(botao7d_, WM_SETFONT, (WPARAM)fonteUi, TRUE);
    SendMessageW(botao24h_, BM_SETCHECK, BST_CHECKED, 0);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void JanelaGrafico::Atualizar() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void JanelaGrafico::Fechar() {
    if (hwnd_) DestroyWindow(hwnd_);
}

LRESULT CALLBACK JanelaGrafico::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    auto* eu = reinterpret_cast<JanelaGrafico*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!eu) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
    case WM_SIZE: {                                    // reposiciona os botões no canto direito
        UINT dpi = GetDpiForWindow(h);
        int w = LOWORD(lp);
        int bw = MulDiv(90, dpi, 96), bh = MulDiv(24, dpi, 96), m = MulDiv(12, dpi, 96);
        MoveWindow(eu->botao24h_, w - 2 * bw - m, m, bw, bh, TRUE);
        MoveWindow(eu->botao7d_, w - bw - m, m, bw, bh, TRUE);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_24H || LOWORD(wp) == ID_7D) {
            eu->seteDias_ = LOWORD(wp) == ID_7D;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;                                      // nós pintamos tudo (evita "piscar")
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        // Desenha numa imagem escondida e copia de uma vez (double buffering = sem piscar).
        HDC mem = CreateCompatibleDC(dc);
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
        eu->hwnd_ = nullptr;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// "1731960000" -> "18/11 14:20" no fuso do PC
static std::wstring DataCurta(double momento) {
    time_t t = (time_t)momento;
    tm local{};
    localtime_s(&local, &t);
    wchar_t buf[32];
    wcsftime(buf, 32, L"%d/%m %H:%M", &local);
    return buf;
}

void JanelaGrafico::Pintar(HDC dc, const RECT& r) {
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.Clear(Color(255, 255, 255, 255));
    float e = GetDpiForWindow(hwnd_) / 96.0f;                // escala de DPI
    FontFamily fam(L"Segoe UI");
    Font fonte(&fam, 12 * e, FontStyleRegular, UnitPixel);
    Font fonteResumo(&fam, 13 * e, FontStyleRegular, UnitPixel);
    SolidBrush preto(Color(255, 40, 40, 40)), cinza(Color(255, 110, 110, 110));

    // Resumo no topo.
    std::wstring resumo = resumo_ ? resumo_() : L"";
    g.DrawString(resumo.c_str(), -1, &fonteResumo, PointF(12 * e, 10 * e), &preto);

    // Área do gráfico (com margens pros textos dos eixos).
    float esq = 52 * e, dir = r.right - 20 * e, topo = 100 * e, base = r.bottom - 48 * e;
    if (dir - esq < 50 || base - topo < 50) return;
    double agora = agora_ ? agora_() : Agora();
    double periodo = seteDias_ ? 7 * 86400.0 : 86400.0;
    double t0 = agora - periodo;
    auto X = [&](double t) { return esq + (float)((t - t0) / periodo) * (dir - esq); };
    auto Y = [&](int pct) { return base - pct / 100.0f * (base - topo); };

    // Linhas de grade horizontais (0, 20, 40... 100%) com os rótulos à esquerda.
    Pen grade(Color(255, 230, 230, 230), 1 * e);
    StringFormat direita;
    direita.SetAlignment(StringAlignmentFar);
    direita.SetLineAlignment(StringAlignmentCenter);
    for (int p = 0; p <= 100; p += 20) {
        g.DrawLine(&grade, esq, Y(p), dir, Y(p));
        std::wstring rot = std::to_wstring(p) + L"%";
        g.DrawString(rot.c_str(), -1, &fonte, RectF(0, Y(p) - 10 * e, esq - 6 * e, 20 * e), &direita, &cinza);
    }
    // Rótulos de tempo embaixo (5 marcas).
    // A 1ª e a última data são alinhadas pra dentro, senão saem cortadas na borda da janela.
    for (int i = 0; i <= 4; ++i) {
        double t = t0 + periodo * i / 4;
        std::wstring rot = DataCurta(t);
        StringFormat fmt;
        RectF caixa(X(t) - 50 * e, base + 8 * e, 100 * e, 20 * e);              // centralizada na marca
        if (i == 0) { fmt.SetAlignment(StringAlignmentNear); caixa.X = X(t); }  // começa na marca
        else if (i == 4) { fmt.SetAlignment(StringAlignmentFar); caixa.X = X(t) - 100 * e; }  // termina nela
        else fmt.SetAlignment(StringAlignmentCenter);
        g.DrawString(rot.c_str(), -1, &fonte, caixa, &fmt, &cinza);
    }

    auto pontos = hist_ ? hist_->LeiturasDesde(periodo, agora) : std::vector<PontoHistorico>{};
    if (pontos.empty()) {
        StringFormat meio;
        meio.SetAlignment(StringAlignmentCenter);
        meio.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"Ainda sem dados.\nDeixe o app aberto um tempo.", -1, &fonteResumo,
                     RectF(esq, topo, dir - esq, base - topo), &meio, &cinza);
        return;
    }
    // Trechos no cabo: retângulos azuis claros.
    SolidBrush azul(Color(64, 100, 181, 246));
    double inicioCabo = -1;
    for (size_t i = 0; i < pontos.size(); ++i) {
        if (pontos[i].carregando && inicioCabo < 0) inicioCabo = pontos[i].momento;
        bool fim = !pontos[i].carregando || i + 1 == pontos.size();
        if (inicioCabo >= 0 && fim) {
            double ate = pontos[i].momento;
            g.FillRectangle(&azul, X(inicioCabo), topo, std::max(2.0f, X(ate) - X(inicioCabo)), base - topo);
            inicioCabo = -1;
        }
    }
    // A linha da bateria.
    std::vector<PointF> linha;
    for (const auto& p : pontos) linha.push_back(PointF(X(p.momento), Y(p.percentual)));
    Pen verde(Color(255, 46, 125, 50), 2.5f * e);
    verde.SetLineJoin(LineJoinRound);
    SolidBrush pontoVerde(Color(255, 46, 125, 50));
    if (linha.size() == 1) g.FillEllipse(&pontoVerde, linha[0].X - 3 * e, linha[0].Y - 3 * e, 6 * e, 6 * e);
    else g.DrawLines(&verde, linha.data(), (INT)linha.size());
    Pen borda(Color(255, 180, 180, 180), 1 * e);
    g.DrawRectangle(&borda, esq, topo, dir - esq, base - topo);
}
