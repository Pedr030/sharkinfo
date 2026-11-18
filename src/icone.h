// icone.h — desenha o ícone da bandeja: um mouse em contorno com uma bolinha colorida
// no canto inferior direito (o "selo"), como os selos de notificação dos apps.
//
//   selo verde > 50% | amarelo > aviso | laranja > crítico | vermelho <= crítico
//   azul com raio = carregando | cinza com "z" = dormindo | cinza com "?" = não encontrado
//
// O contorno do mouse é branco na barra de tarefas escura e quase preto na clara.
#pragma once
#include "comum.h"

namespace Gdiplus { class Bitmap; }   // "aviso prévio" da classe, sem incluir o GDI+ inteiro aqui

// Cria um ícone novo (quem chama destrói com DestroyIcon quando trocar). percentual -1 = "?"
// numero = true: em vez do mouse, um quadradinho colorido com o número (opção "número no ícone").
HICON DesenharIcone(int percentual, bool carregando, bool dormindo, int aviso, int critico, int tamanho,
                    bool temaClaro, bool numero = false);
// O mesmo desenho do mouse com selo, como imagem GDI+ (o widget da barra de tarefas usa). Quem chama dá delete.
Gdiplus::Bitmap* CriarBitmapMouse(int percentual, bool carregando, bool dormindo, int aviso, int critico,
                                  int tamanho, bool temaClaro);
COLORREF CorDoNivel(int percentual, bool carregando, bool dormindo, int aviso, int critico);
// Lê no registro se a barra de tarefas (sistema=true) ou os apps (sistema=false) estão no tema claro.
bool TemaClaro(bool sistema);
// Salva um PNG do ícone (usado nos testes).
bool SalvarIconePng(const wchar_t* arquivo, int percentual, bool carregando, bool dormindo, int tamanho,
                    bool temaClaro);
