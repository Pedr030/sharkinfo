// sistema.cpp
#include "sistema.h"

// Chave do registro com a lista de programas que abrem no login (só do seu usuário, sem admin).
static const wchar_t* CHAVE_RUN = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* NOME = L"SharkInfo";

bool IniciaComWindows() {
    HKEY chave;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CHAVE_RUN, 0, KEY_READ, &chave) != ERROR_SUCCESS) return false;
    bool existe = RegQueryValueExW(chave, NOME, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(chave);
    return existe;
}

void DefinirInicioComWindows(bool ligar) {
    HKEY chave;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CHAVE_RUN, 0, KEY_SET_VALUE, &chave) != ERROR_SUCCESS) return;
    if (ligar) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);                 // caminho deste .exe
        std::wstring comando = L"\"" + std::wstring(exe) + L"\"";   // entre aspas (pode ter espaços)
        RegSetValueExW(chave, NOME, 0, REG_SZ, reinterpret_cast<const BYTE*>(comando.c_str()),
                       (DWORD)((comando.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(chave, NOME);
    }
    RegCloseKey(chave);
}

bool JaEstaAberto() {
    // "Local\" = vale só pra sessão do seu usuário. O handle fica aberto até o app fechar.
    CreateMutexW(nullptr, FALSE, L"Local\\SharkInfoMutex");
    return GetLastError() == ERROR_ALREADY_EXISTS;
}
