#pragma once
//  The few things that are genuinely per-platform, behind one door.
//
//  Everything else in shim/ is portable C++ that happens to have been written
//  on Android. These are the exceptions: where diagnostic flags live, and where
//  log lines go. Both were spelled out inline in sixty places, which is fine
//  until a second platform exists - "/sdcard/ran/nulldraw" is not a path iOS
//  has, or could have, since nothing outside an app may write into its sandbox.
//
//  So the flags are named, not pathed, and the root is set once at startup.

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

//  Where diagnostic flag files live. Defaults to /sdcard/ran, which is what
//  Android has always used, so nothing changes there. The iOS entry point
//  points this at a directory inside its sandbox.
void        RanPlat_SetDiagRoot ( const char *dir );

//  "<diag root>/<name>", in a rotating buffer - valid until a few more calls.
//  Prefer the two helpers below; they are what almost every use wants.
const char *RanPlat_DiagPath ( const char *name );

//  Is the flag set? (the file exists)
int         RanPlat_DiagExists ( const char *name );

//  Open a flag file for reading, or NULL. The caller closes it.
FILE       *RanPlat_DiagOpen ( const char *name );

#ifdef __cplusplus
}
#endif
