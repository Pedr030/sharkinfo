// historico.cpp — mesma lógica da versão Python, agora com arquivos .csv em vez de SQLite.
#include "historico.h"

#include <algorithm>
#include <cstdio>

static constexpr double INTERVALO_MINIMO = 60;           // grava no máx. 1 linha/minuto se nada mudar
static constexpr double GUARDAR_SEGUNDOS = 30 * 86400.0; // mantém 30 dias em memória

Historico::Historico(const std::wstring& pasta, double minutosCargaPadrao)
    : arqLeituras_(pasta + L"\\leituras.csv"),
      arqCargas_(pasta + L"\\cargas.csv"),
      minutosCargaPadrao_(minutosCargaPadrao) {
    double limite = Agora() - GUARDAR_SEGUNDOS;
    // Lê o que já foi gravado em execuções anteriores.
    if (FILE* f = _wfopen(arqLeituras_.c_str(), L"r")) {
        double m;
        int p, c;
        while (fscanf(f, "%lf;%d;%d", &m, &p, &c) == 3)       // cada linha: momento;percentual;carregando
            if (m >= limite) leituras_.push_back({m, p, c != 0});
        fclose(f);
    }
    if (FILE* f = _wfopen(arqCargas_.c_str(), L"r")) {
        Carga c;
        while (fscanf(f, "%lf;%lf;%d", &c.inicio, &c.fim, &c.de) == 3) cargas_.push_back(c);
        fclose(f);
    }
    std::sort(leituras_.begin(), leituras_.end(),
              [](const PontoHistorico& a, const PontoHistorico& b) { return a.momento < b.momento; });
}

void Historico::Anexar(const std::wstring& arquivo, const std::string& linha) {
    if (FILE* f = _wfopen(arquivo.c_str(), L"a")) {     // "a" = append: escreve no fim
        fputs(linha.c_str(), f);
        fclose(f);
    }
}

void Historico::DefinirCargaPadrao(double minutos) {
    Segurando t(trava_);
    minutosCargaPadrao_ = minutos;
}

void Historico::Registrar(const Leitura& l) {
    Segurando t(trava_);
    AcompanharCarga(l);
    if (l.percentual < 0 || l.dormindo) return;          // sem número confiável: não anota
    PontoHistorico p{l.momento, l.percentual, l.carregando};
    bool mudou = !temUltima_ || ultima_.percentual != p.percentual || ultima_.carregando != p.carregando;
    bool passouTempo = !temUltima_ || p.momento - ultima_.momento >= INTERVALO_MINIMO;
    if (!mudou && !passouTempo) return;
    leituras_.push_back(p);
    char linha[64];
    snprintf(linha, sizeof(linha), "%.0f;%d;%d\n", p.momento, p.percentual, p.carregando ? 1 : 0);
    Anexar(arqLeituras_, linha);
    ultima_ = p;
    temUltima_ = true;
}

void Historico::AcompanharCarga(const Leitura& l) {
    if (l.dormindo) return;
    if (!l.carregando) {                                  // na bateria
        if (l.percentual >= 0) ultimoDescarregando_ = l.percentual;
        cargaInicio_ = -1;                                // tirou do cabo no meio: descarta a carga em andamento
        return;
    }
    if (cargaInicio_ < 0) {                               // acabou de plugar
        cargaInicio_ = l.momento;
        cargaDe_ = ultimoDescarregando_;
    }
    if (l.percentual == 100 && cargaDe_ >= 0 && cargaDe_ < 95) {   // encheu: aprende com essa carga
        cargas_.push_back({cargaInicio_, l.momento, cargaDe_});
        char linha[96];
        snprintf(linha, sizeof(linha), "%.0f;%.0f;%d\n", cargaInicio_, l.momento, cargaDe_);
        Anexar(arqCargas_, linha);
        cargaDe_ = 100;                                   // não grava a mesma carga duas vezes
    }
}

std::vector<PontoHistorico> Historico::LeiturasDesde(double segundos, double agora) const {
    Segurando t(trava_);
    std::vector<PontoHistorico> r;
    for (const auto& p : leituras_)
        if (p.momento >= agora - segundos) r.push_back(p);
    return r;
}

static constexpr double BURACO = 30 * 60;          // 30 min sem leitura = o mouse dormiu / o PC desligou
static constexpr int SALTO_RECARGA = 20;            // subir tanto depois de um buraco = foi carregado fora do app

// Cada descarga vai de um cabo ao próximo. Dentro dela o "relógio de uso" só anda enquanto há leituras
// seguidas: um buraco (mouse dormindo, PC desligado) não conta. Assim, se o mouse caiu de 80 para 70
// ontem e de 70 para 60 hoje, a taxa já sai com 2 marcos, em vez de esperar 2 marcos no mesmo dia.
std::vector<std::vector<std::pair<double, double>>> SepararSessoes(const std::vector<PontoHistorico>& pts) {
    std::vector<std::vector<std::pair<double, double>>> sessoes;
    std::vector<std::pair<double, double>> atual;
    double anterior = -1, relogio = 0;
    int pctAnterior = -1;
    for (const auto& p : pts) {
        bool buraco = anterior >= 0 && p.momento - anterior > BURACO;
        bool recarregouFora = buraco && pctAnterior >= 0 && p.percentual >= pctAnterior + SALTO_RECARGA;
        if ((p.carregando || recarregouFora) && !atual.empty()) {   // fecha a descarga em andamento
            sessoes.push_back(atual);
            atual.clear();
            relogio = 0;
        }
        if (!p.carregando) {
            if (!atual.empty() && !buraco) relogio += p.momento - anterior;   // só soma tempo "de uso"
            atual.push_back({relogio, (double)p.percentual});
            pctAnterior = p.percentual;
        }
        anterior = p.momento;
    }
    if (!atual.empty()) sessoes.push_back(atual);
    return sessoes;
}

double TaxaPorMarcos(const std::vector<std::pair<double, double>>& sessao) {
    if (sessao.empty()) return -1;
    double minimo = sessao.front().second;
    std::vector<std::pair<double, double>> marcos;
    for (const auto& p : sessao)
        if (p.second < minimo) {                         // chegou num valor mais baixo que nunca: marco!
            minimo = p.second;
            marcos.push_back(p);
        }
    if (marcos.size() < 2) return -1;
    double tempo = marcos.back().first - marcos.front().first;
    double queda = marcos.front().second - marcos.back().second;
    if (tempo < 10 * 60 || queda <= 0) return -1;
    return queda / tempo;                                // % por segundo
}

EstimativaDescarga Historico::EstimarDescarga(int pctAtual) const {
    EstimativaDescarga e;
    if (pctAtual < 0) return e;
    Segurando t(trava_);
    auto sessoes = SepararSessoes(leituras_);
    // 1) Medida na sessão de agora (a última, se ela ainda está "viva").
    bool sessaoViva = !leituras_.empty() && !leituras_.back().carregando && !sessoes.empty();
    if (sessaoViva) {
        double taxa = TaxaPorMarcos(sessoes.back());
        if (taxa > 0) {
            e.minutos = pctAtual / taxa / 60;            // % ÷ (% por segundo) = segundos -> minutos
            e.fonte = EstimativaDescarga::SESSAO;
            return e;
        }
    }
    // 2) Aprendida nas sessões anteriores: média ponderada pelo tempo de cada uma.
    double somaQueda = 0, somaTempo = 0;
    for (size_t i = 0; i + (sessaoViva ? 1 : 0) < sessoes.size(); ++i) {
        double taxa = TaxaPorMarcos(sessoes[i]);
        if (taxa <= 0) continue;
        double tempo = sessoes[i].back().first - sessoes[i].front().first;
        somaQueda += taxa * tempo;
        somaTempo += tempo;
    }
    if (somaTempo > 0) {
        e.minutos = pctAtual / (somaQueda / somaTempo) / 60;
        e.fonte = EstimativaDescarga::HISTORICO;
        return e;
    }
    // 3) Ainda não tem 2 marcos em lugar nenhum: sem chute, a interface mostra "calculando…".
    return e;
}

double Historico::MinutosPorPorcentoCarga(bool& aprendido) const {
    Segurando t(trava_);
    std::vector<double> taxas;
    for (auto it = cargas_.rbegin(); it != cargas_.rend() && taxas.size() < 5; ++it)  // últimas 5 cargas
        if (it->de < 100 && it->fim > it->inicio)
            taxas.push_back((it->fim - it->inicio) / 60.0 / (100 - it->de));
    if (taxas.empty()) {
        aprendido = false;
        return minutosCargaPadrao_ / 100.0;
    }
    std::sort(taxas.begin(), taxas.end());                // mediana: o valor do meio
    size_t n = taxas.size();
    aprendido = true;
    return n % 2 ? taxas[n / 2] : (taxas[n / 2 - 1] + taxas[n / 2]) / 2;
}

EstimativaCarga Historico::EstimarCarga(double agora) const {
    EstimativaCarga e;
    double inicio;
    int de;
    {
        Segurando t(trava_);
        inicio = cargaInicio_;
        de = cargaDe_;
    }
    if (inicio < 0 || de < 0 || de >= 100) return e;
    double taxa = MinutosPorPorcentoCarga(e.aprendido);
    double passados = (agora - inicio) / 60.0;            // minutos no cabo
    double total = (100 - de) * taxa;                      // minutos pra encher a partir do % inicial
    e.valida = true;
    e.minutos = std::max(1.0, total - passados);           // nunca "0 min" antes do mouse avisar
    e.percentual = std::min(99, (int)(de + passados / taxa));
    return e;
}
