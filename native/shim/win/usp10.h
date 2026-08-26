// Uniscribe stubs — DXUT's GUI references these types but the game never calls them.
#pragma once
#include "windows.h"
typedef void *SCRIPT_STRING_ANALYSIS;
typedef void *SCRIPT_CACHE;
typedef struct tagSCRIPT_CONTROL { DWORD dwFlags; } SCRIPT_CONTROL;
typedef struct tagSCRIPT_STATE { WORD flags; } SCRIPT_STATE;
typedef struct tagSCRIPT_DIGITSUBSTITUTE { DWORD NationalDigitLanguage, TraditionalDigitLanguage, DigitSubstitute, dwReserved; } SCRIPT_DIGITSUBSTITUTE;
typedef struct tagSCRIPT_TABDEF { int cTabStops, iScale, *pTabStops, iTabOrigin; } SCRIPT_TABDEF;
typedef struct tagSCRIPT_LOGATTR { BYTE fSoftBreak; } SCRIPT_LOGATTR;
typedef struct tagSCRIPT_ANALYSIS { WORD flags; } SCRIPT_ANALYSIS;
typedef struct tagSCRIPT_ITEM { int iCharPos; SCRIPT_ANALYSIS a; } SCRIPT_ITEM;
