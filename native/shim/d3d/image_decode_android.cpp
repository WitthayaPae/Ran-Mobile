//  Everything Android itself can decode — JPEG above all.
//
//  The shipped texture set is nearly all DDS, but a handful of map textures are
//  JPEG (35 of them, `black.jpg` among them), and the terrain that references
//  one drew untextured grey across the whole ground. Rather than vendoring a
//  JPEG decoder, this asks the platform: AImageDecoder reads JPEG, PNG, WebP
//  and GIF and hands back straight RGBA.
//
//  AImageDecoder is API 30, while this build targets API 24 so it still runs on
//  older devices, so the entry points are resolved at runtime: on anything older
//  the call simply fails and the texture behaves as it did before.
//
//  It is only reached after the shim's own DDS/PNG/BMP/TGA readers decline the
//  bytes, so the path real game textures take is untouched.

#include "windows.h"
#include "../platform/ran_plat.h"
#include <d3d9.h>

#include "image_decode.h"

#include <dlfcn.h>
#include <string.h>

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanImg", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanImg", __VA_ARGS__)

namespace {

struct AImageDecoder;
struct AImageDecoderHeaderInfo;

const int kDecoderSuccess = 0;
const int kFormatRGBA8888 = 1;             // ANDROID_BITMAP_FORMAT_RGBA_8888

struct Api {
    int  (*createFromBuffer)(const void *, size_t, AImageDecoder **);
    void (*deleteDecoder)(AImageDecoder *);
    const AImageDecoderHeaderInfo *(*getHeaderInfo)(const AImageDecoder *);
    int32_t (*getWidth)(const AImageDecoderHeaderInfo *);
    int32_t (*getHeight)(const AImageDecoderHeaderInfo *);
    int  (*setFormat)(AImageDecoder *, int32_t);
    int  (*setUnpremultiplied)(AImageDecoder *, bool);
    size_t (*minimumStride)(AImageDecoder *);
    int  (*decodeImage)(AImageDecoder *, void *, size_t, size_t);
    bool ready;
};

const Api &api() {
    static Api a = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, false };
    static bool tried = false;
    if (tried) return a;
    tried = true;

    void *lib = dlopen("libjnigraphics.so", RTLD_NOW);
    if (!lib) return a;

    *(void **)&a.createFromBuffer   = dlsym(lib, "AImageDecoder_createFromBuffer");
    *(void **)&a.deleteDecoder      = dlsym(lib, "AImageDecoder_delete");
    *(void **)&a.getHeaderInfo      = dlsym(lib, "AImageDecoder_getHeaderInfo");
    *(void **)&a.getWidth           = dlsym(lib, "AImageDecoderHeaderInfo_getWidth");
    *(void **)&a.getHeight          = dlsym(lib, "AImageDecoderHeaderInfo_getHeight");
    *(void **)&a.setFormat          = dlsym(lib, "AImageDecoder_setAndroidBitmapFormat");
    *(void **)&a.setUnpremultiplied = dlsym(lib, "AImageDecoder_setUnpremultipliedRequired");
    *(void **)&a.minimumStride      = dlsym(lib, "AImageDecoder_getMinimumStride");
    *(void **)&a.decodeImage        = dlsym(lib, "AImageDecoder_decodeImage");

    a.ready = a.createFromBuffer && a.deleteDecoder && a.getHeaderInfo && a.getWidth &&
              a.getHeight && a.setFormat && a.minimumStride && a.decodeImage;
    LOGI("platform image decoder %s", a.ready ? "available" : "unavailable (pre-API 30)");
    return a;
}

} // namespace

bool RanImage_DecodeAndroid(const void *data, size_t size, RanImage &out) {
    const Api &a = api();
    if (!a.ready || !data || !size) return false;

    AImageDecoder *decoder = NULL;
    if (a.createFromBuffer(data, size, &decoder) != kDecoderSuccess || !decoder) return false;

    const AImageDecoderHeaderInfo *info = a.getHeaderInfo(decoder);
    const int32_t width  = info ? a.getWidth(info)  : 0;
    const int32_t height = info ? a.getHeight(info) : 0;
    if (width <= 0 || height <= 0) { a.deleteDecoder(decoder); return false; }

    //  Straight 8-bit RGBA, unpremultiplied: the same shape the other decoders
    //  produce, so the upload path needs no special case.
    a.setFormat(decoder, kFormatRGBA8888);
    if (a.setUnpremultiplied) a.setUnpremultiplied(decoder, true);

    const size_t stride = a.minimumStride(decoder);
    const size_t bytes = stride * (size_t)height;

    std::vector<BYTE> pixels(bytes);
    const int result = a.decodeImage(decoder, pixels.empty() ? NULL : &pixels[0], stride, bytes);
    a.deleteDecoder(decoder);
    if (result != kDecoderSuccess) {
        LOGE("platform decoder failed on a %dx%d image: %d", width, height, result);
        return false;
    }

    //  D3DFMT_A8R8G8B8 is BGRA in memory; the decoder gave RGBA.
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const BYTE r = pixels[i];
        pixels[i] = pixels[i + 2];
        pixels[i + 2] = r;
    }

    out.width = (UINT)width;
    out.height = (UINT)height;
    out.mipLevels = 1;
    out.format = D3DFMT_A8R8G8B8;
    out.fileFormat = D3DXIFF_JPG;
    out.levels.clear();
    out.levels.push_back(std::vector<BYTE>());
    out.levels[0].swap(pixels);
    return true;
}
