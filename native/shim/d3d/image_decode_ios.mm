//  iOS only, and the mirror of image_decode_android.cpp: whatever the platform
//  can decode that our own DDS/PNG/BMP/TGA readers decline - JPEG above all.
//
//  35 map textures are JPEG, and the terrain that references one draws
//  untextured grey without this. ImageIO is the iOS equivalent of
//  AImageDecoder, and unlike it needs no runtime version check: CGImageSource
//  has been there since iOS 4.
//
//  NOT YET COMPILED - no Mac on this machine.
#if defined(__APPLE__)

//  The frameworks FIRST, and the order is load-bearing.
//
//  windows.h does "#define interface struct" for the COM declarations in
//  d3d9.h. Objective-C headers are full of @interface, and the preprocessor
//  expands the token after the @ like any other - so every Apple header parsed
//  after that line dies with "prefix attribute must be followed by an
//  interface, protocol, or implementation". Importing them first means the
//  macro is defined only once nothing is left to break.
#import <ImageIO/ImageIO.h>
#import <CoreGraphics/CoreGraphics.h>

#include "windows.h"
#include "../platform/ran_plat.h"
#include <d3d9.h>

#include "image_decode.h"

#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanImg", __VA_ARGS__)

bool RanImage_DecodePlatform(const void *data, size_t size, RanImage &out) {
    if (!data || !size) return false;

    //  kCFAllocatorNull: the bytes belong to the caller and outlive this call,
    //  so CoreFoundation must not try to free them.
    CFDataRef cf = CFDataCreateWithBytesNoCopy ( kCFAllocatorDefault,
                                                 (const UInt8 *)data,
                                                 (CFIndex)size, kCFAllocatorNull );
    if (!cf) return false;

    CGImageSourceRef src = CGImageSourceCreateWithData ( cf, NULL );
    if (!src) { CFRelease(cf); return false; }

    CGImageRef img = CGImageSourceCreateImageAtIndex ( src, 0, NULL );
    CFRelease ( src );
    if (!img) { CFRelease(cf); return false; }

    const size_t width  = CGImageGetWidth ( img );
    const size_t height = CGImageGetHeight ( img );
    if (!width || !height) { CGImageRelease(img); CFRelease(cf); return false; }

    //  Drawn into a context of our own rather than read out of the image,
    //  because the source's own layout is whatever the file happened to use.
    //
    //  AlphaFirst + ByteOrder32Little is BGRA in memory, which is what
    //  D3DFMT_A8R8G8B8 is - so unlike the Android path this needs no channel
    //  swap afterwards. Premultiplied because CGBitmapContext will not take
    //  unpremultiplied 8-bit alpha at all (only *Premultiplied* and *NoneSkip*
    //  are legal); it is undone below.
    const size_t stride = width * 4;
    std::vector<BYTE> pixels ( stride * height );

    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB ();
    CGContextRef ctx = CGBitmapContextCreate (
            pixels.empty() ? NULL : &pixels[0], width, height, 8, stride, rgb,
            kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little );
    CGColorSpaceRelease ( rgb );

    if (!ctx) {
        LOGE ( "no bitmap context for a %ux%u image",
               (unsigned)width, (unsigned)height );
        CGImageRelease ( img ); CFRelease ( cf );
        return false;
    }

    CGContextSetBlendMode ( ctx, kCGBlendModeCopy );   //  no compositing, just the pixels

    //  Flip first, or every decoded image comes out upside down.
    //
    //  A CGBitmapContext has its origin at the BOTTOM left, so drawing an image
    //  into one puts the image's top row at the END of the buffer. Every other
    //  decoder in the shim - the Android one, the DDS/TGA/BMP readers - returns
    //  rows top-down, and D3DFMT_A8R8G8B8 surfaces are read that way throughout.
    //  Translating up by the height and scaling y by -1 makes CoreGraphics
    //  write the same order.
    CGContextTranslateCTM ( ctx, 0, (CGFloat)height );
    CGContextScaleCTM ( ctx, 1.0, -1.0 );

    CGContextDrawImage ( ctx, CGRectMake ( 0, 0, (CGFloat)width, (CGFloat)height ), img );
    CGContextRelease ( ctx );
    CGImageRelease ( img );
    CFRelease ( cf );

    //  Undo the premultiply, so this returns the same thing every other reader
    //  in the shim does. A JPEG has no alpha and every pixel takes the fast
    //  branch; only a WebP or a GIF with transparency does any work here.
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const unsigned a = pixels[i + 3];
        if (a == 255 || a == 0) continue;
        for (int c = 0; c < 3; ++c) {
            const unsigned v = (unsigned)pixels[i + c] * 255u / a;
            pixels[i + c] = (BYTE)( v > 255u ? 255u : v );
        }
    }

    out.width      = (UINT)width;
    out.height     = (UINT)height;
    out.mipLevels  = 1;
    out.format     = D3DFMT_A8R8G8B8;
    out.fileFormat = D3DXIFF_JPG;
    out.levels.clear ();
    out.levels.push_back ( std::vector<BYTE>() );
    out.levels[0].swap ( pixels );
    return true;
}

#endif  //  __APPLE__
