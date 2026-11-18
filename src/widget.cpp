// widget.cpp — implementação do widget da barra de tarefas.
//
// Peças do Windows envolvidas:
//   Shell_TrayWnd        -> a barra de tarefas inteira (janela do Explorer)
//   TrayNotifyWnd        -> a área de notificação (setinha ^, ícones, relógio)
//   ReBarWindow32        -> (só Windows 10) a área dos botões dos programas abertos
//   janela "em camadas"  -> WS_EX_LAYERED + UpdateLayeredWindow: cada pixel com sua transparência
#include "widget.h"
#include "gdiplus_inc.h"
#include "icone.h"

#include <commctrl.h>   // tooltip (a dica ao parar o mouse em cima)

using namespace Gdiplus;

namespace widget {
namespace {

HWND gBarra = nullptr, gNotif = nullptr, gRebar = nullptr;   // janelas do Explorer
HWND gJanela = nullptr, gDica = nullptr;                      // as nossas
HWND gApp = nullptr;
UINT gMsgClique = 0;
Conteudo gConteudo;
bool gHover = false;           // mouse do usuário em cima do widget?
SIZE gTam{};                   // tamanho atual do widget
UINT gDpi = 96;
bool gVertical = false;        // barra encostada na esquerda/direita da tela
RECT gUltimo{};                // última posição aplicada (evita mexer à toa)

float Escala() { return gDpi / 96.0f; }
int Px(float v) { return (int)(v * Escala() + 0.5f); }

// Altura que o widget deve ter: a da barra (horizontal) ou um bloco fixo (vertical).
int Altura() {
    RECT r{};
    GetClientRect(gBarra, &r);
    return gVertical ? Px(34) : (int)r.bottom;
}

std::wstring Texto() {
    if (gConteudo.percentual < 0) return L"—";
    return std::to_wstring(gConteudo.percentual) + L"%";
}

// Desenha tudo numa imagem com transparência e entrega pro Windows (UpdateLayeredWindow).
void Pintar() {
    if (!gJanela) return;
    bool claro = TemaClaro(true);
    float e = Escala();
    FontFamily familia(L"Segoe UI");
    Font fonte(&familia, 12.5f * e, FontStyleRegular, UnitPixel);   // parecido com o relógio
    std::wstring texto = Texto();

    // Mede o texto pra saber a largura do widget.
    RectF medida;
    {
        Bitmap tmp(1, 1, PixelFormat32bppPARGB);
        Graphics g(&tmp);
        g.MeasureString(texto.c_str(), -1, &fonte, PointF(0, 0), &medida);
    }
    int tamIcone = Px(20), margem = Px(6), espaco = Px(3);
    int w = margem + tamIcone + espaco + (int)(medida.Width + 0.5f) + margem;
    int h = Altura();
    if (w <= 0 || h <= 0) return;

    // Uma "tela" de pixels na memória (DIB), de cima pra baixo, 4 bytes por pixel (azul, verde, vermelho, alfa).
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;                    // negativo = linha 0 é a de cima
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC tela = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(tela);
    HBITMAP dib = CreateDIBSection(tela, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ antigo = SelectObject(mem, dib);
    {
        // "PARGB" = cores pré-multiplicadas pelo alfa, o formato que o UpdateLayeredWindow espera.
        Bitmap alvo(w, h, w * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Graphics g(&alvo);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);   // ClearType fica feio em fundo transparente
        // Pixel 100% transparente deixa o clique "vazar" pra barra. Alfa 1 é invisível mas recebe clique.
        g.Clear(Color(1, 0, 0, 0));
        if (gHover) {                              // realce ao passar o mouse, como os botões da barra
            SolidBrush realce(claro ? Color(18, 0, 0, 0) : Color(24, 255, 255, 255));
            float r = 4 * e, d = 2 * r, x = 1, y = Px(4) * 1.0f, ww = w - 2.0f, hh = h - Px(8) * 1.0f;
            GraphicsPath p;
            p.AddArc(x, y, d, d, 180, 90);
            p.AddArc(x + ww - d, y, d, d, 270, 90);
            p.AddArc(x + ww - d, y + hh - d, d, d, 0, 90);
            p.AddArc(x, y + hh - d, d, d, 90, 90);
            p.CloseFigure();
            g.FillPath(&realce, &p);
        }
        Bitmap* icone = CriarBitmapMouse(gConteudo.percentual, gConteudo.carregando, gConteudo.dormindo,
                                         gConteudo.aviso, gConteudo.critico, tamIcone, claro);
        g.DrawImage(icone, margem, (h - tamIcone) / 2, tamIcone, tamIcone);
        delete icone;
        BYTE alfa = gConteudo.dormindo ? 160 : 255;              // dormindo: texto mais apagado
        SolidBrush corTexto(claro ? Color(alfa, 20, 20, 20) : Color(alfa, 255, 255, 255));
        StringFormat fmt;
        fmt.SetLineAlignment(StringAlignmentCenter);
        RectF caixa((float)(margem + tamIcone + espaco), 0, medida.Width + 2, (float)h);
        g.DrawString(texto.c_str(), -1, &fonte, caixa, &fmt, &corTexto);
    }
    SIZE tam{w, h};
    POINT origem{0, 0};
    BLENDFUNCTION mistura{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(gJanela, nullptr, nullptr, &tam, mem, &origem, 0, &mistura, ULW_ALPHA);
    SelectObject(mem, antigo);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, tela);
    bool mudouTamanho = tam.cx != gTam.cx || tam.cy != gTam.cy;
    gTam = tam;
    if (mudouTamanho) {
        SetRectEmpty(&gUltimo);                    // força reposicionar
        Reposicionar();
    }
}

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;                      // clicar não "rouba" o foco de ninguém
    case WM_MOUSEMOVE:
        if (!gHover) {
            gHover = true;
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};   // avise quando o mouse sair
            TrackMouseEvent(&t);
            Pintar();
        }
        return 0;
    case WM_MOUSELEAVE:
        gHover = false;
        Pintar();
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP: {
        POINT pt;
        GetCursorPos(&pt);
        PostMessageW(gApp, gMsgClique, msg == WM_LBUTTONUP ? 1 : 2, MAKELPARAM(pt.x, pt.y));
        return 0;
    }
    }
    (void)wp;
    (void)lp;
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

bool Ativo() { return gJanela && IsWindow(gJanela) && gBarra && IsWindow(gBarra); }

bool Criar(HINSTANCE inst, HWND janelaApp, UINT msgClique) {
    Destruir();
    gBarra = FindWindowW(L"Shell_TrayWnd", nullptr);
    gNotif = gBarra ? FindWindowExW(gBarra, nullptr, L"TrayNotifyWnd", nullptr) : nullptr;
    if (!gBarra || !gNotif) {                      // barra diferente do esperado: desiste com elegância
        gBarra = gNotif = nullptr;
        return false;
    }
    // No Windows 11 a barra é feita em XAML (tem a "DesktopWindowContentBridge"). No Windows 10 os
    // botões dos programas ficam no ReBarWindow32, que às vezes precisa ser encurtado pra não passar por baixo.
    bool win11 = FindWindowExW(gBarra, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr);
    gRebar = win11 ? nullptr : FindWindowExW(gBarra, nullptr, L"ReBarWindow32", nullptr);
    gApp = janelaApp;
    gMsgClique = msgClique;
    gDpi = GetDpiForWindow(gBarra);

    static bool registrada = false;
    if (!registrada) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = Proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SharkInfoWidget";
        registrada = RegisterClassW(&wc) != 0;
    }
    // WS_CHILD + pai = barra de tarefas: é isso que "pendura" o widget dentro da barra.
    gJanela = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, L"SharkInfoWidget", L"", WS_CHILD | WS_CLIPSIBLINGS,
                              0, 0, 1, 1, gBarra, nullptr, inst, nullptr);
    if (!gJanela) {
        gBarra = gNotif = gRebar = nullptr;
        return false;
    }
    // Dica ao parar o mouse em cima (tooltip padrão do Windows).
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    gDica = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0,
                            0, 0, janelaApp, nullptr, inst, nullptr);
    if (gDica) {
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;   // "a área da dica é a janela inteira do widget"
        ti.hwnd = janelaApp;
        ti.uId = (UINT_PTR)gJanela;
        ti.lpszText = const_cast<wchar_t*>(L"SharkInfo");
        SendMessageW(gDica, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(gDica, TTM_SETMAXTIPWIDTH, 0, 400);   // permite várias linhas
    }
    RECT rb;
    GetWindowRect(gBarra, &rb);
    gVertical = rb.bottom - rb.top > rb.right - rb.left;
    gTam = {};
    SetRectEmpty(&gUltimo);
    Pintar();
    ShowWindow(gJanela, SW_SHOWNOACTIVATE);
    return true;
}

void Destruir() {
    if (gRebar && IsWindow(gRebar) && gBarra && IsWindow(gBarra)) {
        // Windows 10: pede pro Explorer redistribuir os botões na largura toda de novo.
        RECT r;
        GetClientRect(gBarra, &r);
        PostMessageW(gBarra, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.right, r.bottom));
    }
    if (gDica) DestroyWindow(gDica);
    if (gJanela && IsWindow(gJanela)) DestroyWindow(gJanela);
    gDica = gJanela = nullptr;
    gBarra = gNotif = gRebar = nullptr;
    gHover = false;
}

void Atualizar(const Conteudo& c) {
    gConteudo = c;
    if (!Ativo()) return;
    if (gDica) {
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.hwnd = gApp;
        ti.uId = (UINT_PTR)gJanela;
        ti.lpszText = const_cast<wchar_t*>(gConteudo.dica.c_str());
        SendMessageW(gDica, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    }
    Pintar();
}

void Reposicionar() {
    if (!Ativo() || gTam.cx == 0) return;
    // Mudou o zoom da tela ou a barra foi pra lateral? Redesenha no tamanho novo.
    UINT dpi = GetDpiForWindow(gBarra);
    RECT barra, notif;
    GetWindowRect(gBarra, &barra);
    GetWindowRect(gNotif, &notif);
    bool vertical = barra.bottom - barra.top > barra.right - barra.left;
    if (dpi != gDpi || vertical != gVertical || gTam.cy != Altura()) {
        gDpi = dpi;
        gVertical = vertical;
        Pintar();                                  // Pintar chama Reposicionar de novo com o tamanho certo
        return;
    }
    RECT cliente;
    GetClientRect(gBarra, &cliente);
    int x, y;
    if (gVertical) {                               // barra lateral: logo acima da área de notificação
        POINT p{barra.left, notif.top};
        ScreenToClient(gBarra, &p);
        x = std::max(0, (int)(cliente.right - gTam.cx) / 2);
        y = std::max(0, (int)(p.y - gTam.cy - Px(2)));
    } else {                                       // barra horizontal: logo à esquerda da área de notificação
        int borda = notif.left;
        // Windows 10: o "Notícias e interesses"/clima fica colado à esquerda da bandeja; vamos pra antes dele.
        for (const wchar_t* cls : {L"ShellFeedsTaskbarViewWindow", L"DynamicContent2"}) {
            HWND f = FindWindowExW(gBarra, nullptr, cls, nullptr);
            RECT fr;
            if (f && IsWindowVisible(f) && GetWindowRect(f, &fr) && fr.left < borda && fr.right > fr.left)
                borda = fr.left;
        }
        POINT p{borda, barra.top};
        ScreenToClient(gBarra, &p);
        x = p.x - gTam.cx - Px(2);
        y = 0;
        // Windows 10: se os botões dos programas invadem o nosso espaço, encurta a área deles.
        if (gRebar && IsWindow(gRebar)) {
            RECT rr;
            GetWindowRect(gRebar, &rr);
            POINT tl{rr.left, rr.top};
            ScreenToClient(gBarra, &tl);
            if (tl.x + (rr.right - rr.left) > x && x > tl.x)
                MoveWindow(gRebar, tl.x, tl.y, x - tl.x, rr.bottom - rr.top, TRUE);
        }
    }
    RECT novo{x, y, x + gTam.cx, y + gTam.cy};
    bool coberto = GetWindow(gJanela, GW_HWNDPREV) != nullptr;   // alguma janela da barra ficou por cima?
    if (!coberto && EqualRect(&novo, &gUltimo)) return;          // nada mudou
    gUltimo = novo;
    SetWindowPos(gJanela, HWND_TOP, x, y, gTam.cx, gTam.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

}  // namespace widget
