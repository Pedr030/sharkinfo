// comum.h — peças usadas por todos os arquivos.
//
// Pensa neste arquivo como a "caixa de ferramentas" compartilhada da obra:
// a ficha de leitura da bateria, uma trava pra threads e funções de tempo/texto.
#pragma once

#ifndef UNICODE
#define UNICODE                 // usa as versões "W" (texto Unicode) das funções do Windows
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX                // impede o windows.h de criar macros min/max que quebram std::min
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

// Ficha com o resultado de uma leitura (igual à "Leitura" da versão Python).
struct Leitura {
    int percentual = -1;                    // 0..100, ou -1 = ainda não sabemos
    bool carregando = false;                // no cabo/dock?
    bool dormindo = false;                  // mouse parou de responder?
    std::wstring modelo = L"Mouse sem fio"; // nome pra mostrar
    std::wstring conexao = L"receptor 2.4G";
    double momento = 0;                     // segundos desde 01/01/1970 (horário "Unix")
};

// Agora, em segundos Unix com fração. O Windows conta em "ticks" de 100 ns desde 1601,
// então convertemos: tira a diferença entre 1601 e 1970 e divide por 10 milhões.
inline double Agora() {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER t;
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    return (double)(t.QuadPart - 116444736000000000ULL) / 1e7;
}

// Trava (mutex) do próprio Windows. Só uma thread por vez entra na "sala".
class Trava {
public:
    Trava() { InitializeCriticalSection(&cs_); }
    ~Trava() { DeleteCriticalSection(&cs_); }
    void Entrar() { EnterCriticalSection(&cs_); }
    void Sair() { LeaveCriticalSection(&cs_); }
private:
    CRITICAL_SECTION cs_;
};

// Segura a trava enquanto existir e solta sozinha no fim do bloco { } (técnica RAII).
class Segurando {
public:
    explicit Segurando(Trava& t) : t_(t) { t_.Entrar(); }
    ~Segurando() { t_.Sair(); }
private:
    Trava& t_;
};

// "125.4" minutos -> "2 h 05 min"; menos de 0 -> "calculando…"
inline std::wstring FormatarMinutos(double minutos) {
    if (minutos < 0) return L"calculando…";
    int m = (int)(minutos + 0.5);           // arredonda
    wchar_t buf[32];
    if (m < 60) swprintf(buf, 32, L"%d min", m);
    else swprintf(buf, 32, L"%d h %02d min", m / 60, m % 60);
    return buf;
}

// Converte texto do Windows (UTF-16) pra UTF-8 (usado no JSON do celular e nos arquivos).
inline std::string ParaUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// Pasta de dados: %APPDATA%\SharkInfo (criada se não existir).
inline std::wstring PastaDados(bool demo = false) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring pasta = (n > 0 && n < MAX_PATH) ? std::wstring(buf) : L".";
    pasta += L"\\SharkInfo";
    CreateDirectoryW(pasta.c_str(), nullptr);   // se já existir, só falha em silêncio
    if (demo) {
        pasta += L"\\demo";                      // a demonstração não suja o histórico real
        CreateDirectoryW(pasta.c_str(), nullptr);
    }
    return pasta;
}

// Mensagens "particulares" que o app troca entre as suas threads e a janela.
constexpr UINT WM_APP_BANDEJA = WM_APP + 1;   // clique no ícone da bandeja
constexpr UINT WM_APP_LEITURA = WM_APP + 2;   // a thread de leitura tem dados novos
constexpr UINT WM_APP_DIAG_OK = WM_APP + 3;   // diagnóstico terminou
constexpr UINT WM_APP_WIDGET = WM_APP + 4;    // clique no widget da barra de tarefas
constexpr UINT WM_APP_ATUALIZACAO = WM_APP + 5;   // o atualizador tem novidade
