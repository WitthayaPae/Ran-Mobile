// Android entry point — NativeActivity.
//
// PHASE 2: this boots the real client and runs its frame loop with the headless
// D3D9 shim. Nothing is drawn; the point is that RANPARAM, the RCC packs, the
// glogic data and the network layer all run exactly as they do on PC, and say so
// in logcat. Phase 3 attaches EGL to the surface handled below and the same loop
// starts producing pixels.
//
// The data root is NOT inside the APK: the shipped game data is several GB, so
// it is pushed to external storage and pointed at here. Order of preference:
//   1. /sdcard/ran            (adb push target — what testing uses)
//   2. the app's own external files dir
//   3. the app's internal files dir

#include <android_native_app_glue.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "RanMain", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "RanMain", __VA_ARGS__)

extern "C" int  RanApp_Boot(const char *dataRoot, int width, int height);
extern "C" int  RanApp_Frame(void);
extern "C" void RanApp_Shutdown(void);
extern "C" void RanSound_LogStats(void);

// GL backend (shim/gl). The context is created on this thread — the only thread
// that ever touches GL.
extern "C" int  RanGL_Init(void *nativeWindow);
extern "C" void RanGL_Shutdown(void);
extern "C" int  RanGL_Width(void);
extern "C" int  RanGL_Height(void);
extern "C" int  RanGL_LogicalWidth(void);
extern "C" int  RanGL_LogicalHeight(void);
extern "C" int  RanGL_UIScale(void);
extern "C" int  RanGL_InputScale(void);
extern "C" int  RanGLR_Init(void);

// Input, fed into the DirectInput device the engine actually reads (shim/platform).
extern "C" void RanInput_PointerMove(int x, int y);
extern "C" void RanInput_PointerButton(int button, int down);
extern "C" void RanInput_Key(int scanCode, int down);

namespace {

struct AppState {
    bool ready = false;       // surface exists
    bool booted = false;      // client booted
    bool quit = false;
    int  width = 1280;
    int  height = 720;
};

bool dirHas(const char *root, const char *rel) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    struct stat st;
    return stat(path, &st) == 0;
}

// A data root is only accepted if it actually contains the client's data tree —
// otherwise the engine would fail deep inside a loader with a confusing error
// instead of here, where the cause is obvious.
const char *pickDataRoot(android_app *app) {
    static char chosen[1024];
    const char *candidates[3] = { "/sdcard/ran", NULL, NULL };
    if (app->activity->externalDataPath) candidates[1] = app->activity->externalDataPath;
    if (app->activity->internalDataPath) candidates[2] = app->activity->internalDataPath;

    for (int i = 0; i < 3; ++i) {
        if (!candidates[i]) continue;
        if (dirHas(candidates[i], "data/gui") || dirHas(candidates[i], "config.ini")) {
            snprintf(chosen, sizeof(chosen), "%s/", candidates[i]);
            LOGI("data root: %s", chosen);
            return chosen;
        }
        LOGI("data root candidate has no client data: %s", candidates[i]);
    }
    snprintf(chosen, sizeof(chosen), "%s/", candidates[0]);
    LOGE("NO CLIENT DATA FOUND. Push it first, e.g.:");
    LOGE("  adb push \"CLIENT/config.ini\" /sdcard/ran/");
    LOGE("  adb push \"CLIENT/data\"       /sdcard/ran/");
    return chosen;
}

// Android key code -> DirectInput scan code, for the keys the client binds.
// Only what a login screen needs is mapped; the rest is added when the game
// stage starts using it.
int scanCodeFor(int32_t keyCode) {
    switch (keyCode) {
        case AKEYCODE_ENTER:     return 0x1C;   // DIK_RETURN
        case AKEYCODE_ESCAPE:    return 0x01;   // DIK_ESCAPE
        case AKEYCODE_DEL:       return 0x0E;   // DIK_BACK
        case AKEYCODE_TAB:       return 0x0F;   // DIK_TAB
        case AKEYCODE_SPACE:     return 0x39;   // DIK_SPACE
        case AKEYCODE_DPAD_UP:   return 0xC8;
        case AKEYCODE_DPAD_DOWN: return 0xD0;
        case AKEYCODE_DPAD_LEFT: return 0xCB;
        case AKEYCODE_DPAD_RIGHT:return 0xCD;
        default: break;
    }
    // Letters and digits sit in contiguous ranges on both sides, but the DIK
    // order is the keyboard layout, not the alphabet, so it is a table.
    static const int kLetters[26] = {
        0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
        0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C
    };
    if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) return kLetters[keyCode - AKEYCODE_A];
    static const int kDigits[10] = { 0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A };
    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) return kDigits[keyCode - AKEYCODE_0];
    return 0;
}

// Touch becomes the left mouse button. The pointer is moved before the press
// because the UI hit-tests using the current position and a touch delivers both
// at the same instant.
int32_t onInputEvent(android_app *app, AInputEvent *event) {
    (void)app;
    const int32_t type = AInputEvent_getType(event);

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        //  Touches arrive in panel pixels; the frame is drawn smaller than the
        //  panel and the display scales it, so undo that here.
        const int scale = RanGL_InputScale();
        const int x = (int)AMotionEvent_getX(event, 0) / scale;
        const int y = (int)AMotionEvent_getY(event, 0) / scale;
        switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
                RanInput_PointerMove(x, y);
                RanInput_PointerButton(0, 1);
                return 1;
            case AMOTION_EVENT_ACTION_MOVE:
                RanInput_PointerMove(x, y);
                return 1;
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                RanInput_PointerMove(x, y);
                RanInput_PointerButton(0, 0);
                return 1;
            default:
                return 0;
        }
    }

    if (type == AINPUT_EVENT_TYPE_KEY) {
        const int32_t action = AKeyEvent_getAction(event);
        const int scan = scanCodeFor(AKeyEvent_getKeyCode(event));
        if (!scan) return 0;
        if (action == AKEY_EVENT_ACTION_DOWN)     RanInput_Key(scan, 1);
        else if (action == AKEY_EVENT_ACTION_UP)  RanInput_Key(scan, 0);
        return 1;
    }

    return 0;
}

void onAppCmd(android_app *app, int32_t cmd) {
    AppState *st = (AppState *)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                st->width  = ANativeWindow_getWidth(app->window);
                st->height = ANativeWindow_getHeight(app->window);
                LOGI("window %dx%d", st->width, st->height);
                // EGL first: device creation queries the real surface size, and
                // every texture upload needs a live context.
                if (!RanGL_Init(app->window)) {
                    LOGE("EGL init failed - cannot render");
                    st->quit = true;
                    break;
                }
                if (RanGL_Width() > 0) {
                    //  The client is booted at the logical size — see
                    //  RanGL_UIScale: a PC-sized GUI on a 2560x1440 panel is
                    //  unusable with a finger.
                    st->width  = RanGL_LogicalWidth();
                    st->height = RanGL_LogicalHeight();
                    LOGI("logical %dx%d (scale %d of %dx%d)", st->width, st->height,
                         RanGL_UIScale(), RanGL_Width(), RanGL_Height());
                }
                if (!RanGLR_Init()) {
                    LOGE("GL renderer init failed");
                    st->quit = true;
                    break;
                }
                st->ready = true;
            }
            break;
        case APP_CMD_TERM_WINDOW:
            st->ready = false;
            break;
        case APP_CMD_DESTROY:
            st->quit = true;
            break;
        default:
            break;
    }
}

} // namespace

extern "C" void android_main(android_app *app) {
    AppState state;
    app->userData = &state;
    app->onAppCmd = onAppCmd;
    app->onInputEvent = onInputEvent;

    LOGI("=== RAN native client (phase 3: GLES3) ===");

    unsigned long frames = 0;
    while (!state.quit) {
        int events;
        android_poll_source *source;
        // Block until the surface exists; poll once booted so frames keep running.
        int timeout = (state.ready && state.booted) ? 0 : -1;
        while (ALooper_pollOnce(timeout, NULL, &events, (void **)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) { state.quit = true; break; }
            timeout = 0;
        }
        if (state.quit) break;

        if (state.ready && !state.booted) {
            const char *root = pickDataRoot(app);
            if (RanApp_Boot(root, state.width, state.height)) {
                state.booted = true;
            } else {
                LOGE("boot failed — stopping");
                break;
            }
        }

        if (state.booted) {
            if (!RanApp_Frame()) { LOGE("frame failed — stopping"); break; }
            // A headless device returns instantly, so the loop spins millions of
            // times a second; log rarely enough that the boot lines survive.
            if ((++frames % 300) == 0) LOGI("running — %lu frames", frames);
        }
    }

    if (state.booted) { RanSound_LogStats(); RanApp_Shutdown(); }
    RanGL_Shutdown();
    LOGI("=== exit after %lu frames ===", frames);
}
