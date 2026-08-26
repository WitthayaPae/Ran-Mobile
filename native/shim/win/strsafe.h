#pragma once
#include "windows.h"
#include "fmt_msvc.h"
#define STRSAFE_E_INVALID_PARAMETER ((HRESULT)0x80070057L)
#define STRSAFE_E_INSUFFICIENT_BUFFER ((HRESULT)0x8007007AL)
#define STRSAFE_MAX_CCH 2147483647
#ifdef __cplusplus
inline HRESULT StringCchCopyA(char *d, size_t n, const char *s) { if (!d || !n) return STRSAFE_E_INVALID_PARAMETER; strncpy(d, s ? s : "", n); d[n-1] = 0; return S_OK; }
inline HRESULT StringCchCatA(char *d, size_t n, const char *s) { if (!d || !n) return STRSAFE_E_INVALID_PARAMETER; size_t l = strlen(d); if (l + 1 < n) strncat(d, s ? s : "", n - l - 1); return S_OK; }
inline HRESULT StringCchLengthA(const char *s, size_t, size_t *pl) { if (pl) *pl = s ? strlen(s) : 0; return S_OK; }
inline HRESULT StringCchVPrintfA(char *d, size_t n, const char *f, va_list a) {
    //  Work on a copy, so the caller's va_list survives the call.
    //
    //  The client formats the same argument list two or three times in a row
    //  without va_copy - see CInnerInterface::PrintConsoleTextDlg. That is only
    //  well defined because a 32-bit MSVC va_list is a bare char* passed by
    //  value; here it is __va_list_tag[1] and decays to a pointer, so consuming
    //  it would empty the caller's list and every later read would be garbage.
    //  The tyranny end notice showed a literal 1 as "68".
    va_list c;
    va_copy(c, a);
    //  See fmt_msvc.h: the client writes "%I64d", which bionic drops.
    if (RanFmt_NeedsRewrite(f)) { std::string r = RanFmt_MsvcToPosix(f); vsnprintf(d, n, r.c_str(), c); }
    else vsnprintf(d, n, f, c);
    va_end(c);
    return S_OK;
}
inline HRESULT StringCchPrintfA(char *d, size_t n, const char *f, ...) { va_list a; va_start(a, f); StringCchVPrintfA(d, n, f, a); va_end(a); return S_OK; }
inline HRESULT StringCchCopyW(wchar_t *d, size_t n, const wchar_t *s) { if (!d || !n) return STRSAFE_E_INVALID_PARAMETER; wcsncpy(d, s ? s : L"", n); d[n-1] = 0; return S_OK; }
inline HRESULT StringCchCatW(wchar_t *d, size_t n, const wchar_t *s) { if (!d || !n) return STRSAFE_E_INVALID_PARAMETER; size_t l = wcslen(d); if (l + 1 < n) wcsncat(d, s ? s : L"", n - l - 1); return S_OK; }
inline HRESULT StringCchLengthW(const wchar_t *s, size_t, size_t *pl) { if (pl) *pl = s ? wcslen(s) : 0; return S_OK; }
#define StringCchCopy    StringCchCopyA
#define StringCchCat     StringCchCatA
#define StringCchLength  StringCchLengthA
#define StringCchPrintf  StringCchPrintfA
#define StringCchVPrintf StringCchVPrintfA
#define StringCbCopy     StringCchCopyA
#define StringCbCat      StringCchCatA
#define StringCbPrintf   StringCchPrintfA
#define StringCbVPrintf  StringCchVPrintfA
#define StringCbLength   StringCchLengthA
#define StringCbVPrintfA StringCchVPrintfA
#endif
#ifdef __cplusplus
inline HRESULT StringCchCopyNA(char *d, size_t n, const char *s, size_t c) { if (!d || !n) return STRSAFE_E_INVALID_PARAMETER; size_t k = c < n - 1 ? c : n - 1; strncpy(d, s ? s : "", k); d[k] = 0; return S_OK; }
#define StringCchCopyN StringCchCopyNA
#endif
