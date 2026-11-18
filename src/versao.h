// versao.h — versão do app. O script de build (no GitHub Actions) reescreve estas duas linhas
// com a versão da tag (ex.: v1.2.0). Compilando à mão, fica "0.0.0-dev" e o atualizador se desliga.
#pragma once
#define SHARKINFO_VERSAO "0.0.0-dev"
#define SHARKINFO_VERSAO_NUM 0,0,0,0

#define SHARKINFO_REPO "Pedr030/sharkinfo"   // de onde vêm as atualizações (GitHub Releases)

// Truque do pré-processador pra transformar "texto" em L"texto" (texto largo, do Windows).
#define SHARKINFO_LARGO2(x) L##x
#define SHARKINFO_LARGO(x) SHARKINFO_LARGO2(x)
