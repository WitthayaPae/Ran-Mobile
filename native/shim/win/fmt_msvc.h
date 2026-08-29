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
    //  Any %I form, not just %I64.
    //
    //  This used to test for "I64" alone, which skipped the money pickup line -
    //  its text is "%Id", the pointer-sized spelling - so bionic printed the
    //  conversion literally and it read "collected Id coins".
    //
    //  Over-triggering costs one string copy and nothing else: the rewriter
    //  passes anything it does not recognise through unchanged.
    return fmt && strchr(fmt, '%') != NULL && strchr(fmt, 'I') != NULL;
}

//  %I64 -> %ll, %I32 -> % (a plain int), and a bare %I -> %ll, leaving flags,
//  width and precision where they are. Everything else is copied untouched.
//
//  A bare %I is MSVC for "pointer-sized": 32 bits in the PC build, 64 here. That
//  is not a like-for-like translation, but the one string that uses it is handed
//  a LONGLONG, so 64 is what the argument actually is - reading it as 32 would
//  print half a number and leave the rest of the arguments misaligned.
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
        else if (p[0] == 'I')                               { out += "ll"; p += 1; }
        //  The conversion character itself is copied by the next iteration.
    }
    return out;
}
