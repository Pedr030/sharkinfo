// main.cpp — o "maestro": cria o ícone da bandeja, o menu e a thread de leitura.
//
// Como as peças conversam (cozinha de restaurante):
//   * Thread "garçom" (LacoLeitura): a cada X segundos lê a bateria, anota no histórico,
//     faz as estimativas e avisa a janela com PostMessage(WM_APP_LEITURA).
//   * Thread principal: roda o "laço de mensagens" do Windows. É a única que mexe em
//     janelas, ícone e menu. Recebe o recado do garçom e atualiza tudo.
//
// Uso:  SharkInfo.exe               -> normal
//       SharkInfo.exe --demo        -> mouse simulado (testar avisos e estimativas rápido)
//       SharkInfo.exe --diagnostico -> gera diagnostico.txt e abre
//       SharkInfo.exe --hub         -> já abre o hub ao iniciar (o clique esquerdo no ícone também abre)
#include "comum.h"
#include "atualizador.h"
#include "versao.h"
#include "avisos.h"
#include "config.h"
#include "gdiplus_inc.h"
#include "grafico.h"
#include "historico.h"
#include "hub.h"
#include "icone.h"
#include "leitores.h"
#include "polling.h"
#include "sistema.h"
#include "suavizador.h"
#include "widget.h"

#include <commctrl.h>   // TaskDialog (a janela das novidades da atualização)
#include <shellapi.h>   // Shell_NotifyIcon (bandeja), ShellExecute (abrir arquivos)
#include <windowsx.h>   // GET_X_LPARAM

#include <memory>

// ---------------------------------------------------------------------------
// Estado global do app
// ---------------------------------------------------------------------------
struct Estado {
    bool temLeitor = false;      // achou o receptor?
    bool temLeitura = false;     // conseguiu ler algo nessa rodada?
    std::wstring nomeLeitor;
    Leitura l;
    EstimativaDescarga descarga;    // tempo restante de bateria (e de onde veio a conta)
    EstimativaCarga carga;
};

static HINSTANCE gInst;
static HWND gJanela;                       // janela invisível que recebe os recados
static NOTIFYICONDATAW gNid{};             // "ficha" do ícone da bandeja
static HICON gIconeAtual = nullptr;
static UINT WM_TASKBARCREATED;             // o Explorer reiniciou: precisa recolocar o ícone
static Config gCfg;
static Trava gTravaCfg, gTravaEstado;
static Estado gEstado;                     // escrito pela thread de leitura, lido pela principal
static std::unique_ptr<Historico> gHist;
static std::unique_ptr<GerenciadorAvisos> gAvisos;
static Faixa gFaixa;
static JanelaGrafico gGrafico;
static Hub gHub;
static bool gDemo = false;
static HANDLE gParar;                      // sinal pra thread de leitura parar
static HANDLE gThreadLeitura;
static volatile LONG gReconectar = 0;      // 1 = descartar o leitor e procurar de novo

enum Comando : UINT {
    CMD_HISTORICO = 1, CMD_FAIXA, CMD_NOTIF, CMD_CHEIO, CMD_TESTE_FAIXA, CMD_CELULAR, CMD_COPIAR_TOPICO,
    CMD_TESTE_CELULAR, CMD_INICIO, CMD_CONFIG, CMD_RECARREGAR, CMD_DIAGNOSTICO, CMD_SAIR, CMD_WIDGET, CMD_NUMERO,
    CMD_PROCURAR_ATUALIZACAO, CMD_INSTALAR_ATUALIZACAO, CMD_AUTO_ATUALIZAR,
};
constexpr UINT_PTR TIMER_WIDGET = 1;       // de 1 em 1 s confere se o widget continua no lugar certo
constexpr UINT_PTR TIMER_ATUALIZA_1 = 2;   // 10 s depois de abrir: primeira procura por atualização
constexpr UINT_PTR TIMER_ATUALIZA = 3;     // depois, a cada 30 min
constexpr UINT_PTR TIMER_AVISO_ATUALIZA = 4;   // tentando avisar enquanto você está jogando: espera 1 min

static bool gNotifEhDeAtualizacao = false; // a última notificação foi "atualização pronta"? (clique abre a janela)
static bool gAvisoAtualizacaoPendente = false;
static std::wstring gVersaoJaAvisada;      // não fica avisando a mesma versão toda hora
static bool gSaindoPraAtualizar = false;

// ---------------------------------------------------------------------------
// Thread de leitura
// ---------------------------------------------------------------------------
static DWORD WINAPI LacoLeitura(LPVOID) {
    std::unique_ptr<Leitor> leitor;
    Suavizador suavizador;                                             // tira o "pula-pula" da %
    for (;;) {
        int intervalo, forcar;
        {
            Segurando t(gTravaCfg);
            intervalo = gCfg.intervaloSegundos;
            forcar = gCfg.forcarEscalaDezenas;
        }
        if (InterlockedExchange(&gReconectar, 0)) leitor.reset();     // pediram pra reconectar
        if (!leitor) {
            leitor = gDemo ? CriarLeitorDemo() : DetectarLeitor(forcar);  // procura o receptor
            suavizador = Suavizador{};                                 // mouse "novo": filtro começa do zero
        }

        Estado e;
        if (leitor) {
            Leitura l;
            if (leitor->Ler(l)) {
                l = suavizador.Filtrar(l);                             // a partir daqui, só o valor filtrado
                e.temLeitor = e.temLeitura = true;
                e.l = l;
                gHist->Registrar(l);                                   // anota no caderninho
                if (l.carregando) e.carga = gHist->EstimarCarga(l.momento);
                else e.descarga = gHist->EstimarDescarga(l.percentual);
            } else {
                leitor.reset();                                        // receptor sumiu
            }
        }
        {
            Segurando t(gTravaEstado);
            gEstado = e;
        }
        PostMessageW(gJanela, WM_APP_LEITURA, 0, 0);                  // "ô janela, tem novidade"
        DWORD espera = gDemo ? 1000 : (DWORD)intervalo * 1000;
        if (WaitForSingleObject(gParar, espera) == WAIT_OBJECT_0) break;   // dorme, ou sai se mandarem parar
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Textos
// ---------------------------------------------------------------------------
static std::wstring TextoStatus(const Estado& e) {
    if (!e.temLeitor) return L"Mouse não encontrado";
    const Leitura& l = e.l;
    if (l.percentual < 0 && !l.carregando) return l.modelo + L": aguardando…";
    if (l.carregando) return l.modelo + L": carregando (" + l.conexao + L")";
    std::wstring pct = l.percentual >= 0 ? std::to_wstring(l.percentual) + L"%" : L"?";
    if (l.dormindo) return l.modelo + L": " + pct + L" (dormindo)";
    return l.modelo + L": " + pct;
}

static std::wstring TextoEstimativa(const Estado& e) {
    if (!e.temLeitura) return L"Tempo restante: —";
    if (e.l.carregando) {
        if (e.l.percentual == 100) return L"Carga completa ✔";
        if (e.carga.valida)
            return L"Cheio em ~" + FormatarMinutos(e.carga.minutos) + (e.carga.aprendido ? L"" : L" (aprendendo)");
        return L"Cheio em: calculando…";
    }
    if (e.descarga.minutos < 0) return L"Tempo restante: calculando…";
    if (e.descarga.fonte == EstimativaDescarga::MINIMO)       // ainda medindo: só dá pra garantir um mínimo
        return L"Bateria dura pelo menos " + FormatarMinutos(e.descarga.minutos) + L" (medindo)";
    return L"Bateria dura ~" + FormatarMinutos(e.descarga.minutos);
}

static Estado CopiaEstado() {
    Segurando t(gTravaEstado);
    return gEstado;
}

// ---------------------------------------------------------------------------
// Ícone da bandeja
// ---------------------------------------------------------------------------
static int TamanhoIcone() { return GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem()); }

static void AdicionarIcone() {
    gNid.cbSize = sizeof(gNid);
    gNid.hWnd = gJanela;
    gNid.uID = 1;
    gNid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    gNid.uCallbackMessage = WM_APP_BANDEJA;                   // cliques chegam como essa mensagem
    if (!gIconeAtual) gIconeAtual = DesenharIcone(-1, false, true, 20, 10, TamanhoIcone(), TemaClaro(true));
    gNid.hIcon = gIconeAtual;
    wcscpy_s(gNid.szTip, L"SharkInfo");
    Shell_NotifyIconW(NIM_ADD, &gNid);
    gNid.uVersion = NOTIFYICON_VERSION_4;                     // jeito "moderno" de receber cliques
    Shell_NotifyIconW(NIM_SETVERSION, &gNid);
}

static void AtualizarIcone(const Estado& e) {
    int pct = -1;
    bool carregando = false, dormindo = true;
    if (e.temLeitura) {
        pct = e.l.percentual;
        carregando = e.l.carregando;
        dormindo = e.l.dormindo;
        // No cabo o % fica congelado: mostra o palpite da carga.
        if (carregando && e.carga.valida && pct != 100) pct = std::max(pct, e.carga.percentual);
    }
    HICON novo = DesenharIcone(pct, carregando, dormindo, gCfg.avisoPercentual, gCfg.criticoPercentual,
                               TamanhoIcone(), TemaClaro(true),          // contorno conforme o tema da barra
                               gCfg.iconeNumero);                        // ou o número, se o usuário escolheu
    gNid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    gNid.hIcon = novo;
    std::wstring dica = TextoStatus(e) + L"\n" + TextoEstimativa(e);
    wcsncpy_s(gNid.szTip, dica.c_str(), _TRUNCATE);           // limite de 127 letras do Windows
    Shell_NotifyIconW(NIM_MODIFY, &gNid);
    if (gIconeAtual) DestroyIcon(gIconeAtual);                // libera o ícone antigo
    gIconeAtual = novo;

    // O widget da barra mostra o mesmo estado, com a % escrita ao lado.
    widget::Conteudo c;
    c.percentual = pct;
    c.carregando = carregando;
    c.dormindo = dormindo;
    c.aviso = gCfg.avisoPercentual;
    c.critico = gCfg.criticoPercentual;
    c.dica = dica;
    widget::Atualizar(c);
}

// Liga/desliga o widget conforme a configuração (e recria se o Explorer tiver reiniciado).
static void AplicarWidget() {
    if (gCfg.widgetBarra && !widget::Ativo()) {
        if (widget::Criar(gInst, gJanela, WM_APP_WIDGET)) AtualizarIcone(CopiaEstado());
    } else if (!gCfg.widgetBarra && widget::Ativo()) {
        widget::Destruir();
    }
}

static HICON gIconeNotificacao = nullptr;

static void Notificar(const std::wstring& titulo, const std::wstring& msg, bool urgente) {
    (void)urgente;
    gNotifEhDeAtualizacao = false;
    // Em vez do triângulo amarelo padrão, a notificação mostra o nosso ícone colorido com a %.
    Estado e = CopiaEstado();
    int pct = e.temLeitura ? e.l.percentual : -1;
    bool carregando = e.temLeitura && e.l.carregando;
    int tam = GetSystemMetricsForDpi(SM_CXICON, GetDpiForSystem());   // tamanho de ícone "grande"
    HICON novo = DesenharIcone(pct, carregando, false, gCfg.avisoPercentual, gCfg.criticoPercentual, tam,
                               TemaClaro(true));                        // notificações seguem o tema do Windows
    gNid.uFlags = NIF_INFO;                                   // "balão" = notificação do Windows
    wcsncpy_s(gNid.szInfoTitle, titulo.c_str(), _TRUNCATE);
    wcsncpy_s(gNid.szInfo, msg.c_str(), _TRUNCATE);
    gNid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;           // NIIF_USER = "use o ícone que eu mandar"
    gNid.hBalloonIcon = novo;
    Shell_NotifyIconW(NIM_MODIFY, &gNid);
    if (gIconeNotificacao) DestroyIcon(gIconeNotificacao);    // libera o da notificação anterior
    gIconeNotificacao = novo;
}

// ---------------------------------------------------------------------------
// Ações do menu
// ---------------------------------------------------------------------------
static std::wstring ResumoGrafico() {
    Estado e = CopiaEstado();
    bool aprendido;
    double taxa = gHist->MinutosPorPorcentoCarga(aprendido);
    std::wstring pct = e.temLeitura && e.l.percentual >= 0 ? std::to_wstring(e.l.percentual) + L"%" : L"?";
    std::wstring origem = e.descarga.fonte == EstimativaDescarga::SESSAO      ? L" (medido agora)"
                          : e.descarga.fonte == EstimativaDescarga::HISTORICO ? L" (pelas descargas anteriores)"
                                                                              : L" (no mínimo; ainda medindo)";
    if (e.descarga.minutos < 0) origem.clear();
    return L"Bateria agora: " + pct + L"\nTempo restante estimado: " + FormatarMinutos(e.descarga.minutos) + origem +
           L"\nCarga completa (0→100%): " + FormatarMinutos(taxa * 100) +
           (aprendido ? L"" : L" (padrão, ainda aprendendo)");
}

static double AgoraGrafico() {
    if (!gDemo) return Agora();
    Estado e = CopiaEstado();                                 // na demo o relógio é acelerado
    return e.temLeitura ? e.l.momento : Agora();
}

static void CopiarTexto(const std::wstring& texto) {
    if (!OpenClipboard(gJanela)) return;
    EmptyClipboard();
    size_t bytes = (texto.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    memcpy(GlobalLock(mem), texto.c_str(), bytes);
    GlobalUnlock(mem);
    SetClipboardData(CF_UNICODETEXT, mem);                    // o Windows passa a ser dono da memória
    CloseClipboard();
}

static DWORD WINAPI ThreadDiagnostico(LPVOID) {
    std::wstring texto = RelatorioDiagnostico();
    std::wstring caminho = PastaDados() + L"\\diagnostico.txt";
    if (FILE* f = _wfopen(caminho.c_str(), L"wb")) {
        fputs("\xEF\xBB\xBF", f);                             // "BOM": avisa o Bloco de Notas que é UTF-8
        fputs(ParaUtf8(texto).c_str(), f);
        fclose(f);
    }
    PostMessageW(gJanela, WM_APP_DIAG_OK, 0, 0);
    return 0;
}

static void AlternarConfig(bool Config::*campo) {          // "ponteiro pra membro": qual campo inverter
    Segurando t(gTravaCfg);
    gCfg.*campo = !(gCfg.*campo);
    gCfg.Salvar();
}

// ---------------------------------------------------------------------------
// Atualização
// ---------------------------------------------------------------------------
// Tira os "#", "*" e "`" do Markdown das notas da release, pra ler bem numa janela comum.
static std::wstring LimparMarkdown(const std::wstring& t) {
    std::wstring r;
    bool inicioLinha = true;
    for (size_t i = 0; i < t.size(); ++i) {
        wchar_t c = t[i];
        if (inicioLinha && c == L'#') {                       // "## Mudanças" -> "Mudanças"
            while (i < t.size() && (t[i] == L'#' || t[i] == L' ')) ++i;
            --i;
            inicioLinha = false;
            continue;
        }
        if (c == L'*' || c == L'`') continue;
        if (inicioLinha && c == L'-' && i + 1 < t.size() && t[i + 1] == L' ') c = L'•';   // "- item" -> "• item"
        r += c;
        inicioLinha = c == L'\n';
    }
    return r;
}

static void MostrarDialogoAtualizacao() {
    if (atualizador::EstadoAtual() != atualizador::Estado::Pronta) return;
    atualizador::InfoRelease info = atualizador::Pendente();
    std::wstring titulo = L"SharkInfo " + info.versao + L" está pronto para instalar";
    std::wstring texto = L"Você está na versão " + atualizador::VersaoAtual() +
                         L". A atualização já foi baixada e conferida.";
    std::wstring notas = LimparMarkdown(info.notas);
    TASKDIALOG_BUTTON botoes[] = {
        {100, L"Reiniciar agora\nO SharkInfo fecha e abre de novo já atualizado (leva 1 segundo)."},
        {101, L"Depois\nInstala sozinho quando o SharkInfo for fechado."},
    };
    TASKDIALOGCONFIG tc{};
    tc.cbSize = sizeof(tc);
    tc.hInstance = gInst;
    tc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_USE_HICON_MAIN | TDF_ALLOW_DIALOG_CANCELLATION |
                 (notas.empty() ? 0 : TDF_EXPANDED_BY_DEFAULT);
    tc.pszWindowTitle = L"SharkInfo — Atualização";
    tc.hMainIcon = LoadIconW(gInst, MAKEINTRESOURCEW(1));
    tc.pszMainInstruction = titulo.c_str();
    tc.pszContent = texto.c_str();
    if (!notas.empty()) {                                     // as novidades, num bloco que abre e fecha
        tc.pszExpandedInformation = notas.c_str();
        tc.pszExpandedControlText = L"Esconder novidades";
        tc.pszCollapsedControlText = L"Ver novidades";
    }
    tc.pButtons = botoes;
    tc.cButtons = 2;
    tc.nDefaultButton = 100;
    int escolha = 0;
    SetForegroundWindow(gJanela);
    if (FAILED(TaskDialogIndirect(&tc, &escolha, nullptr, nullptr))) return;
    if (escolha == 100) {
        if (atualizador::Instalar(true)) {                    // o novo já foi aberto: este aqui sai
            gSaindoPraAtualizar = true;
            DestroyWindow(gJanela);
        } else {
            Notificar(L"Não consegui atualizar sozinho",
                      L"Baixe a versão nova em github.com/" SHARKINFO_LARGO(SHARKINFO_REPO) L"/releases", true);
        }
    }
}

// Avisa que tem atualização, mas nunca no meio de um jogo em tela cheia.
static void TentarAvisarAtualizacao() {
    if (atualizador::EstadoAtual() != atualizador::Estado::Pronta) {
        gAvisoAtualizacaoPendente = false;
        KillTimer(gJanela, TIMER_AVISO_ATUALIZA);
        return;
    }
    if (atualizador::UsuarioOcupado()) {                      // jogando: tenta de novo daqui a 1 min
        gAvisoAtualizacaoPendente = true;
        SetTimer(gJanela, TIMER_AVISO_ATUALIZA, 60 * 1000, nullptr);
        return;
    }
    gAvisoAtualizacaoPendente = false;
    KillTimer(gJanela, TIMER_AVISO_ATUALIZA);
    atualizador::InfoRelease info = atualizador::Pendente();
    gVersaoJaAvisada = info.versao;
    Notificar(L"Atualização pronta: SharkInfo " + info.versao, L"Clique aqui para ver as novidades e instalar.", false);
    gNotifEhDeAtualizacao = true;                             // clique na notificação abre a janela
}

static void AoMudarAtualizacao(atualizador::Estado e, bool manual) {
    using atualizador::Estado;
    switch (e) {
    case Estado::Pronta:
        if (manual) MostrarDialogoAtualizacao();
        else if (atualizador::Pendente().versao != gVersaoJaAvisada) TentarAvisarAtualizacao();
        break;
    case Estado::EmDia:
        if (manual) Notificar(L"SharkInfo em dia", L"Você já está na versão mais nova (" + atualizador::VersaoAtual() + L").", false);
        break;
    case Estado::Erro:
        if (manual) Notificar(L"Não deu pra procurar atualização", L"Confira a internet e tente de novo.", false);
        break;
    case Estado::Dev:
        if (manual) Notificar(L"Versão de desenvolvimento", L"Esta cópia foi compilada à mão; o atualizador fica desligado.", false);
        break;
    default:
        break;
    }
}

static void AjustarTimersAtualizacao() {
    if (gCfg.atualizarAutomaticamente && !atualizador::EhVersaoDev()) {
        SetTimer(gJanela, TIMER_ATUALIZA, 30 * 60 * 1000, nullptr);
    } else {
        KillTimer(gJanela, TIMER_ATUALIZA);
        KillTimer(gJanela, TIMER_ATUALIZA_1);
    }
}

static void ExecutarComando(UINT cmd);   // mais abaixo: faz o que cada item do menu pede (o hub usa os mesmos)

static void MostrarMenu(POINT pt) {
    Estado e = CopiaEstado();
    auto marca = [](bool b) { return MF_STRING | (b ? MF_CHECKED : MF_UNCHECKED); };

    HMENU avisos = CreatePopupMenu();
    AppendMenuW(avisos, marca(gCfg.avisoNaTela), CMD_FAIXA, L"Faixa por cima do jogo");
    AppendMenuW(avisos, marca(gCfg.notificacaoWindows), CMD_NOTIF, L"Notificação do Windows");
    AppendMenuW(avisos, marca(gCfg.avisarCargaCompleta), CMD_CHEIO, L"Avisar quando terminar de carregar");
    AppendMenuW(avisos, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(avisos, MF_STRING, CMD_TESTE_FAIXA, L"Testar faixa na tela");

    HMENU celular = CreatePopupMenu();
    AppendMenuW(celular, marca(gCfg.celularAtivo), CMD_CELULAR, L"Enviar avisos pro celular");
    AppendMenuW(celular, MF_STRING, CMD_COPIAR_TOPICO, (L"Copiar tópico: " + gCfg.celularTopico).c_str());
    AppendMenuW(celular, MF_STRING, CMD_TESTE_CELULAR, L"Enviar teste");

    HMENU aparencia = CreatePopupMenu();
    AppendMenuW(aparencia, marca(gCfg.widgetBarra), CMD_WIDGET, L"Mostrar % na barra de tarefas");
    AppendMenuW(aparencia, marca(gCfg.iconeNumero), CMD_NUMERO, L"Número no ícone da bandeja");

    HMENU atualiz = CreatePopupMenu();
    {
        using atualizador::Estado;
        Estado ea = atualizador::EstadoAtual();
        std::wstring versao = L"Você está na versão " + atualizador::VersaoAtual();
        AppendMenuW(atualiz, MF_STRING | MF_GRAYED, 0, versao.c_str());
        if (ea == Estado::Pronta) {
            std::wstring t = L"Instalar a versão " + atualizador::Pendente().versao + L" (reinicia)";
            AppendMenuW(atualiz, MF_STRING, CMD_INSTALAR_ATUALIZACAO, t.c_str());
        } else if (ea == Estado::Procurando || ea == Estado::Baixando) {
            AppendMenuW(atualiz, MF_STRING | MF_GRAYED, 0,
                        ea == Estado::Procurando ? L"Procurando atualização…" : L"Baixando atualização…");
        } else {
            AppendMenuW(atualiz, MF_STRING, CMD_PROCURAR_ATUALIZACAO, L"Procurar atualizações agora");
        }
        AppendMenuW(atualiz, marca(gCfg.atualizarAutomaticamente), CMD_AUTO_ATUALIZAR, L"Atualizar automaticamente");
    }

    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, TextoStatus(e).c_str());
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, TextoEstimativa(e).c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_HISTORICO, L"Ver histórico");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)avisos, L"Avisos");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)celular, L"Celular (ntfy)");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)aparencia, L"Aparência");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)atualiz, atualizador::EstadoAtual() == atualizador::Estado::Pronta
                                                    ? L"Atualizações (nova versão!)"
                                                    : L"Atualizações");
    AppendMenuW(m, marca(IniciaComWindows()), CMD_INICIO, L"Iniciar com o Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_CONFIG, L"Abrir configurações");
    AppendMenuW(m, MF_STRING, CMD_RECARREGAR, L"Recarregar configurações");
    AppendMenuW(m, MF_STRING, CMD_DIAGNOSTICO, L"Diagnóstico do mouse");
    AppendMenuW(m, MF_STRING, CMD_SAIR, L"Sair");
    SetMenuDefaultItem(m, CMD_HISTORICO, FALSE);              // "Ver histórico" em negrito

    SetForegroundWindow(gJanela);                             // sem isso o menu não fecha ao clicar fora
    UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, gJanela,
                              nullptr);
    DestroyMenu(m);                                           // destrói os submenus junto
    PostMessageW(gJanela, WM_NULL, 0, 0);
    ExecutarComando(cmd);
}

static void ExecutarComando(UINT cmd) {
    switch (cmd) {
    case CMD_HISTORICO: gGrafico.Abrir(gInst, gHist.get(), ResumoGrafico, AgoraGrafico); break;
    case CMD_FAIXA: AlternarConfig(&Config::avisoNaTela); break;
    case CMD_NOTIF: AlternarConfig(&Config::notificacaoWindows); break;
    case CMD_CHEIO: AlternarConfig(&Config::avisarCargaCompleta); break;
    case CMD_CELULAR: AlternarConfig(&Config::celularAtivo); break;
    case CMD_WIDGET:
        AlternarConfig(&Config::widgetBarra);
        AplicarWidget();
        break;
    case CMD_NUMERO:
        AlternarConfig(&Config::iconeNumero);
        AtualizarIcone(CopiaEstado());
        break;
    case CMD_PROCURAR_ATUALIZACAO: atualizador::Procurar(true); break;
    case CMD_INSTALAR_ATUALIZACAO: MostrarDialogoAtualizacao(); break;
    case CMD_AUTO_ATUALIZAR:
        AlternarConfig(&Config::atualizarAutomaticamente);
        AjustarTimersAtualizacao();
        break;
    case CMD_TESTE_FAIXA:
        gFaixa.Mostrar(L"Teste: mouse com 10%, carregue agora!", RGB(198, 40, 40), gCfg.segundosAvisoNaTela,
                       gCfg.avisoNoTopo);
        break;
    case CMD_COPIAR_TOPICO:
        CopiarTexto(gCfg.celularTopico);
        Notificar(L"Tópico copiado", L"Cole no app ntfy do celular (botão +)", false);
        break;
    case CMD_TESTE_CELULAR: EnviarCelular(gCfg, L"SharkInfo", L"Teste: funcionando! 🦈", 3); break;
    case CMD_INICIO: DefinirInicioComWindows(!IniciaComWindows()); break;
    case CMD_CONFIG:
        ShellExecuteW(nullptr, L"open", L"notepad.exe", Config::Caminho().c_str(), nullptr, SW_SHOWNORMAL);
        break;
    case CMD_RECARREGAR: {
        Config novo = Config::Carregar();
        {
            Segurando t(gTravaCfg);
            gCfg = novo;
        }
        gHist->DefinirCargaPadrao(gCfg.minutosCargaPadrao);
        InterlockedExchange(&gReconectar, 1);                 // caso 'forcar_escala_dezenas' tenha mudado
        AplicarWidget();
        AjustarTimersAtualizacao();
        AtualizarIcone(CopiaEstado());
        Notificar(L"SharkInfo", L"Configurações recarregadas", false);
        break;
    }
    case CMD_DIAGNOSTICO: {
        Notificar(L"Diagnóstico", L"Escutando o receptor por ~8 s…", false);
        HANDLE t = CreateThread(nullptr, 0, ThreadDiagnostico, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
        break;
    }
    case CMD_SAIR: DestroyWindow(gJanela); break;
    }
}

// ---------------------------------------------------------------------------
// O hub (janela aberta pelo clique esquerdo no ícone)
// ---------------------------------------------------------------------------
static DadosHub MontarDadosHub() {
    DadosHub d;
    Estado e = CopiaEstado();
    d.temLeitor = e.temLeitor;
    d.temLeitura = e.temLeitura;
    d.l = e.l;
    d.descarga = e.descarga;
    d.carga = e.carga;
    {
        Segurando t(gTravaCfg);
        d.cfg = gCfg;
    }
    bool aprendido = false;
    d.minutosCargaCompleta = gHist->MinutosPorPorcentoCarga(aprendido) * 100;
    d.cargaAprendida = aprendido;
    polling::Resultado p = polling::Atual();
    d.pollingHz = p.hz;
    d.pollingAoVivo = p.aoVivo;
    static bool inicio = false;                               // o registro só é consultado de 3 em 3 s (o hub repinta muito)
    static ULONGLONG ultimaConsulta = 0;
    if (GetTickCount64() - ultimaConsulta > 3000 || ultimaConsulta == 0) {
        inicio = IniciaComWindows();
        ultimaConsulta = GetTickCount64();
    }
    d.iniciaComWindows = inicio;
    d.versao = atualizador::VersaoAtual();
    atualizador::Estado ea = atualizador::EstadoAtual();
    if (atualizador::EhVersaoDev()) d.atualizacao = 3;
    else if (ea == atualizador::Estado::Pronta) d.atualizacao = 2;
    else if (ea == atualizador::Estado::Procurando || ea == atualizador::Estado::Baixando) d.atualizacao = 1;
    if (ea == atualizador::Estado::Pronta) d.versaoNova = atualizador::Pendente().versao;
    return d;
}

// Os limites de aviso mudam de 5 em 5, o crítico sempre abaixo do primeiro aviso.
static void AjustarLimite(bool primeiro, int sentido) {
    {
        Segurando t(gTravaCfg);
        if (primeiro) {
            int v = gCfg.avisoPercentual + sentido * 5;
            gCfg.avisoPercentual = std::max(gCfg.criticoPercentual + 5, std::min(50, v));
        } else {
            int v = gCfg.criticoPercentual + sentido * 5;
            gCfg.criticoPercentual = std::max(5, std::min(gCfg.avisoPercentual - 5, v));
        }
        gCfg.Salvar();
    }
    AtualizarIcone(CopiaEstado());                            // as cores do selo dependem dos limites
}

// Cada botão do hub faz o mesmo que o item equivalente do menu da bandeja.
static void AcaoDoHub(int acao) {
    switch (acao) {
    case HA_FAIXA: ExecutarComando(CMD_FAIXA); break;
    case HA_NOTIF: ExecutarComando(CMD_NOTIF); break;
    case HA_CHEIO: ExecutarComando(CMD_CHEIO); break;
    case HA_CELULAR: ExecutarComando(CMD_CELULAR); break;
    case HA_TESTE_FAIXA: ExecutarComando(CMD_TESTE_FAIXA); break;
    case HA_COPIAR_TOPICO: ExecutarComando(CMD_COPIAR_TOPICO); break;
    case HA_TESTE_CELULAR: ExecutarComando(CMD_TESTE_CELULAR); break;
    case HA_WIDGET: ExecutarComando(CMD_WIDGET); break;
    case HA_NUMERO: ExecutarComando(CMD_NUMERO); break;
    case HA_INICIO: ExecutarComando(CMD_INICIO); break;
    case HA_CONFIG: ExecutarComando(CMD_CONFIG); break;
    case HA_RECARREGAR: ExecutarComando(CMD_RECARREGAR); break;
    case HA_DIAGNOSTICO: ExecutarComando(CMD_DIAGNOSTICO); break;
    case HA_PROCURAR_ATUALIZACAO: ExecutarComando(CMD_PROCURAR_ATUALIZACAO); break;
    case HA_INSTALAR_ATUALIZACAO: ExecutarComando(CMD_INSTALAR_ATUALIZACAO); break;
    case HA_AUTO_ATUALIZAR: ExecutarComando(CMD_AUTO_ATUALIZAR); break;
    case HA_SAIR: ExecutarComando(CMD_SAIR); return;          // a janela já foi destruída
    case HA_AVISO_MAIS: AjustarLimite(true, +1); break;
    case HA_AVISO_MENOS: AjustarLimite(true, -1); break;
    case HA_CRITICO_MAIS: AjustarLimite(false, +1); break;
    case HA_CRITICO_MENOS: AjustarLimite(false, -1); break;
    }
    gHub.Atualizar();
}

static void AbrirHub(Hub::Pagina pagina = Hub::INICIO) {
    gHub.Abrir(gInst, MontarDadosHub, gHist.get(), AcaoDoHub, AgoraGrafico, pagina);
}

// ---------------------------------------------------------------------------
// A "caixa de correio" da janela invisível
// ---------------------------------------------------------------------------
static LRESULT CALLBACK ProcPrincipal(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TASKBARCREATED) {                           // Explorer reiniciou: a barra é nova
        AdicionarIcone();
        widget::Destruir();                                   // o widget antigo morreu junto com a barra velha
        AplicarWidget();
        return 0;
    }
    switch (msg) {
    case WM_APP_BANDEJA:
        switch (LOWORD(lp)) {                                 // qual foi o clique?
        case WM_CONTEXTMENU: {                                // botão direito
            POINT pt{GET_X_LPARAM(wp), GET_Y_LPARAM(wp)};
            MostrarMenu(pt);
            break;
        }
        case NIN_BALLOONUSERCLICK:                            // clicou na notificação
            if (gNotifEhDeAtualizacao) MostrarDialogoAtualizacao();
            break;
        case NIN_SELECT:                                      // clique esquerdo
        case NIN_KEYSELECT:
            AbrirHub();
            break;
        }
        return 0;
    case WM_APP_WIDGET: {                                     // clique no widget da barra
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (wp == 1) AbrirHub();
        else MostrarMenu(pt);
        return 0;
    }
    case WM_APP_ATUALIZACAO:
        AoMudarAtualizacao((atualizador::Estado)wp, lp != 0);
        return 0;
    case WM_TIMER:
        if (wp == TIMER_ATUALIZA_1) {                         // a primeira procura é uma vez só
            KillTimer(h, TIMER_ATUALIZA_1);
            atualizador::Procurar(false);
        } else if (wp == TIMER_ATUALIZA) {
            atualizador::Procurar(false);
        } else if (wp == TIMER_AVISO_ATUALIZA) {
            TentarAvisarAtualizacao();
        }
        if (wp == TIMER_WIDGET && gCfg.widgetBarra) {
            if (widget::Ativo()) widget::Reposicionar();      // acompanha mudanças de layout da barra
            else AplicarWidget();                             // perdeu o widget? tenta de novo
        }
        return 0;
    case WM_APP_LEITURA: {                                    // a thread de leitura tem novidade
        Estado e = CopiaEstado();
        AtualizarIcone(e);
        if (e.temLeitura) {
            // Nos avisos só entra tempo medido ou aprendido; o "pelo menos" fica de fora (seria enganoso).
            bool medido = e.descarga.minutos >= 0 && e.descarga.fonte != EstimativaDescarga::MINIMO;
            std::wstring restante = medido ? FormatarMinutos(e.descarga.minutos) : L"";
            gAvisos->Processar(e.l, restante);
        }
        gGrafico.Atualizar();
        gHub.Atualizar();
        return 0;
    }
    case WM_SETTINGCHANGE:                                    // trocou o tema claro/escuro? redesenha
        if (lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0)
            AtualizarIcone(CopiaEstado());
        return 0;
    case WM_APP_DIAG_OK: {
        std::wstring caminho = PastaDados() + L"\\diagnostico.txt";
        ShellExecuteW(nullptr, L"open", caminho.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, TIMER_WIDGET);
        widget::Destruir();
        // Tinha atualização baixada e você escolheu "Depois" (ou nem viu)? Instala agora, ao fechar.
        if (!gSaindoPraAtualizar && atualizador::EstadoAtual() == atualizador::Estado::Pronta)
            atualizador::Instalar(false);
        SetEvent(gParar);                                     // pede pra thread de leitura parar
        WaitForSingleObject(gThreadLeitura, 5000);
        gGrafico.Fechar();
        gHub.Fechar();
        polling::Parar();
        Shell_NotifyIconW(NIM_DELETE, &gNid);                 // tira o ícone da bandeja
        PostQuitMessage(0);                                   // encerra o laço de mensagens
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Ponto de entrada
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    gInst = inst;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);  // nítido em telas com zoom

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool diagnostico = false, abrirHub = false;
    DWORD pidAntigo = 0;                                      // veio de uma atualização? (PID da versão velha)
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--demo") == 0) gDemo = true;
        if (wcscmp(argv[i], L"--hub") == 0) abrirHub = true;
        if (wcscmp(argv[i], L"--diagnostico") == 0) diagnostico = true;
        if (wcscmp(argv[i], L"--pos-atualizacao") == 0 && i + 1 < argc) pidAntigo = wcstoul(argv[++i], nullptr, 10);
    }
    LocalFree(argv);
    // Espera a versão antiga fechar (se veio de atualização) e apaga o SharkInfo.exe.old, se sobrou.
    atualizador::LimparPosAtualizacao(pidAntigo);

    if (diagnostico) {                                        // modo diagnóstico: gera o .txt e sai
        ThreadDiagnostico(nullptr);
        std::wstring caminho = PastaDados() + L"\\diagnostico.txt";
        ShellExecuteW(nullptr, L"open", caminho.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    if (!gDemo && JaEstaAberto()) {
        MessageBoxW(nullptr, L"O SharkInfo já está aberto (olhe na bandeja, perto do relógio).",
                    L"SharkInfo", MB_ICONINFORMATION);
        return 0;
    }

    Gdiplus::GdiplusStartupInput gdiEntrada;                   // liga o GDI+
    ULONG_PTR gdiToken;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiEntrada, nullptr);

    gCfg = Config::Carregar();
    gHist = std::make_unique<Historico>(PastaDados(gDemo), gCfg.minutosCargaPadrao);
    gAvisos = std::make_unique<GerenciadorAvisos>(
        gCfg, Notificar,
        [](const std::wstring& m, COLORREF c) { gFaixa.Mostrar(m, c, gCfg.segundosAvisoNaTela, gCfg.avisoNoTopo); });

    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW wc{};
    wc.lpfnWndProc = ProcPrincipal;
    wc.hInstance = inst;
    wc.lpszClassName = L"SharkInfoPrincipal";
    RegisterClassW(&wc);
    gJanela = CreateWindowExW(0, wc.lpszClassName, L"SharkInfo", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                              inst, nullptr);                 // nunca é mostrada
    gFaixa.Criar(inst);
    AdicionarIcone();
    AplicarWidget();
    SetTimer(gJanela, TIMER_WIDGET, 1000, nullptr);

    atualizador::Configurar(gJanela, WM_APP_ATUALIZACAO);
    AjustarTimersAtualizacao();
    if (gCfg.atualizarAutomaticamente && !gDemo) SetTimer(gJanela, TIMER_ATUALIZA_1, 10 * 1000, nullptr);
    if (pidAntigo)                                            // acabou de se atualizar
        Notificar(L"SharkInfo atualizado", L"Agora na versão " + atualizador::VersaoAtual() + L". Bom jogo!", false);

    gParar = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    gThreadLeitura = CreateThread(nullptr, 0, LacoLeitura, nullptr, 0, nullptr);
    polling::Iniciar(inst, PastaDados(gDemo));                // mede o polling rate pelo movimento do mouse
    if (abrirHub) AbrirHub();                                 // --hub: já abre o hub (pra testar)

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {           // o "laço de mensagens": coração de todo app Windows
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CloseHandle(gThreadLeitura);
    CloseHandle(gParar);
    if (gIconeAtual) DestroyIcon(gIconeAtual);
    if (gIconeNotificacao) DestroyIcon(gIconeNotificacao);
    gAvisos.reset();
    gHist.reset();
    Gdiplus::GdiplusShutdown(gdiToken);
    return 0;
}
