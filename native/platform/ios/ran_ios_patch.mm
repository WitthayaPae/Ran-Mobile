//  The patcher, in UIKit. What RanLauncher.java is on Android.
//
//  Deliberately the same algorithm, step for step, against the same store and
//  the same signed manifest — the server does not know which client is asking.
//  Where it differs from the Java it is because the platform forces it:
//
//    * there is no APK offer. iOS cannot install a binary over itself, so a
//      build too old for the server is a hard stop with an App Store / TestFlight
//      message instead of a download.
//    * `minApk` is an Android versionCode and means nothing here; the gate is
//      `minIos` when the manifest carries one. Until the manifest grows that
//      key an iOS build must not be pointed at the live server — see
//      MOBILE/IOS-PORT-PLAN.md.
//
//  NOT YET COMPILED — there is no Mac on this machine.
#ifdef __APPLE__

#import <Foundation/Foundation.h>
#import <Security/Security.h>
#import <CommonCrypto/CommonDigest.h>

#include "../../shim/platform/ran_plat.h"

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanPatch", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanPatch", __VA_ARGS__)

extern "C" const char *RanIOS_DataRoot ( void );

//  RanPatchProgress / RanPatchDone: a headline, a detail line and a permille
//  for the bar, straight out of the Java.
#include "ran_ios_patch.h"

//  The same key the Android launcher pins, and the same reasoning: every blob
//  is verified against a hash out of the manifest, so whoever writes the
//  manifest decides what lands on the device. Over plain HTTP that would be
//  anyone on the network path. An attacker who cannot sign cannot publish.
//
//  P-256 public key, X.509 SubjectPublicKeyInfo, base64.
static NSString *const kManifestPubKey =
    @"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE/kqyu7XQLuP/WlBSpgnfKrN91qevUOtyVEMA3nL6hMX+lBTv9K7PHs/tQ1t1BZgpb9ugHasRVkTOk8b1F93jUQ==";

static NSString *const kBaseDefault = @"https://ran-legacy-m.com/launcher_mobile/";

static NSString *const kVerFile   = @".patchver";
static NSString *const kIndexFile = @".patchindex";

// ------------------------------------------------------------------ helpers

//  Streamed, not read whole: the biggest file in the store is the APK blob at
//  340 MB and this runs on a phone.
static NSString *Sha256OfFile ( NSString *path )
{
    NSFileHandle *fh = [NSFileHandle fileHandleForReadingAtPath:path];
    if (!fh) return nil;
    CC_SHA256_CTX ctx;
    CC_SHA256_Init ( &ctx );
    for (;;) {
        @autoreleasepool {
            NSData *chunk = [fh readDataOfLength:1 << 20];
            if (chunk.length == 0) break;
            CC_SHA256_Update ( &ctx, chunk.bytes, (CC_LONG)chunk.length );
        }
    }
    [fh closeFile];
    unsigned char out[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final ( out, &ctx );
    NSMutableString *s = [NSMutableString stringWithCapacity:64];
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; ++i) [s appendFormat:@"%02x", out[i]];
    return s;
}

//  Fails closed: a missing, malformed or wrong signature is a hard stop, never
//  a warning. A check that can be skipped by deleting a file is not a check.
static BOOL VerifyManifest ( NSData *body, NSData *sigText, NSString **err )
{
    NSString *b64 = [[[NSString alloc] initWithData:sigText encoding:NSUTF8StringEncoding]
                        stringByTrimmingCharactersInSet:
                            NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSData *der = b64 ? [[NSData alloc] initWithBase64EncodedString:b64 options:0] : nil;
    if (!der.length) { *err = @"manifest signature is not valid base64"; return NO; }

    NSData *spki = [[NSData alloc] initWithBase64EncodedString:kManifestPubKey options:0];
    //  SecKeyCreateWithData wants the raw X9.63 point, not the SubjectPublicKeyInfo
    //  wrapper that Java's X509EncodedKeySpec takes. For P-256 that point is the
    //  trailing 65 bytes (0x04 || X || Y), and it must actually start with 0x04.
    if (spki.length < 65) { *err = @"pinned key is malformed"; return NO; }
    NSData *point = [spki subdataWithRange:NSMakeRange(spki.length - 65, 65)];
    if (((const uint8_t *)point.bytes)[0] != 0x04) {
        *err = @"pinned key is not an uncompressed P-256 point";
        return NO;
    }

    NSDictionary *attrs = @{ (id)kSecAttrKeyType   : (id)kSecAttrKeyTypeECSECPrimeRandom,
                             (id)kSecAttrKeyClass  : (id)kSecAttrKeyClassPublic,
                             (id)kSecAttrKeySizeInBits : @256 };
    CFErrorRef cfe = NULL;
    SecKeyRef key = SecKeyCreateWithData ( (__bridge CFDataRef)point,
                                           (__bridge CFDictionaryRef)attrs, &cfe );
    if (!key) {
        if (cfe) CFRelease ( cfe );
        *err = @"pinned key could not be loaded";
        return NO;
    }

    //  X962 + SHA256 over the message: the exact pair Java's "SHA256withECDSA"
    //  produces, DER-encoded signature included.
    const BOOL ok = SecKeyVerifySignature ( key,
                        kSecKeyAlgorithmECDSASignatureMessageX962SHA256,
                        (__bridge CFDataRef)body, (__bridge CFDataRef)der, &cfe );
    CFRelease ( key );
    if (cfe) CFRelease ( cfe );
    if (!ok) { *err = @"manifest signature does not verify - refusing this update"; return NO; }
    return YES;
}

//  Where a manifest entry is allowed to land. "path" comes off the network and
//  is used directly as a destination, so an entry of "../../../x" would write
//  outside the data root — and the download path deletes the destination before
//  renaming over it, so a hostile manifest could remove files as well as create
//  them. One place decides, and both passes use it.
static NSString *SafeDest ( NSString *root, NSString *rel, NSString **err )
{
    if (rel.length == 0) { *err = @"empty path in manifest"; return nil; }
    const unichar first = [rel characterAtIndex:0];
    if (first == '/' || first == '\\') { *err = [@"absolute path in manifest: " stringByAppendingString:rel]; return nil; }
    if (rel.length > 1 && [rel characterAtIndex:1] == ':') { *err = [@"drive-qualified path in manifest: " stringByAppendingString:rel]; return nil; }
    if ([rel rangeOfString:@"\\"].location != NSNotFound) { *err = [@"backslash in manifest path: " stringByAppendingString:rel]; return nil; }
    for (NSString *seg in [rel componentsSeparatedByString:@"/"])
        if ([seg isEqualToString:@".."]) { *err = [@"path escapes the data root: " stringByAppendingString:rel]; return nil; }

    //  Belt and braces: symlinks, and anything the checks above did not
    //  anticipate, still have to resolve to somewhere under the root.
    NSString *full = [[root stringByAppendingPathComponent:rel] stringByStandardizingPath];
    NSString *base = [root stringByStandardizingPath];
    if (![full isEqualToString:base] &&
        ![full hasPrefix:[base stringByAppendingString:@"/"]]) {
        *err = [@"path escapes the data root: " stringByAppendingString:rel];
        return nil;
    }
    return full;
}

// ------------------------------------------------------------------- fetch

//  No HTTP cache, ever.
//
//  NSURLSession.sharedSession caches through NSURLCache, and the server sends
//  Last-Modified with no Cache-Control, so a response may be reused on a
//  heuristic freshness guess. The two files are not treated alike: the 97-byte
//  manifest.sig fits in the cache, while the 3.6 MB manifest.json is over the
//  per-entry limit and is always fetched fresh. After a publish the phone
//  paired the new manifest with the previous signature and refused it:
//  "manifest signature does not verify". An ephemeral session with no cache
//  and a reload policy fetches both from the network every time.
//  How many files download at once - the same number as the Android launcher
//  (DL_THREADS in RanLauncher.java). A fresh install is 23,368 files, and for
//  most of them the time is the round trip, not the bytes: one at a time was
//  the whole reason the iPhone patched far slower than Android.
static const int kDlThreads = 8;

static NSURLSession *PatchSession ( void )
{
    static NSURLSession *s;
    static dispatch_once_t once;
    dispatch_once ( &once, ^{
        NSURLSessionConfiguration *c = NSURLSessionConfiguration.ephemeralSessionConfiguration;
        c.URLCache = nil;
        //  iOS allows 4 connections per host by default; with 8 workers the
        //  other 4 would queue behind them instead of downloading.
        c.HTTPMaximumConnectionsPerHost = kDlThreads;
        c.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        s = [NSURLSession sessionWithConfiguration:c];
    });
    return s;
}

static NSURLRequest *FreshRequest ( NSString *url )
{
    NSMutableURLRequest *q = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:url]];
    q.cachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
    //  For anything in between (a proxy, the CDN) as well as for this device.
    [q setValue:@"no-cache" forHTTPHeaderField:@"Cache-Control"];
    return q;
}

//  Synchronous by design: this whole patcher runs on its own thread, exactly as
//  the Java one does, and a state machine would buy nothing.
static NSData *HttpGet ( NSString *url, NSString **err )
{
    __block NSData *out = nil;
    __block NSString *fail = nil;
    dispatch_semaphore_t sem = dispatch_semaphore_create ( 0 );

    NSURLSessionDataTask *t = [PatchSession()
        dataTaskWithRequest:FreshRequest ( url )
        completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
            const long code = [r isKindOfClass:NSHTTPURLResponse.class]
                            ? (long)((NSHTTPURLResponse *)r).statusCode : 0;
            if (e) fail = e.localizedDescription;
            else if (code != 200) fail = [NSString stringWithFormat:@"HTTP %ld", code];
            else out = d;
            dispatch_semaphore_signal ( sem );
        }];
    [t resume];
    dispatch_semaphore_wait ( sem, DISPATCH_TIME_FOREVER );
    if (!out) *err = fail ?: @"no response";
    return out;
}

//  To a file, because a blob can be hundreds of megabytes and must not be held
//  in memory on a phone.
static BOOL HttpToFile ( NSString *url, NSString *dest, long long expect, NSString **err )
{
    __block BOOL ok = NO;
    __block NSString *fail = nil;
    dispatch_semaphore_t sem = dispatch_semaphore_create ( 0 );

    NSURLSessionDownloadTask *t = [PatchSession()
        downloadTaskWithRequest:FreshRequest ( url )
        completionHandler:^(NSURL *tmp, NSURLResponse *r, NSError *e) {
            const long code = [r isKindOfClass:NSHTTPURLResponse.class]
                            ? (long)((NSHTTPURLResponse *)r).statusCode : 0;
            if (e)               fail = e.localizedDescription;
            else if (code != 200) fail = [NSString stringWithFormat:@"HTTP %ld", code];
            else {
                NSError *mv = nil;
                [NSFileManager.defaultManager removeItemAtPath:dest error:NULL];
                if (![NSFileManager.defaultManager moveItemAtURL:tmp
                                                          toURL:[NSURL fileURLWithPath:dest]
                                                          error:&mv])
                    fail = mv.localizedDescription;
                else ok = YES;
            }
            dispatch_semaphore_signal ( sem );
        }];
    [t resume];
    dispatch_semaphore_wait ( sem, DISPATCH_TIME_FOREVER );

    if (ok && expect > 0) {
        NSDictionary *a = [NSFileManager.defaultManager attributesOfItemAtPath:dest error:NULL];
        if (a && (long long)[a fileSize] != expect) {
            ok = NO;
            fail = [NSString stringWithFormat:@"short download (%lld of %lld bytes)",
                    (long long)[a fileSize], expect];
        }
    }
    if (!ok) *err = fail ?: @"download failed";
    return ok;
}

// -------------------------------------------------------------------- state

//  Without the trailing slash RanIOS_DataRoot now carries: this file joins with
//  stringByAppendingPathComponent and compares prefixes in SafeDest, and a
//  root ending in "/" would make that check test for "//" and reject every
//  path. stringByStandardizingPath drops it.
static NSString *RootDir ( void ) {
    return [@(RanIOS_DataRoot()) stringByStandardizingPath];
}

static int ReadVersion ( void )
{
    NSString *p = [RootDir() stringByAppendingPathComponent:kVerFile];
    NSString *s = [NSString stringWithContentsOfFile:p encoding:NSUTF8StringEncoding error:NULL];
    return s ? s.intValue : -1;
}

static void WriteVersion ( int v )
{
    [[NSString stringWithFormat:@"%d", v]
        writeToFile:[RootDir() stringByAppendingPathComponent:kVerFile]
         atomically:YES encoding:NSUTF8StringEncoding error:NULL];
}

//  path -> "size:mtime:sha". Hashing 16,000 files takes minutes; this is what
//  makes the second launch fast, and it is only ever a cache — a miss costs a
//  hash, never a wrong answer.
static NSMutableDictionary *ReadIndex ( void )
{
    NSMutableDictionary *m = [NSMutableDictionary dictionary];
    NSString *s = [NSString stringWithContentsOfFile:
                    [RootDir() stringByAppendingPathComponent:kIndexFile]
                    encoding:NSUTF8StringEncoding error:NULL];
    for (NSString *line in [s componentsSeparatedByString:@"\n"]) {
        const NSRange tab = [line rangeOfString:@"\t"];
        if (tab.location == NSNotFound || tab.location == 0) continue;
        m[[line substringToIndex:tab.location]] =
            [[line substringFromIndex:tab.location + 1]
                stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    }
    return m;
}

static void WriteIndexFrom ( NSArray *files, NSString *root )
{
    NSMutableString *out = [NSMutableString string];
    for (NSDictionary *e in files) {
        NSString *rel = e[@"path"], *sha = e[@"sha256"];
        if (!rel || !sha) continue;
        NSString *err = nil;
        NSString *full = SafeDest ( root, rel, &err );
        if (!full) continue;
        NSDictionary *a = [NSFileManager.defaultManager attributesOfItemAtPath:full error:NULL];
        if (!a) continue;
        [out appendFormat:@"%@\t%lld:%lld:%@\n", rel, (long long)[a fileSize],
             (long long)([[a fileModificationDate] timeIntervalSince1970] * 1000.0), sha];
    }
    [out writeToFile:[root stringByAppendingPathComponent:kIndexFile]
          atomically:YES encoding:NSUTF8StringEncoding error:NULL];
}

// ---------------------------------------------------------------- download

//  A file the store also keeps as slices ("parts" in its manifest entry), the
//  same as downloadParts in RanLauncher.java.
//
//  Cloudflare caches nothing over 512 MB, and Map.rcc is 548 MB: whole, it
//  came from the origin at about 1 MB/s; as 64 MB parts it comes out of the
//  cache like everything else. Each part is its own blob, checked against its
//  own hash and appended; the caller then checks the joined file against the
//  whole-file hash before it is renamed into place, so a wrong part list
//  cannot install anything either. A part is deleted once appended, which
//  keeps the peak at the file plus one part. Returns nil, or what went wrong.
static NSString *DownloadParts ( NSString *dest, NSString *tmp, NSString *blobBase,
                                 NSArray *parts, long long size, NSString *rel )
{
    NSCharacterSet *notHex = [[NSCharacterSet characterSetWithCharactersInString:
                                 @"0123456789abcdefABCDEF"] invertedSet];
    long long total = 0;
    for (NSDictionary *p in parts) {
        if (![p isKindOfClass:NSDictionary.class]) return [@"bad part list for " stringByAppendingString:rel];
        NSString *ps = p[@"sha256"];
        if (![ps isKindOfClass:NSString.class] || ps.length != 64 ||
            [ps rangeOfCharacterFromSet:notHex].location != NSNotFound)
            return [@"bad part hash for " stringByAppendingString:rel];
        total += [p[@"size"] longLongValue];
    }
    if (total != size) return [@"parts do not add up for " stringByAppendingString:rel];

    NSFileManager *fm = NSFileManager.defaultManager;
    [fm removeItemAtPath:tmp error:NULL];
    if (![fm createFileAtPath:tmp contents:nil attributes:nil])
        return [@"cannot create " stringByAppendingString:rel];
    NSFileHandle *out = [NSFileHandle fileHandleForWritingAtPath:tmp];
    if (!out) return [@"cannot write " stringByAppendingString:rel];

    NSString *fail = nil;
    for (NSUInteger k = 0; k < parts.count && !fail; ++k) {
        @autoreleasepool {
            NSString *psha = parts[k][@"sha256"];
            const long long psize = [parts[k][@"size"] longLongValue];
            NSString *pf = [dest stringByAppendingFormat:@".part%lu", (unsigned long)k];
            NSString *err = nil;
            if (!HttpToFile ( [blobBase stringByAppendingString:psha], pf, psize, &err )) {
                fail = [NSString stringWithFormat:@"%@ part %lu: %@", rel, (unsigned long)k, err];
                break;
            }
            if ([psha caseInsensitiveCompare:Sha256OfFile(pf) ?: @""] != NSOrderedSame) {
                [fm removeItemAtPath:pf error:NULL];
                fail = [NSString stringWithFormat:@"checksum failed for part %lu of %@", (unsigned long)k, rel];
                break;
            }
            NSFileHandle *in = [NSFileHandle fileHandleForReadingAtPath:pf];
            if (!in) { fail = [@"cannot read a part of " stringByAppendingString:rel]; break; }
            for (;;) {
                @autoreleasepool {
                    NSData *chunk = [in readDataOfLength:1 << 20];
                    if (chunk.length == 0) break;
                    NSError *we = nil;
                    if (![out writeData:chunk error:&we]) {
                        fail = [NSString stringWithFormat:@"cannot write %@: %@", rel, we.localizedDescription];
                        break;
                    }
                }
            }
            [in closeFile];
            [fm removeItemAtPath:pf error:NULL];
        }
    }
    [out closeFile];
    //  A failed join is restarted from nothing next time, so the partial file
    //  is only disk space - up to 548 MB of it.
    if (fail) [fm removeItemAtPath:tmp error:NULL];
    return fail;
}

//  One todo entry into place: the blob (or its parts) to a .tmp, the whole-file
//  hash checked, then renamed over the destination. Replacing only once the
//  bytes are known good means being killed mid-download can never leave a
//  corrupt file behind. Safe to run on several threads at once: every path is
//  per file, and directory creation tolerates a race. Returns nil, or what
//  went wrong.
//  Where the blobs come from - same rule as blobBase() in RanLauncher.java. The
//  signed manifest may name a storage bucket on the CDN (Cloudflare R2) so a
//  fresh install never waits on the game server's upload; anything that is not
//  an https folder means the store itself. Every blob is checked against its
//  hash either way.
static NSString *BlobBase ( NSDictionary *m, NSString *base )
{
    id b = m[@"blobBase"];
    if ([b isKindOfClass:NSString.class] && [(NSString *)b hasPrefix:@"https://"]
        && [(NSString *)b hasSuffix:@"/"] && [(NSString *)b length] < 512)
        return (NSString *)b;
    return [base stringByAppendingString:@"blobs/"];
}

static NSString *DownloadOneFrom ( NSString *root, NSString *blobBase, NSArray *t );

//  From the bucket first; a blob it does not have (not uploaded yet) or serves
//  wrong comes from the store, which has every one.
static NSString *DownloadOne ( NSString *root, NSString *blobBase, NSString *storeBase, NSArray *t )
{
    NSString *fe = DownloadOneFrom ( root, blobBase, t );
    if (fe && ![blobBase isEqualToString:storeBase])
        fe = DownloadOneFrom ( root, storeBase, t );
    return fe;
}

static NSString *DownloadOneFrom ( NSString *root, NSString *blobBase, NSArray *t )
{
    NSString *rel = t[0], *sha = t[1];
    const long long size = [t[2] longLongValue];

    NSString *perr = nil;
    NSString *dest = SafeDest ( root, rel, &perr );
    if (!dest) return perr;

    NSFileManager *fm = NSFileManager.defaultManager;
    [fm createDirectoryAtPath:[dest stringByDeletingLastPathComponent]
  withIntermediateDirectories:YES attributes:nil error:NULL];

    NSString *tmp = [dest stringByAppendingString:@".tmp"];
    if ([t[3] isKindOfClass:NSArray.class]) {
        NSString *pe = DownloadParts ( dest, tmp, blobBase, t[3], size, rel );
        if (pe) return pe;
    } else {
        NSString *err = nil;
        if (!HttpToFile ( [blobBase stringByAppendingString:sha], tmp, size, &err ))
            return [NSString stringWithFormat:@"%@: %@", rel, err];
    }

    if ([sha caseInsensitiveCompare:Sha256OfFile(tmp) ?: @""] != NSOrderedSame) {
        [fm removeItemAtPath:tmp error:NULL];
        return [@"checksum failed for " stringByAppendingString:rel];
    }

    [fm removeItemAtPath:dest error:NULL];
    NSError *mv = nil;
    if (![fm moveItemAtPath:tmp toPath:dest error:&mv])
        return [NSString stringWithFormat:@"cannot replace %@: %@", rel, mv.localizedDescription];
    return nil;
}

// --------------------------------------------------------------------- run

extern "C" void RanIOS_RunPatch ( RanPatchProgress say, RanPatchDone done )
{
    dispatch_async ( dispatch_get_global_queue ( QOS_CLASS_UTILITY, 0 ), ^{
        NSString *err = nil;
        NSString *base = kBaseDefault;
        NSString *root = RootDir();

        //  The host is not the player's business, and a screenshot of this
        //  screen should not hand anyone the patch address. It goes to the log,
        //  where someone diagnosing a patch failure is already looking.
        LOGI ( "patch base %s", base.UTF8String );
        say ( @"กำลังตรวจสอบอัปเดต", nil, -1 );

        //  Fetched as bytes and checked before being parsed: a JSON parser is
        //  the first thing an attacker reaches, so it must not run on anything
        //  unverified.
        NSData *body = HttpGet ( [base stringByAppendingString:@"manifest.json"], &err );
        if (!body) { done ( NO, NO, [@"cannot reach the patch server: " stringByAppendingString:err] ); return; }

        NSData *sig = HttpGet ( [base stringByAppendingString:@"manifest.sig"], &err );
        if (!sig) { done ( NO, NO, [@"no manifest signature on the server: " stringByAppendingString:err] ); return; }

        if (!VerifyManifest ( body, sig, &err )) {
            //  What was actually checked, so a mismatch can be compared with the
            //  server's copies (sha256sum manifest.json, cat manifest.sig).
            unsigned char h[CC_SHA256_DIGEST_LENGTH];
            CC_SHA256 ( body.bytes, (CC_LONG)body.length, h );
            NSMutableString *hex = [NSMutableString string];
            for (int i = 0; i < 8; ++i) [hex appendFormat:@"%02x", h[i]];
            NSString *sigText = [[NSString alloc] initWithData:sig encoding:NSUTF8StringEncoding];
            LOGI ( "manifest refused: body %lu bytes sha256 %s..., sig %lu bytes '%s'",
                   (unsigned long)body.length, hex.UTF8String, (unsigned long)sig.length,
                   sigText.UTF8String ?: "(not UTF-8)" );
            done ( NO, NO, err );
            return;
        }

        NSDictionary *m = [NSJSONSerialization JSONObjectWithData:body options:0 error:NULL];
        if (![m isKindOfClass:NSDictionary.class]) { done ( NO, NO, @"manifest is not an object" ); return; }

        const int version = [m[@"version"] intValue];

        //  The iOS build gate. minApk is an Android versionCode and says
        //  nothing about this build, so it is deliberately ignored; minIos is
        //  the key that governs here. A manifest without one predates iOS
        //  support, and being offered its data is a mistake, not a no-op.
        if (!m[@"minIos"]) {
            done ( NO, NO, @"this patch server does not support the iOS client yet" );
            return;
        }
        const int minIos = [m[@"minIos"] intValue];
        const int myBuild = [[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleVersion"] intValue];
        if (minIos > myBuild) {
            //  There is no download to offer: iOS cannot install a build over
            //  itself. A stale packet layout is a hard stop either way.
            //  The game is sideloaded, not on TestFlight or the App Store, so
            //  the message names where an update actually comes from.
            //  The one fatal ending, as RanLauncher.fail() is on Android: no
            //  amount of retrying fixes an app that is too old. "Title\nbody",
            //  in Thai like the rest of the page.
            done ( NO, YES, [NSString stringWithFormat:
                        @"เวอร์ชันแอปเก่าเกินไป\n"
                        @"เซิร์ฟเวอร์ต้องการแอปเวอร์ชัน %d แต่เครื่องนี้เป็น %d\n"
                        @"กรุณาอัปเดตแอปใน AltStore หรือ SideStore",
                        minIos, myBuild] );
            return;
        }

        [NSFileManager.defaultManager createDirectoryAtPath:root
                                withIntermediateDirectories:YES attributes:nil error:NULL];

        const int localVersion = ReadVersion ();
        if (localVersion == version) {
            say ( @"เป็นเวอร์ชันล่าสุด", [NSString stringWithFormat:@"เวอร์ชัน %d", version], 1000 );
            done ( YES, NO, nil );
            return;
        }

        //  Never go backwards. A signature stops an attacker writing a
        //  manifest, but not replaying one we signed ourselves — an old
        //  manifest stays validly signed forever. To publish old content
        //  deliberately, republish it under a higher number.
        if (localVersion >= 0 && version < localVersion) {
            done ( NO, NO, [NSString stringWithFormat:
                        @"server offers version %d, older than the installed %d",
                        version, localVersion] );
            return;
        }

        NSArray *files = m[@"files"];
        if (![files isKindOfClass:NSArray.class]) { done ( NO, NO, @"manifest has no file list" ); return; }

        NSDictionary *index = ReadIndex ();
        NSMutableArray *todo = [NSMutableArray array];      //  @[rel, sha, @(size), parts or NSNull]
        long long todoBytes = 0;

        say ( @"กำลังตรวจสอบไฟล์", [NSString stringWithFormat:@"%lu ไฟล์",
                                  (unsigned long)files.count], 0 );

        for (NSUInteger i = 0; i < files.count; ++i) {
            @autoreleasepool {
                NSDictionary *e = files[i];
                NSString *rel = e[@"path"], *sha = e[@"sha256"];
                const long long size = [e[@"size"] longLongValue];
                if (!rel || !sha) continue;

                NSString *perr = nil;
                NSString *full = SafeDest ( root, rel, &perr );
                if (!full) { done ( NO, NO, perr ); return; }

                const BOOL exists = [NSFileManager.defaultManager fileExistsAtPath:full];
                BOOL ok = NO;

                //  A seeded file belongs to the player once it exists.
                //  option.ini is rewritten by the client whenever settings are
                //  saved, so its hash stops matching immediately and the normal
                //  path would reset graphics, sound and gameplay on every patch.
                if ([e[@"seed"] boolValue] && exists) ok = YES;
                else if (exists) {
                    NSDictionary *a = [NSFileManager.defaultManager attributesOfItemAtPath:full error:NULL];
                    if (a && (long long)[a fileSize] == size) {
                        NSString *want = [NSString stringWithFormat:@"%lld:%lld:%@", size,
                            (long long)([[a fileModificationDate] timeIntervalSince1970] * 1000.0), sha];
                        NSString *have = index[rel];
                        if (have && [have isEqualToString:want]) ok = YES;         //  trusted
                        else ok = [sha caseInsensitiveCompare:Sha256OfFile(full) ?: @""] == NSOrderedSame;
                    }
                }
                if (!ok) {
                    id parts = e[@"parts"];
                    [todo addObject:@[rel, sha, @(size),
                                      [parts isKindOfClass:NSArray.class] ? parts : NSNull.null]];
                    todoBytes += size;
                }
                if ((i & 255) == 0)
                    say ( nil, [NSString stringWithFormat:@"checked %lu / %lu",
                                (unsigned long)i, (unsigned long)files.count],
                          (int)(i * 1000 / MAX((NSUInteger)1, files.count)) );
            }
        }

        if (todo.count == 0) {
            WriteIndexFrom ( files, root );
            WriteVersion ( version );
            say ( @"เป็นเวอร์ชันล่าสุด", [NSString stringWithFormat:@"เวอร์ชัน %d", version], 1000 );
            done ( YES, NO, nil );
            return;
        }

        say ( @"กำลังดาวน์โหลดอัปเดต",
              [NSString stringWithFormat:@"%lu ไฟล์, %.1f MB",
               (unsigned long)todo.count, todoBytes / 1048576.0], 0 );

        //  kDlThreads workers pull entries off one shared counter. A failure
        //  stops new files from starting; the ones already running finish or
        //  fail on their own, and nothing half-written is ever renamed into
        //  place, so the next launch resumes cleanly.
        const NSUInteger count = todo.count;
        NSString *storeBase = [base stringByAppendingString:@"blobs/"];
        NSString *blobBase = BlobBase ( m, base );
        NSLock *lock = [NSLock new];
        __block NSUInteger next = 0, doneFiles = 0;
        __block long long gotBytes = 0;
        __block NSString *failMsg = nil;

        dispatch_group_t group = dispatch_group_create ();
        dispatch_queue_t workers = dispatch_get_global_queue ( QOS_CLASS_UTILITY, 0 );
        for (int w = 0; w < kDlThreads; ++w) {
            dispatch_group_async ( group, workers, ^{
                for (;;) {
                    NSArray *t = nil;
                    [lock lock];
                    if (!failMsg && next < count) t = todo[next++];
                    [lock unlock];
                    if (!t) break;
                    @autoreleasepool {
                        NSString *fe = DownloadOne ( root, blobBase, storeBase, t );
                        [lock lock];
                        if (fe) { if (!failMsg) failMsg = fe; }
                        else    { ++doneFiles; gotBytes += [t[2] longLongValue]; }
                        [lock unlock];
                    }
                }
            });
        }

        //  Progress from here, four times a second, not once per file from the
        //  workers - that was 23,000 updates on a fresh install.
        for (;;) {
            const long waited = dispatch_group_wait ( group,
                                    dispatch_time ( DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC ) );
            [lock lock];
            const NSUInteger f = doneFiles;
            const long long b = gotBytes;
            [lock unlock];
            say ( nil, [NSString stringWithFormat:@"%lu / %lu   %.1f of %.1f MB",
                        (unsigned long)f, (unsigned long)count,
                        b / 1048576.0, todoBytes / 1048576.0],
                  (int)(todoBytes == 0 ? 1000 : b * 1000 / todoBytes) );
            if (waited == 0) break;
        }
        if (failMsg) { done ( NO, NO, failMsg ); return; }

        WriteIndexFrom ( files, root );
        WriteVersion ( version );                   //  last, always
        say ( @"อัปเดตเสร็จแล้ว", [NSString stringWithFormat:@"เวอร์ชัน %d", version], 1000 );
        done ( YES, NO, nil );
    });
}

#endif  //  __APPLE__
