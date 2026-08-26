#pragma once
#include "windows.h"
typedef ULONG_PTR HCRYPTPROV, HCRYPTHASH, HCRYPTKEY;
#define PROV_RSA_FULL 1
#define CRYPT_VERIFYCONTEXT 0xF0000000
#define CALG_MD5 0x8003
#define HP_HASHVAL 0x0002
#ifdef __cplusplus
extern "C" {
#endif
BOOL CryptAcquireContextA(HCRYPTPROV*, LPCSTR, LPCSTR, DWORD, DWORD);
#define CryptAcquireContext CryptAcquireContextA
BOOL CryptReleaseContext(HCRYPTPROV, DWORD);
BOOL CryptCreateHash(HCRYPTPROV, DWORD, HCRYPTKEY, DWORD, HCRYPTHASH*);
BOOL CryptHashData(HCRYPTHASH, const BYTE*, DWORD, DWORD);
BOOL CryptGetHashParam(HCRYPTHASH, DWORD, BYTE*, DWORD*, DWORD);
BOOL CryptDestroyHash(HCRYPTHASH);
#ifdef __cplusplus
}
#endif
