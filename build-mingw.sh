#!/bin/sh
# Compila com MinGW-w64 (no Linux, no MSYS2 ou no GitHub Actions), gerando um .exe que roda no Windows.
#
#   ./build-mingw.sh                 -> versão de desenvolvimento (0.0.0-dev, sem atualizador)
#   VERSAO=1.2.0 ./build-mingw.sh    -> versão de release (é assim que o GitHub Actions chama)
set -e
CXX=${CXX:-x86_64-w64-mingw32-g++}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
if [ -n "$VERSAO" ]; then
  # "1.2.0" -> "1,2,0,0" (o formato numérico que o Windows usa nas propriedades do arquivo)
  NUM=$(echo "$VERSAO" | sed 's/-.*//' | awk -F. '{printf "%d,%d,%d,0", $1, $2, $3}')
  sed -i "s/^#define SHARKINFO_VERSAO \".*\"/#define SHARKINFO_VERSAO \"$VERSAO\"/; s/^#define SHARKINFO_VERSAO_NUM .*/#define SHARKINFO_VERSAO_NUM $NUM/" src/versao.h
fi
FLAGS="-std=c++17 -O2 -Wall -municode -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00"
LIBS="-lhid -lsetupapi -lgdiplus -lwinhttp -lbcrypt -lshell32 -lcomctl32 -lgdi32 -luser32 -ladvapi32"
mkdir -p build
$WINDRES res/app.rc -O coff -o build/app.res.o
$CXX $FLAGS src/*.cpp build/app.res.o -o build/SharkInfo.exe -mwindows -static -s $LIBS
# Testes (rodam no Windows de verdade, no GitHub Actions)
$CXX $FLAGS -Isrc tests/testes.cpp src/leitores.cpp src/historico.cpp src/avisos.cpp src/icone.cpp \
  src/config.cpp src/atualizador.cpp src/polling.cpp -o build/testes.exe -static $LIBS
rm -f build/app.res.o
echo "Pronto: build/SharkInfo.exe ($(grep '^#define SHARKINFO_VERSAO ' src/versao.h | cut -d'"' -f2))"
