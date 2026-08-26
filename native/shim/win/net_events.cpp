// Winsock event notification, which the client's whole network path is built on.
//
// CNetClient connects like this:
//
//   WSAEventSelect(s, ev, FD_CONNECT); connect(...)  -> expects WSAEWOULDBLOCK
//   WSAWaitForMultipleEvents(1, &ev, ..., 10000, ...)
//   WSAEnumNetworkEvents(s, ev, &events)             -> expects FD_CONNECT, error 0
//
// and then its network thread waits on THREE handles at once — kill, work, and
// the socket — with WaitForMultipleObjects, dispatching FD_READ / FD_WRITE /
// FD_CLOSE. So three things have to be real: the error code a non-blocking
// connect reports, the socket-to-event binding, and a wait that can watch more
// than one handle.
//
// The events are level-polled with poll(2) rather than emulated with a thread.
// FD_WRITE is the exception: Windows raises it once when the socket becomes
// writable and again only after a send fails, so it is armed rather than
// reported continuously — otherwise the network thread spins on it.

#include "windows.h"
#include <winsock2.h>

#include <poll.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <android/log.h>
#include <map>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "RanNet", __VA_ARGS__)

namespace {

struct Binding {
    WSAEVENT event;
    long     mask;
    bool     writeArmed;
    bool     connectPending;
};

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
std::map<SOCKET, Binding> g_bindings;

struct Guard {
    Guard() { pthread_mutex_lock(&g_lock); }
    ~Guard() { pthread_mutex_unlock(&g_lock); }
};

// Which of the caller's requested events are ready right now.
long readyEvents(SOCKET s, Binding &b) {
    struct pollfd pfd;
    pfd.fd = (int)s;
    pfd.events = 0;
    if (b.mask & (FD_READ | FD_CLOSE | FD_ACCEPT)) pfd.events |= POLLIN;
    if (b.mask & (FD_WRITE | FD_CONNECT))          pfd.events |= POLLOUT;
    pfd.revents = 0;

    if (poll(&pfd, 1, 0) <= 0) return 0;

    long out = 0;
    if ((b.mask & FD_CONNECT) && b.connectPending && (pfd.revents & (POLLOUT | POLLERR | POLLHUP)))
        out |= FD_CONNECT;
    if ((b.mask & FD_READ) && (pfd.revents & POLLIN))
        out |= FD_READ;
    if ((b.mask & FD_WRITE) && b.writeArmed && (pfd.revents & POLLOUT))
        out |= FD_WRITE;
    if ((b.mask & FD_CLOSE) && (pfd.revents & (POLLHUP | POLLERR)))
        out |= FD_CLOSE;
    return out;
}

// The event handle bound to a socket, if any.
SOCKET socketForEvent(WSAEVENT ev) {
    for (std::map<SOCKET, Binding>::iterator it = g_bindings.begin(); it != g_bindings.end(); ++it)
        if (it->second.event == ev) return it->first;
    return (SOCKET)-1;
}

} // namespace

extern "C" {

int WSAEventSelect(SOCKET s, WSAEVENT hEventObject, long lNetworkEvents) {
    Guard g;
    Binding b;
    b.event = hEventObject;
    b.mask = lNetworkEvents;
    b.writeArmed = (lNetworkEvents & FD_WRITE) != 0;
    b.connectPending = (lNetworkEvents & FD_CONNECT) != 0;
    g_bindings[s] = b;
    // Windows puts the socket into non-blocking mode as a side effect.
    ULONG nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    return 0;
}

int WSAEnumNetworkEvents(SOCKET s, WSAEVENT hEventObject, LPWSANETWORKEVENTS lpNetworkEvents) {
    if (!lpNetworkEvents) return SOCKET_ERROR;
    memset(lpNetworkEvents, 0, sizeof(*lpNetworkEvents));

    Guard g;
    std::map<SOCKET, Binding>::iterator it = g_bindings.find(s);
    if (it == g_bindings.end()) return 0;

    Binding &b = it->second;
    long ready = readyEvents(s, b);
    lpNetworkEvents->lNetworkEvents = ready;

    if (ready & FD_CONNECT) {
        // The result of a non-blocking connect lives in SO_ERROR.
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt((int)s, SOL_SOCKET, SO_ERROR, &err, &len);
        lpNetworkEvents->iErrorCode[FD_CONNECT_BIT] = err ? WSAECONNREFUSED : 0;
        b.connectPending = false;
    }
    if (ready & FD_WRITE) b.writeArmed = false;

    if (hEventObject) WSAResetEvent(hEventObject);
    return 0;
}

// Re-arm FD_WRITE after a send could not take everything, matching Windows.
void RanNet_SendWouldBlock(SOCKET s) {
    Guard g;
    std::map<SOCKET, Binding>::iterator it = g_bindings.find(s);
    if (it != g_bindings.end()) it->second.writeArmed = true;
}

void RanNet_ForgetSocket(SOCKET s) {
    Guard g;
    g_bindings.erase(s);
}

// Wait until any of the handles is signalled. Socket-bound events are polled;
// ordinary events use their own state. Returning the INDEX matters: the network
// thread switches on WAIT_OBJECT_0 + n to decide what happened.
DWORD RanNet_WaitForMultiple(DWORD count, const HANDLE *handles, BOOL waitAll, DWORD ms) {
    if (!count || !handles) { Sleep(ms == INFINITE ? 1 : ms); return WAIT_TIMEOUT; }
    if (waitAll) {
        // Not used by the client; waiting on the first keeps it honest rather
        // than pretending all were signalled.
        return WaitForSingleObject(handles[0], ms);
    }

    const DWORD start = GetTickCount();
    for (;;) {
        for (DWORD i = 0; i < count; ++i) {
            SOCKET s;
            {
                Guard g;
                s = socketForEvent(handles[i]);
                if (s != (SOCKET)-1) {
                    std::map<SOCKET, Binding>::iterator it = g_bindings.find(s);
                    if (it != g_bindings.end() && readyEvents(s, it->second)) {
                        WSASetEvent(handles[i]);
                        return WAIT_OBJECT_0 + i;
                    }
                    continue;
                }
            }
            if (WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0)
                return WAIT_OBJECT_0 + i;
        }

        if (ms != INFINITE && (GetTickCount() - start) >= ms) return WAIT_TIMEOUT;
        Sleep(1);
    }
}

} // extern "C"
