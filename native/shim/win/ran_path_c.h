// Redirect fopen for C libraries that never see windows.h.
//
// minizip (in Lib_ZLib) opens the .rcc archives directly with fopen, so without
// this the engine's Windows-shaped paths ("\Data\GUI\Gui.rcc") never resolve on
// a case-sensitive filesystem and every archive fails to open.
#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
FILE *ran_fopen(const char *path, const char *mode);
#ifdef __cplusplus
}
#endif
#define fopen(p, m) ran_fopen((p), (m))
