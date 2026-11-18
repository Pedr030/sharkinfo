// grafico.h — janela "Histórico da bateria": linha da % ao longo do tempo,
// trechos no cabo pintados de azul, e um resumo com as estimativas.
#pragma once
#include "comum.h"
#include "historico.h"

#include <functional>

class JanelaGrafico {
public:
    // resumo(): texto do topo. agora(): "agora" (na demo o relógio é acelerado).
    void Abrir(HINSTANCE inst, Historico* hist, std::function<std::wstring()> resumo,
               std::function<double()> agora);
    void Atualizar();   // redesenha, se estiver aberta
    void Fechar();

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void Pintar(HDC dc, const RECT& r);
    HWND hwnd_ = nullptr;
    HWND botao24h_ = nullptr, botao7d_ = nullptr;
    Historico* hist_ = nullptr;
    std::function<std::wstring()> resumo_;
    std::function<double()> agora_;
    bool seteDias_ = false;
};
