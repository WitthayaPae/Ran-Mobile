//  The patcher's one entry point. See ran_ios_patch.mm.
#pragma once
#ifdef __APPLE__
#import <Foundation/Foundation.h>

//  status/detail may be nil to leave that line as it was; permille is -1 for
//  an indeterminate bar. Both blocks are called off the main thread.
typedef void (^RanPatchProgress) ( NSString *status, NSString *detail, int permille );
//  fatal: this run cannot succeed by trying again (the app itself is too old),
//  so the page says so and stops. Every other failure is worth a retry - the
//  same split RanLauncher.java makes between fail() and its retry loop.
typedef void (^RanPatchDone) ( BOOL ok, BOOL fatal, NSString *error );

#ifdef __cplusplus
extern "C" {
#endif
void RanIOS_RunPatch ( RanPatchProgress say, RanPatchDone done );
#ifdef __cplusplus
}
#endif

#endif  //  __APPLE__
