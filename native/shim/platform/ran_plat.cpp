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
    fprintf ( stderr, "%s %s: ",
              level == RANLOG_ERROR ? "E" : level == RANLOG_WARN ? "W" : "I",
              tag ? tag : "Ran" );
    vfprintf ( stderr, fmt, ap );
    fputc ( 0x0A, stderr );
#endif
    va_end ( ap );
}

extern "C" void RanPlat_SetDiagRoot ( const char *dir )
{
    if ( !dir || !*dir ) return;
    snprintf ( g_root, sizeof(g_root), "%s", dir );
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
