// polling.h — descobre a "taxa de polling" do mouse (quantas vezes por segundo ele avisa o PC).
//
// Analogia: o polling é o ritmo de um carteiro. Um mouse de 1000 Hz passa 1000 vezes por segundo;
// um de 125 Hz, 125 vezes. Quanto mais rápido o ritmo, mais a bateria gasta, por isso vale saber.
//
// Nenhum receptor Attack Shark conta isso pelo USB (só entrega a bateria). Então a gente MEDE: o Windows
// avisa cada movimento do mouse (Raw Input) e o app conta quantos avisos chegam por segundo enquanto o
// mouse está se mexendo. Por isso a primeira medição só aparece depois que você mexer o mouse um pouco.
#pragma once
#include "comum.h"

#include <vector>

namespace polling {

// ---- As contas (sem Windows, por isso dá pra testar) -----------------------------------------------

// Acompanha os movimentos de UM mouse. Cada Evento() é um aviso de movimento, com a hora em milissegundos.
class Medidor {
public:
    bool Evento(double ms);   // true = acabou de fechar uma medida nova (vale chamar Hz() de novo)
    int Hz() const;           // taxa comum mais próxima (125, 250, 500, 1000...). 0 = ainda sem dados suficientes
    void Zerar();

private:
    bool FecharTrecho();
    double inicio_ = 0, ultimo_ = -1;   // o "trecho" é um movimento contínuo, sem pausas
    int n_ = 0;
    std::vector<double> taxas_;         // taxa medida em cada trecho recente (avisos por segundo)
};

// 947 -> 1000, 480 -> 500, 126 -> 125. Fora da faixa de 100 a 9000 Hz devolve o próprio número arredondado.
int AjustarParaTaxaComum(double hz);

// ---- A parte do Windows ----------------------------------------------------------------------------

struct Resultado {
    int hz = 0;               // 0 = nunca medido
    bool aoVivo = false;      // true = medido nesta execução; false = a última medição salva
    double momento = 0;       // quando foi medido (segundos Unix)
    unsigned eventos = 0;     // quantos avisos de movimento o Windows já mandou (pra saber se a escuta funciona)
    int trechos = 0;          // quantos trechos de movimento já serviram de medida
};

// Liga a medição numa thread própria (a principal precisa ficar livre). 'pasta' guarda o polling.csv.
bool Iniciar(HINSTANCE inst, const std::wstring& pasta);
void Parar();
Resultado Atual();

}  // namespace polling
