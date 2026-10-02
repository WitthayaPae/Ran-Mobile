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
#import <UIKit/UIKit.h>

//  Open a link in Safari (or whatever the phone's default browser is).
//
//  openURL:options:completionHandler: rather than the deprecated openURL: - the
//  old one is gone in the SDKs this builds against. Dispatched to the main queue
//  because UIApplication is main-thread only and this is called from the game
//  thread.
extern "C" void RanPlat_OpenURL ( const char *url )
{
    if ( !url || !*url )	return;

    NSString *s = [NSString stringWithUTF8String:url];
    NSURL *u = s ? [NSURL URLWithString:s] : nil;
    if ( !u )	return;

    dispatch_async ( dispatch_get_main_queue(), ^{
        [[UIApplication sharedApplication] openURL:u options:@{} completionHandler:nil];
    } );
}
#include "../../shim/platform/ran_plat.h"
#include <os/proc.h>

//  What is left of this app's own memory limit - the number iOS kills at 0.
//  The texture budget tightens when it is low (d3d9_impl.cpp texBudgetPass).
extern "C" int RanPlat_MemHeadroomMB ( void )
{
    if ( @available(iOS 13.0, *) )
        return (int)( os_proc_available_memory() / 1048576 );
    return -1;
}

//  The game's Exit (WM_CLOSE, shim win_impl.cpp) - the same job as
//  RanActivity.ranQuit on Android: silence, then end the process. iOS has no
//  "close the app" call; exit(0) is what is left, on the main queue so UIKit
//  is not torn down under a frame in progress.
extern "C" void RanAudioSink_Pause ( int paused );

extern "C" void RanPlat_Quit ( void )
{
    RanAudioSink_Pause ( 1 );
    dispatch_async ( dispatch_get_main_queue(), ^{ exit ( 0 ); } );
}

//  Send crash_pending/*.txt home (native: RanCrash_Begin).
//
//  The same job as RanActivity.ranUploadCrashReports on Android, with the same
//  rules: same URL and headers, oldest first, delete on HTTP 200, stop at the
//  first failure and leave the rest for the next launch. Off the main thread
//  and off the game thread - boot must not wait on a network.
static NSString *const kCrashURL = @"https://ran-legacy-m.com/crash/upload.php";

extern "C" void RanPlat_UploadCrashReports ( const char *dir )
{
    if ( !dir || !*dir )    return;
    NSString *d = [NSString stringWithUTF8String:dir];
    if ( !d )   return;
    NSString *app = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleVersion"] ?: @"?";

    dispatch_async ( dispatch_get_global_queue ( QOS_CLASS_UTILITY, 0 ), ^{
        NSFileManager *fm = NSFileManager.defaultManager;
        NSArray<NSString *> *names =
            [[fm contentsOfDirectoryAtPath:d error:nil] sortedArrayUsingSelector:@selector(compare:)];
        if ( names.count == 0 )  return;

        NSURLSession *session =
            [NSURLSession sessionWithConfiguration:NSURLSessionConfiguration.ephemeralSessionConfiguration];
        for ( NSString *n in names ) {
            if ( ![n hasSuffix:@".txt"] )   continue;
            NSString *path = [d stringByAppendingPathComponent:n];
            NSData *body = [NSData dataWithContentsOfFile:path];
            if ( !body )    continue;
            if ( body.length > 600 * 1024 ) body = [body subdataWithRange:NSMakeRange ( 0, 600 * 1024 )];

            NSMutableURLRequest *r =
                [NSMutableURLRequest requestWithURL:[NSURL URLWithString:kCrashURL]
                                        cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                    timeoutInterval:20];
            r.HTTPMethod = @"POST";
            [r setValue:@"text/plain; charset=utf-8" forHTTPHeaderField:@"Content-Type"];
            [r setValue:@"1" forHTTPHeaderField:@"X-Ran-Crash"];
            [r setValue:app forHTTPHeaderField:@"X-Ran-App"];

            __block NSInteger code = 0;
            dispatch_semaphore_t done = dispatch_semaphore_create ( 0 );
            [[session uploadTaskWithRequest:r fromData:body
                          completionHandler:^( NSData *data, NSURLResponse *resp, NSError *err ) {
                if ( !err && [resp isKindOfClass:NSHTTPURLResponse.class] )
                    code = ((NSHTTPURLResponse *) resp).statusCode;
                dispatch_semaphore_signal ( done );
            }] resume];
            dispatch_semaphore_wait ( done, dispatch_time ( DISPATCH_TIME_NOW, 40 * NSEC_PER_SEC ) );

            RanPlat_Log ( RANLOG_INFO, "RanCrash", "sent %s -> HTTP %ld", n.UTF8String, (long) code );
            if ( code != 200 )  break;
            [fm removeItemAtPath:path error:nil];
        }
        [session finishTasksAndInvalidate];
    } );
}

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

    //  WITH a trailing slash, because that is the contract the engine was
    //  built against and Android has always met it:
    //
    //      pickDataRoot():  snprintf(chosen, n, "%s/", candidate)
    //      ran_app.cpp:276: std::string(g_appPath) + "Data/Map/Map.rcc"
    //
    //  Without it that concatenation produced ".../Application Support/ranData/
    //  Map/Map.rcc" - one missing separator - so the 574 MB archive never
    //  opened, the client logged "engine data: loose files" and fell back to
    //  loose files that do not exist, because every .wld and .chf lives inside
    //  that archive. The login scene came up with 0 leaf nodes, nothing was
    //  submitted to the device, and the phone showed a black screen at a
    //  steady 60 fps. Paths built with an explicit separator, like
    //  RANPARAM::LOAD's "ran\param.ini", were unaffected, which is why the
    //  boot got as far as it did.
    cached = [path hasSuffix:@"/"] ? path : [path stringByAppendingString:@"/"];
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
