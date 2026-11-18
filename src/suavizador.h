// suavizador.h — filtro que impede a % de "pular pra cima e pra baixo".
//
// Por que pula: o mouse calcula a carga pela TENSÃO da bateria. Em uso ela cai; em repouso ela
// se recupera um pouco (igual celular que "ganha" 1% parado). Em modelos que só informam de 10 em 10%
// (X6/R1), estar na divisa entre dois degraus faz o número ficar indo e voltando: 80, 90, 80...
//
// As regras (pensa num juiz que só muda o placar com certeza):
//   * DESCER: a maioria das leituras dos últimos 3 minutos tem que concordar (a "moda" da janela).
//   * SUBIR sem estar no cabo: o valor mais alto tem que se manter por 10 minutos seguidos.
//     (Cobre o caso de você ter carregado o mouse em outro PC, por exemplo.)
//   * Saiu do cabo: aceita o valor novo na hora, sem esperar.
#pragma once
#include "comum.h"

#include <deque>
#include <map>

class Suavizador {
public:
    static constexpr double JANELA = 180;       // 3 minutos
    static constexpr double PRAZO_SUBIR = 600;  // 10 minutos

    Leitura Filtrar(const Leitura& l) {
        Leitura s = l;
        if (l.carregando) {                      // no cabo: passa direto e "zera" a memória
            estavaCarregando_ = true;
            janela_.clear();
            subindoDesde_ = -1;
            if (l.percentual >= 0) mostrado_ = l.percentual;
            else if (mostrado_ >= 0) s.percentual = mostrado_;   // cabo sem % informada: mostra o último
            return s;
        }
        if (l.dormindo || l.percentual < 0) {    // sem leitura confiável: mostra o último valor bom
            if (mostrado_ >= 0) s.percentual = mostrado_;
            return s;
        }
        if (estavaCarregando_ || mostrado_ < 0) {  // acabou de sair do cabo, ou é a 1ª leitura
            estavaCarregando_ = false;
            mostrado_ = l.percentual;
            janela_.clear();
            janela_.push_back({l.momento, l.percentual});
            subindoDesde_ = -1;
            return s;
        }
        janela_.push_back({l.momento, l.percentual});
        while (!janela_.empty() && l.momento - janela_.front().first > JANELA)
            janela_.pop_front();                 // esquece o que tem mais de 3 minutos

        int moda = Moda();
        if (moda < mostrado_) {                  // a maioria diz que caiu: desce
            mostrado_ = moda;
            subindoDesde_ = -1;
        } else if (moda > mostrado_) {           // diz que subiu: só depois de 10 min seguidos
            if (subindoDesde_ < 0) subindoDesde_ = l.momento;
            else if (l.momento - subindoDesde_ >= PRAZO_SUBIR) {
                mostrado_ = moda;
                subindoDesde_ = -1;
            }
        } else {
            subindoDesde_ = -1;                  // voltou ao normal: zera a contagem da subida
        }
        s.percentual = mostrado_;
        return s;
    }

private:
    // Moda = o valor que mais aparece na janela. Empate: fica com o que já está na tela (estabilidade).
    int Moda() const {
        std::map<int, int> contagem;
        for (const auto& p : janela_) contagem[p.second]++;
        int melhor = mostrado_;
        int vezes = contagem.count(mostrado_) ? contagem.at(mostrado_) : 0;
        for (const auto& [valor, n] : contagem)
            if (n > vezes) {                     // só troca se aparecer MAIS vezes (empate não conta)
                melhor = valor;
                vezes = n;
            }
        return melhor;
    }

    std::deque<std::pair<double, int>> janela_;  // (momento, % cru) dos últimos 3 min
    int mostrado_ = -1;                          // o que estamos exibindo
    double subindoDesde_ = -1;                   // desde quando a moda está acima do exibido
    bool estavaCarregando_ = false;
};
