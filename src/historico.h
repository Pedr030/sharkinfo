// historico.h — o "caderninho" das leituras e as contas de tempo.
//
//  * Bateria restante: regressão linear (a "reta média") sobre a descarga atual.
//    Ex.: 80% às 14h e 70% às 16h = 5%/h; com 70% sobram ~14 h.
//  * Tempo de carga: no cabo o mouse não diz a %. É como um bolo de forno fechado:
//    sabemos quando entrou e quando apitou. Com algumas "fornadas", aprendemos o tempo.
//
// Os dados ficam em dois arquivos de texto simples na pasta do app:
//   leituras.csv -> momento;percentual;carregando
//   cargas.csv   -> inicio;fim;percentual_inicial
#pragma once
#include "comum.h"

struct PontoHistorico {
    double momento;
    int percentual;
    bool carregando;
};

struct EstimativaDescarga {
    double minutos = -1;       // quanto tempo de USO ainda resta (-1 = sem estimativa)
    enum Fonte { SESSAO, HISTORICO, PADRAO } fonte = PADRAO;
    // SESSAO = medida agora | HISTORICO = aprendida nos dias anteriores | PADRAO = chute inicial
};

struct EstimativaCarga {
    bool valida = false;
    double minutos = -1;       // quanto falta pra encher
    bool aprendido = false;    // já aprendeu com cargas reais, ou está no padrão?
    int percentual = -1;       // palpite do % atual
};

class Historico {
public:
    Historico(const std::wstring& pasta, double minutosCargaPadrao, double horasBateriaPadrao = 60);
    void Registrar(const Leitura& l);
    std::vector<PontoHistorico> LeiturasDesde(double segundos, double agora) const;
    EstimativaDescarga EstimarDescarga(int percentualAtual) const;
    double MinutosPorPorcentoCarga(bool& aprendido) const;
    EstimativaCarga EstimarCarga(double agora) const;
    void DefinirCargaPadrao(double minutos);
    void DefinirBateriaPadrao(double horas);

private:
    void AcompanharCarga(const Leitura& l);
    void Anexar(const std::wstring& arquivo, const std::string& linha);

    mutable Trava trava_;                   // a janela do gráfico lê enquanto a thread de leitura escreve
    std::wstring arqLeituras_, arqCargas_;
    std::vector<PontoHistorico> leituras_;  // tudo dos últimos 30 dias, em memória
    struct Carga { double inicio, fim; int de; };
    std::vector<Carga> cargas_;
    double minutosCargaPadrao_;
    double horasBateriaPadrao_;
    bool temUltima_ = false;
    PontoHistorico ultima_{};               // última linha gravada
    double cargaInicio_ = -1;               // quando plugou o cabo (-1 = não está carregando)
    int cargaDe_ = -1;                      // com quantos % estava
    int ultimoDescarregando_ = -1;          // último % visto fora do cabo
};

// Velocidade de descarga (% por segundo) de uma sessão, pelos "marcos de queda": os momentos em que
// a bateria chegou a um valor mais baixo do que já tinha chegado. Subidas (tensão se recuperando) são
// ignoradas. Precisa de 2 marcos com pelo menos 10 min entre eles. Devolve -1 se não der.
double TaxaPorMarcos(const std::vector<std::pair<double, double>>& sessao);
// Separa o histórico em sessões de uso: corta no cabo e em buracos sem leitura (PC desligado, mouse dormindo).
std::vector<std::vector<std::pair<double, double>>> SepararSessoes(const std::vector<PontoHistorico>& pts);
