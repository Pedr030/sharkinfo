// config.h — configurações num arquivo config.ini em %APPDATA%\SharkInfo.
// O formato .ini é o "clássico" do Windows e ele mesmo sabe ler/gravar (GetPrivateProfile*).
#pragma once
#include "comum.h"

struct Config {
    int avisoPercentual = 20;          // 1º aviso
    int criticoPercentual = 10;        // aviso urgente
    int intervaloSegundos = 10;        // de quanto em quanto tempo atualiza
    bool avisoNaTela = true;           // faixa por cima do jogo
    int segundosAvisoNaTela = 6;
    bool avisoNoTopo = true;           // false = embaixo
    bool notificacaoWindows = true;
    bool celularAtivo = false;         // notificação via app ntfy
    std::wstring celularTopico;        // gerado aleatório na 1ª vez
    std::wstring celularServidor = L"https://ntfy.sh";
    bool avisarCargaCompleta = true;
    int minutosCargaPadrao = 120;      // até aprender o tempo real
    int forcarEscalaDezenas = -1;      // -1 automático, 1 = dezenas, 0 = porcentagem
    bool widgetBarra = true;           // mouse + "93%" encaixado na barra de tarefas (padrão)
    bool iconeNumero = false;          // ícone da bandeja mostra o número em vez do mouse
    bool atualizarAutomaticamente = true;  // procura (e baixa) atualizações sozinho

    static std::wstring Caminho();
    static Config Carregar();
    void Salvar() const;
};
