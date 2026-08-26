#pragma once
#include "windows.h"
typedef struct _IP_ADAPTER_INFO {
    struct _IP_ADAPTER_INFO *Next; DWORD ComboIndex; char AdapterName[260], Description[132];
    UINT AddressLength; BYTE Address[8]; DWORD Index, Type, DhcpEnabled;
} IP_ADAPTER_INFO, *PIP_ADAPTER_INFO;
#ifdef __cplusplus
inline DWORD GetAdaptersInfo(PIP_ADAPTER_INFO, PULONG n) { if (n) *n = 0; return 111; }
#endif
