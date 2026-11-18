// icone.cpp — desenho do ícone com GDI+.
//
// Receita (as medidas são frações do tamanho S do ícone, então funciona em 16, 20, 24, 32 px...):
//   1. contorno do mouse: uma "cápsula" alta, encostada no canto superior esquerdo
//   2. a linha que separa os dois botões
//   3. "borracha": apaga um círculo um pouco maior que o selo, pra ele não encostar no contorno
//   4. o selo colorido no canto inferior direito, com raio/z/? dentro quando precisa
#include "icone.h"
#include "gdiplus_inc.h"

using namespace Gdiplus;

COLORREF CorDoNivel(int pct, bool carregando, bool dormindo, int aviso, int critico) {
    if (carregando) return RGB(10, 132, 255);                  // azul
    if (pct < 0 || dormindo) return RGB(142, 142, 147);        // cinza
    if (pct > 50) return RGB(52, 199, 89);                     // verde
    if (pct > aviso) return RGB(255, 204, 0);                  // amarelo
    if (pct > critico) return RGB(255, 149, 0);                // laranja
    return RGB(255, 59, 48);                                   // vermelho
}

bool TemaClaro(bool sistema) {
    DWORD valor = 0, tam = sizeof(valor);
    // O Windows guarda as duas escolhas de tema aqui (1 = claro, 0 = escuro).
    LSTATUS r = RegGetValueW(HKEY_CURRENT_USER,
                             L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                             sistema ? L"SystemUsesLightTheme" : L"AppsUseLightTheme", RRF_RT_REG_DWORD,
                             nullptr, &valor, &tam);
    return r == ERROR_SUCCESS && valor == 1;
}

Bitmap* CriarBitmapMouse(int pct, bool carregando, bool dormindo, int aviso, int critico, int tam, bool claro) {
    const float S = (float)tam;
    auto* bmp = new Bitmap(tam, tam, PixelFormat32bppARGB);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);                // bordas suaves
    g.SetPixelOffsetMode(PixelOffsetModeHalf);                 // alinha melhor com a grade de pixels
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    g.Clear(Color(0, 0, 0, 0));                                // fundo transparente

    // 1) Contorno do mouse.
    Color corTraco = claro ? Color(255, 28, 28, 28) : Color(255, 255, 255, 255);
    float esp = std::max(1.0f, S * 0.085f);                    // espessura da linha
    float w = S * 0.60f, h = S * 0.88f;                        // largura e altura do mouse
    float x0 = S * 0.10f, y0 = S * 0.02f;
    // A caneta desenha no MEIO da linha; recuamos meia espessura pra o traço ficar dentro da caixa.
    float a = x0 + esp / 2, b = y0 + esp / 2, ww = w - esp, hh = h - esp;
    GraphicsPath capsula;
    capsula.AddArc(a, b, ww, ww, 180, 180);                    // meia-lua de cima
    capsula.AddArc(a, b + hh - ww, ww, ww, 0, 180);            // meia-lua de baixo (as retas ligam sozinhas)
    capsula.CloseFigure();
    Pen caneta(corTraco, esp);
    g.DrawPath(&caneta, &capsula);

    // 2) Divisória dos botões.
    float cx = x0 + w / 2;
    caneta.SetStartCap(LineCapFlat);
    g.DrawLine(&caneta, cx, y0 + esp * 0.5f, cx, y0 + h * 0.36f);

    // 3) Borracha em volta do selo (SourceCopy = "pinta transparente por cima", apagando).
    float rs = S * 0.22f;                                      // raio do selo
    float sx = S - rs - S * 0.01f, sy = S - rs - S * 0.01f;    // centro do selo
    float folga = S * 0.07f;
    g.SetCompositingMode(CompositingModeSourceCopy);
    SolidBrush transparente(Color(0, 0, 0, 0));
    g.FillEllipse(&transparente, sx - rs - folga, sy - rs - folga, 2 * (rs + folga), 2 * (rs + folga));
    g.SetCompositingMode(CompositingModeSourceOver);           // volta ao normal

    // 4) O selo colorido.
    COLORREF c = CorDoNivel(pct, carregando, dormindo, aviso, critico);
    SolidBrush corSelo(Color(255, GetRValue(c), GetGValue(c), GetBValue(c)));
    g.FillEllipse(&corSelo, sx - rs, sy - rs, 2 * rs, 2 * rs);
    SolidBrush branco(Color(255, 255, 255, 255));
    if (carregando) {                                          // raio
        float k = rs / 10;
        PointF raio[] = {{sx + 1.2f * k, sy - 6.5f * k}, {sx - 3.8f * k, sy + 1.0f * k}, {sx - 0.4f * k, sy + 1.0f * k},
                         {sx - 1.6f * k, sy + 6.5f * k}, {sx + 3.8f * k, sy - 1.2f * k}, {sx + 0.4f * k, sy - 1.2f * k},
                         {sx + 1.8f * k, sy - 6.5f * k}};
        g.FillPolygon(&branco, raio, 7);
    } else if (dormindo || pct < 0) {                          // "z" ou "?"
        FontFamily familia(L"Segoe UI");
        Font fonte(&familia, rs * 1.45f, FontStyleBold, UnitPixel);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        RectF caixa(sx - rs, sy - rs, 2 * rs, 2 * rs);
        g.DrawString(pct < 0 ? L"?" : L"z", -1, &fonte, caixa, &fmt, &branco);
    }
    return bmp;
}

// Modo "número no ícone": quadrado arredondado na cor do nível, com a % em branco.
static Bitmap* CriarBitmapNumero(int pct, bool carregando, bool dormindo, int aviso, int critico, int tam) {
    const float S = (float)tam;
    auto* bmp = new Bitmap(tam, tam, PixelFormat32bppARGB);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));
    COLORREF c = CorDoNivel(pct, carregando, dormindo, aviso, critico);
    SolidBrush fundo(Color(255, GetRValue(c), GetGValue(c), GetBValue(c)));
    float r = S * 0.19f, d = r * 2, w = S - 1;
    GraphicsPath caminho;                                      // 4 cantos arredondados
    caminho.AddArc(0.0f, 0.0f, d, d, 180, 90);
    caminho.AddArc(w - d, 0.0f, d, d, 270, 90);
    caminho.AddArc(w - d, w - d, d, d, 0, 90);
    caminho.AddArc(0.0f, w - d, d, d, 90, 90);
    caminho.CloseFigure();
    g.FillPath(&fundo, &caminho);
    std::wstring texto = pct < 0 ? L"?" : std::to_wstring(pct);
    float prop = texto.size() == 1 ? 0.78f : texto.size() == 2 ? 0.66f : 0.47f;   // "100" precisa de letra menor
    FontFamily familia(L"Segoe UI");
    Font fonte(&familia, S * prop, FontStyleBold, UnitPixel);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    SolidBrush branco(Color(255, 255, 255, 255));
    RectF caixa(0, S * 0.02f, S, S);
    g.DrawString(texto.c_str(), -1, &fonte, caixa, &fmt, &branco);
    return bmp;
}

HICON DesenharIcone(int pct, bool carregando, bool dormindo, int aviso, int critico, int tam, bool claro,
                    bool numero) {
    Bitmap* bmp = numero ? CriarBitmapNumero(pct, carregando, dormindo, aviso, critico, tam)
                         : CriarBitmapMouse(pct, carregando, dormindo, aviso, critico, tam, claro);
    HICON icone = nullptr;
    bmp->GetHICON(&icone);                                     // converte a imagem em ícone do Windows
    delete bmp;
    return icone;
}

// Acha o "codificador" de PNG do GDI+ (necessário pra salvar).
static bool AcharCodificadorPng(CLSID& clsid) {
    UINT n = 0, tam = 0;
    GetImageEncodersSize(&n, &tam);
    if (!tam) return false;
    std::vector<BYTE> buf(tam);
    auto* cods = reinterpret_cast<ImageCodecInfo*>(buf.data());
    GetImageEncoders(n, tam, cods);
    for (UINT i = 0; i < n; ++i)
        if (wcscmp(cods[i].MimeType, L"image/png") == 0) {
            clsid = cods[i].Clsid;
            return true;
        }
    return false;
}

bool SalvarIconePng(const wchar_t* arquivo, int pct, bool carregando, bool dormindo, int tam, bool claro) {
    CLSID png;
    if (!AcharCodificadorPng(png)) return false;
    Bitmap* bmp = CriarBitmapMouse(pct, carregando, dormindo, 20, 10, tam, claro);
    bool ok = bmp->Save(arquivo, &png, nullptr) == Ok;
    delete bmp;
    return ok;
}
