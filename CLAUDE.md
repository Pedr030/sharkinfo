# CLAUDE.md — SharkInfo

App de bandeja do Windows (C++17 + API do Windows, sem bibliotecas externas) que mostra a bateria de mouses Attack Shark. **Leia o `HANDOFF.md` antes de qualquer mudança**: ele tem a arquitetura, os protocolos, o que já foi provado no hardware real e o que falta.

## Regras rápidas

- Compilar: `./build-mingw.sh` (precisa de `g++-mingw-w64-x86-64`). Sem `VERSAO`, sai `0.0.0-dev`.
- Os testes (`build/testes.exe`) só rodam no Windows; o CI roda em `windows-latest`. Não faça commit do `src/versao.h` alterado pelo build.
- Para publicar, adicione `## vX.Y.Z` no `CHANGELOG.md`, faça o commit, depois `git tag vX.Y.Z && git push origin vX.Y.Z`. Não renomeie os anexos `SharkInfo.exe` e `SharkInfo.exe.sha256` (o atualizador depende deles).
- Código e comentários em português, didáticos, quase linha por linha (o dono aprende pelo código).
- Commits em Conventional Commits em português, **sem linha de coautoria/IA**.
- Zero avisos com `-Wall -Wextra`. Funções de cálculo ficam separadas e ganham teste.
- A thread principal precisa ser rápida: o widget é filho da barra de tarefas do Explorer.
- Parte visual só está pronta com **print real** do dono; não confie em prévias simuladas.
- O projeto do krisgfx é GPL: use só fatos de protocolo, nunca copie código.
