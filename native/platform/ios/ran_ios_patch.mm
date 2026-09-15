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
static NSURLSession *PatchSession ( void )
{
    static NSURLSession *s;
    static dispatch_once_t once;
    dispatch_once ( &once, ^{
        NSURLSessionConfiguration *c = NSURLSessionConfiguration.ephemeralSessionConfiguration;
        c.URLCache = nil;
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
        say ( @"Checking for updates", nil, -1 );

        //  Fetched as bytes and checked before being parsed: a JSON parser is
        //  the first thing an attacker reaches, so it must not run on anything
        //  unverified.
        NSData *body = HttpGet ( [base stringByAppendingString:@"manifest.json"], &err );
        if (!body) { done ( NO, [@"cannot reach the patch server: " stringByAppendingString:err] ); return; }

        NSData *sig = HttpGet ( [base stringByAppendingString:@"manifest.sig"], &err );
        if (!sig) { done ( NO, [@"no manifest signature on the server: " stringByAppendingString:err] ); return; }

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
            done ( NO, err );
            return;
        }

        NSDictionary *m = [NSJSONSerialization JSONObjectWithData:body options:0 error:NULL];
        if (![m isKindOfClass:NSDictionary.class]) { done ( NO, @"manifest is not an object" ); return; }

        const int version = [m[@"version"] intValue];

        //  The iOS build gate. minApk is an Android versionCode and says
        //  nothing about this build, so it is deliberately ignored; minIos is
        //  the key that governs here. A manifest without one predates iOS
        //  support, and being offered its data is a mistake, not a no-op.
        if (!m[@"minIos"]) {
            done ( NO, @"this patch server does not support the iOS client yet" );
            return;
        }
        const int minIos = [m[@"minIos"] intValue];
        const int myBuild = [[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleVersion"] intValue];
        if (minIos > myBuild) {
            //  There is no download to offer: iOS cannot install a build over
            //  itself. A stale packet layout is a hard stop either way.
            done ( NO, [NSString stringWithFormat:
                        @"This version of RAN is out of date.\n"
                        @"The server needs build %d, this is %d.\n"
                        @"Update from TestFlight or the App Store.", minIos, myBuild] );
            return;
        }

        [NSFileManager.defaultManager createDirectoryAtPath:root
                                withIntermediateDirectories:YES attributes:nil error:NULL];

        const int localVersion = ReadVersion ();
        if (localVersion == version) {
            say ( @"Up to date", [NSString stringWithFormat:@"version %d", version], 1000 );
            done ( YES, nil );
            return;
        }

        //  Never go backwards. A signature stops an attacker writing a
        //  manifest, but not replaying one we signed ourselves — an old
        //  manifest stays validly signed forever. To publish old content
        //  deliberately, republish it under a higher number.
        if (localVersion >= 0 && version < localVersion) {
            done ( NO, [NSString stringWithFormat:
                        @"server offers version %d, older than the installed %d",
                        version, localVersion] );
            return;
        }

        NSArray *files = m[@"files"];
        if (![files isKindOfClass:NSArray.class]) { done ( NO, @"manifest has no file list" ); return; }

        NSDictionary *index = ReadIndex ();
        NSMutableArray *todo = [NSMutableArray array];      //  @[rel, sha, @(size)]
        long long todoBytes = 0;

        say ( @"Checking files", [NSString stringWithFormat:@"%lu files",
                                  (unsigned long)files.count], 0 );

        for (NSUInteger i = 0; i < files.count; ++i) {
            @autoreleasepool {
                NSDictionary *e = files[i];
                NSString *rel = e[@"path"], *sha = e[@"sha256"];
                const long long size = [e[@"size"] longLongValue];
                if (!rel || !sha) continue;

                NSString *perr = nil;
                NSString *full = SafeDest ( root, rel, &perr );
                if (!full) { done ( NO, perr ); return; }

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
                if (!ok) { [todo addObject:@[rel, sha, @(size)]]; todoBytes += size; }
                if ((i & 255) == 0)
                    say ( nil, [NSString stringWithFormat:@"checked %lu / %lu",
                                (unsigned long)i, (unsigned long)files.count],
                          (int)(i * 1000 / MAX((NSUInteger)1, files.count)) );
            }
        }

        if (todo.count == 0) {
            WriteIndexFrom ( files, root );
            WriteVersion ( version );
            say ( @"Up to date", [NSString stringWithFormat:@"version %d", version], 1000 );
            done ( YES, nil );
            return;
        }

        say ( @"Downloading update",
              [NSString stringWithFormat:@"%lu files, %.1f MB",
               (unsigned long)todo.count, todoBytes / 1048576.0], 0 );

        long long got = 0;
        for (NSUInteger i = 0; i < todo.count; ++i) {
            @autoreleasepool {
                NSString *rel = todo[i][0], *sha = todo[i][1];
                const long long size = [todo[i][2] longLongValue];

                NSString *perr = nil;
                NSString *dest = SafeDest ( root, rel, &perr );
                if (!dest) { done ( NO, perr ); return; }

                [NSFileManager.defaultManager
                    createDirectoryAtPath:[dest stringByDeletingLastPathComponent]
                    withIntermediateDirectories:YES attributes:nil error:NULL];

                NSString *tmp = [dest stringByAppendingString:@".tmp"];
                if (!HttpToFile ( [NSString stringWithFormat:@"%@blobs/%@", base, sha],
                                  tmp, size, &err )) {
                    done ( NO, [NSString stringWithFormat:@"%@: %@", rel, err] );
                    return;
                }

                if ([sha caseInsensitiveCompare:Sha256OfFile(tmp) ?: @""] != NSOrderedSame) {
                    [NSFileManager.defaultManager removeItemAtPath:tmp error:NULL];
                    done ( NO, [@"checksum failed for " stringByAppendingString:rel] );
                    return;
                }

                //  Replace only once the bytes are known good, so being killed
                //  mid-download can never leave a corrupt file behind.
                [NSFileManager.defaultManager removeItemAtPath:dest error:NULL];
                NSError *mv = nil;
                if (![NSFileManager.defaultManager moveItemAtPath:tmp toPath:dest error:&mv]) {
                    done ( NO, [NSString stringWithFormat:@"cannot replace %@: %@",
                                rel, mv.localizedDescription] );
                    return;
                }

                got += size;
                say ( nil, [NSString stringWithFormat:@"%lu / %lu   %.1f of %.1f MB",
                            (unsigned long)(i + 1), (unsigned long)todo.count,
                            got / 1048576.0, todoBytes / 1048576.0],
                      (int)(todoBytes == 0 ? 1000 : got * 1000 / todoBytes) );
            }
        }

        WriteIndexFrom ( files, root );
        WriteVersion ( version );                   //  last, always
        say ( @"Updated", [NSString stringWithFormat:@"version %d", version], 1000 );
        done ( YES, nil );
    });
}

#endif  //  __APPLE__
