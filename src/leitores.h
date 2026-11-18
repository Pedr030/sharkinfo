// leitores.h — quem "conversa" com o receptor USB do mouse.
//
// O receptor é como um rádio. Dois tipos:
//   * Xenta (VID 1D57; X11, X6, R1, X3, V3 Pro, V6...): TRANSMITE a bateria sozinho a cada ~2 s.
//     A gente só escuta.
//   * Rongyuan (R3 e vários outros): só responde se a gente PERGUNTAR.
// E um leitor de mentira (demo) pra testar os avisos sem esperar a bateria acabar.
#pragma once
#include "comum.h"

#include <memory>

// Interface comum: todo leitor sabe "ler".
class Leitor {
public:
    virtual ~Leitor() = default;
    // Preenche 'saida'. Devolve false se o receptor sumiu (aí o app procura de novo).
    virtual bool Ler(Leitura& saida) = 0;
};

// Procura o receptor plugado e devolve o leitor certo (ou nullptr se não achar).
// forcarDezenas: -1 = automático, 1 = nível vem em dezenas (0..10), 0 = vem em %.
std::unique_ptr<Leitor> DetectarLeitor(int forcarDezenas);
std::unique_ptr<Leitor> CriarLeitorDemo();

// Texto com tudo que o programa enxerga no USB (pra quando o mouse não é detectado).
std::wstring RelatorioDiagnostico();

// Funções "puras" (sem USB), separadas pra poderem ser testadas.
bool InterpretarQuadroXenta(const uint8_t* dados, size_t n, int& modelo, int& estado, int& nivel);
int ConverterNivelXenta(int modelo, int nivel, int forcarDezenas);   // -1 = inválido
int InterpretarRespostaRongyuan(const uint8_t* r, size_t n);          // -1 = sem dado
