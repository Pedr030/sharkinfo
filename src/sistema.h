// sistema.h — iniciar com o Windows e impedir que o app abra duas vezes.
#pragma once
#include "comum.h"

bool IniciaComWindows();
void DefinirInicioComWindows(bool ligar);
bool JaEstaAberto();   // cria uma "bandeirinha" (mutex) com nome; se já existir, tem outro aberto
