// avisos.h — tudo que "grita" quando a bateria está acabando.
//   1. Notificação do Windows (feita pelo main.cpp, que é dono do ícone)
//   2. Faixa por cima do jogo (janela que não rouba foco nem clique)
//   3. Celular, pelo app gratuito "ntfy"
// E o "porteiro" (GerenciadorAvisos), que decide QUANDO avisar: uma vez por descarga.
#pragma once
#include "comum.h"
#include "config.h"

#include <functional>

// Envia pro ntfy numa thread separada (não trava o app).
void EnviarCelular(const Config& cfg, const std::wstring& titulo, const std::wstring& msg, int prioridade);

// A faixa por cima do jogo.
class Faixa {
public:
    bool Criar(HINSTANCE inst);
    void Mostrar(const std::wstring& texto, COLORREF cor, int segundos, bool topo);
private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    HWND hwnd_ = nullptr;
    std::wstring texto_;
    COLORREF cor_ = RGB(198, 40, 40);
};

class GerenciadorAvisos {
public:
    // notificar(titulo, mensagem, urgente) e mostrarNaTela(mensagem, cor)
    using FnNotificar = std::function<void(const std::wstring&, const std::wstring&, bool)>;
    using FnTela = std::function<void(const std::wstring&, COLORREF)>;
    GerenciadorAvisos(const Config& cfg, FnNotificar notificar, FnTela tela)
        : cfg_(cfg), notificar_(std::move(notificar)), tela_(std::move(tela)) {}
    void Processar(const Leitura& l, const std::wstring& textoRestante);

private:
    void Disparar(const std::wstring& titulo, const std::wstring& msg, COLORREF cor, int prioridade);
    const Config& cfg_;
    FnNotificar notificar_;
    FnTela tela_;
    bool avisouAviso_ = false, avisouCritico_ = false, avisouCheio_ = false;
};
