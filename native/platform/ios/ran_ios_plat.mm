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

//  Diagnostic flags, dumps and the log file. On Android these are files under
//  /sdcard/ran that adb can touch from outside.
//
//  Documents, not Application Support, and that is the whole point: Documents
//  is the ONLY directory in an iOS container anything outside the app can see,
//  and only because Info.plist sets UIFileSharingEnabled and
//  LSSupportsOpeningDocumentsInPlace. Without that there is no way to put a
//  flag file on the device and no way to get a dump or a log back off it, so
//  every instrument the port has built - audiolog, audiodump, drawlimit,
//  nulldraw, renderscale - would be unreachable on iOS.
//
//  It costs the 4.7 GB argument nothing: the DATA root stays in Application
//  Support, and what lands here is kilobytes of text plus whatever a dump is
//  asked for.
extern "C" const char *RanIOS_DiagRoot(void)
{
    static NSString *cached = nil;
    if (!cached) cached = EnsureDir(NSDocumentDirectory, @"ran");
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
