//  iOS side of ran_plat.h: where things live inside the sandbox.
//
//  NOT YET COMPILED. There is no Mac on this machine; this is written against
//  the documented APIs and reviewed by eye. Treat every line as unverified
//  until it has been through clang once.
//
//  Three roots, and none of them can be a string literal the way Android's
//  were, because an iOS container path contains a UUID that changes on every
//  reinstall. They are resolved once at launch and handed to the shim.
#ifdef __APPLE__

#import <Foundation/Foundation.h>
#include "../../shim/platform/ran_plat.h"

static NSString *EnsureDir(NSSearchPathDirectory what, NSString *leaf)
{
    NSArray<NSURL *> *dirs =
        [[NSFileManager defaultManager] URLsForDirectory:what inDomains:NSUserDomainMask];
    if (dirs.count == 0) return nil;

    NSURL *url = leaf.length ? [dirs.firstObject URLByAppendingPathComponent:leaf]
                             : dirs.firstObject;
    NSError *err = nil;
    [[NSFileManager defaultManager] createDirectoryAtURL:url
                             withIntermediateDirectories:YES
                                              attributes:nil
                                                   error:&err];
    return url.path;
}

//  The client data root: Library/Application Support/ran.
//
//  Not Documents - that is user-visible and iCloud-backed, and 4.7 GB going to
//  iCloud is both a bad experience and a documented rejection. Not Caches
//  either: iOS purges that under storage pressure, which would silently throw
//  away the whole download.
//
//  Application Support is still backed up by default, so the directory is
//  marked excluded once, when it is created.
extern "C" const char *RanIOS_DataRoot(void)
{
    static NSString *cached = nil;
    if (cached) return cached.fileSystemRepresentation;

    NSString *path = EnsureDir(NSApplicationSupportDirectory, @"ran");
    if (!path) return "";

    NSURL *url = [NSURL fileURLWithPath:path];
    NSError *err = nil;
    [url setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&err];

    cached = path;
    return cached.fileSystemRepresentation;
}

//  Diagnostic flags. On Android these are files under /sdcard/ran that adb can
//  touch from outside; nothing outside an app may write into an iOS container,
//  so they live in the sandbox and something inside the app has to create them
//  - a debug menu, or a file dropped in through Files.app if the bundle opts
//  into UIFileSharingEnabled.
extern "C" const char *RanIOS_DiagRoot(void)
{
    static NSString *cached = nil;
    if (!cached) cached = EnsureDir(NSApplicationSupportDirectory, @"ran-diag");
    return cached ? cached.fileSystemRepresentation : "";
}

//  Fonts. iOS will not let an app open the system faces as files, so whatever
//  the client needs has to be in the bundle - the Thai face above all, since
//  that is what the UI is actually laid out with.
//
//  Resources/fonts, matching the CMake bundling. RanFont_Resolve reads the
//  directory and picks by filename, so the four faces there are named exactly
//  as the Android system files it was written against:
//  NotoSansThai-Regular/Bold and Roboto-Regular/Bold. Both are redistributable
//  (OFL and Apache 2.0) and the licence texts sit beside them.
extern "C" const char *RanIOS_FontDir(void)
{
    static NSString *cached = nil;
    if (!cached) cached = [[[NSBundle mainBundle] resourcePath]
                            stringByAppendingPathComponent:@"fonts"];
    return cached ? cached.fileSystemRepresentation : "";
}

//  Called once, before anything in the shim runs.
extern "C" void RanIOS_InstallPlatformPaths(void)
{
    RanPlat_SetDiagRoot ( RanIOS_DiagRoot() );
    //  The fallback face has to exist in the bundle under this name.
    RanPlat_SetFontDir ( RanIOS_FontDir(), "NotoSansThai-Regular.ttf" );
}

#endif  //  __APPLE__
