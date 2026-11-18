// config.cpp — lê e grava o config.ini.
#include "config.h"

#include <algorithm>
#include <bcrypt.h>   // BCryptGenRandom: números aleatórios seguros, do próprio Windows

static const wchar_t* SECAO = L"SharkInfo";   // o [SharkInfo] no topo do .ini

std::wstring Config::Caminho() { return PastaDados() + L"\\config.ini"; }

static int LerInt(const wchar_t* chave, int padrao) {
    return (int)GetPrivateProfileIntW(SECAO, chave, padrao, Config::Caminho().c_str());
}
static std::wstring LerTexto(const wchar_t* chave, const std::wstring& padrao) {
    wchar_t buf[512];
    GetPrivateProfileStringW(SECAO, chave, padrao.c_str(), buf, 512, Config::Caminho().c_str());
    return buf;
}
static void GravarInt(const wchar_t* chave, int valor) {
    WritePrivateProfileStringW(SECAO, chave, std::to_wstring(valor).c_str(), Config::Caminho().c_str());
}
static void GravarTexto(const wchar_t* chave, const std::wstring& valor) {
    WritePrivateProfileStringW(SECAO, chave, valor.c_str(), Config::Caminho().c_str());
}

static std::wstring TopicoAleatorio() {
    unsigned char bytes[5];
    BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    std::wstring t = L"sharkinfo-";
    wchar_t hex[3];
    for (unsigned char b : bytes) {
        swprintf(hex, 3, L"%02x", b);
        t += hex;
    }
    return t;
}

Config Config::Carregar() {
    Config c;                                        // começa com os padrões
    c.avisoPercentual = LerInt(L"aviso_percentual", c.avisoPercentual);
    c.criticoPercentual = LerInt(L"critico_percentual", c.criticoPercentual);
    c.intervaloSegundos = std::max(2, LerInt(L"intervalo_segundos", c.intervaloSegundos));
    c.avisoNaTela = LerInt(L"aviso_na_tela", c.avisoNaTela) != 0;
    c.segundosAvisoNaTela = LerInt(L"segundos_aviso_na_tela", c.segundosAvisoNaTela);
    c.avisoNoTopo = LerTexto(L"posicao_aviso", L"topo") != L"base";
    c.notificacaoWindows = LerInt(L"notificacao_windows", c.notificacaoWindows) != 0;
    c.celularAtivo = LerInt(L"celular_ativo", c.celularAtivo) != 0;
    c.celularTopico = LerTexto(L"celular_topico", L"");
    c.celularServidor = LerTexto(L"celular_servidor", c.celularServidor);
    c.avisarCargaCompleta = LerInt(L"avisar_carga_completa", c.avisarCargaCompleta) != 0;
    c.minutosCargaPadrao = std::max(10, LerInt(L"minutos_carga_padrao", c.minutosCargaPadrao));
    c.forcarEscalaDezenas = LerInt(L"forcar_escala_dezenas", c.forcarEscalaDezenas);
    c.widgetBarra = LerInt(L"widget_barra", c.widgetBarra) != 0;
    c.iconeNumero = LerInt(L"icone_numero", c.iconeNumero) != 0;
    c.atualizarAutomaticamente = LerInt(L"atualizar_automaticamente", c.atualizarAutomaticamente) != 0;
    if (c.celularTopico.empty()) c.celularTopico = TopicoAleatorio();   // 1ª vez: canal só seu
    c.Salvar();                                      // grava, pra todas as chaves aparecerem no arquivo
    return c;
}

void Config::Salvar() const {
    GravarInt(L"aviso_percentual", avisoPercentual);
    GravarInt(L"critico_percentual", criticoPercentual);
    GravarInt(L"intervalo_segundos", intervaloSegundos);
    GravarInt(L"aviso_na_tela", avisoNaTela);
    GravarInt(L"segundos_aviso_na_tela", segundosAvisoNaTela);
    GravarTexto(L"posicao_aviso", avisoNoTopo ? L"topo" : L"base");
    GravarInt(L"notificacao_windows", notificacaoWindows);
    GravarInt(L"celular_ativo", celularAtivo);
    GravarTexto(L"celular_topico", celularTopico);
    GravarTexto(L"celular_servidor", celularServidor);
    GravarInt(L"avisar_carga_completa", avisarCargaCompleta);
    GravarInt(L"minutos_carga_padrao", minutosCargaPadrao);
    GravarInt(L"forcar_escala_dezenas", forcarEscalaDezenas);
    GravarInt(L"widget_barra", widgetBarra);
    GravarInt(L"icone_numero", iconeNumero);
    GravarInt(L"atualizar_automaticamente", atualizarAutomaticamente);
}
