// testes.cpp — testes automáticos. Rodam num Windows de verdade no GitHub Actions a cada envio.
// Testam as partes de "cálculo" (protocolo, estimativas, filtro, avisos, atualizador, ícones)
// sem precisar de mouse plugado nem de internet.
#include "atualizador.h"
#include "avisos.h"
#include "gdiplus_inc.h"
#include "historico.h"
#include "icone.h"
#include "leitores.h"
#include "polling.h"
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
        Historico h(pasta, 120);
        auto e = h.EstimarDescarga(80);
        CHECA(e.minutos < 0);                                      // sem dados: "calculando…", nada de chute
        double t = t0;
        int seq = 0;
        for (; t < t0 + 40 * 60; t += 60) h.Registrar(L((seq++ % 2) ? 90 : 80, t));   // pula-pula no começo
        for (; t < t0 + 100 * 60; t += 60) h.Registrar(L(70, t));
        for (; t < t0 + 160 * 60; t += 60) h.Registrar(L(60, t));
        h.Registrar(L(50, t));
        e = h.EstimarDescarga(50);
        CHECA(e.fonte == EstimativaDescarga::SESSAO && std::fabs(e.minutos - 300) < 15);
        double t2 = t + 3 * 3600;                                  // PC desligado 3 h (o buraco não conta como uso)
        h.Registrar(L(50, t2));
        h.Registrar(L(50, t2 + 600));
        h.Registrar(L(40, t2 + 1200));
        e = h.EstimarDescarga(40);                                 // marcos 70..40: 30% em 120+20 min de uso
        CHECA(e.fonte == EstimativaDescarga::SESSAO && std::fabs(e.minutos - 187) < 10);
        h.Registrar(L(40, t2 + 1300, true));                       // carga de 40% a 100% em 60 min
        auto c = h.EstimarCarga(t2 + 1300 + 600);
        CHECA(c.valida && !c.aprendido && std::fabs(c.minutos - 62) < 1);
        h.Registrar(L(100, t2 + 1300 + 3600, true));
        bool apr;
        CHECA(std::fabs(h.MinutosPorPorcentoCarga(apr) - 1.0) < 0.05 && apr);
        h.Registrar(L(100, t2 + 1300 + 3600 + 60));                // tirou do cabo: descarga nova, sem marcos
        e = h.EstimarDescarga(100);
        CHECA(e.fonte == EstimativaDescarga::HISTORICO && std::fabs(e.minutos - 467) < 20);
    }
    {
        // Mouse carregado fora do app: depois do buraco a % subiu 40 pontos sem cabo. Não pode misturar
        // com a descarga de antes, senão os marcos novos nunca apareceriam.
        std::wstring pasta2 = std::wstring(tmp) + L"sharkinfo_teste2";
        CreateDirectoryW(pasta2.c_str(), nullptr);
        DeleteFileW((pasta2 + L"\\leituras.csv").c_str());
        DeleteFileW((pasta2 + L"\\cargas.csv").c_str());
        Historico h3(pasta2, 120);
        double u = Agora() - 2 * 86400;
        h3.Registrar(L(80, u));
        h3.Registrar(L(70, u + 1200));
        h3.Registrar(L(60, u + 2400));
        double u2 = u + 2400 + 5 * 3600;
        h3.Registrar(L(100, u2));
        h3.Registrar(L(90, u2 + 1200));
        h3.Registrar(L(80, u2 + 2400));
        auto e3 = h3.EstimarDescarga(80);                          // marcos 90 e 80: 10% em 20 min -> 80% dura 160 min
        CHECA(e3.fonte == EstimativaDescarga::SESSAO && std::fabs(e3.minutos - 160) < 5);
    }
    {
        // Sem taxa ainda, mas 40 min no mesmo degrau: "dura pelo menos" 80 * 40 / 10 = 320 min.
        std::wstring pasta3 = std::wstring(tmp) + L"sharkinfo_teste3";
        CreateDirectoryW(pasta3.c_str(), nullptr);
        DeleteFileW((pasta3 + L"\\leituras.csv").c_str());
        DeleteFileW((pasta3 + L"\\cargas.csv").c_str());
        Historico h4(pasta3, 120);
        double v = Agora() - 2 * 86400;
        h4.Registrar(L(90, v));
        for (int m = 1; m <= 41; ++m) h4.Registrar(L(80, v + m * 60));
        auto e4 = h4.EstimarDescarga(80);
        CHECA(e4.fonte == EstimativaDescarga::MINIMO && std::fabs(e4.minutos - 320) < 10);
        h4.Registrar(L(80, v + 42 * 60));
        for (int m = 43; m <= 60; ++m) h4.Registrar(L(70, v + m * 60));   // caiu pro 70: já são 2 marcos (80 e 70)
        e4 = h4.EstimarDescarga(70);                               // 10% em 42 min -> 70% dura 294 min
        CHECA(e4.fonte == EstimativaDescarga::SESSAO && std::fabs(e4.minutos - 294) < 10);
    }
    {
        // Mouse pulando 90 ↔ 80 na divisa por 50 min: o marco do 80 vale o fim do pula-pula, não o 1º 80.
        std::wstring pasta4 = std::wstring(tmp) + L"sharkinfo_teste4";
        CreateDirectoryW(pasta4.c_str(), nullptr);
        DeleteFileW((pasta4 + L"\\leituras.csv").c_str());
        DeleteFileW((pasta4 + L"\\cargas.csv").c_str());
        Historico h5(pasta4, 120);
        double w = Agora() - 2 * 86400;
        int m = 0;
        for (; m < 50; ++m) h5.Registrar(L(m % 2 ? 80 : 90, w + m * 60));
        for (; m < 110; ++m) h5.Registrar(L(80, w + m * 60));
        for (; m < 170; ++m) h5.Registrar(L(70, w + m * 60));
        h5.Registrar(L(60, w + 170 * 60));
        auto e5 = h5.EstimarDescarga(60);                          // marcos 80 (min 49), 70 (110), 60 (170)
        CHECA(e5.fonte == EstimativaDescarga::SESSAO && std::fabs(e5.minutos - 360) < 15);
    }
    {
        Historico h2(pasta, 120);                                  // relê dos arquivos
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

// Simula um mouse mexendo: 'taxa' avisos por segundo, em rajadas de 0,6 s com pausas de 0,2 s.
// 'falha' = fração de avisos que não vieram (mouse devagar: nem todo aviso tem movimento).
static int MedirSimulado(double taxa, double falha, double segundos) {
    polling::Medidor m;
    double passo = 1000.0 / taxa, t = 5000;
    unsigned semente = 12345;
    for (double fim = t + segundos * 1000; t < fim;) {
        for (double r = 0; r < 600; r += passo, t += passo) {
            semente = semente * 1103515245u + 12345u;
            double sorteio = ((semente >> 16) & 0x7fff) / 32768.0;
            double jitter = (sorteio - 0.5) * passo * 0.3;       // o Windows entrega com um pouco de bagunça
            if (sorteio >= falha) m.Evento(t + jitter);
        }
        t += 200;
    }
    return m.Hz();
}

static void TestarPolling() {
    CHECA(polling::AjustarParaTaxaComum(947) == 1000);
    CHECA(polling::AjustarParaTaxaComum(480) == 500);
    CHECA(polling::AjustarParaTaxaComum(130) == 125);
    CHECA(polling::AjustarParaTaxaComum(1900) == 2000);
    CHECA(polling::AjustarParaTaxaComum(50) == 50);                 // fora da faixa: devolve o número
    CHECA(MedirSimulado(1000, 0, 4) == 1000);
    CHECA(MedirSimulado(500, 0, 4) == 500);
    CHECA(MedirSimulado(125, 0, 6) == 125);
    CHECA(MedirSimulado(1000, 0.12, 4) == 1000);                    // mouse devagar, 12% dos avisos faltando
    polling::Medidor poucos;
    for (int i = 0; i < 30; ++i) poucos.Evento(1000 + i);           // 30 ms de movimento: pouco demais
    CHECA(poucos.Hz() == 0);
}

int wmain() {
    TestarPolling();
    TestarProtocolo();
    TestarHistorico();
    TestarSuavizador();
    TestarAvisos();
    TestarAtualizador();
    TestarIcones();
    printf("%d de %d verificações passaram\n", total - falhas, total);
    return falhas ? 1 : 0;
}
