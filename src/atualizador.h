// atualizador.h — atualização automática pelo GitHub Releases (mesmo jeito do app Sinal).
//
// O ciclo:
//   1. Procura: pergunta ao GitHub qual é a última release (10 s depois de abrir e a cada 30 min).
//   2. Baixa: se for mais nova, baixa o SharkInfo.exe novo em segundo plano e confere a
//      "impressão digital" dele (SHA-256) com a que a release publicou. Arquivo corrompido ou
//      adulterado é descartado.
//   3. Avisa: quando você NÃO está jogando em tela cheia, mostra uma notificação. Clicando,
//      aparecem as novidades com "Reiniciar agora" ou "Depois" (instala quando o app fechar).
//   4. Troca: o Windows não deixa sobrescrever um .exe aberto, mas deixa RENOMEAR. Então:
//      SharkInfo.exe -> SharkInfo.exe.old, o novo vai pro lugar, abre o novo e o velho sai.
//      O novo apaga o .old quando abre.
#pragma once
#include "comum.h"

namespace atualizador {

enum class Estado { Ocioso, Procurando, Baixando, Pronta, EmDia, Erro, Dev };

struct InfoRelease {
    std::wstring versao;      // "1.2.0" (sem o "v")
    std::wstring notas;       // texto da release (as novidades)
    std::string urlExe, urlSha;
};

// --- Funções "puras" (sem internet), separadas pra teste ---
int CompararVersoes(const std::wstring& a, const std::wstring& b);   // -1 se a<b, 0 igual, 1 se a>b
bool InterpretarRelease(const std::string& json, InfoRelease& saida);
std::string Sha256Hex(const std::wstring& arquivo);                  // "" se falhar
std::string LerHashDoArquivoSha(const std::string& conteudo);        // pega os 64 hex do .sha256

// --- O fluxo ---
// msgAviso chega na janela com wParam = Estado (como número) e lParam = 1 se foi pedido na mão.
void Configurar(HWND janela, UINT msgAviso);
std::wstring VersaoAtual();
bool EhVersaoDev();
void Procurar(bool manual);            // roda numa thread; responde com msgAviso
Estado EstadoAtual();
InfoRelease Pendente();                // dados da versão baixada (quando Estado::Pronta)
// Troca o .exe pelo novo. reiniciar = abre o novo logo em seguida. Devolve false se não conseguiu.
bool Instalar(bool reiniciar);
// Chamar logo ao abrir: espera o processo antigo sair (pós-atualização) e apaga o .old.
void LimparPosAtualizacao(DWORD pidAntigo);
// Está jogando/apresentando em tela cheia? (aí não é hora de aparecer nada)
bool UsuarioOcupado();

}  // namespace atualizador
