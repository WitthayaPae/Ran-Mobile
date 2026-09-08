// Winsock -> BSD sockets shim.
#pragma once
#ifndef RAN_WINSOCK2_SHIM_H
#define RAN_WINSOCK2_SHIM_H

#include "windows.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

//  Darwin defines the byte-order calls as MACROS, not functions:
//
//    #define htons(x) __DARWIN_OSSwapInt16(x)
//
//  and the client writes ::htons(nPort) - s_NetClient.cpp:466 among others.
//  A qualified name cannot be a macro invocation target that way, so the
//  expansion is a syntax error: "expected unqualified-id". bionic declares
//  them as real functions, which is why Android never saw it.
//
//  Undefine and re-declare as inline functions in the global namespace, which
//  is what :: is asking for and what the Windows headers actually provide.
#if defined(__APPLE__)
#  undef htons
#  undef htonl
#  undef ntohs
#  undef ntohl
inline uint16_t htons ( uint16_t v ) { return __builtin_bswap16 ( v ); }
inline uint32_t htonl ( uint32_t v ) { return __builtin_bswap32 ( v ); }
inline uint16_t ntohs ( uint16_t v ) { return __builtin_bswap16 ( v ); }
inline uint32_t ntohl ( uint32_t v ) { return __builtin_bswap32 ( v ); }
#endif

typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
typedef struct sockaddr      SOCKADDR, *PSOCKADDR, *LPSOCKADDR;
typedef struct sockaddr_in   SOCKADDR_IN, *PSOCKADDR_IN, *LPSOCKADDR_IN;
typedef struct in_addr       IN_ADDR;
typedef struct hostent       HOSTENT, *LPHOSTENT;
typedef struct linger        LINGER;
typedef struct timeval       TIMEVAL;
typedef fd_set               FD_SET_T;

typedef struct WSAData {
    WORD wVersion, wHighVersion;
    char szDescription[257], szSystemStatus[129];
    unsigned short iMaxSockets, iMaxUdpDg;
    char *lpVendorInfo;
} WSADATA, *LPWSADATA;
typedef struct _WSABUF { ULONG len; char *buf; } WSABUF, *LPWSABUF;
typedef OVERLAPPED WSAOVERLAPPED, *LPWSAOVERLAPPED;

#define MAKEWORD_WS(a,b) MAKEWORD(a,b)
#define SD_RECEIVE SHUT_RD
#define SD_SEND    SHUT_WR
#define SD_BOTH    SHUT_RDWR
#ifndef INADDR_NONE
#define INADDR_NONE 0xFFFFFFFFu
#endif

#ifdef __cplusplus
extern "C" {
#endif
int  WSAStartup(WORD ver, LPWSADATA d);
int  WSACleanup(void);
int  WSAGetLastError(void);
void WSASetLastError(int e);
int  closesocket(SOCKET s);
int  ioctlsocket(SOCKET s, long cmd, ULONG *arg);
#define FIONBIO_WS 0x8004667E
int  WSAIoctl(SOCKET, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPVOID, LPVOID);
#ifdef __cplusplus
}
#endif

#endif // RAN_WINSOCK2_SHIM_H
#define FD_READ 0x01
#define FD_WRITE 0x02
#define FD_OOB 0x04
#define FD_ACCEPT 0x08
#define FD_CONNECT 0x10
#define FD_CLOSE 0x20
#define FD_MAX_EVENTS 10
#define WSA_INVALID_HANDLE 6
#define WSA_NOT_ENOUGH_MEMORY 8
#define WSA_INVALID_PARAMETER 87
typedef HANDLE WSAEVENT;
typedef struct _WSANETWORKEVENTS { long lNetworkEvents; int iErrorCode[FD_MAX_EVENTS]; } WSANETWORKEVENTS, *LPWSANETWORKEVENTS;

#define WSASYSNOTREADY 10091
#define WSAVERNOTSUPPORTED 10092
#define WSANOTINITIALISED 10093
#define WSA_INVALID_EVENT ((WSAEVENT)0)
#define WSA_WAIT_TIMEOUT WAIT_TIMEOUT
#define WSA_WAIT_FAILED WAIT_FAILED
#define WSA_WAIT_EVENT_0 0
#ifdef __cplusplus
inline WSAEVENT WSACreateEvent() { return CreateEventA(NULL, TRUE, FALSE, NULL); }
inline BOOL WSACloseEvent(WSAEVENT h) { return CloseHandle(h); }
inline BOOL WSAResetEvent(WSAEVENT h) { return ResetEvent(h); }
inline BOOL WSASetEvent(WSAEVENT h) { return SetEvent(h); }
// Real implementations live in net_events.cpp: the client's network thread waits
// on three handles at once and dispatches FD_READ/FD_WRITE/FD_CLOSE, so these
// cannot be no-ops that only look at the first handle.
extern "C" int  WSAEventSelect(SOCKET s, WSAEVENT ev, long mask);
extern "C" int  WSAEnumNetworkEvents(SOCKET s, WSAEVENT ev, LPWSANETWORKEVENTS out);
extern "C" DWORD RanNet_WaitForMultiple(DWORD count, const HANDLE *handles, BOOL waitAll, DWORD ms);
extern "C" void RanNet_SendWouldBlock(SOCKET s);
extern "C" void RanNet_ForgetSocket(SOCKET s);

inline DWORD WSAWaitForMultipleEvents(DWORD n, const WSAEVENT *h, BOOL waitAll, DWORD ms, BOOL) {
    return RanNet_WaitForMultiple(n, (const HANDLE *)h, waitAll, ms);
}
inline DWORD WaitForMultipleObjects(DWORD n, const HANDLE *h, BOOL waitAll, DWORD ms) {
    return RanNet_WaitForMultiple(n, h, waitAll, ms);
}
inline DWORD SleepEx(DWORD ms, BOOL) { Sleep(ms); return 0; }
#endif
#define WSAEDISCON 10101
#define WSAENOMORE 10102
#define WSAECANCELLED 10103
#define WSAEINVALIDPROCTABLE 10104
#define WSAEINVALIDPROVIDER 10105
#define WSAEPROVIDERFAILEDINIT 10106
#define WSASYSCALLFAILURE 10107
#define FD_CONNECT_BIT 4
#define FD_READ_BIT 0
#define FD_WRITE_BIT 1
#define FD_CLOSE_BIT 5
#ifndef FIONBIO
#define FIONBIO 0x5421
#endif

// Real Windows WSA error numbers. The shim translates errno to these in
// WSAGetLastError so the client's switch/compare logic keeps working.
#define WSABASEERR        10000
#define WSAEINTR          10004
#define WSAEBADF          10009
#define WSAEACCES         10013
#define WSAEFAULT         10014
#define WSAEINVAL         10022
#define WSAEMFILE         10024
#define WSAEWOULDBLOCK    10035
#define WSAEINPROGRESS    10036
#define WSAEALREADY       10037
#define WSAENOTSOCK       10038
#define WSAEDESTADDRREQ   10039
#define WSAEMSGSIZE       10040
#define WSAEPROTOTYPE     10041
#define WSAENOPROTOOPT    10042
#define WSAEPROTONOSUPPORT 10043
#define WSAESOCKTNOSUPPORT 10044
#define WSAEOPNOTSUPP     10045
#define WSAEPFNOSUPPORT   10046
#define WSAEAFNOSUPPORT   10047
#define WSAEADDRINUSE     10048
#define WSAEADDRNOTAVAIL  10049
#define WSAENETDOWN       10050
#define WSAENETUNREACH    10051
#define WSAENETRESET      10052
#define WSAECONNABORTED   10053
#define WSAECONNRESET     10054
#define WSAENOBUFS        10055
#define WSAEISCONN        10056
#define WSAENOTCONN       10057
#define WSAESHUTDOWN      10058
#define WSAETOOMANYREFS   10059
#define WSAETIMEDOUT      10060
#define WSAECONNREFUSED   10061
#define WSAELOOP          10062
#define WSAENAMETOOLONG   10063
#define WSAEHOSTDOWN      10064
#define WSAEHOSTUNREACH   10065
#define WSAENOTEMPTY      10066
#define WSAEPROCLIM       10067
#define WSAEUSERS         10068
#define WSAEDQUOT         10069
#define WSAESTALE         10070
#define WSAEREMOTE        10071
#define WSAEREFUSED       10112
#define WSAHOST_NOT_FOUND 11001
#define WSATRY_AGAIN      11002
#define WSANO_RECOVERY    11003
#define WSANO_DATA        11004
#define WSASERVICE_NOT_FOUND 10108
#define WSATYPE_NOT_FOUND 10109
#define WSA_E_NO_MORE     10110
#define WSA_E_CANCELLED   10111
#define WSA_IO_PENDING    997
