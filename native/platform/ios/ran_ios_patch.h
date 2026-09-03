//  The patcher's one entry point. See ran_ios_patch.mm.
#pragma once
#ifdef __APPLE__
#import <Foundation/Foundation.h>

//  status/detail may be nil to leave that line as it was; permille is -1 for
//  an indeterminate bar. Both blocks are called off the main thread.
typedef void (^RanPatchProgress) ( NSString *status, NSString *detail, int permille );
typedef void (^RanPatchDone) ( BOOL ok, NSString *error );

#ifdef __cplusplus
extern "C" {
#endif
void RanIOS_RunPatch ( RanPatchProgress say, RanPatchDone done );
#ifdef __cplusplus
}
#endif

#endif  //  __APPLE__
