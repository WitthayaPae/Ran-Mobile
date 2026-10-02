//  See ran_plat.h. Portable: no Android headers, no iOS headers.
#include "ran_plat.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>

namespace {

//  /sdcard/ran is where every flag has lived since the port started, and the
//  notes, the scripts and the habits all say so. Keeping it as the default
//  means this change moves no cheese on Android.
char g_root[512] = "/sdcard/ran";

//  Rotating, because callers write things like
//      access(RanPlat_DiagPath("a"), F_OK) && access(RanPlat_DiagPath("b"), F_OK)
//  and a single static buffer would have the second call eat the first.
const int  kSlots = 4;
char       g_buf[kSlots][640];
int        g_next = 0;

}   // namespace

//  The one genuinely platform-conditional thing in this file. __ANDROID__ is
//  defined by the NDK; anything else gets stderr, which is what a Mac console
//  and Xcode both read.
#ifdef __ANDROID__
#include <android/log.h>
#else
//  Everywhere that is not Android, the log ALSO goes to a file.
//
//  On Android logcat is the log and adb reads it from outside. iOS has no
//  equivalent a Windows machine can reach: stderr from a sideloaded app goes
//  into the system log, and reading that wants Xcode or Console.app, i.e. a
//  Mac. A file under the diagnostic root - which is Documents/ran, shared over
//  USB - is the only route off the device from here.
//
//  Truncated once per run, and capped, because it is a debugging artefact and
//  not a record: a session that logs hard should not quietly fill the phone.
#include <pthread.h>
#include <stdlib.h>
#if defined(__APPLE__)
//  An iOS app's stderr is not routed anywhere: nothing reads it unless a
//  debugger is attached, so on a sideloaded build every line written there is
//  simply lost. That is how the first run on a phone produced an empty screen,
//  an empty ran.log and no syslog at all - three blind instruments and no way
//  to tell which of them was the broken one.
//
//  os_log is what the system console reads, and what pymobiledevice3 syslog
//  streams over USB. %{public}s because os_log redacts %s to <private> by
//  default, which would leave the lines visible and their contents not.
#include <os/log.h>
#endif

//  The real fopen, not the shim's.
//
//  windows.h does "#define fopen(p, m) ran_fopen((p), (m))" so the client's
//  thousands of fopen calls go through the case-insensitive path resolver.
//  This file is compiled with the engine's StdAfx force-included, so it got
//  that macro too - and ran_fopen LOGS, through this very function, which is
//  already holding g_logLock. A non-recursive mutex, taken twice, on the first
//  line the client ever logged.
//
//  That is exactly what the first run on a real iPhone did: ran.log created and
//  left at 0 bytes, the patcher thread stopped dead on its opening line, the
//  screen showing a patch page whose callbacks never fired, and no crash to
//  explain any of it. path_resolve.cpp carries a comment about this same
//  recursion; the warning was there and I walked into it anyway.
#undef fopen
static pthread_mutex_t g_logLock = PTHREAD_MUTEX_INITIALIZER;
static FILE           *g_logFile = NULL;
static long            g_logBytes = 0;
static int             g_logTried = 0;
static const long      kLogCap = 8L * 1024 * 1024;
#endif
#include <stdarg.h>

//  Every line also goes into the crash recorder's ring (below), so a report
//  can say what the game was doing - on Android the log is otherwise only in
//  logcat, which is gone by the time anyone asks.
static void RanCrash_LogLine ( int level, const char *tag, const char *fmt, va_list ap );

extern "C" void RanPlat_Log ( int level, const char *tag, const char *fmt, ... )
{
    va_list ap;
    va_start ( ap, fmt );
    {
        va_list apr;
        va_copy ( apr, ap );
        RanCrash_LogLine ( level, tag, fmt, apr );
        va_end ( apr );
    }
#ifdef __ANDROID__
    const int pri = level == RANLOG_ERROR ? ANDROID_LOG_ERROR
                  : level == RANLOG_WARN  ? ANDROID_LOG_WARN
                                          : ANDROID_LOG_INFO;
    __android_log_vprint ( pri, tag, fmt, ap );
#else
    const char *pri = level == RANLOG_ERROR ? "E"
                    : level == RANLOG_WARN  ? "W" : "I";
    fprintf ( stderr, "%s %s: ", pri, tag ? tag : "Ran" );
    //  One va_list can only be walked once, and this walks it twice.
    va_list ap2;
    va_copy ( ap2, ap );
    vfprintf ( stderr, fmt, ap );
    fputc ( 0x0A, stderr );

#if defined(__APPLE__)
    //  Formatted once for the system log. Done before the file, so a line
    //  still reaches somewhere readable if the file was never opened.
    {
        va_list ap3;
        va_copy ( ap3, ap2 );
        char msg[1024];
        vsnprintf ( msg, sizeof(msg), fmt, ap3 );
        va_end ( ap3 );
        os_log ( OS_LOG_DEFAULT, "%{public}s %{public}s: %{public}s",
                 pri, tag ? tag : "Ran", msg );
    }
#endif

    //  Belt and braces. Nothing below should log, but this file cannot be the
    //  thing that hangs the client: a re-entrant call skips the file and the
    //  line still reaches stderr and os_log above.
    static __thread int s_inLog = 0;
    if ( s_inLog ) { va_end ( ap2 ); va_end ( ap ); return; }
    s_inLog = 1;

    pthread_mutex_lock ( &g_logLock );
    if ( !g_logFile && !g_logTried ) {
        g_logTried = 1;
        char path[640];
        snprintf ( path, sizeof(path), "%s/ran.log", g_root );
        g_logFile = fopen ( path, "wb" );
    }
    if ( g_logFile && g_logBytes < kLogCap ) {
        g_logBytes += fprintf ( g_logFile, "%s %s: ", pri, tag ? tag : "Ran" );
        g_logBytes += vfprintf ( g_logFile, fmt, ap2 );
        fputc ( 0x0A, g_logFile );
        ++g_logBytes;
        //  Flushed every line on purpose: the interesting log is the one from
        //  the run that crashed, and a buffered tail is exactly what is lost.
        fflush ( g_logFile );
    }
    pthread_mutex_unlock ( &g_logLock );
    s_inLog = 0;
    va_end ( ap2 );
#endif
    va_end ( ap );
}

extern "C" void RanPlat_SetDiagRoot ( const char *dir )
{
    if ( !dir || !*dir ) return;
    snprintf ( g_root, sizeof(g_root), "%s", dir );
#ifndef __ANDROID__
    //  The root moved, so the log file has to. Anything logged before this
    //  point went to a path that did not exist; one retry is owed.
    pthread_mutex_lock ( &g_logLock );
    if ( g_logFile ) { fclose ( g_logFile ); g_logFile = NULL; }
    g_logTried = 0;
    g_logBytes = 0;
    pthread_mutex_unlock ( &g_logLock );
#endif
    //  Trailing slash off, so the join below is predictable.
    size_t n = strlen ( g_root );
    while ( n > 1 && g_root[n-1] == '/' ) g_root[--n] = '\0';
}

namespace {
//  What Android has always used. Nothing changes there.
char g_fontDir[512]  = "/system/fonts";
char g_fontFall[128] = "Roboto-Regular.ttf";
char g_fontFallPath[640];
}

extern "C" void RanPlat_SetFontDir ( const char *dir, const char *fallbackFile )
{
    if ( dir && *dir ) {
        snprintf ( g_fontDir, sizeof(g_fontDir), "%s", dir );
        size_t n = strlen ( g_fontDir );
        while ( n > 1 && g_fontDir[n-1] == '/' ) g_fontDir[--n] = '\0';
    }
    if ( fallbackFile && *fallbackFile )
        snprintf ( g_fontFall, sizeof(g_fontFall), "%s", fallbackFile );
}

extern "C" const char *RanPlat_FontDir ( void ) { return g_fontDir; }

extern "C" const char *RanPlat_FontFallback ( void )
{
    snprintf ( g_fontFallPath, sizeof(g_fontFallPath), "%s/%s", g_fontDir, g_fontFall );
    return g_fontFallPath;
}

extern "C" const char *RanPlat_DiagPath ( const char *name )
{
    char *out = g_buf[g_next];
    g_next = ( g_next + 1 ) % kSlots;
    snprintf ( out, sizeof(g_buf[0]), "%s/%s", g_root, name ? name : "" );
    return out;
}

//  Does a diagnostic file exist?
//
//  Cached, because the answer costs far more than it looks. The diagnostic root
//  is on /sdcard, which on modern Android is FUSE: every access() is a round
//  trip to a userspace daemon. Measured on the Tab S9, one call is about
//  330 us - a third of a millisecond, per call.
//
//  That was not theory. DxEffectMesh::Render asked "does effmesh exist?" once
//  per effect mesh per frame, and with a weapon and a buff card on one
//  character that is ten calls: 3.3 ms of a 10 ms frame, spent entirely on
//  asking the filesystem about a file that was not there. It read as "effects
//  are expensive", and it was the instrument all along.
//
//  Answers from a small table and re-asks each name at most twice a second, so
//  a file dropped in still takes effect while you are looking at the screen,
//  and a call in a draw loop costs a string compare.
#include <time.h>

extern "C" int RanPlat_DiagExists ( const char *name )
{
    if ( !name || !*name ) return 0;

    //  Room for every diagnostic name the client uses, with slack.
    //
    //  Sixteen was not enough and the overflow was silent: the table filled,
    //  every name after it fell through to the live access(), and the whole
    //  point of the cache was lost for exactly the names that arrived last.
    //  A sampling profile put 14.8% of the process in __faccessat, under
    //  DxEffectMesh::Render, which is what "the weapon effect is expensive"
    //  turned out to be. There are 26 names today.
    struct Entry { char name[32]; int present; long checkedMs; };
    static Entry s_cache[64];
    static int   s_count = 0;
    static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

    struct timespec ts;
    clock_gettime ( CLOCK_MONOTONIC, &ts );
    const long nowMs = (long)( ts.tv_sec * 1000 + ts.tv_nsec / 1000000 );

    pthread_mutex_lock ( &s_lock );

    Entry *e = NULL;
    for ( int i = 0; i < s_count; ++i ) {
        if ( strcmp ( s_cache[i].name, name ) == 0 ) { e = &s_cache[i]; break; }
    }
    if ( !e ) {
        //  A name that does not fit the table is answered live rather than
        //  wrongly; sixteen is more diagnostics than anything uses at once.
        //  Still possible in principle, and it must be loud rather than slow:
        //  a name that does not fit is answered live, which is correct and
        //  expensive, so say so once.
        if ( s_count >= (int)( sizeof(s_cache)/sizeof(s_cache[0]) ) ||
             strlen ( name ) >= sizeof(s_cache[0].name) ) {
            static int s_warned = 0;
            if ( !s_warned ) {
                s_warned = 1;
                RanPlat_Log ( RANLOG_WARN, "RanPlat",
                    "diagnostic cache full at %d names - \"%s\" is being stat'd live",
                    s_count, name );
            }
            pthread_mutex_unlock ( &s_lock );
            return access ( RanPlat_DiagPath ( name ), F_OK ) == 0 ? 1 : 0;
        }
        e = &s_cache[s_count++];
        snprintf ( e->name, sizeof(e->name), "%s", name );
        e->present = -1;
        e->checkedMs = 0;
    }

    //  One second, not half: with thirty-odd names even the refresh is real
    //  work at 120 us a stat, and no diagnostic needs to be noticed sooner.
    if ( e->present < 0 || nowMs - e->checkedMs >= 1000 ) {
        e->checkedMs = nowMs;
        pthread_mutex_unlock ( &s_lock );
        const int present = access ( RanPlat_DiagPath ( name ), F_OK ) == 0 ? 1 : 0;
        pthread_mutex_lock ( &s_lock );
        e->present = present;
    }

    const int answer = e->present > 0 ? 1 : 0;
    pthread_mutex_unlock ( &s_lock );
    return answer;
}

extern "C" FILE *RanPlat_DiagOpenWrite ( const char *name )
{
    return fopen ( RanPlat_DiagPath ( name ), "wb" );
}

extern "C" FILE *RanPlat_DiagOpen ( const char *name )
{
    return fopen ( RanPlat_DiagPath ( name ), "rb" );
}

//  A watchdog that turns "the process vanished" into a backtrace.
//
//  A runaway allocation ends as SIGKILL from the kernel, and SIGKILL leaves
//  nothing: no tombstone, no log line, no stack. The loop that did it is the
//  one thing worth knowing and it is the one thing that is never recorded.
//
//  So watch the resident size from a second thread, and when it crosses the
//  line, abort the thread that armed the watchdog - not this one. debuggerd
//  then dumps that thread as the crashing thread, with its full backtrace in
//  the log, while it is still standing in the loop. Needs no root, which
//  matters: neither the tablet nor the emulator has any.
#ifdef __ANDROID__
#include <unistd.h>
#include <signal.h>
#include <sys/syscall.h>

namespace {

pid_t           g_wdTid     = 0;        //  0 = disarmed
int             g_wdLimitMB = 0;
pthread_t       g_wdThread;
bool            g_wdStarted = false;

int ResidentMB ()
{
    FILE *fp = fopen ( "/proc/self/statm", "r" );
    if ( !fp )  return 0;
    long lSize = 0, lResident = 0;
    if ( fscanf ( fp, "%ld %ld", &lSize, &lResident ) != 2 )    lResident = 0;
    fclose ( fp );
    return (int)( ( lResident * 4096LL ) / ( 1024LL * 1024LL ) );
}

void *WatchdogMain ( void * )
{
    for ( ;; )
    {
        usleep ( 50 * 1000 );

        const pid_t tid = g_wdTid;
        if ( !tid )     continue;

        const int mb = ResidentMB ();
        if ( mb < g_wdLimitMB )     continue;

        g_wdTid = 0;
        RanPlat_Log ( RANLOG_ERROR, "RanWatchdog",
            "resident %d MB past the %d MB limit - aborting thread %d for a backtrace",
            mb, g_wdLimitMB, (int) tid );
        syscall ( SYS_tgkill, getpid(), tid, SIGABRT );
    }
    return NULL;
}

}   //  namespace

extern "C" void RanPlat_WatchdogArm ( int limitMB )
{
    if ( limitMB <= 0 )     return;
    g_wdLimitMB = limitMB;
    g_wdTid     = (pid_t) syscall ( SYS_gettid );

    if ( !g_wdStarted )
    {
        g_wdStarted = true;
        pthread_create ( &g_wdThread, NULL, WatchdogMain, NULL );
    }
}

extern "C" void RanPlat_WatchdogDisarm ()
{
    g_wdTid = 0;
}
#else
extern "C" void RanPlat_WatchdogArm ( int )     {}
extern "C" void RanPlat_WatchdogDisarm ()       {}
#endif

//  ===========================================================================
//  Crash reports.
//
//  "The game crashed on my phone" used to be the end of the trail: the log is
//  in logcat (Android) or nowhere (iOS), it is gone by the time anyone asks,
//  and the phone is not on this desk. So the client records its own last run
//  and sends it home.
//
//  The record is a memory-mapped file, lastrun.bin, in the diagnostic root:
//
//      [ 64-byte header | crash text, up to 16 KB | log ring, 128 KB ]
//
//  Mapped, not written: every log line lands in the page cache the moment it
//  is copied, and the kernel writes those pages out even if the process is
//  then SIGKILLed - by the low-memory killer, by iOS's jetsam - which leaves
//  no signal to catch and would otherwise leave nothing at all. A crash signal
//  adds a backtrace to the crash area with nothing but memory stores (no
//  allocation, no stdio), which is what a signal handler may safely do.
//
//  The header carries the run's state: FOREGROUND while the app is on screen,
//  BACKGROUND when it is not, CRASHED from the signal handler, CLEAN on an
//  orderly exit. The next boot reads it before re-arming. CRASHED is a crash;
//  FOREGROUND means the process died while the player was looking at it,
//  without a signal - in practice the system killing it for memory. A death in
//  the background is the player swiping it away, and is not reported.
//
//  The report goes to crash_pending/<time>.txt, as text; each platform layer
//  uploads that folder (RanPlat_UploadCrashReports) and deletes what the
//  server accepted. Built here, once, so Android and iOS cannot disagree about
//  what a report says.
//  ===========================================================================
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <dlfcn.h>
#include <dirent.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#if defined(__ANDROID__)
#include <unwind.h>
#include <link.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/system_properties.h>
#elif defined(__APPLE__)
#include <execinfo.h>
#include <sys/sysctl.h>
#endif

namespace {

enum { RUN_NONE = 0, RUN_FOREGROUND = 1, RUN_BACKGROUND = 2, RUN_CRASHED = 3, RUN_CLEAN = 4 };

const uint32_t kHdrBytes  = 64;
const uint32_t kHeadBytes = 16384;                  //  header + crash text
const uint32_t kCrashMax  = kHeadBytes - kHdrBytes;
const uint32_t kRingBytes = 131072;                 //  power of two: the index wraps by mask
const uint32_t kRunBytes  = kHeadBytes + kRingBytes;
const char     kRunMagic[8] = { 'R','A','N','R','U','N','0','1' };

struct RunHdr {
    char              magic[8];
    volatile uint32_t state;
    volatile uint32_t logPos;       //  bytes ever written to the ring
    uint32_t          ringBytes;
    volatile uint32_t crashLen;
    int64_t           startUnix;
    char              build[32];    //  .patchver: the patch the run was on
};
static_assert ( sizeof(RunHdr) == 64, "lastrun.bin header is 64 bytes" );

//  Until Begin maps the file the ring is plain memory, and Begin copies what
//  it holds across - so the boot's own lines are in the report too.
char               g_memRing[kRingBytes];
uint32_t           g_memPos   = 0;
char              *g_ring     = g_memRing;
volatile uint32_t *g_ringPos  = &g_memPos;
RunHdr            *g_run      = NULL;
pthread_mutex_t    g_ringLock = PTHREAD_MUTEX_INITIALIZER;
struct timespec    g_t0;
int                g_t0Set    = 0;

void RingPut ( const char *s, uint32_t n )
{
    uint32_t pos = *g_ringPos;
    while ( n ) {
        const uint32_t at    = pos & ( kRingBytes - 1 );
        uint32_t       chunk = kRingBytes - at;
        if ( chunk > n ) chunk = n;
        memcpy ( g_ring + at, s, chunk );
        s += chunk; n -= chunk; pos += chunk;
    }
    *g_ringPos = pos;
}

//  --- signal-safe writers into the crash area -------------------------------

void CW ( const char *s )
{
    if ( !g_run || !s ) return;
    char *dst = (char *) g_run + kHdrBytes;
    uint32_t len = g_run->crashLen;
    while ( *s && len < kCrashMax ) dst[len++] = *s++;
    g_run->crashLen = len;          //  per call, so a second fault keeps the first half
}

void CWHex ( uintptr_t v )
{
    char b[2 + 16 + 1];
    char *p = b + sizeof(b) - 1;
    *p = 0;
    do { *--p = "0123456789abcdef"[v & 15]; v >>= 4; } while ( v );
    *--p = 'x'; *--p = '0';
    CW ( p );
}

void CWDec ( long v )
{
    char b[24];
    char *p = b + sizeof(b) - 1;
    *p = 0;
    const int neg = v < 0;
    unsigned long u = neg ? (unsigned long)( -v ) : (unsigned long) v;
    do { *--p = (char)( '0' + u % 10 ); u /= 10; } while ( u );
    if ( neg ) *--p = '-';
    CW ( p );
}

//  "#03 pc 0x7a1b2c3d  libran.so+0x123456 (Symbol+0x40)". The library offset
//  is the part that matters: the shipped libran.so is stripped, and the offset
//  is what llvm-symbolizer turns back into a file and line against the
//  libran.debug archived for this build (tools/crash/symbolize.sh).
void CWFrame ( int i, uintptr_t pc )
{
    CW ( "  #" ); if ( i < 10 ) CW ( "0" ); CWDec ( i );
    CW ( " pc " ); CWHex ( pc );
    Dl_info di;
    if ( pc && dladdr ( (void *) pc, &di ) && di.dli_fname ) {
        const char *base = strrchr ( di.dli_fname, '/' );
        CW ( "  " ); CW ( base ? base + 1 : di.dli_fname );
        CW ( "+" ); CWHex ( pc - (uintptr_t) di.dli_fbase );
        if ( di.dli_sname ) {
            CW ( " (" ); CW ( di.dli_sname );
            CW ( "+" ); CWHex ( pc - (uintptr_t) di.dli_saddr ); CW ( ")" );
        }
    }
    CW ( "\n" );
}

const char *SigName ( int sig )
{
    switch ( sig ) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS:  return "SIGBUS";
    case SIGFPE:  return "SIGFPE";
    case SIGILL:  return "SIGILL";
    case SIGABRT: return "SIGABRT";
    case SIGTRAP: return "SIGTRAP";
    case SIGSYS:  return "SIGSYS";
    default:      return "signal";
    }
}

#if defined(__ANDROID__)
struct UnwindState { uintptr_t *pcs; int n, max; };

_Unwind_Reason_Code UnwindStep ( struct _Unwind_Context *ctx, void *arg )
{
    UnwindState *s = (UnwindState *) arg;
    const uintptr_t ip = (uintptr_t) _Unwind_GetIP ( ctx );
    if ( ip && s->n < s->max ) s->pcs[s->n++] = ip;
    return s->n < s->max ? _URC_NO_REASON : _URC_END_OF_STACK;
}

#if defined(__aarch64__)
//  A read that cannot fault. Walking frame pointers means trusting values
//  off a stack that may be the very thing that broke; process_vm_readv on
//  our own pid answers EFAULT for a bad address instead of a second SIGSEGV
//  inside the handler, which would lose the report.
int SafeRead ( uintptr_t addr, void *out, size_t n )
{
    struct iovec local  = { out, n };
    struct iovec remote = { (void *) addr, n };
    return syscall ( SYS_process_vm_readv, getpid(), &local, 1, &remote, 1, 0 ) == (long) n;
}
#endif
#endif

const int kCrashSignals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP, SIGSYS };
struct sigaction g_oldAct[32];
volatile int     g_inCrash = 0;

void OnCrashSignal ( int sig, siginfo_t *si, void *ucv )
{
    if ( __sync_lock_test_and_set ( &g_inCrash, 1 ) == 0 && g_run ) {
        //  First, so that whatever goes wrong below, the next boot knows.
        g_run->state = RUN_CRASHED;

        CW ( "signal " ); CWDec ( sig ); CW ( " (" ); CW ( SigName ( sig ) ); CW ( ")" );
        CW ( " code " ); CWDec ( si ? si->si_code : 0 );
        CW ( " fault addr " ); CWHex ( si ? (uintptr_t) si->si_addr : 0 ); CW ( "\n" );

        char tname[64] = "";
#if defined(__ANDROID__)
        prctl ( PR_GET_NAME, (unsigned long) tname, 0, 0, 0 );
        CW ( "thread " ); CWDec ( (long) syscall ( SYS_gettid ) );
#else
        pthread_getname_np ( pthread_self(), tname, sizeof(tname) );
        CW ( "thread" );
#endif
        CW ( " \"" ); CW ( tname ); CW ( "\"\n" );

        //  Registers at the fault, straight from the signal frame: correct even
        //  when the unwinder below cannot get past the trampoline.
        uintptr_t pc = 0, lr = 0, sp = 0, fp = 0;
#if defined(__ANDROID__) && defined(__aarch64__)
        const ucontext_t *uc = (const ucontext_t *) ucv;
        pc = uc->uc_mcontext.pc;
        lr = uc->uc_mcontext.regs[30];
        fp = uc->uc_mcontext.regs[29];
        sp = uc->uc_mcontext.sp;
#elif defined(__ANDROID__) && defined(__x86_64__)
        const ucontext_t *uc = (const ucontext_t *) ucv;
        pc = uc->uc_mcontext.gregs[REG_RIP];
        fp = uc->uc_mcontext.gregs[REG_RBP];
        sp = uc->uc_mcontext.gregs[REG_RSP];
#else
        (void) ucv;
#endif
        if ( pc ) {
            CW ( "registers: sp " ); CWHex ( sp ); CW ( " fp " ); CWHex ( fp ); CW ( "\n" );
            CW ( "fault frame:\n" );
            CWFrame ( 0, pc );
            if ( lr ) CWFrame ( 1, lr );
        }

        uintptr_t pcs[64];
        int n = 0;
#if defined(__ANDROID__)
        UnwindState st = { pcs, 0, 64 };
        _Unwind_Backtrace ( UnwindStep, &st );
        n = st.n;
#elif defined(__APPLE__)
        n = backtrace ( (void **) pcs, 64 );
#endif
        CW ( "backtrace:\n" );
        for ( int i = 0; i < n; ++i ) CWFrame ( i, pcs[i] );

#if defined(__ANDROID__) && defined(__aarch64__)
        //  The frame-pointer chain from the fault, as a second opinion: arm64
        //  keeps x29 as a frame pointer, so this reaches the faulting code's
        //  callers even if the unwinder stopped at the signal frame.
        if ( fp ) {
            CW ( "frame chain:\n" );
            uintptr_t cur = fp;
            for ( int i = 0; i < 48 && cur && ( cur & 15 ) == 0 && cur >= sp; ++i ) {
                uintptr_t rec[2];                       //  { previous fp, return address }
                if ( !SafeRead ( cur, rec, sizeof(rec) ) ) break;
                if ( rec[1] ) CWFrame ( i, rec[1] );
                if ( rec[0] <= cur || rec[0] - cur > 1024 * 1024 ) break;
                cur = rec[0];
            }
        }
#endif
    }

    //  Hand the signal on: debuggerd's tombstone on Android, the system crash
    //  report on iOS. A fault re-raises itself when the instruction runs
    //  again; a sent signal (abort, kill) has to be sent again.
    sigaction ( sig, &g_oldAct[sig], NULL );
    if ( !si || si->si_code <= 0 ) raise ( sig );
}

void InstallCrashHandlers ()
{
    //  Its own stack, so a stack overflow can still be reported. Only the
    //  thread that calls Begin gets one - the game thread, which is the one
    //  that recurses.
    static char s_altStack[64 * 1024];
    stack_t ss;
    memset ( &ss, 0, sizeof(ss) );
    ss.ss_sp    = s_altStack;
    ss.ss_size  = sizeof(s_altStack);
    sigaltstack ( &ss, NULL );

    struct sigaction sa;
    memset ( &sa, 0, sizeof(sa) );
    sa.sa_sigaction = OnCrashSignal;
    sa.sa_flags     = SA_SIGINFO | SA_ONSTACK;
    sigemptyset ( &sa.sa_mask );
    for ( size_t i = 0; i < sizeof(kCrashSignals) / sizeof(kCrashSignals[0]); ++i )
        sigaction ( kCrashSignals[i], &sa, &g_oldAct[kCrashSignals[i]] );
}

//  --- the report, built on the next boot (ordinary code from here on) --------

struct Text {
    char  *p;
    size_t n, cap;
    void add ( const char *s, size_t len ) {
        if ( n + len + 1 > cap ) {
            size_t c = cap ? cap : 4096;
            while ( c < n + len + 1 ) c *= 2;
            char *q = (char *) realloc ( p, c );
            if ( !q ) return;
            p = q; cap = c;
        }
        memcpy ( p + n, s, len ); n += len; p[n] = 0;
    }
    void add ( const char *s ) { add ( s, strlen ( s ) ); }
    void addf ( const char *fmt, ... ) {
        char b[512];
        va_list ap; va_start ( ap, fmt );
        const int k = vsnprintf ( b, sizeof(b), fmt, ap );
        va_end ( ap );
        if ( k > 0 ) add ( b, (size_t)( k < (int) sizeof(b) ? k : (int) sizeof(b) - 1 ) );
    }
};

void ReadSmallFile ( const char *path, char *out, size_t cap )
{
    out[0] = 0;
    const int fd = open ( path, O_RDONLY | O_CLOEXEC );
    if ( fd < 0 ) return;
    const ssize_t k = read ( fd, out, cap - 1 );
    close ( fd );
    if ( k <= 0 ) return;
    out[k] = 0;
    for ( ssize_t i = 0; i < k; ++i )
        if ( out[i] == '\r' || out[i] == '\n' ) { out[i] = 0; break; }
}

void AddDevice ( Text &t )
{
#if defined(__ANDROID__)
    char man[PROP_VALUE_MAX] = "", model[PROP_VALUE_MAX] = "";
    char rel[PROP_VALUE_MAX] = "", sdk[PROP_VALUE_MAX] = "";
    __system_property_get ( "ro.product.manufacturer", man );
    __system_property_get ( "ro.product.model", model );
    __system_property_get ( "ro.build.version.release", rel );
    __system_property_get ( "ro.build.version.sdk", sdk );
#if defined(__aarch64__)
    const char *abi = "arm64-v8a";
#elif defined(__x86_64__)
    const char *abi = "x86_64";
#else
    const char *abi = "other";
#endif
    t.addf ( "platform: android %s (API %s) %s\n", rel, sdk, abi );
    t.addf ( "device: %s %s\n", man, model );
#elif defined(__APPLE__)
    char machine[64] = "", osv[64] = "";
    size_t len = sizeof(machine);
    sysctlbyname ( "hw.machine", machine, &len, NULL, 0 );
    len = sizeof(osv);
    sysctlbyname ( "kern.osproductversion", osv, &len, NULL, 0 );
    t.addf ( "platform: ios %s arm64\n", osv );
    t.addf ( "device: Apple %s\n", machine );
#else
    t.add ( "platform: other\n" );
#endif
}

//  The GNU build-id of the library this code is in. It names the exact build,
//  so build-apk.sh files each libran.debug under it and a report from any old
//  version can still be symbolized.
#if defined(__ANDROID__)
int FindBuildId ( struct dl_phdr_info *info, size_t, void *arg )
{
    const uintptr_t self = (uintptr_t) &FindBuildId;
    int mine = 0;
    for ( int i = 0; i < info->dlpi_phnum; ++i ) {
        const ElfW(Phdr) &ph = info->dlpi_phdr[i];
        if ( ph.p_type != PT_LOAD ) continue;
        const uintptr_t lo = info->dlpi_addr + ph.p_vaddr;
        if ( self >= lo && self < lo + ph.p_memsz ) { mine = 1; break; }
    }
    if ( !mine ) return 0;
    for ( int i = 0; i < info->dlpi_phnum; ++i ) {
        const ElfW(Phdr) &ph = info->dlpi_phdr[i];
        if ( ph.p_type != PT_NOTE ) continue;
        const char *p   = (const char *)( info->dlpi_addr + ph.p_vaddr );
        const char *end = p + ph.p_memsz;
        while ( p + 12 <= end ) {
            const uint32_t nsz = ( (const uint32_t *) p )[0];
            const uint32_t dsz = ( (const uint32_t *) p )[1];
            const uint32_t typ = ( (const uint32_t *) p )[2];
            const char *name = p + 12;
            const char *desc = name + ( ( nsz + 3 ) & ~3u );
            if ( typ == 3 && nsz == 4 && memcmp ( name, "GNU", 4 ) == 0 ) {
                char *out = (char *) arg;
                for ( uint32_t k = 0; k < dsz && k < 32; ++k )
                    snprintf ( out + k * 2, 3, "%02x", (unsigned char) desc[k] );
                return 1;
            }
            p = desc + ( ( dsz + 3 ) & ~3u );
        }
    }
    return 1;
}
#endif

int CompareNames ( const void *a, const void *b )
{
    return strcmp ( (const char *) a, (const char *) b );
}

void WritePending ( const RunHdr &h, const char *crash, uint32_t crashLen,
                    const char *ring, uint32_t ringPos )
{
    Text t = { NULL, 0, 0 };
    t.add ( "RAN LEGACY M crash report\n" );
    t.add ( h.state == RUN_CRASHED
        ? "kind: crash\n"
        : "kind: killed\n"
          "note: closed while on screen with no crash signal - usually the system killing it for memory\n" );
    char build[33];
    memcpy ( build, h.build, 32 ); build[32] = 0;
    t.addf ( "patch: %s\n", build[0] ? build : "?" );
    char bid[65] = "";
#if defined(__ANDROID__)
    dl_iterate_phdr ( FindBuildId, bid );
#endif
    t.addf ( "build-id: %s\n", bid[0] ? bid : "?" );
    AddDevice ( t );
    const time_t start = (time_t) h.startUnix, now = time ( NULL );
    char ts[32] = "", tn[32] = "";
    struct tm tm;
    if ( localtime_r ( &start, &tm ) ) strftime ( ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm );
    if ( localtime_r ( &now,   &tm ) ) strftime ( tn, sizeof(tn), "%Y-%m-%d %H:%M:%S", &tm );
    t.addf ( "run started: %s\nreported: %s\n", ts, tn );

    t.add ( "\n--- crash ---\n" );
    if ( crashLen ) t.add ( crash, crashLen );
    else            t.add ( "(no crash signal recorded)\n" );

    //  Oldest first. After a wrap the first line is cut in half, so it goes.
    t.add ( "\n--- last log (newest at the bottom) ---\n" );
    const uint32_t have = ringPos < kRingBytes ? ringPos : kRingBytes;
    const uint32_t from = ringPos - have;
    Text body = { NULL, 0, 0 };
    for ( uint32_t k = 0; k < have; ) {
        const uint32_t at = ( from + k ) & ( kRingBytes - 1 );
        uint32_t chunk = kRingBytes - at;
        if ( chunk > have - k ) chunk = have - k;
        body.add ( ring + at, chunk );
        k += chunk;
    }
    if ( body.p ) {
        const char *s = body.p;
        if ( ringPos > kRingBytes ) {
            const char *nl = strchr ( s, '\n' );
            s = nl ? nl + 1 : s;
        }
        t.add ( s );
        free ( body.p );
    }
    if ( !t.p ) return;

    char dir[640];
    snprintf ( dir, sizeof(dir), "%s", RanPlat_DiagPath ( "crash_pending" ) );
    //  Group-readable: adb's shell user is in the data group, so a report
    //  can be pulled off a test device. Other apps cannot reach Android/data.
    mkdir ( dir, 0770 );
    chmod ( dir, 0770 );

    //  Ten at most. A phone that crashes on every launch and never reaches the
    //  server should not fill up with copies of one report.
    {
        static char names[64][64];
        int count = 0;
        if ( DIR *d = opendir ( dir ) ) {
            while ( struct dirent *e = readdir ( d ) ) {
                if ( e->d_name[0] == '.' || count >= 64 ) continue;
                snprintf ( names[count++], sizeof(names[0]), "%s", e->d_name );
            }
            closedir ( d );
        }
        qsort ( names, count, sizeof(names[0]), CompareNames );
        for ( int i = 0; i + 9 < count; ++i ) {
            char p[720];
            snprintf ( p, sizeof(p), "%s/%s", dir, names[i] );
            unlink ( p );
        }
    }

    char path[720];
    snprintf ( path, sizeof(path), "%s/%lld.txt", dir, (long long) now );
    const int fd = open ( path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0664 );
    if ( fd >= 0 ) {
        fchmod ( fd, 0664 );        //  past the app's umask; the folder still keeps other apps out
        size_t off = 0;
        while ( off < t.n ) {
            const ssize_t w = write ( fd, t.p + off, t.n - off );
            if ( w <= 0 ) break;
            off += (size_t) w;
        }
        close ( fd );
    }
    free ( t.p );
}

void ReportPreviousRun ( const char *path )
{
    const int fd = open ( path, O_RDONLY | O_CLOEXEC );
    if ( fd < 0 ) return;
    char *buf = (char *) malloc ( kRunBytes );
    ssize_t got = 0;
    if ( buf ) {
        while ( got < (ssize_t) kRunBytes ) {
            const ssize_t k = read ( fd, buf + got, kRunBytes - got );
            if ( k <= 0 ) break;
            got += k;
        }
    }
    close ( fd );
    if ( !buf ) return;

    RunHdr h;
    memcpy ( &h, buf, sizeof(h) );
    if ( got == (ssize_t) kRunBytes && memcmp ( h.magic, kRunMagic, 8 ) == 0 &&
         h.ringBytes == kRingBytes &&
         ( h.state == RUN_CRASHED || h.state == RUN_FOREGROUND ) ) {
        const uint32_t cl = h.crashLen < kCrashMax ? h.crashLen : kCrashMax;
        RanPlat_Log ( RANLOG_WARN, "RanCrash", "previous run ended %s - writing a report",
                      h.state == RUN_CRASHED ? "in a crash" : "killed while on screen" );
        WritePending ( h, buf + kHdrBytes, cl, buf + kHeadBytes, h.logPos );
    }
    free ( buf );
}

}   //  namespace

static void RanCrash_LogLine ( int level, const char *tag, const char *fmt, va_list ap )
{
    char line[1100];
    struct timespec now;
    clock_gettime ( CLOCK_MONOTONIC, &now );
    pthread_mutex_lock ( &g_ringLock );
    if ( !g_t0Set ) { g_t0 = now; g_t0Set = 1; }
    const long ms = (long)( ( now.tv_sec - g_t0.tv_sec ) * 1000 +
                            ( now.tv_nsec - g_t0.tv_nsec ) / 1000000 );
    int k = snprintf ( line, sizeof(line), "%5ld.%03ld %c %s: ", ms / 1000, ms % 1000,
                       level == RANLOG_ERROR ? 'E' : level == RANLOG_WARN ? 'W' : 'I',
                       tag ? tag : "Ran" );
    if ( k < 0 ) k = 0;
    if ( k > (int) sizeof(line) - 2 ) k = (int) sizeof(line) - 2;
    const int room = (int) sizeof(line) - k - 1;        //  one kept back for the newline
    const int m = vsnprintf ( line + k, room, fmt, ap );
    if ( m > 0 ) k += m < room ? m : room - 1;
    line[k++] = '\n';
    RingPut ( line, (uint32_t) k );
    pthread_mutex_unlock ( &g_ringLock );
}

extern "C" void RanCrash_Begin ( void )
{
    static int s_done = 0;
    if ( s_done ) return;
    s_done = 1;

    char path[640];
    snprintf ( path, sizeof(path), "%s", RanPlat_DiagPath ( "lastrun.bin" ) );

    ReportPreviousRun ( path );

    const int fd = open ( path, O_RDWR | O_CREAT | O_CLOEXEC, 0664 );
    void *m = MAP_FAILED;
    if ( fd >= 0 ) {
        fchmod ( fd, 0664 );
        if ( ftruncate ( fd, kRunBytes ) == 0 )
            m = mmap ( NULL, kRunBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
        close ( fd );
    }
    if ( m == MAP_FAILED ) {
        RanPlat_Log ( RANLOG_WARN, "RanCrash", "cannot map %s - no crash reports this run", path );
    } else {
        RunHdr *h = (RunHdr *) m;
        memset ( m, 0, kHeadBytes );
        h->ringBytes = kRingBytes;
        h->startUnix = (int64_t) time ( NULL );
        ReadSmallFile ( RanPlat_DiagPath ( ".patchver" ), h->build, sizeof(h->build) );

        //  Move the ring onto the file, oldest line first.
        pthread_mutex_lock ( &g_ringLock );
        char *fileRing = (char *) m + kHeadBytes;
        const uint32_t pos  = g_memPos;
        const uint32_t have = pos < kRingBytes ? pos : kRingBytes;
        for ( uint32_t k = 0; k < have; ++k )
            fileRing[k] = g_memRing[( pos - have + k ) & ( kRingBytes - 1 )];
        h->logPos = have;
        g_ring    = fileRing;
        g_ringPos = &h->logPos;
        pthread_mutex_unlock ( &g_ringLock );

        h->state = RUN_FOREGROUND;
        memcpy ( h->magic, kRunMagic, 8 );      //  last: a half-made header is never read as valid
        g_run = h;
        InstallCrashHandlers ();
        RanPlat_Log ( RANLOG_INFO, "RanCrash", "recorder armed (patch %s)",
                      h->build[0] ? h->build : "?" );
    }

    //  A deliberate fault, to test the whole chain on a device: put a file
    //  named "crashtest" in the diagnostic root (adb push). It is deleted
    //  first, so the next launch boots normally and sends the report.
    if ( g_run && RanPlat_DiagExists ( "crashtest" ) ) {
        unlink ( RanPlat_DiagPath ( "crashtest" ) );
        RanPlat_Log ( RANLOG_WARN, "RanCrash", "crashtest: faulting on purpose" );
        *(volatile int *) (uintptr_t) 8 = 1;
    }

    RanPlat_UploadCrashReports ( RanPlat_DiagPath ( "crash_pending" ) );
}

extern "C" void RanCrash_SetForeground ( int foreground )
{
    if ( g_run && g_run->state != RUN_CRASHED )
        g_run->state = foreground ? RUN_FOREGROUND : RUN_BACKGROUND;
}

extern "C" void RanCrash_CleanExit ( void )
{
    if ( g_run && g_run->state != RUN_CRASHED ) g_run->state = RUN_CLEAN;
}
