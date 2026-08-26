#pragma once
#include "windows.h"
typedef struct tagCOMPOSITIONFORM { DWORD dwStyle; POINT ptCurrentPos; RECT rcArea; } COMPOSITIONFORM, *LPCOMPOSITIONFORM;
typedef struct tagCANDIDATEFORM { DWORD dwIndex, dwStyle; POINT ptCurrentPos; RECT rcArea; } CANDIDATEFORM, *LPCANDIDATEFORM;
typedef struct tagCANDIDATELIST { DWORD dwSize, dwStyle, dwCount, dwSelection, dwPageStart, dwPageSize, dwOffset[1]; } CANDIDATELIST, *LPCANDIDATELIST;
#define CFS_POINT 0x0002
#define CFS_CANDIDATEPOS 0x0040
#define GCS_COMPSTR 0x0008
#define GCS_RESULTSTR 0x0800
#define GCS_COMPATTR 0x0010
#define GCS_CURSORPOS 0x0080
#define IMN_OPENCANDIDATE 0x0005
#define IMN_CLOSECANDIDATE 0x0004
#define IMN_CHANGECANDIDATE 0x0003
#define IMN_SETCONVERSIONMODE 0x0006
#define IMN_SETOPENSTATUS 0x0008
#define IME_CMODE_NATIVE 0x0001
#define ATTR_TARGET_CONVERTED 2
#define ATTR_TARGET_NOTCONVERTED 4
#ifdef __cplusplus
extern "C" {
#endif
HIMC  ImmGetContext(HWND);
BOOL  ImmReleaseContext(HWND, HIMC);
LONG  ImmGetCompositionStringA(HIMC, DWORD, LPVOID, DWORD);
#define ImmGetCompositionString ImmGetCompositionStringA
BOOL  ImmSetCompositionWindow(HIMC, LPCOMPOSITIONFORM);
BOOL  ImmSetCandidateWindow(HIMC, LPCANDIDATEFORM);
DWORD ImmGetCandidateListA(HIMC, DWORD, LPCANDIDATELIST, DWORD);
#define ImmGetCandidateList ImmGetCandidateListA
BOOL  ImmGetOpenStatus(HIMC);
BOOL  ImmSetOpenStatus(HIMC, BOOL);
BOOL  ImmGetConversionStatus(HIMC, LPDWORD, LPDWORD);
BOOL  ImmSetConversionStatus(HIMC, DWORD, DWORD);
HIMC  ImmAssociateContext(HWND, HIMC);
BOOL  ImmNotifyIME(HIMC, DWORD, DWORD, DWORD);
#ifdef __cplusplus
}
#endif
typedef HANDLE HIMCC;
typedef struct tagINPUTCONTEXT { HWND hWnd; BOOL fOpen; POINT ptStatusWndPos, ptSoftKbdPos;
    DWORD fdwConversion, fdwSentence; HIMCC hCompStr, hCandInfo, hGuideLine, hPrivate;
    DWORD dwNumMsgBuf; HIMCC hMsgBuf; DWORD fdwInit; } INPUTCONTEXT, *LPINPUTCONTEXT;
#define IMM_ERROR_NODATA (-1)
#define IMM_ERROR_GENERAL (-2)
