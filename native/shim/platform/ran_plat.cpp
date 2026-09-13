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

extern "C" void RanPlat_Log ( int level, const char *tag, const char *fmt, ... )
{
    va_list ap;
    va_start ( ap, fmt );
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
