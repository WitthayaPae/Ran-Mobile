#pragma once
// minilzo's config detection trips over the game's WIN32 define; hide it.
#pragma push_macro("WIN32")
#pragma push_macro("_WINDOWS")
#undef WIN32
#undef _WINDOWS
#include <lzoconf.h>
#pragma pop_macro("_WINDOWS")
#pragma pop_macro("WIN32")
