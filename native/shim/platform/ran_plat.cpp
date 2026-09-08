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

extern "C" int RanPlat_DiagExists ( const char *name )
{
    return access ( RanPlat_DiagPath ( name ), F_OK ) == 0 ? 1 : 0;
}

extern "C" FILE *RanPlat_DiagOpenWrite ( const char *name )
{
    return fopen ( RanPlat_DiagPath ( name ), "wb" );
}

extern "C" FILE *RanPlat_DiagOpen ( const char *name )
{
    return fopen ( RanPlat_DiagPath ( name ), "rb" );
}
