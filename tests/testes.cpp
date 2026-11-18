// testes.cpp — testes automáticos. Rodam num Windows de verdade no GitHub Actions a cada envio.
// Testam as partes de "cálculo" (protocolo, estimativas, filtro, avisos, atualizador, ícones)
// sem precisar de mouse plugado nem de internet.
#include "atualizador.h"
#include "avisos.h"
#include "gdiplus_inc.h"
#include "historico.h"
#include "icone.h"
#include "leitores.h"
#include "suavizador.h"

#include <cmath>
#include <cstdio>

static int falhas = 0, total = 0;
#define CHECA(c)                                                   \
    do {                                                           \
        ++total;                                                   \
        if (!(c)) {                                                \
            printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c);  \
            ++falhas;                                              \
        }                                                          \
    } while (0)

static Leitura L(int p, double m, bool c = false) {
    Leitura l;
    l.percentual = p;
    l.momento = m;
    l.carregando = c;
    return l;
}

static void TestarProtocolo() {
    uint8_t q[] = {0x03, 0x55, 0x40, 0x01, 0x5D};
    int mo, es, ni;
    CHECA(InterpretarQuadroXenta(q, 5, mo, es, ni) && mo == 0x55 && es == 1 && ni == 93);
    uint8_t q2[] = {0x03, 0x24, 0x41, 0x01, 0x64};
    CHECA(InterpretarQuadroXenta(q2, 5, mo, es, ni) && ni == 100);
    uint8_t q3[] = {0x01, 0, 0, 0, 0};
    CHECA(!InterpretarQuadroXenta(q3, 5, mo, es, ni));
    CHECA(ConverterNivelXenta(0x55, 93, -1) == 93);
    CHECA(ConverterNivelXenta(0x10, 7, -1) == 70);      // X6/R1 manda em dezenas
    CHECA(ConverterNivelXenta(0x10, 70, -1) == -1);
    CHECA(ConverterNivelXenta(0x55, 7, 1) == 70);
    uint8_t r1[] = {0, 1, 0, 80, 1, 0, 0}, r2[] = {0, 1, 0, 80, 1, 1, 0}, r3[] = {0, 1, 0, 0, 0, 0, 0};
    CHECA(InterpretarRespostaRongyuan(r1, 7) == 80);
    CHECA(InterpretarRespostaRongyuan(r2, 7) == -1);
    CHECA(InterpretarRespostaRongyuan(r3, 7) == -1);
}

static void TestarHistorico() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring pasta = std::wstring(tmp) + L"sharkinfo_teste";
    CreateDirectoryW(pasta.c_str(), nullptr);
    DeleteFileW((pasta + L"\\leituras.csv").c_str());
    DeleteFileW((pasta + L"\\cargas.csv").c_str());
    double t0 = Agora() - 3 * 86400;
    {
        Historico h(pasta, 120, 60);
        auto e = h.EstimarDescarga(80);
        CHECA(e.fonte == EstimativaDescarga::PADRAO && std::fabs(e.minutos - 2880) < 1);
        double t = t0;
        int seq = 0;
        for (; t < t0 + 40 * 60; t += 60) h.Registrar(L((seq++ % 2) ? 90 : 80, t));   // pula-pula no começo
        for (; t < t0 + 100 * 60; t += 60) h.Registrar(L(70, t));
        for (; t < t0 + 160 * 60; t += 60) h.Registrar(L(60, t));
        h.Registrar(L(50, t));
        e = h.EstimarDescarga(50);
        CHECA(e.fonte == EstimativaDescarga::SESSAO && std::fabs(e.minutos - 300) < 15);
        double t2 = t + 3 * 3600;                                  // PC desligado 3 h
        h.Registrar(L(50, t2));
        h.Registrar(L(50, t2 + 600));
        h.Registrar(L(40, t2 + 1200));
        e = h.EstimarDescarga(40);
        CHECA(e.fonte == EstimativaDescarga::HISTORICO && std::fabs(e.minutos - 240) < 20);
        h.Registrar(L(40, t2 + 1300, true));                       // carga de 40% a 100% em 60 min
        auto c = h.EstimarCarga(t2 + 1300 + 600);
        CHECA(c.valida && !c.aprendido && std::fabs(c.minutos - 62) < 1);
        h.Registrar(L(100, t2 + 1300 + 3600, true));
        bool apr;
        CHECA(std::fabs(h.MinutosPorPorcentoCarga(apr) - 1.0) < 0.05 && apr);
    }
    {
        Historico h2(pasta, 120, 60);                              // relê dos arquivos
        bool apr;
        h2.MinutosPorPorcentoCarga(apr);
        CHECA(apr);
        CHECA(h2.LeiturasDesde(10 * 86400, Agora()).size() > 100);
    }
}

static void TestarSuavizador() {
    Suavizador sv;
    double t = 0;
    auto ler = [&](int p, bool c = false) {
        Leitura l = L(p, t, c);
        t += 10;
        return sv.Filtrar(l).percentual;
    };
    CHECA(ler(80) == 80);
    bool ficou = true;
    for (int i = 0; i < 90; ++i) ficou = ficou && ler(i % 3 == 0 ? 90 : 80) == 80;
    CHECA(ficou);                                                  // 80/90 oscilando: fica em 80
    int v = 0;
    for (int i = 0; i < 80; ++i) v = ler(90);
    CHECA(v == 90);                                                // 90 por mais de 10 min: sobe
    CHECA(ler(70) == 90);                                          // um 70 isolado não derruba
    CHECA(ler(-1, true) == 90);                                    // cabo sem %: mostra o último
    CHECA(ler(100, true) == 100);
    CHECA(ler(100) == 100);
    CHECA(ler(90) == 100);                                         // empate não derruba
}

static void TestarAvisos() {
    Config cfg;
    std::vector<std::wstring> ev;
    GerenciadorAvisos g(
        cfg, [&](const std::wstring& t, const std::wstring&, bool) { ev.push_back(t); },
        [&](const std::wstring&, COLORREF) {});
    for (int p : {50, 25, 20, 19, 18, 15, 10, 9, 22, 24, 20}) g.Processar(L(p, 0), L"1 h");
    CHECA(ev.size() == 2 && ev[0] == L"Bateria do mouse baixa" && ev[1] == L"Bateria do mouse CRÍTICA");
    g.Processar(L(30, 0), L"");
    g.Processar(L(20, 0), L"");
    CHECA(ev.size() == 3);
    g.Processar(L(-1, 0, true), L"");
    g.Processar(L(100, 0, true), L"");
    g.Processar(L(100, 0, true), L"");
    CHECA(ev.size() == 4 && ev.back() == L"Mouse carregado");
}

static void TestarAtualizador() {
    using namespace atualizador;
    CHECA(CompararVersoes(L"1.2.10", L"1.2.9") == 1);
    CHECA(CompararVersoes(L"v1.0.0", L"1.0.0") == 0);
    CHECA(CompararVersoes(L"1.0", L"1.0.1") == -1);
    CHECA(CompararVersoes(L"1.0.0-dev", L"1.0.0") == -1);
    CHECA(CompararVersoes(L"0.0.0-dev", L"1.0.0") == -1);
    // Pedaço de uma resposta real da API do GitHub (com acento escapado e emoji).
    std::string json = R"({"url":"x","tag_name":"v1.3.0","name":"SharkInfo v1.3.0",
      "body":"## Mudanças\r\n- Filtro novo 🦈\n- \"aspas\"",
      "assets":[{"name":"SharkInfo.exe","browser_download_url":"https://github.com/Pedr030/sharkinfo/releases/download/v1.3.0/SharkInfo.exe"},
                {"name":"SharkInfo.exe.sha256","browser_download_url":"https://github.com/Pedr030/sharkinfo/releases/download/v1.3.0/SharkInfo.exe.sha256"}]})";
    InfoRelease r;
    CHECA(InterpretarRelease(json, r));
    CHECA(r.versao == L"1.3.0");
    CHECA(r.notas.find(L"Mudanças") != std::wstring::npos);
    CHECA(r.notas.find(L"\"aspas\"") != std::wstring::npos);
    CHECA(r.notas.find(L"\U0001F988") != std::wstring::npos || r.notas.find(L"\xD83E\xDD88") != std::wstring::npos);
    CHECA(r.urlExe.size() > 10 && r.urlSha.find(".sha256") != std::string::npos);
    CHECA(!InterpretarRelease(R"({"tag_name":"v2.0.0","assets":[]})", r));   // sem os anexos: ignora
    std::string hex = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    CHECA(LerHashDoArquivoSha(hex + "  SharkInfo.exe\n") == hex);
    CHECA(LerHashDoArquivoSha("curto") == "");
    // SHA-256 de um arquivo vazio é conhecido: e3b0c442...
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring vazio = std::wstring(tmp) + L"sharkinfo_vazio.bin";
    HANDLE f = CreateFileW(vazio.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    CloseHandle(f);
    CHECA(Sha256Hex(vazio) == hex);
    DeleteFileW(vazio.c_str());
}

static void TestarIcones() {
    Gdiplus::GdiplusStartupInput in;
    ULONG_PTR tok;
    Gdiplus::GdiplusStartup(&tok, &in, nullptr);
    for (int tam : {16, 20, 24, 32}) {
        HICON a = DesenharIcone(80, false, false, 20, 10, tam, false);
        HICON b = DesenharIcone(80, false, false, 20, 10, tam, true, true);
        HICON c = DesenharIcone(-1, true, false, 20, 10, tam, false);
        CHECA(a && b && c);
        DestroyIcon(a);
        DestroyIcon(b);
        DestroyIcon(c);
    }
    CHECA(SalvarIconePng(L"icone_teste.png", 15, false, false, 64, false));
    DeleteFileW(L"icone_teste.png");
    Gdiplus::GdiplusShutdown(tok);
}

int wmain() {
    TestarProtocolo();
    TestarHistorico();
    TestarSuavizador();
    TestarAvisos();
    TestarAtualizador();
    TestarIcones();
    printf("%d de %d verificações passaram\n", total - falhas, total);
    return falhas ? 1 : 0;
}
