#pragma once
#include "windows.h"
#ifdef __cplusplus
inline const char *DXGetErrorString9(HRESULT) { return "D3D error"; }
inline const char *DXGetErrorDescription9(HRESULT) { return "D3D error"; }
inline HRESULT DXTraceA(const char *, DWORD, HRESULT hr, const char *, BOOL) { return hr; }
#endif
#define DXTRACE_ERR(str,hr)     DXTraceA(__FILE__, (DWORD)__LINE__, hr, str, FALSE)
#define DXTRACE_ERR_MSGBOX(s,h) DXTraceA(__FILE__, (DWORD)__LINE__, h, s, FALSE)
#define DXTRACE_MSG(str)        DXTraceA(__FILE__, (DWORD)__LINE__, 0, str, FALSE)
#define DXTrace DXTraceA
