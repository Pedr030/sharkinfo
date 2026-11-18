// widget.h — o "🖱 93%" encaixado na barra de tarefas, ao lado do relógio.
//
// O Windows não deixa escrever texto ao lado dos ícones da bandeja. O truque (o mesmo do app do
// krisgfx) é criar uma janelinha nossa e "pendurá-la" DENTRO da barra de tarefas, como filha dela,
// logo à esquerda da área de notificação. Ela tem fundo transparente e desenha o mouse com selo + a %.
//
// Como isso não é um recurso oficial, uma atualização do Windows pode mudar a barra. Se não der pra
// encaixar, Criar() devolve false e o app continua só com o ícone normal da bandeja.
#pragma once
#include "comum.h"

namespace widget {

struct Conteudo {
    int percentual = -1;        // -1 = desconhecido (mostra "—")
    bool carregando = false;
    bool dormindo = false;
    int aviso = 20, critico = 10;
    std::wstring dica;          // texto que aparece ao parar o mouse em cima
};

// msgClique chega na janelaApp com wParam 1 (clique esquerdo) ou 2 (direito) e lParam = x,y da tela.
bool Criar(HINSTANCE inst, HWND janelaApp, UINT msgClique);
void Destruir();
bool Ativo();
void Atualizar(const Conteudo& c);
void Reposicionar();            // chamar de tempos em tempos: a barra muda de layout sozinha

}  // namespace widget
