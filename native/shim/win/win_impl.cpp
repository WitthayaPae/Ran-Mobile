// Implementation of the Win32 subset the RAN client uses, on Android/POSIX.
#include "windows.h"
#include "../platform/ran_plat.h"
#include "mfc_compat.h"
#include "winsock2.h"
#include "io.h"
#include "wincrypt.h"
#include "imm.h"
#include <stdlib.h>

#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>          //  hw.memsize, in place of _SC_PHYS_PAGES
#endif
#include <ctype.h>

#include <string>
#include <vector>
#include <map>

#define LOGI(...) RanPlat_Log(RANLOG_INFO, "RAN", __VA_ARGS__)

static thread_local DWORD g_lastError = 0;

// ------------------------------------------------------------------ time / misc
static uint64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
}
extern "C" {

DWORD GetTickCount(void) { return (DWORD)nowMs(); }

//  Win32 clock(): wall milliseconds since the process started, to pair with
//  the CLOCKS_PER_SEC == 1000 the shim header declares. The client subtracts
//  two of these and prints the result as a ping in milliseconds.
clock_t RanWin_clock(void) { return (clock_t)nowMs(); }
DWORD timeGetTime(void)  { return (DWORD)nowMs(); }
DWORD timeBeginPeriod(UINT) { return 0; }
DWORD timeEndPeriod(UINT)   { return 0; }

void Sleep(DWORD ms) {
    if (ms == 0) { sched_yield(); return; }
    struct timespec ts = { (time_t)(ms / 1000), (long)((ms % 1000) * 1000000L) };
    nanosleep(&ts, NULL);
}
BOOL QueryPerformanceCounter(LARGE_INTEGER *p) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    p->QuadPart = (LONGLONG)ts.tv_sec * 1000000000ll + ts.tv_nsec;
    return TRUE;
}
BOOL QueryPerformanceFrequency(LARGE_INTEGER *p) { p->QuadPart = 1000000000ll; return TRUE; }
DWORD GetCurrentThreadId(void)  { return (DWORD)(uintptr_t)pthread_self(); }
DWORD GetCurrentProcessId(void) { return (DWORD)getpid(); }
HANDLE GetCurrentProcess(void)  { return (HANDLE)(intptr_t)-1; }
DWORD GetLastError(void)        { return g_lastError; }
void  SetLastError(DWORD e)     { g_lastError = e; }
void  OutputDebugStringA(LPCSTR s) { if (s) LOGI("%s", s); }

char *strupr_shim(char *s) { for (char *p = s; p && *p; ++p) *p = (char)toupper((unsigned char)*p); return s; }
char *strlwr_shim(char *s) { for (char *p = s; p && *p; ++p) *p = (char)tolower((unsigned char)*p); return s; }

int _splitpath_shim(const char *path, char *drv, char *dir, char *fname, char *ext) {
    if (drv) drv[0] = 0;
    const char *p = path ? path : "";
    const char *slash = strrchr(p, '/');
    const char *bslash = strrchr(p, '\\');
    if (bslash > slash) slash = bslash;
    const char *base = slash ? slash + 1 : p;
    if (dir) { size_t n = (size_t)(base - p); memcpy(dir, p, n); dir[n] = 0; }
    const char *dot = strrchr(base, '.');
    if (fname) { size_t n = dot ? (size_t)(dot - base) : strlen(base); memcpy(fname, base, n); fname[n] = 0; }
    if (ext) { if (dot) strcpy(ext, dot); else ext[0] = 0; }
    return 0;
}
void _makepath_shim(char *path, const char *drv, const char *dir, const char *f, const char *e) {
    path[0] = 0;
    if (dir && *dir) strcat(path, dir);
    if (f && *f) strcat(path, f);
    if (e && *e) strcat(path, e);
}

// -------------------------------------------------------------------- atomics
LONG InterlockedIncrement(LONG volatile *p) { return __sync_add_and_fetch(p, 1); }
LONG InterlockedDecrement(LONG volatile *p) { return __sync_sub_and_fetch(p, 1); }
LONG InterlockedExchange(LONG volatile *p, LONG v) { return __sync_lock_test_and_set(p, v); }
LONG InterlockedExchangeAdd(LONG volatile *p, LONG v) { return __sync_fetch_and_add(p, v); }
LONG InterlockedCompareExchange(LONG volatile *p, LONG ex, LONG cmp) {
    return __sync_val_compare_and_swap(p, cmp, ex);
}

// --------------------------------------------------------- critical sections
void InitializeCriticalSection(LPCRITICAL_SECTION cs) {
    pthread_mutex_t *m = new pthread_mutex_t;
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    cs->impl = m;
}
void InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD) { InitializeCriticalSection(cs); }
void DeleteCriticalSection(LPCRITICAL_SECTION cs) {
    if (!cs->impl) return;
    pthread_mutex_destroy((pthread_mutex_t *)cs->impl);
    delete (pthread_mutex_t *)cs->impl;
    cs->impl = NULL;
}
void EnterCriticalSection(LPCRITICAL_SECTION cs) {
    if (!cs->impl) InitializeCriticalSection(cs);
    pthread_mutex_lock((pthread_mutex_t *)cs->impl);
}
void LeaveCriticalSection(LPCRITICAL_SECTION cs) {
    if (cs->impl) pthread_mutex_unlock((pthread_mutex_t *)cs->impl);
}
BOOL TryEnterCriticalSection(LPCRITICAL_SECTION cs) {
    if (!cs->impl) InitializeCriticalSection(cs);
    return pthread_mutex_trylock((pthread_mutex_t *)cs->impl) == 0;
}

// ---------------------------------------------------------- threads / events
struct RanHandle {
    enum Kind { Thread, Event, Mutex, File, Find } kind;
    pthread_t thread = 0;
    // A pthread_t is valid exactly once: joining OR detaching consumes it.
    // Doing either twice aborts the process on bionic ("pthread_internal_find"),
    // and the client does exactly that — CNetClient::CloseConnect waits on its
    // network thread and then closes the handle.
    bool threadConsumed = false;
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    bool signalled = false, manualReset = false;
    FILE *fp = NULL;
    DIR *dir = NULL;
    std::string pattern, root;
};

struct ThreadStart { DWORD (*fn)(LPVOID); LPVOID arg; unsigned (*fn2)(void *); void (*fn3)(void *); };
static void *threadTrampoline(void *p) {
    ThreadStart *ts = (ThreadStart *)p;
    if (ts->fn)  ts->fn(ts->arg);
    else if (ts->fn2) ts->fn2(ts->arg);
    else if (ts->fn3) ts->fn3(ts->arg);
    delete ts;
    return NULL;
}
HANDLE CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T stack, DWORD (*start)(LPVOID),
                    LPVOID param, DWORD, LPDWORD tid) {
    RanHandle *h = new RanHandle; h->kind = RanHandle::Thread;
    ThreadStart *ts = new ThreadStart{start, param, NULL, NULL};
    pthread_attr_t attr; pthread_attr_init(&attr);
    if (stack) pthread_attr_setstacksize(&attr, stack < 65536 ? 65536 : stack);
    if (pthread_create(&h->thread, &attr, threadTrampoline, ts) != 0) { delete ts; delete h; return NULL; }
    pthread_attr_destroy(&attr);
    if (tid) *tid = (DWORD)(uintptr_t)h->thread;
    return (HANDLE)h;
}
uintptr_t _beginthreadex(void *, unsigned stack, unsigned (*start)(void *), void *arg, unsigned, unsigned *tid) {
    RanHandle *h = new RanHandle; h->kind = RanHandle::Thread;
    ThreadStart *ts = new ThreadStart{NULL, NULL, start, NULL};
    ts->arg = arg;
    pthread_attr_t attr; pthread_attr_init(&attr);
    if (stack) pthread_attr_setstacksize(&attr, stack < 65536 ? 65536 : stack);
    if (pthread_create(&h->thread, &attr, threadTrampoline, ts) != 0) { delete ts; delete h; return 0; }
    pthread_attr_destroy(&attr);
    if (tid) *tid = (unsigned)(uintptr_t)h->thread;
    return (uintptr_t)h;
}
uintptr_t _beginthread(void (*start)(void *), unsigned stack, void *arg) {
    RanHandle *h = new RanHandle; h->kind = RanHandle::Thread;
    ThreadStart *ts = new ThreadStart{NULL, arg, NULL, start};
    pthread_attr_t attr; pthread_attr_init(&attr);
    if (stack) pthread_attr_setstacksize(&attr, stack < 65536 ? 65536 : stack);
    if (pthread_create(&h->thread, &attr, threadTrampoline, ts) != 0) { delete ts; delete h; return 0; }
    pthread_attr_destroy(&attr);
    return (uintptr_t)h;
}
void _endthreadex(unsigned) { pthread_exit(NULL); }
void _endthread(void)       { pthread_exit(NULL); }
void ExitThread(DWORD)      { pthread_exit(NULL); }
BOOL TerminateThread(HANDLE, DWORD) { return FALSE; }
BOOL GetExitCodeThread(HANDLE, LPDWORD code) { if (code) *code = 0; return TRUE; }
BOOL SetThreadPriority(HANDLE, int) { return TRUE; }

HANDLE CreateEventA(LPSECURITY_ATTRIBUTES, BOOL manual, BOOL initial, LPCSTR) {
    RanHandle *h = new RanHandle; h->kind = RanHandle::Event;
    h->manualReset = manual != 0; h->signalled = initial != 0;
    return (HANDLE)h;
}
//  ------------------------------------------------------- multimedia timer
//
//  One caller and one shape: BgmSound asks for
//      timeSetEvent(20, 10, (LPTIMECALLBACK)m_evtBlockFree, 0,
//                   TIME_PERIODIC | TIME_CALLBACK_EVENT_SET)
//  and waits on that event for its next block of music. So what is implemented
//  is exactly that - a thread per timer that sets the event every delay - and a
//  function callback says so once rather than pretending to work.
//
//  Kept simple deliberately: at 20 ms this thread wakes 50 times a second and
//  does one SetEvent, which is cheaper than any shared-timer bookkeeping.
namespace {
struct MMTimer {
    pthread_t thread;
    HANDLE    event;
    unsigned  delayMs;
    volatile bool stop;
    bool      used;
};
MMTimer g_mmTimers[8];
pthread_mutex_t g_mmLock = PTHREAD_MUTEX_INITIALIZER;

void *mmTimerThread(void *arg) {
    MMTimer *t = (MMTimer *) arg;
    while (!t->stop) {
        struct timespec ts;
        ts.tv_sec  = t->delayMs / 1000;
        ts.tv_nsec = (long) ( t->delayMs % 1000 ) * 1000000L;
        nanosleep(&ts, NULL);
        if (t->stop) break;
        SetEvent(t->event);
    }
    return NULL;
}
} // namespace

extern "C" MMRESULT timeSetEvent(UINT delayMs, UINT, void *callback, DWORD_PTR, UINT flags) {
    if (!( flags & TIME_CALLBACK_EVENT_SET )) {
        static bool said = false;
        if (!said) { said = true; RanPlat_Log(RANLOG_WARN, "RanTimer",
            "timeSetEvent with a function callback is not implemented (flags %08x)",
            (unsigned) flags); }
        return 0;
    }
    if (!callback) return 0;

    pthread_mutex_lock(&g_mmLock);
    int slot = -1;
    for (int i = 0; i < 8; ++i) if (!g_mmTimers[i].used) { slot = i; break; }
    if (slot < 0) { pthread_mutex_unlock(&g_mmLock); return 0; }

    MMTimer &t = g_mmTimers[slot];
    t.used = true;
    t.stop = false;
    t.event = (HANDLE) callback;
    t.delayMs = delayMs ? delayMs : 1;
    pthread_mutex_unlock(&g_mmLock);

    if (pthread_create(&t.thread, NULL, mmTimerThread, &t) != 0) {
        t.used = false;
        return 0;
    }
    //  Ids are 1-based: 0 is the failure value the caller checks.
    return (MMRESULT) ( slot + 1 );
}

extern "C" MMRESULT timeKillEvent(UINT id) {
    if (id == 0 || id > 8) return 0;
    MMTimer &t = g_mmTimers[id - 1];
    if (!t.used) return 0;
    t.stop = true;
    pthread_join(t.thread, NULL);
    t.used = false;
    return 0;
}

BOOL SetEvent(HANDLE hh) {
    RanHandle *h = (RanHandle *)hh; if (!h) return FALSE;
    pthread_mutex_lock(&h->mtx); h->signalled = true;
    pthread_cond_broadcast(&h->cond); pthread_mutex_unlock(&h->mtx); return TRUE;
}
BOOL ResetEvent(HANDLE hh) {
    RanHandle *h = (RanHandle *)hh; if (!h) return FALSE;
    pthread_mutex_lock(&h->mtx); h->signalled = false; pthread_mutex_unlock(&h->mtx); return TRUE;
}
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL owner, LPCSTR) {
    RanHandle *h = new RanHandle; h->kind = RanHandle::Mutex;
    h->signalled = !owner; h->manualReset = false;
    return (HANDLE)h;
}
BOOL ReleaseMutex(HANDLE h) { return SetEvent(h); }

DWORD WaitForSingleObject(HANDLE hh, DWORD ms) {
    RanHandle *h = (RanHandle *)hh;
    if (!h) return WAIT_FAILED;
    if (h->kind == RanHandle::Thread) {
        if (ms == INFINITE) {
            if (!h->threadConsumed) {
                h->threadConsumed = true;
                pthread_join(h->thread, NULL);
            }
            return WAIT_OBJECT_0;
        }
        Sleep(ms);
        return WAIT_TIMEOUT;
    }
    pthread_mutex_lock(&h->mtx);
    DWORD r = WAIT_OBJECT_0;
    if (!h->signalled) {
        if (ms == 0) r = WAIT_TIMEOUT;
        else if (ms == INFINITE) {
            while (!h->signalled) pthread_cond_wait(&h->cond, &h->mtx);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += ms / 1000;
            ts.tv_nsec += (long)(ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            while (!h->signalled) {
                if (pthread_cond_timedwait(&h->cond, &h->mtx, &ts) != 0) { r = WAIT_TIMEOUT; break; }
            }
        }
    }
    if (r == WAIT_OBJECT_0 && !h->manualReset) h->signalled = false;
    pthread_mutex_unlock(&h->mtx);
    return r;
}
BOOL CloseHandle(HANDLE hh) {
    RanHandle *h = (RanHandle *)hh;
    if (!h || hh == INVALID_HANDLE_VALUE) return FALSE;
    if (h->kind == RanHandle::File && h->fp) fclose(h->fp);
    if (h->kind == RanHandle::Find && h->dir) closedir(h->dir);
    if (h->kind == RanHandle::Thread && !h->threadConsumed) {
        h->threadConsumed = true;
        pthread_detach(h->thread);
    }
    delete h;
    return TRUE;
}

// ------------------------------------------------------------------- file API
HANDLE CreateFileA(LPCSTR rawName, DWORD access, DWORD, LPSECURITY_ATTRIBUTES,
                   DWORD disp, DWORD, HANDLE) {
    const char *name = RanPath_Resolve(rawName);
    const char *mode;
    bool wr = (access & GENERIC_WRITE) != 0;
    if (disp == CREATE_ALWAYS || disp == CREATE_NEW) mode = "wb+";
    else if (disp == OPEN_ALWAYS) mode = "ab+";
    else mode = wr ? "rb+" : "rb";
    FILE *fp = fopen(name, mode);
    if (!fp) { g_lastError = ERROR_FILE_NOT_FOUND; return INVALID_HANDLE_VALUE; }
    if (disp == OPEN_ALWAYS) fseek(fp, 0, SEEK_SET);
    RanHandle *h = new RanHandle; h->kind = RanHandle::File; h->fp = fp;
    return (HANDLE)h;
}
BOOL ReadFile(HANDLE hh, LPVOID buf, DWORD n, LPDWORD read, LPOVERLAPPED) {
    RanHandle *h = (RanHandle *)hh;
    if (!h || h->kind != RanHandle::File) return FALSE;
    size_t r = fread(buf, 1, n, h->fp);
    if (read) *read = (DWORD)r;
    return TRUE;
}
BOOL WriteFile(HANDLE hh, LPCVOID buf, DWORD n, LPDWORD written, LPOVERLAPPED) {
    RanHandle *h = (RanHandle *)hh;
    if (!h || h->kind != RanHandle::File) return FALSE;
    size_t w = fwrite(buf, 1, n, h->fp);
    if (written) *written = (DWORD)w;
    return TRUE;
}
DWORD SetFilePointer(HANDLE hh, LONG dist, PLONG, DWORD method) {
    RanHandle *h = (RanHandle *)hh;
    if (!h || h->kind != RanHandle::File) return INVALID_SET_FILE_POINTER;
    fseek(h->fp, dist, method == FILE_BEGIN ? SEEK_SET : method == FILE_END ? SEEK_END : SEEK_CUR);
    return (DWORD)ftell(h->fp);
}
DWORD GetFileSize(HANDLE hh, LPDWORD hi) {
    RanHandle *h = (RanHandle *)hh;
    if (!h || h->kind != RanHandle::File) return INVALID_FILE_SIZE;
    long cur = ftell(h->fp); fseek(h->fp, 0, SEEK_END);
    long len = ftell(h->fp); fseek(h->fp, cur, SEEK_SET);
    if (hi) *hi = 0;
    return (DWORD)len;
}
BOOL FlushFileBuffers(HANDLE hh) {
    RanHandle *h = (RanHandle *)hh;
    if (h && h->kind == RanHandle::File) fflush(h->fp);
    return TRUE;
}
BOOL DeleteFileA(LPCSTR p) { return unlink(RanPath_Resolve(p)) == 0; }
BOOL CreateDirectoryA(LPCSTR p, LPSECURITY_ATTRIBUTES) {
    //  Goes through the path layer: the client passes Windows separators, and
    //  the parents may not exist yet.
    if (RanPath_MakeDir(p) == 0) return TRUE;
    g_lastError = (errno == EEXIST) ? ERROR_ALREADY_EXISTS : (DWORD)errno;
    return FALSE;
}
BOOL RemoveDirectoryA(LPCSTR p) { return rmdir(RanPath_Resolve(p)) == 0; }
BOOL CopyFileA(LPCSTR a, LPCSTR b, BOOL failIfExists) {
    if (failIfExists && access(b, F_OK) == 0) return FALSE;
    FILE *in = fopen(a, "rb"); if (!in) return FALSE;
    FILE *out = fopen(b, "wb"); if (!out) { fclose(in); return FALSE; }
    char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out); return TRUE;
}
BOOL MoveFileA(LPCSTR a, LPCSTR b) { return rename(a, b) == 0; }
DWORD GetFileAttributesA(LPCSTR p) {
    struct stat st;
    if (stat(RanPath_Resolve(p), &st) != 0) return 0xFFFFFFFFu;
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}
DWORD GetCurrentDirectoryA(DWORD n, LPSTR buf) {
    if (!getcwd(buf, n)) { buf[0] = 0; return 0; }
    return (DWORD)strlen(buf);
}
BOOL SetCurrentDirectoryA(LPCSTR p) { return chdir(p) == 0; }

// The app sets this at startup to the unpacked data root (APK expansion dir).
static std::string g_modulePath = "/data/local/tmp/ran/MiniA.exe";
void RanShim_SetModulePath(const char *p) { if (p) g_modulePath = p; }
DWORD GetModuleFileNameA(HMODULE, LPSTR buf, DWORD n) {
    strncpy(buf, g_modulePath.c_str(), n); buf[n - 1] = 0;
    return (DWORD)strlen(buf);
}
HMODULE GetModuleHandleA(LPCSTR) { return (HMODULE)1; }

// pattern matching for FindFirstFile / _findfirst ("*.rcc" style)
static bool wildMatch(const char *pat, const char *str) {
    if (!*pat) return !*str;
    if (*pat == '*') { for (const char *s = str; ; ++s) { if (wildMatch(pat + 1, s)) return true; if (!*s) return false; } }
    if (!*str) return false;
    if (*pat == '?' || tolower((unsigned char)*pat) == tolower((unsigned char)*str))
        return wildMatch(pat + 1, str + 1);
    return false;
}
// Win32 treats "*.*" as EVERYTHING, including names with no dot - that is how
// the engine enumerates subdirectories ("gui", "lobi", ...). Matching it
// literally made every recursive scan stop at the top level and index nothing.
static const char *win32Pattern(const char *pat) {
    return (pat && strcmp(pat, "*.*") == 0) ? "*" : pat;
}

static void splitPattern(const char *spec, std::string &dir, std::string &pat) {
    std::string s = spec ? spec : "";
    for (auto &c : s) if (c == '\\') c = '/';
    size_t p = s.rfind('/');
    if (p == std::string::npos) { dir = "."; pat = s; }
    else { dir = s.substr(0, p); pat = s.substr(p + 1); if (dir.empty()) dir = "/"; }
    if (pat.empty()) pat = "*";
}
HANDLE FindFirstFileA(LPCSTR spec, LPWIN32_FIND_DATAA fd) {
    std::string dir, pat; splitPattern(spec, dir, pat);
    dir = RanPath_Resolve(dir.c_str());
    DIR *d = opendir(dir.c_str());
    if (!d) { g_lastError = ERROR_FILE_NOT_FOUND; return INVALID_HANDLE_VALUE; }
    RanHandle *h = new RanHandle; h->kind = RanHandle::Find;
    h->dir = d; h->pattern = pat; h->root = dir + "/";
    if (!FindNextFileA((HANDLE)h, fd)) { closedir(d); delete h; return INVALID_HANDLE_VALUE; }
    return (HANDLE)h;
}
BOOL FindNextFileA(HANDLE hh, LPWIN32_FIND_DATAA fd) {
    RanHandle *h = (RanHandle *)hh;
    if (!h || h->kind != RanHandle::Find) return FALSE;
    struct dirent *e;
    while ((e = readdir(h->dir)) != NULL) {
        if (!wildMatch(win32Pattern(h->pattern.c_str()), e->d_name)) continue;
        memset(fd, 0, sizeof(*fd));
        strncpy(fd->cFileName, e->d_name, MAX_PATH - 1);
        struct stat st;
        std::string full = h->root + e->d_name;
        if (stat(full.c_str(), &st) == 0) {
            fd->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            fd->nFileSizeLow = (DWORD)st.st_size;
        }
        return TRUE;
    }
    return FALSE;
}
BOOL FindClose(HANDLE hh) {
    RanHandle *h = (RanHandle *)hh;
    if (!h || hh == INVALID_HANDLE_VALUE) return FALSE;
    if (h->dir) closedir(h->dir);
    delete h; return TRUE;
}
intptr_t _findfirst(const char *spec, struct _finddata_t *fd) {
    WIN32_FIND_DATAA w;
    HANDLE h = FindFirstFileA(spec, &w);
    if (h == INVALID_HANDLE_VALUE) return -1;
    fd->attrib = w.dwFileAttributes; fd->size = (long)w.nFileSizeLow;
    strncpy(fd->name, w.cFileName, 259); fd->name[259] = 0;
    return (intptr_t)h;
}
int _findnext(intptr_t hh, struct _finddata_t *fd) {
    WIN32_FIND_DATAA w;
    if (!FindNextFileA((HANDLE)hh, &w)) return -1;
    fd->attrib = w.dwFileAttributes; fd->size = (long)w.nFileSizeLow;
    strncpy(fd->name, w.cFileName, 259); fd->name[259] = 0;
    return 0;
}
int _findclose(intptr_t hh) { return FindClose((HANDLE)hh) ? 0 : -1; }

// -------------------------------------------------------------- dyn libraries
HMODULE LoadLibraryA(LPCSTR)  { return NULL; }   // no plug-in DLLs on mobile
void   *GetProcAddress(HMODULE, LPCSTR) { return NULL; }
BOOL    FreeLibrary(HMODULE)  { return TRUE; }

// -------------------------------------------------------------------- memory
HGLOBAL GlobalAlloc(UINT flags, SIZE_T n) {
    void *p = (flags & GMEM_ZEROINIT) ? calloc(1, n ? n : 1) : malloc(n ? n : 1);
    return (HGLOBAL)p;
}
LPVOID  GlobalLock(HGLOBAL h) { return (LPVOID)h; }
BOOL    GlobalUnlock(HGLOBAL) { return TRUE; }
HGLOBAL GlobalFree(HGLOBAL h) { free((void *)h); return NULL; }

// ---------------------------------------------------------------------- time
static void tmToSystemTime(const struct tm &t, LPSYSTEMTIME st) {
    st->wYear = (WORD)(t.tm_year + 1900); st->wMonth = (WORD)(t.tm_mon + 1);
    st->wDayOfWeek = (WORD)t.tm_wday; st->wDay = (WORD)t.tm_mday;
    st->wHour = (WORD)t.tm_hour; st->wMinute = (WORD)t.tm_min;
    st->wSecond = (WORD)t.tm_sec; st->wMilliseconds = 0;
}
void GetSystemTime(LPSYSTEMTIME st) { time_t t = time(NULL); struct tm o; gmtime_r(&t, &o); tmToSystemTime(o, st); }
void GetLocalTime(LPSYSTEMTIME st)  { time_t t = time(NULL); struct tm o; localtime_r(&t, &o); tmToSystemTime(o, st); }
BOOL SystemTimeToFileTime(const SYSTEMTIME *st, LPFILETIME ft) {
    struct tm t; memset(&t, 0, sizeof(t));
    t.tm_year = st->wYear - 1900; t.tm_mon = st->wMonth - 1; t.tm_mday = st->wDay;
    t.tm_hour = st->wHour; t.tm_min = st->wMinute; t.tm_sec = st->wSecond;
    uint64_t secs = (uint64_t)timegm(&t);
    uint64_t v = (secs + 11644473600ull) * 10000000ull;
    ft->dwLowDateTime = (DWORD)v; ft->dwHighDateTime = (DWORD)(v >> 32);
    return TRUE;
}
BOOL FileTimeToSystemTime(const FILETIME *ft, LPSYSTEMTIME st) {
    uint64_t v = ((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    time_t secs = (time_t)(v / 10000000ull - 11644473600ull);
    struct tm o; gmtime_r(&secs, &o); tmToSystemTime(o, st); return TRUE;
}
BOOL FileTimeToLocalFileTime(const FILETIME *ft, LPFILETIME out) { *out = *ft; return TRUE; }
void GetSystemInfo(LPSYSTEM_INFO si) {
    memset(si, 0, sizeof(*si));
    si->dwPageSize = (DWORD)sysconf(_SC_PAGESIZE);
    si->dwNumberOfProcessors = (DWORD)sysconf(_SC_NPROCESSORS_ONLN);
    si->dwAllocationGranularity = 65536;
}
void GlobalMemoryStatus(LPMEMORYSTATUS ms) {
    memset(ms, 0, sizeof(*ms));
    ms->dwLength = sizeof(*ms);
    //  _SC_PHYS_PAGES and _SC_AVPHYS_PAGES are Linux extensions and do not
    //  exist on Darwin; sysctl answers the same question. There is no
    //  equivalent of AVPHYS_PAGES at all - iOS does not publish free memory -
    //  so the client is told half the machine is free, which is the shape of
    //  the only thing it uses the number for: is this a small machine.
#if defined(__APPLE__)
    uint64_t total = 0; size_t len = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &len, NULL, 0) != 0) total = 0;
    ms->dwTotalPhys = (SIZE_T)total;
    ms->dwAvailPhys = (SIZE_T)(total / 2);
#else
    long pages = sysconf(_SC_PHYS_PAGES), ps = sysconf(_SC_PAGESIZE);
    ms->dwTotalPhys = (SIZE_T)pages * (SIZE_T)ps;
    ms->dwAvailPhys = (SIZE_T)sysconf(_SC_AVPHYS_PAGES) * (SIZE_T)ps;
#endif
}
//  Windows 7 Professional, and the product type matters.
//
//  This used to fill the five base fields and stop. GetWinVer() asks with an
//  OSVERSIONINFOEX, which it ZeroMemorys first, and its 6.1 arm is
//
//      if      ( wProductType == VER_NT_WORKSTATION )        *nVersion = W7;
//      else if ( wProductType == VER_NT_SERVER || ... )      *nVersion = WSVR2008R2;
//
//  with no else. Leaving wProductType at the zero it was cleared to matched
//  neither, so nVersion kept its initial WUNKNOWN (0) - below WNTFIRST (101),
//  which is the test everything downstream uses to mean "Windows 9x".
//
//  Two things read that, and both are about text:
//
//      CTextUtil::OneTimeSceneInit   nVersion < WNTFIRST -> m_bUsage = FALSE
//      CD3DFontX::InitDeviceObjects  nVersion < WNTFIRST -> m_iOutLine = 0
//
//  and the black outline behind every glyph is drawn only when
//  CTextUtil::m_bUsage && m_iOutLine. So the client had been told it was
//  running on Windows 98 and had quietly turned the outline off - text still
//  rendered, which is why it read as a missing feature rather than a fault.
//
//  Filling the EX fields is the fix; the size field says whether the caller
//  passed room for them, exactly as the real GetVersionEx decides.
BOOL GetVersionExA(LPOSVERSIONINFOA v) {
    if (!v) return FALSE;
    v->dwMajorVersion = 6; v->dwMinorVersion = 1; v->dwBuildNumber = 7601;
    v->dwPlatformId = VER_PLATFORM_WIN32_NT; v->szCSDVersion[0] = 0;

    if (v->dwOSVersionInfoSize >= sizeof(OSVERSIONINFOEXA)) {
        OSVERSIONINFOEXA *ex = (OSVERSIONINFOEXA *)v;
        ex->wServicePackMajor = 1;
        ex->wServicePackMinor = 0;
        ex->wSuiteMask = 0;
        ex->wProductType = VER_NT_WORKSTATION;
        ex->wReserved = 0;
    }
    return TRUE;
}
DWORD GetVersion(void) { return 0x0A280106; }
BOOL GetVersionExW(void *) { return TRUE; }

// ------------------------------------------------------------------- ini files
// Config lives in the same key=value ini the PC client uses; parse it directly.
static bool iniFind(const char *file, const char *app, const char *key, std::string &out) {
    FILE *fp = fopen(file, "rb"); if (!fp) return false;
    char line[1024]; bool inSection = (app == NULL);
    std::string want = app ? std::string("[") + app + "]" : "";
    bool found = false;
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        size_t n = strlen(p);
        while (n && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' ')) p[--n] = 0;
        if (!*p || *p == ';' || *p == '#') continue;
        if (*p == '[') { inSection = app && strcasecmp(p, want.c_str()) == 0; continue; }
        if (!inSection) continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = p; size_t kl = strlen(k);
        while (kl && (k[kl - 1] == ' ' || k[kl - 1] == '\t')) k[--kl] = 0;
        if (strcasecmp(k, key) != 0) continue;
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') ++v;
        out = v; found = true; break;
    }
    fclose(fp);
    return found;
}
DWORD GetPrivateProfileStringA(LPCSTR app, LPCSTR key, LPCSTR def, LPSTR buf, DWORD n, LPCSTR file) {
    std::string v;
    if (!iniFind(file, app, key, v)) v = def ? def : "";
    strncpy(buf, v.c_str(), n); if (n) buf[n - 1] = 0;
    return (DWORD)strlen(buf);
}
UINT GetPrivateProfileIntA(LPCSTR app, LPCSTR key, INT def, LPCSTR file) {
    std::string v;
    return iniFind(file, app, key, v) ? (UINT)atoi(v.c_str()) : (UINT)def;
}
BOOL WritePrivateProfileStringA(LPCSTR app, LPCSTR key, LPCSTR val, LPCSTR file) {
    // Rewrite the whole file with the key replaced/appended.
    std::vector<std::string> lines;
    FILE *fp = fopen(file, "rb");
    if (fp) { char l[1024]; while (fgets(l, sizeof(l), fp)) lines.push_back(l); fclose(fp); }
    std::string want = std::string("[") + (app ? app : "") + "]";
    bool inSec = false, wrote = false;
    int secEnd = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string t = lines[i];
        while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
        if (!t.empty() && t[0] == '[') {
            if (inSec && secEnd < 0) secEnd = (int)i;
            inSec = strcasecmp(t.c_str(), want.c_str()) == 0;
            continue;
        }
        if (!inSec) continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string k = t.substr(0, eq);
        while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
        if (strcasecmp(k.c_str(), key) == 0) {
            lines[i] = std::string(key) + "=" + (val ? val : "") + "\n";
            wrote = true; break;
        }
    }
    if (!wrote) {
        std::string entry = std::string(key) + "=" + (val ? val : "") + "\n";
        bool haveSec = false;
        for (auto &l : lines) if (strncasecmp(l.c_str(), want.c_str(), want.size()) == 0) { haveSec = true; break; }
        if (!haveSec) { lines.push_back(want + "\n"); lines.push_back(entry); }
        else if (secEnd >= 0) lines.insert(lines.begin() + secEnd, entry);
        else lines.push_back(entry);
    }
    fp = fopen(file, "wb"); if (!fp) return FALSE;
    for (auto &l : lines) fwrite(l.data(), 1, l.size(), fp);
    fclose(fp); return TRUE;
}

// ------------------------------------------------------------------- codepage
extern "C" unsigned RanText_ByteToUnicode(int cp, unsigned char b);
extern "C" int RanText_ToWide(int cp, const char *src, int srcLen, unsigned short *dst, int dstLen);

// The client stores text in its legacy codepage (CP874 for Thai) and converts to
// UTF-16 here before drawing. A byte-for-byte copy lands Thai in the Latin-1
// block, where no font has glyphs - every character then renders as .notdef.
int MultiByteToWideChar(UINT cp, DWORD, LPCSTR src, int srcLen, LPWSTR dst, int dstLen) {
    if (!src) return 0;
    int n = (srcLen < 0) ? (int)strlen(src) + 1 : srcLen;
    if (!dst || dstLen == 0) return n;
    // wchar_t is 32-bit here but the client treats these as UTF-16 units, so the
    // conversion writes 16-bit values and they are widened on the way out.
    std::vector<unsigned short> tmp((size_t)dstLen, 0);
    int produced = RanText_ToWide((int)cp, src, n, &tmp[0], dstLen);
    for (int i = 0; i < produced; ++i) dst[i] = (wchar_t)tmp[(size_t)i];
    return produced;
}
// The inverse, for the codepages that are a simple offset.
int WideCharToMultiByte(UINT cp, DWORD, LPCWSTR src, int srcLen, LPSTR dst, int dstLen, LPCSTR, LPBOOL) {
    if (!src) return 0;
    int n = (srcLen < 0) ? (int)wcslen(src) + 1 : srcLen;
    if (!dst || dstLen == 0) return n;
    int i = 0;
    for (; i < n && i < dstLen; ++i) {
        unsigned c = (unsigned)src[i];
        if (cp == 874 && c >= 0x0E01 && c <= 0x0E5B) dst[i] = (char)(0xA0 + (c - 0x0E00));
        else dst[i] = (char)(c & 0xFF);
    }
    return i;
}

// ------------------------------------------------------------- crypto (MD5-ish)
BOOL CryptAcquireContextA(HCRYPTPROV *p, LPCSTR, LPCSTR, DWORD, DWORD) { if (p) *p = 1; return TRUE; }
BOOL CryptReleaseContext(HCRYPTPROV, DWORD) { return TRUE; }
BOOL CryptCreateHash(HCRYPTPROV, DWORD, HCRYPTKEY, DWORD, HCRYPTHASH *h) { if (h) *h = 1; return TRUE; }
BOOL CryptHashData(HCRYPTHASH, const BYTE *, DWORD, DWORD) { return TRUE; }
BOOL CryptGetHashParam(HCRYPTHASH, DWORD, BYTE *, DWORD *, DWORD) { return FALSE; }
BOOL CryptDestroyHash(HCRYPTHASH) { return TRUE; }

// ---------------------------------------------------------------- windowing
// The mobile app owns the surface; these are serviced by platform/android.
static RECT   g_clientRect = { 0, 0, 1280, 720 };
static POINT  g_cursor = { 0, 0 };
static BYTE   g_keys[256] = { 0 };

void RanShim_SetClientSize(int w, int h) { g_clientRect.right = w; g_clientRect.bottom = h; }

//  CSIDL_PERSONAL and friends all map to the data root: it is where the client
//  may write, and it keeps a character's settings next to its data.
BOOL SHGetSpecialFolderPathA(HWND, LPSTR p, int, BOOL) {
    if (!p) return FALSE;
    const char *root = g_modulePath.c_str();
    if (!root || !*root) { p[0] = 0; return FALSE; }

    size_t n = strlen(root);
    //  The client appends "\\Logs\\..." itself, so no trailing separator here.
    while (n > 1 && (root[n - 1] == '/' || root[n - 1] == '\\')) --n;
    memcpy(p, root, n);
    p[n] = 0;
    return TRUE;
}
void RanShim_SetCursor(int x, int y)     { g_cursor.x = x; g_cursor.y = y; }
void RanShim_SetKey(int vk, bool down)   { if (vk >= 0 && vk < 256) g_keys[vk] = down ? 0x80 : 0; }

int  MessageBoxA(HWND, LPCSTR text, LPCSTR cap, UINT) {
    LOGI("[MessageBox] %s: %s", cap ? cap : "", text ? text : "");
    return IDOK;
}
SHORT GetKeyState(int vk)      { return (SHORT)((vk >= 0 && vk < 256 && g_keys[vk]) ? 0x8000 : 0); }
SHORT GetAsyncKeyState(int vk) { return GetKeyState(vk); }
BOOL  GetKeyboardState(PBYTE ks) { memcpy(ks, g_keys, 256); return TRUE; }
// The engine tracks the pointer through GetCursorPos when it is not reading a
// relative device (DxInputDevice::UpdateMouseState only integrates DIMOFS_X/Y
// for a DEVICE mouse). On a phone the touch position IS the cursor, so it comes
// from the input shim rather than from a stored desktop cursor.
extern "C" void RanInput_PointerAbsolute(int *x, int *y);
BOOL  GetCursorPos(LPPOINT p)  {
    if (!p) return FALSE;
    int x = 0, y = 0;
    RanInput_PointerAbsolute(&x, &y);
    p->x = x; p->y = y;
    return TRUE;
}
extern "C" void RanInput_WarpPointer(int x, int y);
//  Has to move the same pointer GetCursorPos reads, or pinning does nothing.
BOOL  SetCursorPos(int x, int y) {
    g_cursor.x = x; g_cursor.y = y;
    RanInput_WarpPointer(x, y);
    return TRUE;
}
//  Win32 keeps a display counter, and callers converge on a value by looping.
//
//  CCursor::SetShowCursor does exactly that:
//
//      int nShow = ShowCursor(FALSE);
//      while ( nShow > -1 ) { nShow = ShowCursor(FALSE); }
//
//  Returning a constant 0 made that an infinite loop - the render thread span
//  at 100% until Android killed the app for not reading input. It only bit on
//  camera rotation, because the middle-drag branch of DxViewPort::FrameMoveMAX
//  is the one place that asks for the cursor to be HIDDEN; asking for it to be
//  shown happens to satisfy its loops on the first call.
//
//  There is no cursor to show on a touch screen, so nothing is drawn either
//  way - but the counter has to behave, or the caller never comes back.
int   ShowCursor(BOOL show) {
    static int s_count = 0;     //  Win32 starts at 0 when a mouse is present
    s_count += show ? 1 : -1;
    return s_count;
}
BOOL  ClientToScreen(HWND, LPPOINT) { return TRUE; }
BOOL  ScreenToClient(HWND, LPPOINT) { return TRUE; }
BOOL  GetClientRect(HWND, LPRECT r) { *r = g_clientRect; return TRUE; }
BOOL  GetWindowRect(HWND, LPRECT r) { *r = g_clientRect; return TRUE; }
BOOL  SetWindowPos(HWND, HWND, int, int, int, int, UINT) { return TRUE; }
BOOL  ShowWindow(HWND, int)    { return TRUE; }
BOOL  UpdateWindow(HWND)       { return TRUE; }
BOOL  InvalidateRect(HWND, const RECT *, BOOL) { return TRUE; }
LRESULT SendMessageA(HWND, UINT, WPARAM, LPARAM) { return 0; }
BOOL  PostMessageA(HWND, UINT, WPARAM, LPARAM)   { return TRUE; }
BOOL  PeekMessageA(LPMSG, HWND, UINT, UINT, UINT) { return FALSE; }
HWND  GetActiveWindow(void)      { return NULL; }
HWND  GetForegroundWindow(void)  { return NULL; }
HWND  SetCapture(HWND)           { return NULL; }
BOOL  ReleaseCapture(void)       { return TRUE; }
HDC   GetDC(HWND)                { return NULL; }
int   ReleaseDC(HWND, HDC)       { return 1; }
int   GetSystemMetrics(int i)    { return i == SM_CXSCREEN ? g_clientRect.right : g_clientRect.bottom; }
BOOL  SetRect(LPRECT r, int l, int t, int rr, int b) { r->left = l; r->top = t; r->right = rr; r->bottom = b; return TRUE; }
BOOL  PtInRect(const RECT *r, POINT p) { return p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom; }
BOOL  IntersectRect(LPRECT o, const RECT *a, const RECT *b) {
    o->left = a->left > b->left ? a->left : b->left;
    o->top = a->top > b->top ? a->top : b->top;
    o->right = a->right < b->right ? a->right : b->right;
    o->bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    if (o->right <= o->left || o->bottom <= o->top) { memset(o, 0, sizeof(*o)); return FALSE; }
    return TRUE;
}
BOOL  OffsetRect(LPRECT r, int dx, int dy) { r->left += dx; r->right += dx; r->top += dy; r->bottom += dy; return TRUE; }
BOOL  SetForegroundWindow(HWND) { return TRUE; }
UINT  MapVirtualKeyA(UINT c, UINT)  { return c; }
int   ToAscii(UINT vk, UINT, const BYTE *, LPWORD out, UINT) { if (out) *out = (WORD)vk; return 1; }

// --------------------------------------------------------------------- IME
HIMC  ImmGetContext(HWND) { return NULL; }
BOOL  ImmReleaseContext(HWND, HIMC) { return TRUE; }
LONG  ImmGetCompositionStringA(HIMC, DWORD, LPVOID, DWORD) { return 0; }
BOOL  ImmSetCompositionWindow(HIMC, LPCOMPOSITIONFORM) { return TRUE; }
BOOL  ImmSetCandidateWindow(HIMC, LPCANDIDATEFORM) { return TRUE; }
DWORD ImmGetCandidateListA(HIMC, DWORD, LPCANDIDATELIST, DWORD) { return 0; }
BOOL  ImmGetOpenStatus(HIMC) { return FALSE; }
BOOL  ImmSetOpenStatus(HIMC, BOOL) { return TRUE; }
BOOL  ImmGetConversionStatus(HIMC, LPDWORD, LPDWORD) { return FALSE; }
BOOL  ImmSetConversionStatus(HIMC, DWORD, DWORD) { return TRUE; }
HIMC  ImmAssociateContext(HWND, HIMC) { return NULL; }
BOOL  ImmNotifyIME(HIMC, DWORD, DWORD, DWORD) { return TRUE; }

// ------------------------------------------------------------------- winsock
int  WSAStartup(WORD, LPWSADATA d) { if (d) memset(d, 0, sizeof(*d)); return 0; }
int  WSACleanup(void) { return 0; }
int  WSAGetLastError(void) {
    switch (errno) {
        case EWOULDBLOCK:    return WSAEWOULDBLOCK;
        // A non-blocking connect sets EINPROGRESS on POSIX and reports
        // WSAEWOULDBLOCK on Windows. CNetClient::Connect only tolerates
        // WSAEWOULDBLOCK, so reporting the literal translation made every
        // connection attempt fail before a packet was ever sent.
        case EINPROGRESS:    return WSAEWOULDBLOCK;
        case EALREADY:       return WSAEALREADY;
        case ENOTSOCK:       return WSAENOTSOCK;
        case EDESTADDRREQ:   return WSAEDESTADDRREQ;
        case EMSGSIZE:       return WSAEMSGSIZE;
        case EPROTOTYPE:     return WSAEPROTOTYPE;
        case ENOPROTOOPT:    return WSAENOPROTOOPT;
        case EPROTONOSUPPORT:return WSAEPROTONOSUPPORT;
        case ESOCKTNOSUPPORT:return WSAESOCKTNOSUPPORT;
        case EOPNOTSUPP:     return WSAEOPNOTSUPP;
        case EPFNOSUPPORT:   return WSAEPFNOSUPPORT;
        case EAFNOSUPPORT:   return WSAEAFNOSUPPORT;
        case EADDRINUSE:     return WSAEADDRINUSE;
        case EADDRNOTAVAIL:  return WSAEADDRNOTAVAIL;
        case ENETDOWN:       return WSAENETDOWN;
        case ENETUNREACH:    return WSAENETUNREACH;
        case ENETRESET:      return WSAENETRESET;
        case ECONNABORTED:   return WSAECONNABORTED;
        case ECONNRESET:     return WSAECONNRESET;
        case ENOBUFS:        return WSAENOBUFS;
        case EISCONN:        return WSAEISCONN;
        case ENOTCONN:       return WSAENOTCONN;
        case ESHUTDOWN:      return WSAESHUTDOWN;
        case ETIMEDOUT:      return WSAETIMEDOUT;
        case ECONNREFUSED:   return WSAECONNREFUSED;
        case EHOSTDOWN:      return WSAEHOSTDOWN;
        case EHOSTUNREACH:   return WSAEHOSTUNREACH;
        case EINTR:          return WSAEINTR;
        case EINVAL:         return WSAEINVAL;
        case EFAULT:         return WSAEFAULT;
        case EACCES:         return WSAEACCES;
        case EMFILE:         return WSAEMFILE;
        default:             return errno;
    }
}
void WSASetLastError(int e) { errno = e; }
int  closesocket(SOCKET s) { RanNet_ForgetSocket(s); return close(s); }
int  ioctlsocket(SOCKET s, long cmd, ULONG *arg) {
    (void)cmd;
    int fl = fcntl(s, F_GETFL, 0);
    if (arg && *arg) fl |= O_NONBLOCK; else fl &= ~O_NONBLOCK;
    return fcntl(s, F_SETFL, fl);
}
int WSAIoctl(SOCKET, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPVOID, LPVOID) { return -1; }

} // extern "C"

// ----------------------------------------------------------------- MFC glue
static CWinApp g_app;
CWinApp  *AfxGetApp() { return &g_app; }
HINSTANCE AfxGetInstanceHandle() { return (HINSTANCE)1; }
CWnd     *AfxGetMainWnd() { return g_app.m_pMainWnd; }
void      AfxMessageBox(const char *text, UINT) { LOGI("[AfxMessageBox] %s", text ? text : ""); }
void      AfxThrowFileException(int, LONG, const char *) { throw CFileException(); }
void      AfxThrowMemoryException() { throw CMemoryException(); }

CFileFind::CFileFind() {}
CFileFind::~CFileFind() { Close(); }
void CFileFind::Close() {
    m_entries.clear();
    m_index = -1;
    m_name.Empty();
}
// MFC semantics, which CFileFindTree::PathRecurse depends on:
//   FindFile()     locates the first match but does NOT make it current
//   FindNextFile() makes the next match current and returns TRUE only while a
//                  FURTHER match exists - so the LAST entry is reported with
//                  FALSE and is still valid for the loop body to use.
// A readdir-as-you-go version returns TRUE for the last entry and then a blank
// FALSE, which made the recursive tree walk insert an empty filename.
BOOL CFileFind::FindFile(const char *pattern, DWORD) {
    Close();
    std::string dir, pat; splitPattern(pattern ? pattern : "*.*", dir, pat);
    // The engine builds Win32 paths (".../Textures" with backslashes); the
    // resolver fixes separators and case before opendir ever sees them.
    dir = RanPath_Resolve(dir.c_str());
    DIR *d = opendir(dir.c_str());
    if (!d) return FALSE;

    m_entries.clear();
    m_index = -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!wildMatch(win32Pattern(pat.c_str()), e->d_name)) continue;
        m_entries.push_back(e->d_name);
    }
    closedir(d);

    m_pattern = pat.c_str();
    m_root = (dir + "/").c_str();
    return m_entries.empty() ? FALSE : TRUE;
}
BOOL CFileFind::FindNextFile() {
    ++m_index;
    if (m_index < 0 || (size_t)m_index >= m_entries.size()) {
        m_name.Empty();
        m_isDir = false;
        return FALSE;
    }
    m_name = m_entries[(size_t)m_index].c_str();
    struct stat st;
    std::string full = std::string(m_root.GetString()) + m_entries[(size_t)m_index];
    m_isDir = (stat(full.c_str(), &st) == 0) && S_ISDIR(st.st_mode);
    return ((size_t)(m_index + 1) < m_entries.size()) ? TRUE : FALSE;
}
CString CFileFind::GetFileTitle() const {
    int p = m_name.ReverseFind('.');
    return p > 0 ? m_name.Left(p) : m_name;
}

extern "C++" { const GUID GUID_NULL_SHIM = {0,0,0,{0,0,0,0,0,0,0,0}}; }

extern "C" char *strrev_shim(char *s) {
    if (!s) return s;
    size_t n = strlen(s);
    for (size_t i = 0; i < n / 2; ++i) { char t = s[i]; s[i] = s[n - 1 - i]; s[n - 1 - i] = t; }
    return s;
}
