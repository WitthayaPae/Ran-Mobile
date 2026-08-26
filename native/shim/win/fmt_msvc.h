#pragma once
//  MSVC's integer size prefixes, rewritten for bionic.
//
//  The client is full of "%I64d" - the Microsoft spelling of a 64-bit
//  conversion. Android's libc does not know %I: it drops the conversion and
//  prints the rest of it literally, so the character window showed "I64d" where
//  the experience total should be, and every 64-bit number in the UI was simply
//  missing. Nothing warns about this; the string just comes out wrong.
//
//  Rewriting the format once, at the few places that own formatting, keeps the
//  158 call sites in SOURCE identical to the PC build.
#include <string>
#include <string.h>

inline bool RanFmt_NeedsRewrite(const char *fmt) {
    return fmt && strstr(fmt, "I64") != NULL;
}

//  %I64 -> %ll and %I32 -> % (a plain int), leaving flags, width and precision
//  where they are. Everything else is copied untouched.
inline std::string RanFmt_MsvcToPosix(const char *fmt) {
    std::string out;
    if (!fmt) return out;
    for (const char *p = fmt; *p; ) {
        if (*p != '%') { out += *p++; continue; }
        out += *p++;                        // the '%'
        if (*p == '%') { out += *p++; continue; }
        //  Flags, then width, then precision - the size prefix comes after them.
        while (*p && strchr("-+ #0", *p)) out += *p++;
        while (*p >= '0' && *p <= '9') out += *p++;
        if (*p == '.') { out += *p++; while (*p >= '0' && *p <= '9') out += *p++; }
        if (p[0] == 'I' && p[1] == '6' && p[2] == '4')      { out += "ll"; p += 3; }
        else if (p[0] == 'I' && p[1] == '3' && p[2] == '2') { p += 3; }
        //  The conversion character itself is copied by the next iteration.
    }
    return out;
}
