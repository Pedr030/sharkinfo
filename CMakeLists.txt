cmake_minimum_required(VERSION 3.16)
project(SharkInfo CXX RC)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SharkInfo WIN32
  src/main.cpp src/leitores.cpp src/historico.cpp src/avisos.cpp
  src/icone.cpp src/grafico.cpp src/config.cpp src/sistema.cpp src/widget.cpp src/atualizador.cpp
  src/polling.cpp src/hub.cpp
  res/app.rc)
target_compile_definitions(SharkInfo PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(SharkInfo PRIVATE hid setupapi gdiplus winhttp bcrypt shell32 comctl32 gdi32 user32 advapi32)

if(MSVC)
  target_compile_options(SharkInfo PRIVATE /utf-8 /W4)          # código-fonte em UTF-8 (acentos)
  target_link_options(SharkInfo PRIVATE /MANIFEST:NO)           # o manifesto já vai no app.rc
  set_property(TARGET SharkInfo PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")  # .exe sem DLLs extras
else()
  target_compile_options(SharkInfo PRIVATE -municode -Wall -O2)
  target_link_options(SharkInfo PRIVATE -municode -static -s)
endif()
