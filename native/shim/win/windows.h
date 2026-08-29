// Win32 shim for the RAN native mobile port.
// Provides the subset of <windows.h> the PC client actually touches, on POSIX/Android.
#pragma once
#ifndef RAN_WINDOWS_SHIM_H
#define RAN_WINDOWS_SHIM_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <time.h>
#include <math.h>

// ---------------------------------------------------------------- basic types
// CRITICAL: the wire protocol is MSVC x86. long/DWORD must stay 32-bit.
typedef unsigned char       BYTE, UCHAR, *PBYTE, *LPBYTE;
typedef unsigned short      WORD, USHORT, *PWORD, *LPWORD;
typedef int32_t             LONG, *PLONG, *LPLONG;
typedef uint32_t            DWORD, ULONG, *PDWORD, *LPDWORD, *PULONG;
typedef int                 INT, BOOL, *PINT, *LPINT, *PBOOL, *LPBOOL;
typedef unsigned int        UINT, *PUINT, *LPUINT;
typedef int64_t             LONGLONG, INT64;
typedef uint64_t            ULONGLONG, UINT64, DWORDLONG, QWORD;
typedef float               FLOAT;
typedef double              DOUBLE;
typedef short               SHORT;
typedef char                CHAR, TCHAR, *PSTR, *LPSTR, *LPTSTR, *PTSTR;
typedef const char         *LPCSTR, *PCSTR, *LPCTSTR, *PCTSTR;
typedef wchar_t             WCHAR, *PWSTR, *LPWSTR;
typedef const wchar_t      *LPCWSTR, *PCWSTR;
typedef void                VOID, *PVOID, *LPVOID;
typedef const void         *LPCVOID;
typedef unsigned char       BOOLEAN;
typedef int32_t             HRESULT;
typedef intptr_t            INT_PTR, LONG_PTR, SSIZE_T;
typedef uintptr_t           UINT_PTR, ULONG_PTR, DWORD_PTR, SIZE_T;
typedef uint32_t            COLORREF;
typedef uint16_t            ATOM;
typedef uint8_t             UINT8;
typedef int8_t              INT8;
typedef uint16_t            UINT16;
typedef int16_t             INT16;
typedef uint32_t            UINT32;
typedef int32_t             INT32;
typedef LONG_PTR            LPARAM, LRESULT;
typedef UINT_PTR            WPARAM;
typedef unsigned long       ULONG_NATIVE;

typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; };
    struct { DWORD LowPart; LONG HighPart; } u;
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;
typedef union _ULARGE_INTEGER {
    struct { DWORD LowPart; DWORD HighPart; };
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

// --------------------------------------------------------------------- handles
typedef void *HANDLE;
#define RAN_DECLARE_HANDLE(n) typedef void *n
RAN_DECLARE_HANDLE(HWND);      RAN_DECLARE_HANDLE(HDC);       RAN_DECLARE_HANDLE(HINSTANCE);
RAN_DECLARE_HANDLE(HMODULE);   RAN_DECLARE_HANDLE(HICON);     RAN_DECLARE_HANDLE(HCURSOR);
RAN_DECLARE_HANDLE(HBITMAP);   RAN_DECLARE_HANDLE(HBRUSH);    RAN_DECLARE_HANDLE(HPEN);
RAN_DECLARE_HANDLE(HFONT);     RAN_DECLARE_HANDLE(HMENU);     RAN_DECLARE_HANDLE(HGDIOBJ);
RAN_DECLARE_HANDLE(HGLOBAL);   RAN_DECLARE_HANDLE(HLOCAL);    RAN_DECLARE_HANDLE(HKEY);
RAN_DECLARE_HANDLE(HRGN);      RAN_DECLARE_HANDLE(HMONITOR);  RAN_DECLARE_HANDLE(HPALETTE);
RAN_DECLARE_HANDLE(HKL);       RAN_DECLARE_HANDLE(HIMC);      RAN_DECLARE_HANDLE(HACCEL);
RAN_DECLARE_HANDLE(HTASK);     RAN_DECLARE_HANDLE(HDROP);     RAN_DECLARE_HANDLE(HRSRC);
typedef HANDLE *PHANDLE, *LPHANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

// ------------------------------------------------------------------- calling
#define WINAPI
#define APIENTRY
#define CALLBACK
#define WINAPIV
#define STDAPI          HRESULT
#define STDAPICALLTYPE
#define __stdcall
#define __cdecl
#define __fastcall
#define _stdcall
#define _cdecl
#define PASCAL
#define FAR
#define NEAR
#define CONST const
#define IN
#define OUT
#define OPTIONAL
#define DECLSPEC_NOVTABLE
#define DECLSPEC_IMPORT
#define WINGDIAPI
#define APIPRIVATE
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif
#ifndef NULL
#define NULL 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#define _MAX_PATH 260
#define _MAX_DRIVE 3
#define _MAX_DIR  256
#define _MAX_FNAME 256
#define _MAX_EXT  256
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define DECLARE_HANDLE(n) typedef void *n

// ------------------------------------------------------------------------ COM
#define interface struct
#ifndef __RPC_FAR
#define __RPC_FAR
#endif
#define STDMETHODCALLTYPE
#define STDMETHOD(m)              virtual HRESULT m
#define STDMETHOD_(t,m)           virtual t m
#define STDMETHODV(m)             virtual HRESULT m
#define STDMETHODV_(t,m)          virtual t m
#define PURE                      = 0
#define THIS_
#define THIS                      void
#define DECLARE_INTERFACE(i)      struct i
#define DECLARE_INTERFACE_(i,b)   struct i : public b
#define DECLARE_INTERFACE_IID_(i,b,x) struct i : public b
#define MIDL_INTERFACE(x)         struct

typedef struct _GUID {
    DWORD Data1; WORD Data2; WORD Data3; BYTE Data4[8];
    bool operator==(const _GUID &o) const { return memcmp(this,&o,sizeof(_GUID))==0; }
    bool operator!=(const _GUID &o) const { return !(*this==o); }
} GUID, IID, CLSID, UUID;
typedef const GUID &REFGUID;
typedef const IID  &REFIID;
typedef const CLSID &REFCLSID;

#ifdef INITGUID
#define DEFINE_GUID(name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8) \
    extern "C" const GUID name = {l,w1,w2,{b1,b2,b3,b4,b5,b6,b7,b8}}
#else
#define DEFINE_GUID(name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8) \
    extern "C" const GUID name
#endif
#define DEFINE_OLEGUID(name,l,w1,w2) DEFINE_GUID(name,l,w1,w2,0xC0,0,0,0,0,0,0,0x46)

struct IUnknown {
    virtual HRESULT QueryInterface(REFIID riid, void **ppv) = 0;
    virtual ULONG   AddRef() = 0;
    virtual ULONG   Release() = 0;
};
typedef IUnknown *LPUNKNOWN;
#define IID_IUnknown_DEFINED

// ---------------------------------------------------------------- HRESULT set
#define S_OK           ((HRESULT)0)
#define S_FALSE        ((HRESULT)1)
#define NOERROR        ((HRESULT)0)
#define E_FAIL         ((HRESULT)0x80004005L)
#define E_INVALIDARG   ((HRESULT)0x80070057L)
#define E_OUTOFMEMORY  ((HRESULT)0x8007000EL)
#define E_NOTIMPL      ((HRESULT)0x80004001L)
#define E_NOINTERFACE  ((HRESULT)0x80004002L)
#define E_POINTER      ((HRESULT)0x80004003L)
#define E_HANDLE       ((HRESULT)0x80070006L)
#define E_ABORT        ((HRESULT)0x80004004L)
#define E_ACCESSDENIED ((HRESULT)0x80070005L)
#define E_UNEXPECTED   ((HRESULT)0x8000FFFFL)
#define SUCCEEDED(hr)  (((HRESULT)(hr)) >= 0)
#define FAILED(hr)     (((HRESULT)(hr)) < 0)
#define MAKE_HRESULT(s,f,c) ((HRESULT)(((unsigned)(s)<<31)|((unsigned)(f)<<16)|((unsigned)(c))))
#define HRESULT_CODE(hr) ((hr) & 0xFFFF)
#define SEVERITY_SUCCESS 0
#define SEVERITY_ERROR   1
#define FACILITY_ITF     4
#define FACILITY_WIN32   7
#define HRESULT_FROM_WIN32(x) ((HRESULT)(x) <= 0 ? (HRESULT)(x) : \
        (HRESULT)(((x) & 0x0000FFFF) | (FACILITY_WIN32 << 16) | 0x80000000))

// ------------------------------------------------------------------ structs
typedef struct tagPOINT { LONG x, y; } POINT, *PPOINT, *LPPOINT;
typedef struct tagPOINTS { SHORT x, y; } POINTS;
typedef struct _POINTL { LONG x, y; } POINTL;
typedef struct tagSIZE { LONG cx, cy; } SIZE, *PSIZE, *LPSIZE;
typedef struct tagRECT { LONG left, top, right, bottom; } RECT, *PRECT, *LPRECT;
typedef const RECT *LPCRECT;
typedef struct _RECTL { LONG left, top, right, bottom; } RECTL;
typedef struct tagPALETTEENTRY { BYTE peRed, peGreen, peBlue, peFlags; } PALETTEENTRY, *LPPALETTEENTRY;
typedef struct tagRGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;
typedef struct _FILETIME { DWORD dwLowDateTime, dwHighDateTime; } FILETIME, *PFILETIME, *LPFILETIME;
typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
    CHAR cFileName[MAX_PATH]; CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, WIN32_FIND_DATA, *LPWIN32_FIND_DATAA, *LPWIN32_FIND_DATA;
typedef struct _SECURITY_ATTRIBUTES { DWORD nLength; LPVOID lpSecurityDescriptor; BOOL bInheritHandle; }
    SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef struct _OVERLAPPED { ULONG_PTR Internal, InternalHigh; DWORD Offset, OffsetHigh; HANDLE hEvent; }
    OVERLAPPED, *LPOVERLAPPED;
typedef struct _SYSTEM_INFO {
    union { DWORD dwOemId; struct { WORD wProcessorArchitecture, wReserved; }; }; DWORD dwPageSize; LPVOID lpMinimumApplicationAddress, lpMaximumApplicationAddress;
    DWORD_PTR dwActiveProcessorMask; DWORD dwNumberOfProcessors, dwProcessorType,
    dwAllocationGranularity; WORD wProcessorLevel, wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;
typedef struct _MEMORYSTATUS {
    DWORD dwLength, dwMemoryLoad; SIZE_T dwTotalPhys, dwAvailPhys, dwTotalPageFile,
    dwAvailPageFile, dwTotalVirtual, dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;
typedef struct _OSVERSIONINFOA {
    DWORD dwOSVersionInfoSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformId;
    CHAR szCSDVersion[128];
} OSVERSIONINFOA, OSVERSIONINFO, *LPOSVERSIONINFOA, *LPOSVERSIONINFO;
typedef struct tagMSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; }
    MSG, *LPMSG, *PMSG;
typedef struct _RTL_CRITICAL_SECTION { void *impl; } CRITICAL_SECTION, *LPCRITICAL_SECTION, RTL_CRITICAL_SECTION;

// -------------------------------------------------------------------- messages
#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_ACTIVATEAPP 0x001C
#define WM_SETCURSOR 0x0020
#define WM_GETMINMAXINFO 0x0024
#define WM_NCHITTEST 0x0084
#define WM_KEYFIRST 0x0100
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_DEADCHAR 0x0103
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_KEYLAST 0x0108
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION 0x010E
#define WM_IME_COMPOSITION 0x010F
#define WM_IME_KEYLAST 0x010F
#define WM_IME_SETCONTEXT 0x0281
#define WM_IME_NOTIFY 0x0282
#define WM_IME_CONTROL 0x0283
#define WM_IME_COMPOSITIONFULL 0x0284
#define WM_IME_SELECT 0x0285
#define WM_IME_CHAR 0x0286
#define WM_IME_REQUEST 0x0288
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_MOUSEFIRST 0x0200
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_MOUSEWHEEL 0x020A
#define WM_MOUSELAST 0x020A
#define WM_NOTIFY 0x004E
#define WM_USER 0x0400
#define WM_APP 0x8000
#define SW_HIDE 0
#define SW_SHOW 5
#define SW_SHOWNORMAL 1
#define SW_MINIMIZE 6
#define SW_RESTORE 9
#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT   0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010
#define WHEEL_DELTA 120
#define GET_WHEEL_DELTA_WPARAM(w) ((short)HIWORD(w))
#define PM_NOREMOVE 0
#define PM_REMOVE 1

// virtual keys
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE

// MessageBox flags
#define MB_OK 0x0
#define MB_OKCANCEL 0x1
#define MB_YESNO 0x4
#define MB_ICONERROR 0x10
#define MB_ICONHAND 0x10
#define MB_ICONQUESTION 0x20
#define MB_ICONEXCLAMATION 0x30
#define MB_ICONWARNING 0x30
#define MB_ICONINFORMATION 0x40
#define MB_ICONASTERISK 0x40
#define MB_SYSTEMMODAL 0x1000
#define MB_TOPMOST 0x40000
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7

// file / misc flags
#define GENERIC_READ  0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x1
#define FILE_SHARE_WRITE 0x2
#define CREATE_ALWAYS 2
#define CREATE_NEW 1
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_READONLY 0x1
#define FILE_ATTRIBUTE_HIDDEN 0x2
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INVALID_FILE_SIZE 0xFFFFFFFFu
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFu
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define WAIT_FAILED 0xFFFFFFFFu
#define INFINITE 0xFFFFFFFFu
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_ALREADY_EXISTS 183
#define GMEM_FIXED 0
#define GMEM_MOVEABLE 2
#define GMEM_ZEROINIT 0x40
#define GHND 0x42
#define GPTR 0x40
#define LMEM_FIXED 0
#define LMEM_ZEROINIT 0x40
#define LPTR 0x40
#define CP_ACP 0
#define CP_UTF8 65001
#define VER_PLATFORM_WIN32_NT 2

// ---------------------------------------------------------------------- macros
#define MAKEWORD(a,b)  ((WORD)(((BYTE)(a))|(((WORD)((BYTE)(b)))<<8)))
#define MAKELONG(a,b)  ((LONG)(((WORD)(a))|(((DWORD)((WORD)(b)))<<16)))
#define LOWORD(l)      ((WORD)(((DWORD_PTR)(l))&0xFFFF))
#define HIWORD(l)      ((WORD)((((DWORD_PTR)(l))>>16)&0xFFFF))
#define LOBYTE(w)      ((BYTE)(((DWORD_PTR)(w))&0xFF))
#define HIBYTE(w)      ((BYTE)((((DWORD_PTR)(w))>>8)&0xFF))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define RGB(r,g,b)     ((COLORREF)(((BYTE)(r))|((WORD)((BYTE)(g))<<8)|(((DWORD)(BYTE)(b))<<16)))
#define GetRValue(c)   ((BYTE)(c))
#define GetGValue(c)   ((BYTE)(((WORD)(c))>>8))
#define GetBValue(c)   ((BYTE)((c)>>16))
#define ZeroMemory(d,l)     memset((d),0,(l))
#define CopyMemory(d,s,l)   memcpy((d),(s),(l))
#define MoveMemory(d,s,l)   memmove((d),(s),(l))
#define FillMemory(d,l,f)   memset((d),(f),(l))
#define SecureZeroMemory(d,l) memset((d),0,(l))
#define MAKEINTRESOURCE(i) ((LPSTR)(ULONG_PTR)(WORD)(i))
#define _T(x) x
#define __TEXT(x) x
#define TEXT(x) x
#define _TEXT(x) x
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#define _countof(a)  (sizeof(a)/sizeof((a)[0]))
#define __forceinline inline
#define __declspec(x)
#define __pragma(x)
#define _inline inline
#define try_ try

// ------------------------------------------------------------------ CRT compat
#define _stricmp   strcasecmp
#define stricmp    strcasecmp
#define _strnicmp  strncasecmp
#define strnicmp   strncasecmp
#define _strcmpi   strcasecmp
#define strcmpi    strcasecmp
#define _wcsicmp   wcscasecmp
#define wcsicmp    wcscasecmp
#define _snprintf  snprintf
#define _vsnprintf vsnprintf
#define _snwprintf swprintf
#define _vsnwprintf vswprintf
#define _alloca    alloca
#define _strdup    strdup
#define _fileno    fileno
#define _unlink    unlink
#define _access    access
#define _chdir     chdir
#define _getcwd    getcwd
#define _mkdir(p)  mkdir((p),0777)
#define _rmdir     rmdir
#define _isnan     isnan
#define _finite    isfinite
#define _hypot     hypot
#define _i64toa(v,b,r) sprintf((b),"%lld",(long long)(v))
#define _ltoa(v,b,r)   sprintf((b),"%ld",(long)(v))
#define _itoa(v,b,r)   sprintf((b),"%d",(int)(v))
#define _ultoa(v,b,r)  sprintf((b),"%lu",(unsigned long)(v))
#define _gcvt(v,d,b)   sprintf((b),"%.*g",(d),(double)(v))
#define _atoi64(s)     strtoll((s),NULL,10)
#define lstrlen        strlen
#define lstrcpy        strcpy
#define lstrcat        strcat
#define lstrcmp        strcmp
#define lstrcmpi       strcasecmp
#define wsprintf       sprintf
#define wsprintfA      sprintf
#define wvsprintf      vsprintf
#define _tcslen        strlen
#define _tcscpy        strcpy
#define _tcscat        strcat
#define _tcscmp        strcmp
#define _tcsicmp       strcasecmp
#define _tcsstr        strstr
#define _tcschr        strchr
#define _tcsrchr       strrchr
#define _tprintf       printf
#define _stprintf      sprintf
#define _sntprintf     snprintf
#define _tcsncpy       strncpy
#define _ttoi          atoi
#define _tfopen        fopen
#define _tcstok        strtok
#define _tcsupr        strupr_shim
#define _tcslwr        strlwr_shim
#define _strupr        strupr_shim
#define _strlwr        strlwr_shim
#define strupr         strupr_shim
#define strlwr         strlwr_shim

#ifdef __cplusplus
extern "C" {
#endif
char *strupr_shim(char *s);
char *strlwr_shim(char *s);
int   _splitpath_shim(const char *path, char *drv, char *dir, char *fname, char *ext);
#define _splitpath(p,d,di,f,e) _splitpath_shim((p),(d),(di),(f),(e))
void  _makepath_shim(char *path, const char *drv, const char *dir, const char *f, const char *e);
#define _makepath(p,d,di,f,e) _makepath_shim((p),(d),(di),(f),(e))

// ---------------------------------------------------------------------- clock()
//  The client measures its ping as a difference of clock() values and prints
//  the result as milliseconds, which is what clock() means on Win32: wall time,
//  CLOCKS_PER_SEC == 1000. Bionic follows POSIX instead - processor time with
//  CLOCKS_PER_SEC == 1000000 - so that subtraction came out about a thousand
//  times too large and the ping display sat pinned at its 1000 ms clamp.
//  Hand the client back the Win32 pair; anything dividing by CLOCKS_PER_SEC
//  stays self-consistent.
clock_t RanWin_clock(void);
#undef  clock
#define clock() RanWin_clock()
#undef  CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 1000

// ---------------------------------------------------------------- kernel32 API
DWORD  GetTickCount(void);
DWORD  timeGetTime(void);
void   Sleep(DWORD ms);
BOOL   QueryPerformanceCounter(LARGE_INTEGER *p);
BOOL   QueryPerformanceFrequency(LARGE_INTEGER *p);
DWORD  GetCurrentThreadId(void);
DWORD  GetCurrentProcessId(void);
HANDLE GetCurrentProcess(void);
DWORD  GetLastError(void);
void   SetLastError(DWORD e);
void   OutputDebugStringA(LPCSTR s);
#define OutputDebugString OutputDebugStringA
#define OutputDebugStringW(s) ((void)0)

LONG   InterlockedIncrement(LONG volatile *p);
LONG   InterlockedDecrement(LONG volatile *p);
LONG   InterlockedExchange(LONG volatile *p, LONG v);
LONG   InterlockedExchangeAdd(LONG volatile *p, LONG v);
LONG   InterlockedCompareExchange(LONG volatile *p, LONG ex, LONG cmp);

void   InitializeCriticalSection(LPCRITICAL_SECTION cs);
void   InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD spin);
void   DeleteCriticalSection(LPCRITICAL_SECTION cs);
void   EnterCriticalSection(LPCRITICAL_SECTION cs);
void   LeaveCriticalSection(LPCRITICAL_SECTION cs);
BOOL   TryEnterCriticalSection(LPCRITICAL_SECTION cs);

HANDLE CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T stack,
                    DWORD (*start)(LPVOID), LPVOID param, DWORD flags, LPDWORD tid);
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCSTR name);
#define CreateEvent CreateEventA
BOOL   SetEvent(HANDLE h);
BOOL   ResetEvent(HANDLE h);
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name);
#define CreateMutex CreateMutexA
BOOL   ReleaseMutex(HANDLE h);
DWORD  WaitForSingleObject(HANDLE h, DWORD ms);
BOOL   CloseHandle(HANDLE h);
BOOL   TerminateThread(HANDLE h, DWORD code);
BOOL   GetExitCodeThread(HANDLE h, LPDWORD code);
BOOL   SetThreadPriority(HANDLE h, int p);
void   ExitThread(DWORD code);

HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                   DWORD disp, DWORD flags, HANDLE tmpl);
#define CreateFile CreateFileA
BOOL   ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD read, LPOVERLAPPED ov);
BOOL   WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD written, LPOVERLAPPED ov);
DWORD  SetFilePointer(HANDLE h, LONG dist, PLONG distHigh, DWORD method);
DWORD  GetFileSize(HANDLE h, LPDWORD hi);
BOOL   FlushFileBuffers(HANDLE h);
BOOL   DeleteFileA(LPCSTR p);
#define DeleteFile DeleteFileA
BOOL   CreateDirectoryA(LPCSTR p, LPSECURITY_ATTRIBUTES sa);
#define CreateDirectory CreateDirectoryA
BOOL   RemoveDirectoryA(LPCSTR p);
#define RemoveDirectory RemoveDirectoryA
BOOL   CopyFileA(LPCSTR a, LPCSTR b, BOOL failIfExists);
#define CopyFile CopyFileA
BOOL   MoveFileA(LPCSTR a, LPCSTR b);
#define MoveFile MoveFileA
DWORD  GetFileAttributesA(LPCSTR p);
#define GetFileAttributes GetFileAttributesA
DWORD  GetCurrentDirectoryA(DWORD n, LPSTR buf);
#define GetCurrentDirectory GetCurrentDirectoryA
BOOL   SetCurrentDirectoryA(LPCSTR p);
#define SetCurrentDirectory SetCurrentDirectoryA
DWORD  GetModuleFileNameA(HMODULE m, LPSTR buf, DWORD n);
#define GetModuleFileName GetModuleFileNameA
HMODULE GetModuleHandleA(LPCSTR n);
#define GetModuleHandle GetModuleHandleA
HANDLE FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA fd);
#define FindFirstFile FindFirstFileA
BOOL   FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA fd);
#define FindNextFile FindNextFileA
BOOL   FindClose(HANDLE h);

HMODULE LoadLibraryA(LPCSTR n);
#define LoadLibrary LoadLibraryA
#define LoadLibraryW(n) ((HMODULE)0)
#define LoadLibraryExA(n,a,b) LoadLibraryA(n)
void   *GetProcAddress(HMODULE m, LPCSTR n);
BOOL   FreeLibrary(HMODULE m);

HGLOBAL GlobalAlloc(UINT flags, SIZE_T n);
LPVOID  GlobalLock(HGLOBAL h);
BOOL    GlobalUnlock(HGLOBAL h);
HGLOBAL GlobalFree(HGLOBAL h);
#define LocalAlloc GlobalAlloc
#define LocalFree  GlobalFree
#define LocalLock  GlobalLock
#define LocalUnlock GlobalUnlock

void   GetSystemTime(LPSYSTEMTIME t);
void   GetLocalTime(LPSYSTEMTIME t);
BOOL   SystemTimeToFileTime(const SYSTEMTIME *st, LPFILETIME ft);
BOOL   FileTimeToSystemTime(const FILETIME *ft, LPSYSTEMTIME st);
BOOL   FileTimeToLocalFileTime(const FILETIME *ft, LPFILETIME out);
void   GetSystemInfo(LPSYSTEM_INFO si);
void   GlobalMemoryStatus(LPMEMORYSTATUS ms);
BOOL   GetVersionExA(LPOSVERSIONINFOA v);
#define GetVersionEx GetVersionExA
DWORD  GetVersion(void);
DWORD  GetPrivateProfileStringA(LPCSTR app, LPCSTR key, LPCSTR def, LPSTR buf, DWORD n, LPCSTR file);
#define GetPrivateProfileString GetPrivateProfileStringA
UINT   GetPrivateProfileIntA(LPCSTR app, LPCSTR key, INT def, LPCSTR file);
#define GetPrivateProfileInt GetPrivateProfileIntA
BOOL   WritePrivateProfileStringA(LPCSTR app, LPCSTR key, LPCSTR val, LPCSTR file);
#define WritePrivateProfileString WritePrivateProfileStringA

int    MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR src, int srcLen, LPWSTR dst, int dstLen);
int    WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR src, int srcLen, LPSTR dst, int dstLen,
                           LPCSTR defChar, LPBOOL usedDef);

// ------------------------------------------------------------------ user32 API
int    MessageBoxA(HWND h, LPCSTR text, LPCSTR caption, UINT type);
#define MessageBox MessageBoxA
SHORT  GetKeyState(int vk);
SHORT  GetAsyncKeyState(int vk);
BOOL   GetKeyboardState(PBYTE ks);
BOOL   GetCursorPos(LPPOINT p);
BOOL   SetCursorPos(int x, int y);
int    ShowCursor(BOOL show);
BOOL   ClientToScreen(HWND h, LPPOINT p);
BOOL   ScreenToClient(HWND h, LPPOINT p);
BOOL   GetClientRect(HWND h, LPRECT r);
BOOL   GetWindowRect(HWND h, LPRECT r);
BOOL   SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags);
BOOL   ShowWindow(HWND h, int cmd);
BOOL   UpdateWindow(HWND h);
BOOL   InvalidateRect(HWND h, const RECT *r, BOOL erase);
LRESULT SendMessageA(HWND h, UINT msg, WPARAM w, LPARAM l);
#define SendMessage SendMessageA
BOOL   PostMessageA(HWND h, UINT msg, WPARAM w, LPARAM l);
#define PostMessage PostMessageA
BOOL   PeekMessageA(LPMSG m, HWND h, UINT min, UINT max, UINT remove);
#define PeekMessage PeekMessageA
HWND   GetActiveWindow(void);
HWND   GetForegroundWindow(void);
HWND   SetCapture(HWND h);
BOOL   ReleaseCapture(void);
HDC    GetDC(HWND h);
int    ReleaseDC(HWND h, HDC dc);
int    GetSystemMetrics(int i);
BOOL   SetRect(LPRECT r, int l, int t, int rr, int b);
BOOL   PtInRect(const RECT *r, POINT p);
BOOL   IntersectRect(LPRECT out, const RECT *a, const RECT *b);
BOOL   OffsetRect(LPRECT r, int dx, int dy);
BOOL   SetForegroundWindow(HWND h);
UINT   MapVirtualKeyA(UINT code, UINT type);
#define MapVirtualKey MapVirtualKeyA
int    ToAscii(UINT vk, UINT scan, const BYTE *ks, LPWORD out, UINT flags);
BOOL   GetVersionExW(void *);
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SWP_NOSIZE 0x1
#define SWP_NOMOVE 0x2
#define SWP_NOZORDER 0x4
#define SWP_SHOWWINDOW 0x40
#define HWND_TOP ((HWND)0)
#define HWND_TOPMOST ((HWND)(intptr_t)-1)

DWORD  timeBeginPeriod(UINT p);
DWORD  timeEndPeriod(UINT p);
#ifdef __cplusplus
}
#endif

// _beginthreadex/_endthreadex (process.h users)
#ifdef __cplusplus
extern "C" {
#endif
uintptr_t _beginthreadex(void *sec, unsigned stack, unsigned (*start)(void *),
                         void *arg, unsigned flags, unsigned *tid);
uintptr_t _beginthread(void (*start)(void *), unsigned stack, void *arg);
void      _endthreadex(unsigned code);
void      _endthread(void);
#ifdef __cplusplus
}
#endif

#endif // RAN_WINDOWS_SHIM_H

// ------------------------------------------------ GDI types the DX SDK needs
#ifndef LF_FACESIZE
#define LF_FACESIZE 32
#endif
typedef struct tagLOGFONTA {
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision,
         lfClipPrecision, lfQuality, lfPitchAndFamily;
    CHAR lfFaceName[LF_FACESIZE];
} LOGFONTA, LOGFONT, *PLOGFONTA, *LPLOGFONTA, *LPLOGFONT;
typedef struct tagLOGFONTW {
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision,
         lfClipPrecision, lfQuality, lfPitchAndFamily;
    WCHAR lfFaceName[LF_FACESIZE];
} LOGFONTW, *PLOGFONTW, *LPLOGFONTW;
typedef struct tagTEXTMETRICA {
    LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading,
         tmAveCharWidth, tmMaxCharWidth, tmWeight, tmOverhang,
         tmDigitizedAspectX, tmDigitizedAspectY;
    BYTE tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar, tmItalic,
         tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
} TEXTMETRICA, TEXTMETRIC, *LPTEXTMETRICA, *LPTEXTMETRIC;
typedef struct tagTEXTMETRICW {
    LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading,
         tmAveCharWidth, tmMaxCharWidth, tmWeight, tmOverhang,
         tmDigitizedAspectX, tmDigitizedAspectY;
    WCHAR tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
    BYTE tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
} TEXTMETRICW, *LPTEXTMETRICW;
typedef struct _RGNDATAHEADER { DWORD dwSize, iType, nCount, nRgnSize; RECT rcBound; } RGNDATAHEADER;
typedef struct _RGNDATA { RGNDATAHEADER rdh; char Buffer[1]; } RGNDATA, *PRGNDATA, *LPRGNDATA;
typedef struct _GLYPHMETRICSFLOAT {
    FLOAT gmfBlackBoxX, gmfBlackBoxY;
    struct { FLOAT x, y; } gmfptGlyphOrigin;
    FLOAT gmfCellIncX, gmfCellIncY;
} GLYPHMETRICSFLOAT, *LPGLYPHMETRICSFLOAT;
typedef GUID *LPGUID;
typedef struct tagBITMAPINFOHEADER {
    DWORD biSize; LONG biWidth, biHeight; WORD biPlanes, biBitCount;
    DWORD biCompression, biSizeImage; LONG biXPelsPerMeter, biYPelsPerMeter;
    DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER;
typedef struct tagBITMAPINFO { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO, *LPBITMAPINFO;
typedef struct tagBITMAPFILEHEADER { WORD bfType; DWORD bfSize; WORD bfReserved1, bfReserved2; DWORD bfOffBits; } BITMAPFILEHEADER;
struct IStream : public IUnknown {
    virtual HRESULT Read(void *, ULONG, ULONG *) = 0;
    virtual HRESULT Write(const void *, ULONG, ULONG *) = 0;
};
typedef IStream *LPSTREAM;
struct IDirect3DBaseTexture9;
#define CW_USEDEFAULT ((int)0x80000000)
#define WS_OVERLAPPEDWINDOW 0x00CF0000
#define WS_POPUP 0x80000000
#define WS_VISIBLE 0x10000000
#define WS_CHILD 0x40000000
#define WS_EX_TOPMOST 0x00000008
#define CS_DBLCLKS 0x0008
#define IDC_ARROW MAKEINTRESOURCE(32512)
#define COLOR_WINDOW 5
#define D3D_SDK_VERSION_SHIM 32
#define DT_LEFT 0x0000
#define DT_CENTER 0x0001
#define DT_RIGHT 0x0002
#define DT_TOP 0x0000
#define DT_VCENTER 0x0004
#define DT_BOTTOM 0x0008
#define DT_WORDBREAK 0x0010
#define DT_SINGLELINE 0x0020
#define DT_NOCLIP 0x0100
#define DT_CALCRECT 0x0400
#define DT_EXPANDTABS 0x0040
#define DT_END_ELLIPSIS 0x8000
#define lstrlenW  wcslen
#define lstrcpyW  wcscpy
#define lstrcatW  wcscat
#define lstrcmpW  wcscmp
#define PRIMARYLANGID(lgid) ((WORD)(lgid) & 0x3ff)
#define SUBLANGID(lgid)     ((WORD)(lgid) >> 10)
#define MAKELANGID(p,s)     ((((WORD)(s)) << 10) | (WORD)(p))
#define LANG_KOREAN 0x12
#define LANG_ENGLISH 0x09
#ifdef __cplusplus
extern const GUID GUID_NULL_SHIM;
#define GUID_NULL GUID_NULL_SHIM
#endif
#define __max(a,b) (((a) > (b)) ? (a) : (b))
#define __min(a,b) (((a) < (b)) ? (a) : (b))
#define _ASSERT(x)  ((void)0)
#define _ASSERTE(x) ((void)0)
typedef long long __time64_t;
typedef int32_t __time32_t;
#define _time64(p) ((__time64_t)time((time_t *)(p)))
#define _localtime64(p) localtime((const time_t *)(p))
#define _gmtime64(p) gmtime((const time_t *)(p))
// x87 control word API — no FPU control on ARM; calls become no-ops.
#define _MCW_PC 0x00030000
#define _MCW_RC 0x00000300
#define _MCW_IC 0x00040000
#define _MCW_EM 0x0008001F
#define _PC_24  0x00020000
#define _PC_53  0x00010000
#define _PC_64  0x00000000
#define _RC_NEAR 0x00000000
#define _RC_UP   0x00000200
#define _RC_DOWN 0x00000100
#define _RC_CHOP 0x00000300
#define _IC_AFFINE 0x00040000
#define _IC_PROJECTIVE 0x00000000
#define _CW_DEFAULT (_PC_53 | _RC_NEAR | _IC_AFFINE)
#define _controlfp(nw,mask) ((unsigned int)0)
#define _control87(nw,mask) ((unsigned int)0)
#define _clearfp() ((unsigned int)0)
#define HEAP_ZERO_MEMORY 0x00000008
#define GetProcessHeap() ((HANDLE)1)
#define HeapAlloc(h,f,n)      (((f) & HEAP_ZERO_MEMORY) ? calloc(1,(n)) : malloc(n))
#define HeapFree(h,f,p)       (free(p), TRUE)
#define HeapReAlloc(h,f,p,n)  realloc((p),(n))
#define _creat(p,m) open((p), O_CREAT | O_TRUNC | O_WRONLY, (m))
#define _close      close
#define _read       read
#define _write      write
#define _lseek      lseek
#define _open       open

// POSIX/Linux macros that collide with RAN identifiers
#undef SS_DISABLE
#undef SS_ONSTACK
#undef SA_RESTART

// remaining Win32 types/APIs used in small corners
typedef char *PCHAR;
typedef char _TCHAR;
typedef float *PFLOAT;
typedef void *HHOOK;
typedef struct { LONG length, flags, showCmd; POINT ptMinPosition, ptMaxPosition; RECT rcNormalPosition; } WINDOWPLACEMENT;
typedef struct { DWORD cbSize, dwFlags; } STICKYKEYS, TOGGLEKEYS, FILTERKEYS;
typedef struct { DWORD dwSignature, dwStrucVersion, dwFileVersionMS, dwFileVersionLS,
    dwProductVersionMS, dwProductVersionLS, dwFileFlagsMask, dwFileFlags, dwFileOS,
    dwFileType, dwFileSubtype, dwFileDateMS, dwFileDateLS; } VS_FIXEDFILEINFO;
typedef struct tagBITMAP { LONG bmType, bmWidth, bmHeight, bmWidthBytes; WORD bmPlanes, bmBitsPixel; LPVOID bmBits; } BITMAP, *LPBITMAP;
typedef struct { DWORD style, dwExtendedStyle; WORD cdit; short x, y, cx, cy; } DLGTEMPLATE;
typedef struct { DWORD style, dwExtendedStyle; short x, y, cx, cy; WORD id; } DLGITEMTEMPLATE;
typedef struct { WORD vt; union { LONG lVal; double dblVal; void *byref; }; } VARIANT;
#define _tstof atof
#define _tstoi atoi
#define _tcstod strtod
#define _ttof   atof
#define lstrcpynA(d,s,n) (strncpy((d),(s),(n)-1), (d)[(n)-1]=0, (d))
#define lstrcpyn lstrcpynA
#define MB_ICONSTOP 0x10
#define ERROR_READ_FAULT 30
#define CO_E_NOTINITIALIZED ((HRESULT)0x800401F0L)
#define MM_TEXT 1
#define DEFAULT_CHARSET 1
#define HANGEUL_CHARSET 129
#define ANSI_CHARSET 0
#define FW_NORMAL 400
#define FW_BOLD 700
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_QUALITY 0
#define ANTIALIASED_QUALITY 4
#define DEFAULT_PITCH 0
#define FF_DONTCARE 0
#define VARIABLE_PITCH 2
#define TRANSPARENT 1
#define OPAQUE 2
#define SRCCOPY 0x00CC0020
#ifdef __cplusplus
extern "C" {
#endif
// GDI/registry/cursor calls that only exist on desktop; inert on mobile.
#ifdef __cplusplus
HGDIOBJ RanGdi_SelectObject(HDC, HGDIOBJ);
inline HGDIOBJ SelectObject(HDC hdc, HGDIOBJ o) { return RanGdi_SelectObject(hdc, o); }
BOOL RanGdi_DeleteObject(HGDIOBJ);
inline BOOL DeleteObject(HGDIOBJ o) { return RanGdi_DeleteObject(o); }
inline HGDIOBJ GetStockObject(int) { return 0; }
COLORREF RanGdi_SetTextColor(HDC, COLORREF);
inline COLORREF SetTextColor(HDC hdc, COLORREF c) { return RanGdi_SetTextColor(hdc, c); }
COLORREF RanGdi_SetBkColor(HDC, COLORREF);
inline COLORREF SetBkColor(HDC hdc, COLORREF c) { return RanGdi_SetBkColor(hdc, c); }
int RanGdi_SetBkMode(HDC, int);
inline int SetBkMode(HDC hdc, int m) { return RanGdi_SetBkMode(hdc, m); }
inline int SetMapMode(HDC, int) { return 0; }
inline HFONT CreateFontIndirectA(const LOGFONTA *) { return 0; }
HDC RanGdi_CreateCompatibleDC(HDC);
inline HDC CreateCompatibleDC(HDC hdc) { return RanGdi_CreateCompatibleDC(hdc); }
inline HBITMAP CreateCompatibleBitmap(HDC, int, int) { return 0; }
HBITMAP RanGdi_CreateDIBSection(HDC, const BITMAPINFO *, UINT, void **, HANDLE, DWORD);
inline HBITMAP CreateDIBSection(HDC hdc, const BITMAPINFO *bmi, UINT u, void **bits, HANDLE h, DWORD o) { return RanGdi_CreateDIBSection(hdc, bmi, u, bits, h, o); }
BOOL RanGdi_DeleteDC(HDC);
inline BOOL DeleteDC(HDC hdc) { return RanGdi_DeleteDC(hdc); }
inline BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD) { return TRUE; }
BOOL RanGdi_GetTextExtentPoint32A(HDC, LPCSTR, int, LPSIZE);
inline BOOL GetTextExtentPoint32A(HDC hdc, LPCSTR s, int n, LPSIZE z) { return RanGdi_GetTextExtentPoint32A(hdc, s, n, z); }
inline BOOL TextOutA(HDC, int, int, LPCSTR, int) { return TRUE; }
void RanGLR_SetGammaRamp(const unsigned short *ramp);
//  The client sets brightness/contrast/overbright through the display ramp
//  (GammaControl::Apply). There is no display LUT to program here, so the
//  renderer applies it as the last step of its shader instead  see
//  RanGLR_SetGammaRamp. Returning FALSE used to make every such setting a no-op.
inline BOOL GetDeviceGammaRamp(HDC, LPVOID) { return FALSE; }
inline BOOL SetDeviceGammaRamp(HDC, LPVOID p) {
    RanGLR_SetGammaRamp((const unsigned short *)p);
    return TRUE;
}
inline HCURSOR LoadCursorFromFileA(LPCSTR) { return 0; }
inline HCURSOR LoadCursorA(HINSTANCE, LPCSTR) { return 0; }
inline HCURSOR SetCursor(HCURSOR) { return 0; }
inline BOOL DestroyCursor(HCURSOR) { return TRUE; }
inline LONG RegOpenKeyExA(HKEY, LPCSTR, DWORD, DWORD, HKEY *) { return 1; }
inline LONG RegQueryValueExA(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return 1; }
inline LONG RegCloseKey(HKEY) { return 0; }
inline LONG RegSetValueExA(HKEY, LPCSTR, DWORD, DWORD, const BYTE *, DWORD) { return 1; }
#define CreateFontIndirect CreateFontIndirectA
#define GetTextExtentPoint32 GetTextExtentPoint32A
#define TextOut TextOutA
#define LoadCursorFromFile LoadCursorFromFileA
#define LoadCursor LoadCursorA
#define RegOpenKeyEx RegOpenKeyExA
#define RegQueryValueEx RegQueryValueExA
#endif
#define HKEY_LOCAL_MACHINE ((HKEY)0x80000002)
#define HKEY_CURRENT_USER  ((HKEY)0x80000001)
#define KEY_READ 0x20019
#define REG_SZ 1
#define REG_DWORD 4
#ifdef __cplusplus
}
#endif
#define _wcsnicmp wcsncasecmp
#define _wcsicmp  wcscasecmp
#define lstrcmpiW wcscasecmp
#define _wtoi(s)  ((int)wcstol((s), NULL, 10))
#define strrev    strrev_shim
#define _strrev   strrev_shim
#define NO_ERROR 0
#define ETO_OPAQUE 0x0002
#define VER_NT_WORKSTATION 1
#define VER_NT_DOMAIN_CONTROLLER 2
#define VER_NT_SERVER 3
#define CSIDL_PERSONAL 0x0005
#define CSIDL_APPDATA 0x001A
#define IME_CMODE_FULLSHAPE 0x0008
#define SPI_GETWHEELSCROLLLINES 0x0068
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define VT_UI4 19
#define VT_I4 3
#define VT_BSTR 8
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
typedef struct _OSVERSIONINFOEXA {
    DWORD dwOSVersionInfoSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformId;
    CHAR szCSDVersion[128]; WORD wServicePackMajor, wServicePackMinor, wSuiteMask;
    BYTE wProductType, wReserved;
} OSVERSIONINFOEXA, OSVERSIONINFOEX, *LPOSVERSIONINFOEXA;
typedef struct _EXCEPTION_RECORD { DWORD ExceptionCode, ExceptionFlags; void *ExceptionRecord, *ExceptionAddress; DWORD NumberParameters; ULONG_PTR ExceptionInformation[15]; } EXCEPTION_RECORD, *PEXCEPTION_RECORD;
typedef struct _CONTEXT { DWORD ContextFlags; } CONTEXT, *PCONTEXT;
typedef struct _EXCEPTION_POINTERS { PEXCEPTION_RECORD ExceptionRecord; PCONTEXT ContextRecord; } EXCEPTION_POINTERS, *PEXCEPTION_POINTERS, *LPEXCEPTION_POINTERS;
typedef LONG (*LPTOP_LEVEL_EXCEPTION_FILTER)(PEXCEPTION_POINTERS);
typedef wchar_t *BSTR;
typedef void *LPDISPATCH;
#ifdef __cplusplus
inline void VariantClear(VARIANT *) {}
inline void VariantInit(VARIANT *) {}
inline BOOL InflateRect(LPRECT r, int dx, int dy) { r->left -= dx; r->right += dx; r->top -= dy; r->bottom += dy; return TRUE; }
inline int  FillRect(HDC, const RECT *, HBRUSH) { return 1; }
inline BOOL SystemParametersInfoA(UINT, UINT, void *, UINT) { return FALSE; }
#define SystemParametersInfo SystemParametersInfoA
int RanGdi_GetDeviceCaps(HDC, int);
inline int  GetDeviceCaps(HDC hdc, int i) { return RanGdi_GetDeviceCaps(hdc, i); }
inline LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER) { return NULL; }
extern "C" char *strrev_shim(char *s);
#endif
#define WS_OVERLAPPED 0x00000000
#define WS_CAPTION 0x00C00000
#define WS_SYSMENU 0x00080000
#define WS_THICKFRAME 0x00040000
#define WS_MINIMIZEBOX 0x00020000
#define WS_MAXIMIZEBOX 0x00010000
#define WS_BORDER 0x00800000
#define WS_CLIPCHILDREN 0x02000000
#define WS_CLIPSIBLINGS 0x04000000
#define BI_RGB 0
#define DIB_RGB_COLORS 0
#define CF_TEXT 1
#define CF_UNICODETEXT 13
#ifdef __cplusplus
BOOL RanGdi_ExtTextOutA(HDC, int, int, UINT, const RECT *, LPCSTR, UINT, const INT *);
inline BOOL ExtTextOutA(HDC hdc, int x, int y, UINT o, const RECT *rc, LPCSTR s, UINT n, const INT *dx) { return RanGdi_ExtTextOutA(hdc, x, y, o, rc, s, n, dx); }
inline BOOL ExtTextOutW(HDC, int, int, UINT, const RECT *, LPCWSTR, UINT, const INT *) { return TRUE; }
#define ExtTextOut ExtTextOutA
HFONT RanGdi_CreateFontA(int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,LPCSTR);
inline HFONT CreateFontA(int h,int w,int e,int o,int wt,DWORD it,DWORD u,DWORD s,DWORD cs,DWORD op,DWORD cp,DWORD q,DWORD pf,LPCSTR face) { return RanGdi_CreateFontA(h,w,e,o,wt,it,u,s,cs,op,cp,q,pf,face); }
#define CreateFont CreateFontA
inline HCURSOR GetCursor() { return 0; }
inline BOOL OpenClipboard(HWND) { return FALSE; }
inline BOOL CloseClipboard() { return FALSE; }
inline BOOL EmptyClipboard() { return FALSE; }
inline HANDLE GetClipboardData(UINT) { return NULL; }
inline HANDLE SetClipboardData(UINT, HANDLE) { return NULL; }
inline UINT RegisterClipboardFormatA(LPCSTR) { return 0; }
#define RegisterClipboardFormat RegisterClipboardFormatA
inline DWORD GetFileVersionInfoSizeA(LPCSTR, LPDWORD) { return 0; }
inline BOOL GetFileVersionInfoA(LPCSTR, DWORD, DWORD, LPVOID) { return FALSE; }
inline BOOL VerQueryValueA(LPCVOID, LPCSTR, LPVOID *, PUINT) { return FALSE; }
#define GetFileVersionInfoSize GetFileVersionInfoSizeA
#define GetFileVersionInfo GetFileVersionInfoA
#define VerQueryValue VerQueryValueA
//  Defined in win_impl.cpp: hands back the data root, which is the only
//  writable location on device (per-character .gameopt, error logs).
extern "C" BOOL SHGetSpecialFolderPathA(HWND, LPSTR p, int folder, BOOL create);
#define SHGetSpecialFolderPath SHGetSpecialFolderPathA
#define RegSetValueEx RegSetValueExA
#endif

// SEH is unavailable on ARM. __try bodies still run; handlers become dead branches.
#ifdef RAN_MOBILE
#define __try        if (1)
#define __except(x)  else if (0)
#define __finally
#define __leave      goto ran_seh_leave
#define GetExceptionInformation() ((PEXCEPTION_POINTERS)0)
#define GetExceptionCode()        (0u)
#endif
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWL_USERDATA (-21)
#define SUBLANG_ENGLISH_US 0x01
#define SUBLANG_DEFAULT 0x01
#define SORT_DEFAULT 0x0
#define NORM_IGNORECASE 0x00000001
#define ERROR_OUTOFMEMORY 14
#define FIELD_OFFSET(t,f) ((LONG)__builtin_offsetof(t,f))
#ifdef __cplusplus
inline int MulDiv(int a, int b, int c) { return c ? (int)(((int64_t)a * b) / c) : -1; }
inline BOOL ClipCursor(const RECT *) { return TRUE; }
inline HRESULT CoInitialize(void *) { return S_OK; }
inline HRESULT CoInitializeEx(void *, DWORD) { return S_OK; }
inline void CoUninitialize() {}
inline HBRUSH CreateSolidBrush(COLORREF) { return 0; }
inline BOOL CryptDestroyKey(ULONG_PTR) { return TRUE; }
inline LONG GetWindowLongA(HWND, int) { return 0; }
inline LONG SetWindowLongA(HWND, int, LONG) { return 0; }
#define GetWindowLong GetWindowLongA
#define SetWindowLong SetWindowLongA
#endif
typedef struct tagMINMAXINFO { POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; } MINMAXINFO, *LPMINMAXINFO;
typedef struct _STARTUPINFOA { DWORD cb; LPSTR lpReserved, lpDesktop, lpTitle;
    DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    WORD wShowWindow, cbReserved2; LPBYTE lpReserved2; HANDLE hStdInput, hStdOutput, hStdError; } STARTUPINFOA, STARTUPINFO, *LPSTARTUPINFOA;
typedef struct _PROCESS_INFORMATION { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; } PROCESS_INFORMATION, *LPPROCESS_INFORMATION;
typedef struct tagPROCESSENTRY32 { DWORD dwSize, cntUsage, th32ProcessID; ULONG_PTR th32DefaultHeapID;
    DWORD th32ModuleID, cntThreads, th32ParentProcessID; LONG pcPriClassBase; DWORD dwFlags; CHAR szExeFile[MAX_PATH]; }
    PROCESSENTRY32, *LPPROCESSENTRY32;
typedef struct tagMODULEENTRY32 { DWORD dwSize, th32ModuleID, th32ProcessID, GlblcntUsage, ProccntUsage;
    LPBYTE modBaseAddr; DWORD modBaseSize; HMODULE hModule; CHAR szModule[256], szExePath[MAX_PATH]; }
    MODULEENTRY32, *LPMODULEENTRY32;
typedef struct { DWORD TotalByteLength, DefinitionLength, HeaderLength, ObjectNameTitleIndex, ObjectNameTitle, ObjectHelpTitleIndex, ObjectHelpTitle, DetailLevel; LONG NumCounters, DefaultCounter, NumInstances, CodePage; LARGE_INTEGER PerfTime, PerfFreq; } PERF_OBJECT_TYPE, *PPERF_OBJECT_TYPE;
typedef struct { WCHAR Signature[4]; DWORD LittleEndian, Version, Revision, TotalByteLength, HeaderLength, NumObjectTypes; LONG DefaultObject; } PERF_DATA_BLOCK, *PPERF_DATA_BLOCK;
typedef struct { DWORD ByteLength, ParentObjectTitleIndex, ParentObjectInstance; LONG UniqueID; DWORD NameOffset, NameLength; } PERF_INSTANCE_DEFINITION, *PPERF_INSTANCE_DEFINITION;
typedef struct { DWORD ByteLength; } PERF_COUNTER_DEFINITION, *PPERF_COUNTER_DEFINITION;
typedef struct { DWORD ByteLength; } PERF_COUNTER_BLOCK, *PPERF_COUNTER_BLOCK;
typedef struct tagWNDCLASSA { UINT style; void *lpfnWndProc; int cbClsExtra, cbWndExtra;
    HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; LPCSTR lpszMenuName, lpszClassName; }
    WNDCLASSA, WNDCLASS, WNDCLASSW;

#define ERROR_CRC 23
#define ERROR_NOT_SUPPORTED 50
#define ERROR_INVALID_PARAMETER 87
#define ERROR_INVALID_DATA 13
#define ERROR_HANDLE_EOF 38
#define ERROR_WRITE_FAULT 29
#define _istdigit isdigit
#define _istalpha isalpha
#define _istspace isspace
#define _stscanf  sscanf
#define TH32CS_SNAPPROCESS 0x00000002
#define TH32CS_SNAPMODULE 0x00000008
#define CT_CTYPE1 1
#define CT_CTYPE2 2
#define CT_CTYPE3 4
#define C3_NONSPACING 0x0001
#define C3_ALPHA 0x8000
#define CRYPT_EXPORTABLE 0x00000001
#define CALG_RC4 0x6801
#define CSTR_LESS_THAN 1
#define CSTR_EQUAL 2
#define CSTR_GREATER_THAN 3
#define MAKELCID(l,s) ((DWORD)(((DWORD)((WORD)(s))<<16)|((DWORD)((WORD)(l)))))
#define LANG_CHINESE 0x04
#ifdef __cplusplus
inline HANDLE CreateToolhelp32Snapshot(DWORD, DWORD) { return INVALID_HANDLE_VALUE; }
inline BOOL Process32First(HANDLE, LPPROCESSENTRY32) { return FALSE; }
inline BOOL Process32Next(HANDLE, LPPROCESSENTRY32) { return FALSE; }
inline BOOL Module32First(HANDLE, LPMODULEENTRY32) { return FALSE; }
inline BOOL Module32Next(HANDLE, LPMODULEENTRY32) { return FALSE; }
inline HRSRC FindResourceA(HMODULE, LPCSTR, LPCSTR) { return NULL; }
#define FindResource FindResourceA
inline UINT WinExec(LPCSTR, UINT) { return 0; }
inline int  CompareStringA(DWORD, DWORD, LPCSTR a, int, LPCSTR b, int) { int r = strcasecmp(a, b); return r < 0 ? CSTR_LESS_THAN : r > 0 ? CSTR_GREATER_THAN : CSTR_EQUAL; }
#define CompareString CompareStringA
inline BOOL GetStringTypeExA(DWORD, DWORD, LPCSTR, int, LPWORD) { return FALSE; }
#define GetStringTypeEx GetStringTypeExA
inline void _strdate(char *b) { time_t t = time(NULL); struct tm o; localtime_r(&t, &o); strftime(b, 9, "%m/%d/%y", &o); }
inline void _strtime(char *b) { time_t t = time(NULL); struct tm o; localtime_r(&t, &o); strftime(b, 9, "%H:%M:%S", &o); }
#endif
#define MMIO_ALLOCBUF 0x00010000
#define WM_CONTEXTMENU 0x007B
#define WM_ENTERMENULOOP 0x0211
#define WM_EXITMENULOOP 0x0212
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE 0x0232
#define WM_POWERBROADCAST 0x0218
#define WHITE_BRUSH 0
#define BLACK_BRUSH 4
#define VIETNAMESE_CHARSET 163
#define TIME_PERIODIC 1
#define TIME_ONESHOT 0
#define TIME_CALLBACK_EVENT_SET 0x0010
#define TIME_CALLBACK_FUNCTION 0x0000
#define HKEY_PERFORMANCE_DATA ((HKEY)0x80000004)
#define REG_BINARY 3
#define REG_MULTI_SZ 7
typedef BOOL (*WNDENUMPROC)(HWND, LPARAM);
#ifdef __cplusplus
inline BOOL SetFileAttributesA(LPCSTR, DWORD) { return TRUE; }
#define SetFileAttributes SetFileAttributesA
inline DWORD GetShortPathNameA(LPCSTR l, LPSTR s, DWORD n) { if (s && l) { strncpy(s, l, n); s[n-1]=0; return (DWORD)strlen(s); } return 0; }
#define GetShortPathName GetShortPathNameA
inline DWORD GetFullPathNameA(LPCSTR l, DWORD n, LPSTR b, LPSTR *p) { if (p) *p = NULL; if (b && l) { strncpy(b, l, n); b[n-1]=0; return (DWORD)strlen(b); } return 0; }
#define GetFullPathName GetFullPathNameA
inline BOOL SetFileTime(HANDLE, const FILETIME *, const FILETIME *, const FILETIME *) { return TRUE; }
inline BOOL GetFileTime(HANDLE, LPFILETIME, LPFILETIME, LPFILETIME) { return FALSE; }
inline DWORD SizeofResource(HMODULE, HRSRC) { return 0; }
inline HGLOBAL LoadResource(HMODULE, HRSRC) { return NULL; }
inline LPVOID LockResource(HGLOBAL) { return NULL; }
inline BOOL GetStringTypeW(DWORD, LPCWSTR, int, LPWORD) { return FALSE; }
inline BOOL CryptDeriveKey(ULONG_PTR, DWORD, ULONG_PTR, DWORD, ULONG_PTR *) { return FALSE; }
inline BOOL EnumWindows(WNDENUMPROC, LPARAM) { return FALSE; }
#endif
#define SC_SIZE 0xF000
#define SC_MOVE 0xF010
#define SC_MINIMIZE 0xF020
#define SC_MAXIMIZE 0xF030
#define SC_KEYMENU 0xF100
#define SC_MONITORPOWER 0xF170
#define SC_SCREENSAVE 0xF140
#define SIZE_RESTORED 0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2
#define SIZE_MAXSHOW 3
#define SIZE_MAXHIDE 4
#define QS_POSTMESSAGE 0x0008
#define QS_ALLINPUT 0x04FF
#define SUBLANG_NEUTRAL 0x00
#define SHIFTJIS_CHARSET 128
#define GB2312_CHARSET 134
#define CHINESEBIG5_CHARSET 136
#define PROCESS_VM_READ 0x0010
#define PROCESS_QUERY_INFORMATION 0x0400
#ifdef __cplusplus
inline void PostQuitMessage(int) {}
inline ATOM RegisterClassA(const WNDCLASSA *) { return 0; }
#define RegisterClass RegisterClassA
inline BOOL AdjustWindowRect(LPRECT, DWORD, BOOL) { return TRUE; }
inline BOOL ReadProcessMemory(HANDLE, LPCVOID, LPVOID, SIZE_T, SIZE_T *) { return FALSE; }
inline HANDLE OpenProcess(DWORD, BOOL, DWORD) { return NULL; }
inline BOOL PathFileExistsA(LPCSTR p) { if (!p) return FALSE; FILE *f = fopen(p, "rb"); if (f) { fclose(f); return TRUE; } return FALSE; }
#define PathFileExists PathFileExistsA
inline HICON LoadIconA(HINSTANCE, LPCSTR) { return NULL; }
#define LoadIcon LoadIconA
inline BOOL GetIconInfo(HICON, void *) { return FALSE; }
inline BOOL DestroyIcon(HICON) { return TRUE; }
#endif
typedef struct { BOOL fIcon; DWORD xHotspot, yHotspot; HBITMAP hbmMask, hbmColor; } ICONINFO;
#define HWND_NOTOPMOST ((HWND)(intptr_t)-2)
#define HWND_BOTTOM ((HWND)1)
#ifdef __cplusplus
inline int GetDIBits(HDC, HBITMAP, UINT, UINT, LPVOID, LPBITMAPINFO, UINT) { return 0; }
inline int SetDIBits(HDC, HBITMAP, UINT, UINT, const void *, const BITMAPINFO *, UINT) { return 0; }
#endif
#define IMAGE_BITMAP 0
#define IMAGE_ICON 1
#define LR_LOADFROMFILE 0x0010
#define LR_CREATEDIBSECTION 0x2000
#define CB_GETCURSEL 0x0147
#define CB_GETITEMDATA 0x0150
#define CB_ADDSTRING 0x0143
#define CB_SETITEMDATA 0x0151
#define CB_SETCURSEL 0x014E
#define CB_RESETCONTENT 0x014B
#ifdef __cplusplus
inline HANDLE LoadImageA(HINSTANCE, LPCSTR, UINT, int, int, UINT) { return NULL; }
#define LoadImage LoadImageA
inline HWND GetDlgItem(HWND, int) { return NULL; }
inline BOOL DosDateTimeToFileTime(WORD, WORD, LPFILETIME ft) { if (ft) { ft->dwLowDateTime = ft->dwHighDateTime = 0; } return TRUE; }
inline BOOL LocalFileTimeToFileTime(const FILETIME *a, LPFILETIME b) { if (a && b) *b = *a; return TRUE; }
inline BOOL CryptEncrypt(ULONG_PTR, ULONG_PTR, BOOL, DWORD, BYTE *, DWORD *, DWORD) { return FALSE; }
inline BOOL CryptDecrypt(ULONG_PTR, ULONG_PTR, BOOL, DWORD, BYTE *, DWORD *) { return FALSE; }
#endif
#define HANGUL_CHARSET 129
#define GMEM_DDESHARE 0x2000
#define KEY_QUERY_VALUE 0x0001
#define KEY_SET_VALUE 0x0002
#define MAX_COMPUTERNAME_LENGTH 15
#ifdef __cplusplus
inline BOOL IsDBCSLeadByteEx(UINT cp, BYTE b) {
    switch (cp) {
        case 932: case 936: case 949: case 950:
            return b >= 0x81 && b <= 0xFE;
        default:
            return FALSE;   // 874 (Thai), 1258, 1252 are single-byte
    }
}
inline BOOL IsDBCSLeadByte(BYTE b) { return b >= 0x81 && b <= 0xFE; }
inline BOOL GetTextExtentPoint32W(HDC, LPCWSTR, int, LPSIZE) { return TRUE; }
inline BOOL GetComputerNameA(LPSTR b, LPDWORD n) { if (b && n && *n) { strncpy(b, "android", *n); b[*n-1]=0; *n=(DWORD)strlen(b); } return TRUE; }
#define GetComputerName GetComputerNameA
#endif
#define SMTO_NORMAL 0x0000
#define SMTO_BLOCK 0x0001
#define SMTO_ABORTIFHUNG 0x0002
#define EM_GETSEL 0x00B0
#define EM_SETSEL 0x00B1
#define EM_SCROLL 0x00B5
#define EM_LINESCROLL 0x00B6
#define EM_SCROLLCARET 0x00B7
#define EM_REPLACESEL 0x00C2
#define EM_LIMITTEXT 0x00C5
#define EM_GETLINECOUNT 0x00BA
#define EM_SETREADONLY 0x00CF
#define SB_LINEUP 0
#define SB_LINEDOWN 1
#define SB_PAGEUP 2
#define SB_PAGEDOWN 3
#define SB_BOTTOM 7
#define RPC_S_OK 0
#define RPC_S_OUT_OF_MEMORY 14
#define INTERNET_FLAG_DONT_CACHE 0x04000000
#define INTERNET_FLAG_RELOAD 0x80000000
#define HTTP_STATUS_OK 200
#define _ftprintf fprintf
#define _tcsftime strftime
#define _fputts   fputs
#define _fgetts   fgets
#ifdef __cplusplus
inline LRESULT SendMessageTimeoutA(HWND, UINT, WPARAM, LPARAM, UINT, UINT, DWORD_PTR *) { return 0; }
#define SendMessageTimeout SendMessageTimeoutA
inline BOOL SendMessageCallbackA(HWND, UINT, WPARAM, LPARAM, void *, ULONG_PTR) { return FALSE; }
#define SendMessageCallback SendMessageCallbackA
inline LONG UuidCreate(GUID *g) { if (g) memset(g, 0, sizeof(*g)); return 0; }
inline LONG UuidToStringA(const GUID *, unsigned char **s) { if (s) *s = NULL; return 0; }
#define UuidToString UuidToStringA
inline LONG UuidFromStringA(unsigned char *, GUID *g) { if (g) memset(g, 0, sizeof(*g)); return 0; }
#define UuidFromString UuidFromStringA
inline LONG RpcStringFreeA(unsigned char **) { return 0; }
#define RpcStringFree RpcStringFreeA
#endif
typedef unsigned char byte;
typedef struct _MEMORYSTATUSEX { DWORD dwLength, dwMemoryLoad; DWORDLONG ullTotalPhys,
    ullAvailPhys, ullTotalPageFile, ullAvailPageFile, ullTotalVirtual, ullAvailVirtual,
    ullAvailExtendedVirtual; } MEMORYSTATUSEX, *LPMEMORYSTATUSEX;
#ifdef __cplusplus
inline BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX *m) { if (m) { memset(m, 0, sizeof(*m)); m->dwLength = sizeof(*m); } return FALSE; }
#endif
#define PROCESSOR_ARCHITECTURE_INTEL 0
#define PROCESSOR_ARCHITECTURE_ARM 5
#define PROCESSOR_ARCHITECTURE_IA64 6
#define PROCESSOR_ARCHITECTURE_AMD64 9
#define PROCESSOR_ARCHITECTURE_ARM64 12
#define VER_PLATFORM_WIN32_WINDOWS 1
#define HIGH_PRIORITY_CLASS 0x00000080
#define NORMAL_PRIORITY_CLASS 0x00000020
#define EWX_LOGOFF 0
#define EWX_SHUTDOWN 1
#define EWX_REBOOT 2
#define EWX_FORCE 4
#define EWX_POWEROFF 8
#define NTE_BAD_KEYSET ((HRESULT)0x80090016L)
#define CRYPT_NEWKEYSET 0x00000008
#define TRY          try
#define CATCH(c,e)   catch (c *e)
#define AND_CATCH(c,e) catch (c *e)
#define END_CATCH
#define CATCH_ALL(e) catch (CException *e)
#define AND_CATCH_ALL(e) catch (CException *e)
#define END_CATCH_ALL
#ifdef __cplusplus
inline DWORD GetPriorityClass(HANDLE) { return NORMAL_PRIORITY_CLASS; }
inline BOOL SetPriorityClass(HANDLE, DWORD) { return TRUE; }
inline BOOL GetVolumeInformationA(LPCSTR, LPSTR, DWORD, LPDWORD sn, LPDWORD, LPDWORD, LPSTR, DWORD) { if (sn) *sn = 0; return FALSE; }
#define GetVolumeInformation GetVolumeInformationA
inline BOOL ExitWindowsEx(UINT, DWORD) { return FALSE; }
#endif
#define OFN_HIDEREADONLY 0x00000004
#define OFN_OVERWRITEPROMPT 0x00000002
#define OFN_FILEMUSTEXIST 0x00001000
#define OFN_PATHMUSTEXIST 0x00000800
#define WS_EX_APPWINDOW 0x00040000
#define TH32CS_SNAPALL 0x0000000F
#define _ttoi64(s) strtoll((s), NULL, 10)
typedef struct { DWORD cb, PageFaultCount; SIZE_T PeakWorkingSetSize, WorkingSetSize,
    QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage,
    QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage; } PROCESS_MEMORY_COUNTERS;
#ifdef __cplusplus
inline BOOL GetProcessMemoryInfo(HANDLE, PROCESS_MEMORY_COUNTERS *c, DWORD) { if (c) memset(c, 0, sizeof(*c)); return FALSE; }
inline DWORD GetModuleFileNameExA(HANDLE, HMODULE, LPSTR b, DWORD n) { if (b && n) b[0] = 0; return 0; }
#define GetModuleFileNameEx GetModuleFileNameExA
inline BOOL MoveWindow(HWND, int, int, int, int, BOOL = TRUE) { return TRUE; }
#endif
#ifdef __cplusplus
inline BOOL AdjustWindowRectEx(LPRECT, DWORD, BOOL, DWORD) { return TRUE; }
#endif
#define MAXWORD 0xFFFF
#define MAXDWORD 0xFFFFFFFF
#define KEYEVENTF_EXTENDEDKEY 0x0001
#define KEYEVENTF_KEYUP 0x0002
#ifdef __cplusplus
inline HINSTANCE ShellExecuteA(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, int) { return NULL; }
#define ShellExecute ShellExecuteA
inline void keybd_event(BYTE, BYTE, DWORD, ULONG_PTR) {}
#endif
#define _strtoi64(s,e,b)  strtoll((s),(e),(b))
#define _strtoui64(s,e,b) strtoull((s),(e),(b))
#define HTCLIENT 1
#define HTCAPTION 2
#define GCL_HCURSOR (-12)
#define GCLP_HCURSOR (-12)
#ifdef __cplusplus
inline HMENU GetMenu(HWND) { return NULL; }
inline BOOL DestroyMenu(HMENU) { return TRUE; }
inline BOOL DestroyWindow(HWND) { return TRUE; }
inline LRESULT DefWindowProcA(HWND, UINT, WPARAM, LPARAM) { return 0; }
#define DefWindowProc DefWindowProcA
inline int GetObjectA(HGDIOBJ, int, LPVOID) { return 0; }
#define GetObject GetObjectA
inline BOOL GetMessageA(LPMSG, HWND, UINT, UINT) { return FALSE; }
#define GetMessage GetMessageA
inline HWND CreateWindowExA(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) { return NULL; }
#define CreateWindowEx CreateWindowExA
#define CreateWindowA(cls,name,style,x,y,w,h,parent,menu,inst,param) CreateWindowExA(0,(cls),(name),(style),(x),(y),(w),(h),(parent),(menu),(inst),(param))
#define CreateWindow CreateWindowA
inline LONG_PTR SetClassLongPtrA(HWND, int, LONG_PTR) { return 0; }
#define SetClassLongPtr SetClassLongPtrA
#define SetClassLong SetClassLongPtrA
inline LONG_PTR GetClassLongPtrA(HWND, int) { return 0; }
#define GetClassLongPtr GetClassLongPtrA
#define GetClassLong GetClassLongPtrA
#endif
#define QS_ALLEVENTS 0x04BF
#ifdef __cplusplus
inline DWORD MsgWaitForMultipleObjects(DWORD n, const HANDLE *h, BOOL, DWORD ms, DWORD) {
    if (!n || !h) { Sleep(ms == INFINITE ? 1 : ms); return WAIT_TIMEOUT; }
    return WaitForSingleObject(h[0], ms);
}
#endif

// ---------------------------------------------------- path resolution (Android)
// The client's paths are Windows-shaped ("\Data\GUI\") and rely on a
// case-insensitive filesystem. RanPath_Resolve fixes separators and case; every
// file entry point in the shim goes through it, and fopen is redirected here so
// engine code that opens files directly is covered too.
#ifdef __cplusplus
extern "C" {
#endif
const char *RanPath_Resolve(const char *path);
// Creates a directory (and its parents) from a Windows-style path.
int RanPath_MakeDir(const char *path);
void        RanPath_LogStats(void);
FILE       *ran_fopen(const char *path, const char *mode);
#ifdef __cplusplus
}
#endif
#define fopen(p, m) ran_fopen((p), (m))
#ifdef __cplusplus
extern "C" void RanLog_IniProgress(const char *file, unsigned lines);
#endif
