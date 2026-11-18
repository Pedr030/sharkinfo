// gdiplus_inc.h — inclui o GDI+ (desenho com antisserrilhado e transparência, que vem no Windows).
// O GDI+ espera as macros min/max, que desligamos com NOMINMAX. Este truque dá a ele as versões do C++.
#pragma once
#include "comum.h"
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}
#include <gdiplus.h>
