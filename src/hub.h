// hub.h — o "hub" do SharkInfo: uma janela escura com tudo num lugar só (estilo central de mouse gamer).
//
// Abre com o clique esquerdo no ícone da bandeja (ou no widget da barra). O clique direito continua
// abrindo o menu de sempre. A janela é desenhada à mão com GDI+, como o gráfico:
//   Início     -> o mouse, a bateria, tempo restante, polling rate, carga e as últimas 24 h
//   Histórico  -> gráfico grande de 24 horas ou 7 dias
//   Avisos     -> faixa, notificação, celular e os limites de 20% / 10%
//   Ajustes    -> widget, ícone, iniciar com o Windows e arquivos de configuração
//   Sobre      -> versão, atualizações e sair
#pragma once
#include "comum.h"
#include "config.h"
#include "historico.h"

#include <functional>

namespace Gdiplus { class Bitmap; }

// Tudo que o hub pode pedir pro app fazer. O main.cpp traduz cada uma pra mesma ação do menu da bandeja.
enum HubAcao : int {
    HA_FAIXA = 1, HA_NOTIF, HA_CHEIO, HA_CELULAR, HA_TESTE_FAIXA, HA_COPIAR_TOPICO, HA_TESTE_CELULAR,
    HA_WIDGET, HA_NUMERO, HA_INICIO, HA_CONFIG, HA_RECARREGAR, HA_DIAGNOSTICO,
    HA_PROCURAR_ATUALIZACAO, HA_INSTALAR_ATUALIZACAO, HA_AUTO_ATUALIZAR, HA_SAIR,
    HA_AVISO_MAIS, HA_AVISO_MENOS, HA_CRITICO_MAIS, HA_CRITICO_MENOS,
};

// Foto do estado do app, entregue ao hub a cada redesenho.
struct DadosHub {
    bool temLeitor = false, temLeitura = false;
    Leitura l;
    EstimativaDescarga descarga;
    EstimativaCarga carga;
    double minutosCargaCompleta = 120;       // tempo de 0 a 100% (aprendido ou padrão)
    bool cargaAprendida = false;
    Config cfg;
    int pollingHz = 0;                       // 0 = ainda não medido
    bool pollingAoVivo = false;              // medido nesta execução (senão é a última medição salva)
    bool iniciaComWindows = false;
    std::wstring versao;
    std::wstring versaoNova;                 // vazio = não há atualização pendente
    int atualizacao = 0;                     // 0 ocioso/em dia | 1 procurando ou baixando | 2 pronta | 3 versão de desenvolvimento
};

class Hub {
public:
    enum Pagina { INICIO = 0, HISTORICO, AVISOS, AJUSTES, SOBRE, TOTAL_PAGINAS };

    ~Hub();
    // dados(): foto do estado | acionar(HubAcao): faz a ação | agora(): "agora" (na demo o relógio é acelerado)
    void Abrir(HINSTANCE inst, std::function<DadosHub()> dados, Historico* hist, std::function<void(int)> acionar,
               std::function<double()> agora, Pagina pagina = INICIO);
    void Atualizar();   // redesenha, se estiver aberto
    void Fechar();
    bool Aberto() const { return hwnd_ != nullptr; }

private:
    struct Zona {
        float x, y, w, h;
        int id;
    };
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void Pintar(HDC dc, const RECT& r);

    // Páginas
    void PaginaInicio(float x, float y, float w, float h);
    void PaginaHistorico(float x, float y, float w, float h);
    void PaginaAvisos(float x, float y, float w, float h);
    void PaginaAjustes(float x, float y, float w, float h);
    void PaginaSobre(float x, float y, float w, float h);

    // Peças de desenho (tudo em pixels da tela; P() converte do "96 dpi" pro zoom da tela)
    float P(float v) const { return v * e_; }
    void Texto(const std::wstring& t, float px, int estilo, unsigned cor, float x, float y, float w, float h,
               int alinhaH = 0, int alinhaV = 0);
    void Cartao(float x, float y, float w, float h, bool destaque = false);
    void Pilula(float x, float y, float w, float h, unsigned cor);
    void Chave(float x, float y, bool ligada, int id);
    void Botao(float x, float y, float w, const std::wstring& texto, int id, bool principal = false);
    void Passo(float x, float y, int valor, int idMenos, int idMais);
    void Grafico(float x, float y, float w, float h, double periodoSeg, bool eixos);
    void Icone(int tipo, float cx, float cy, float tam, unsigned cor, float espessura);
    float Linha(float x, float y, float w, const std::wstring& titulo, const std::wstring& desc, bool separador);
    void AddZona(float x, float y, float w, float h, int id) { zonas_.push_back({x, y, w, h, id}); }

    HWND hwnd_ = nullptr;
    HINSTANCE inst_ = nullptr;
    std::function<DadosHub()> dados_;
    std::function<void(int)> acionar_;
    std::function<double()> agora_;
    Historico* hist_ = nullptr;
    Pagina pagina_ = INICIO;
    bool seteDias_ = false;
    int hover_ = 0;
    bool seguindoMouse_ = false;

    // Só valem durante um Pintar()
    void* g_ = nullptr;                      // Gdiplus::Graphics*
    float e_ = 1;
    DadosHub d_;
    std::vector<Zona> zonas_;

    // Cache do mouse grande (redesenhar o bitmap a cada movimento do mouse seria desperdício)
    Gdiplus::Bitmap* cacheMouse_ = nullptr;
    int cacheChave_ = -1;
};
