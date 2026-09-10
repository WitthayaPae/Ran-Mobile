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
//   1. the app's own external files dir  (where the launcher puts it)
//   2. /sdcard/ran                       (adb push target — test trees)
//   3. the app's internal files dir
//
// The private directory comes first deliberately. Shared storage is writable by
// any app with a storage permission, and these loaders are not hardened against
// hostile input, so the copy nobody else can edit is the one to prefer when
// both exist.

#include <android_native_app_glue.h>
#include "../../shim/platform/ran_plat.h"
#include <android/native_window.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <jni.h>
#include <android/native_activity.h>
#include <android/window.h>

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanMain", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanMain", __VA_ARGS__)

extern "C" int  RanApp_Boot(const char *dataRoot, int width, int height);
extern "C" int  RanApp_Frame(void);
extern "C" void RanApp_Shutdown(void);
extern "C" void RanSound_LogStats(void);
extern "C" void RanAudioSink_Pause(int paused);

// GL backend (shim/gl). The context is created on this thread — the only thread
// that ever touches GL.
extern "C" int  RanGL_Init(void *nativeWindow);
extern "C" void RanSplash_Begin(const char *dataRoot);
extern "C" void RanSplash_End(void);
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
extern "C" void RanInput_PumpButtons(void);
extern "C" int  RanUI_MouseInControl(void);
extern "C" int  RanUI_PointInControl(int x, int y);
extern "C" void RanUI_EndEditIfOutside(int x, int y);
extern "C" int  RanTouch_IsPinching(void);
extern "C" void RanInput_Key(int scanCode, int down);
extern "C" void RanInput_KeyTap(int scanCode);

//  The on-screen controls. They see every pointer before the client does.
#include "../../shim/platform/touch_ui.h"
#include "../../shim/platform/touch_gesture.h"

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
    const char *candidates[3] = { NULL, "/sdcard/ran", NULL };
    if (app->activity->externalDataPath) candidates[0] = app->activity->externalDataPath;
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
    snprintf(chosen, sizeof(chosen), "%s/", candidates[0] ? candidates[0] : "/sdcard/ran");
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
        //  Back becomes Escape, which opens the client's own menu - and, more to
        //  the point, is handled here so it is not handled by the system. An
        //  unmapped key returns 0 and NativeActivity finishes the activity, so
        //  Back used to drop the player out of the game instantly, mid-session,
        //  with no confirmation. On a tablet it is a gesture you hit by accident.
        case AKEYCODE_BACK:      return 0x01;   // DIK_ESCAPE
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

//  One finger has to cover both mouse buttons.
//
//  Right-click does real work in RAN - it uses or equips an item from the
//  inventory, clears a quick slot, and drives various context actions - and a
//  touch screen has no second button, so a long press stands in for it - all of
//  which now lives in shim/platform/touch_gesture.cpp, because iOS needs the
//  same behaviour and had none of it while this was a static in this file.
//  The rules and the reasons moved with the code.

int64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

//  Take the whole screen: no status bar, no navigation bar.
//
//  Two halves, because neither covers both. The theme and the window flag deal
//  with the status bar; the navigation bar only goes away through the View, and
//  there is no native entry point for it - hence the JNI.
//
//  IMMERSIVE_STICKY rather than plain IMMERSIVE: a swipe brings the bars back
//  briefly and they retreat on their own, instead of staying up and shifting the
//  layout under a player who was reaching for something else.
//  The device's own keyboard, raised and lowered with the edit boxes.
//
//  Its keys arrive as ordinary key events and go through scanCodeFor, so the
//  client sees them exactly as it would a physical keyboard - which is why this
//  is enough for a login without any of the IME work: an account name and
//  password are ASCII, and A-Z and 0-9 are already mapped.
//
//  Thai still needs a real IME. Characters that no scan code can express do not
//  arrive at all, so this does not replace that job, it just makes typing
//  possible without the client drawing a keyboard of its own.
android_app *g_app = NULL;

//  Raise and lower the keyboard through RanActivity.
//
//  This used to call InputMethodManager.showSoftInput() on the window's decor
//  view directly. That stopped working: the IME binds and the system reports it
//  as shown, but a decor view is not an editor, so
//
//      mHaveConnection=true  mBoundToMethod=true  mServedInputConnection=null
//
//  - there is nothing for the keyboard to type into and nothing is drawn.
//  SHOW_FORCED, which used to paper over it, was deprecated at API 33 and no
//  longer forces anything. On Android 14 the keyboard is "shown" and invisible.
//
//  RanActivity carries a one-pixel focusable view that answers
//  onCheckIsTextEditor and returns an InputConnection, so the IME has a real
//  target; committed text comes back through nativeCommitText below.
//
//  The old decor-view path, kept for reference:
//
//  ANativeActivity_showSoftInput is the obvious call and it does nothing here -
//  verified, not assumed: the request was logged three times with a live
//  activity and no keyboard appeared. It only works when the window has a
//  focused editable view, which a NativeActivity never has; there is no View to
//  focus, only a surface.
//
//  Going at the manager directly and forcing it is what NDK apps have to do.
//  Digits-only hint for the next RanIME_Show. Defined here rather than beside
//  g_imeActive because imeCall reads it and imeCall comes first.
bool g_imeNumeric = false;

void imeCall(bool show) {
    if (!g_app) return;

    JNIEnv *env = NULL;
    if (g_app->activity->vm->AttachCurrentThread(&env, NULL) != JNI_OK || !env) return;

    jobject act = g_app->activity->clazz;
    jclass  cAct = env->GetObjectClass(act);

    //  RanActivity's own methods. If the activity is the plain NativeActivity
    //  for some reason, these are absent and the old path below still runs.
    {
        jmethodID m = env->GetMethodID(cAct, show ? "ranShowKeyboard" : "ranHideKeyboard",
                                       show ? "(Z)V" : "()V");
        if (m) {
            if (show) env->CallVoidMethod(act, m, (jboolean)(g_imeNumeric ? JNI_TRUE : JNI_FALSE));
            else      env->CallVoidMethod(act, m);
            if (env->ExceptionCheck()) env->ExceptionClear();
            g_app->activity->vm->DetachCurrentThread();
            return;
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    //  getSystemService(Context.INPUT_METHOD_SERVICE)
    jmethodID mGetSvc = env->GetMethodID(cAct, "getSystemService",
                                         "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring   sName = env->NewStringUTF("input_method");
    jobject   imm = (mGetSvc && sName) ? env->CallObjectMethod(act, mGetSvc, sName) : NULL;

    //  ...and the window token to aim it at.
    jmethodID mGetWindow = env->GetMethodID(cAct, "getWindow", "()Landroid/view/Window;");
    jobject   window = mGetWindow ? env->CallObjectMethod(act, mGetWindow) : NULL;
    jobject   decor = NULL;
    if (window) {
        jclass    cWin = env->GetObjectClass(window);
        jmethodID mDecor = env->GetMethodID(cWin, "getDecorView", "()Landroid/view/View;");
        if (mDecor) decor = env->CallObjectMethod(window, mDecor);
    }

    if (imm && decor) {
        jclass cImm = env->GetObjectClass(imm);
        jclass cView = env->GetObjectClass(decor);

        if (show) {
            //  showSoftInput, not toggleSoftInputFromWindow.
            //
            //  Toggle was the first thing that worked, and it is wrong: moving
            //  from the ID field to the password field calls this twice, so the
            //  keyboard appeared and then immediately went away again. Measured -
            //  mInputShown went true on the first tap and false on the second.
            //
            //  SHOW_FORCED (2) because the served view is the NativeActivity's
            //  decor view rather than a real text field, and the polite request
            //  is ignored for it.
            jmethodID mShow = env->GetMethodID(cImm, "showSoftInput",
                                               "(Landroid/view/View;I)Z");
            if (mShow) env->CallBooleanMethod(imm, mShow, decor, 2);
        } else {
            jmethodID mHide = env->GetMethodID(cImm, "hideSoftInputFromWindow",
                                               "(Landroid/os/IBinder;I)Z");
            jmethodID mToken = env->GetMethodID(cView, "getWindowToken",
                                                "()Landroid/os/IBinder;");
            jobject token = mToken ? env->CallObjectMethod(decor, mToken) : NULL;
            if (mHide && token) env->CallBooleanMethod(imm, mHide, token, 0);
        }
    }

    if (env->ExceptionCheck()) env->ExceptionClear();
    g_app->activity->vm->DetachCurrentThread();
}

//  Whether an edit box is taking input. Keys only become text while one is -
//  otherwise every movement key would type itself into the last field touched.
bool g_imeActive = false;


extern "C" void RanIME_InsertUtf8(const char *sz);
extern "C" void RanIME_Backspace(void);

extern "C" void RanIME_Show(void) { g_imeActive = true;  RanGesture_SetImeActive(1); imeCall(true); }
extern "C" void RanIME_Hide(void) { g_imeActive = false; RanGesture_SetImeActive(0); imeCall(false); }
extern "C" void RanIME_SetNumeric(int numeric) { g_imeNumeric = (numeric != 0); }

//  What the soft keyboard produced, on its way to the client's edit buffer.
//
//  Deliberately the same two calls the hardware-key path uses, so there is one
//  place where text enters the client and soft and hard keyboards cannot drift
//  apart.
extern "C" JNIEXPORT void JNICALL
Java_com_ran_launcher_RanActivity_nativeCommitText(JNIEnv *env, jclass, jstring text) {
    if (!text) return;
    const char *sz = env->GetStringUTFChars(text, NULL);
    if (sz) {
        RanIME_InsertUtf8(sz);
        env->ReleaseStringUTFChars(text, sz);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ran_launcher_RanActivity_nativeEnter(JNIEnv *, jclass) {
    //  0x1C is DIK_RETURN. The client wants the same thing a hardware Return
    //  gives it - the key down on a poll, plus the latch RanInput_TakeEnter
    //  reports - and BasicChatRightBody needs both before it will send the
    //  line. A tap rather than a down/up pair because the IME has no release
    //  to give us; see RanInput_KeyTap.
    RanInput_KeyTap(0x1C);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ran_launcher_RanActivity_nativeBackspace(JNIEnv *, jclass) {
    RanIME_Backspace();
}

//  How much of the bottom of the window the soft keyboard covers, in
//  thousandths of the window height.
//
//  The window itself does not shrink - windowSoftInputMode is adjustNothing,
//  deliberately, because letting Android resize it churns the surface and the
//  client is not built to be resized mid-frame. So the keyboard height has to be
//  asked for, and whatever wants to stay visible moves itself.
//
//  Throttled: this is a JNI round trip and the caller is a per-frame layout pass.
extern "C" int RanPlat_ImeInsetPerMille(void) {
    static int  s_cached = 0;
    static long s_lastMs = -1000;

    if (!g_imeActive) { s_cached = 0; return 0; }
    if (!g_app || !g_app->activity) return s_cached;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const long nowMs = (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    if (nowMs - s_lastMs < 100) return s_cached;
    s_lastMs = nowMs;

    JNIEnv *env = NULL;
    if (g_app->activity->vm->AttachCurrentThread(&env, NULL) != JNI_OK || !env) return s_cached;

    do {
        jclass    cAct = env->GetObjectClass(g_app->activity->clazz);
        jmethodID mWin = env->GetMethodID(cAct, "getWindow", "()Landroid/view/Window;");
        jobject   win  = mWin ? env->CallObjectMethod(g_app->activity->clazz, mWin) : NULL;
        if (!win) break;

        jclass    cWin   = env->GetObjectClass(win);
        jmethodID mDecor = env->GetMethodID(cWin, "getDecorView", "()Landroid/view/View;");
        jobject   decor  = mDecor ? env->CallObjectMethod(win, mDecor) : NULL;
        if (!decor) break;

        jclass    cView = env->GetObjectClass(decor);
        jmethodID mRoot = env->GetMethodID(cView, "getRootWindowInsets", "()Landroid/view/WindowInsets;");
        jobject   ins   = mRoot ? env->CallObjectMethod(decor, mRoot) : NULL;
        if (!ins) break;

        //  WindowInsets.Type.ime() is a static int; ask for it rather than
        //  hard-coding, because it is not part of the documented ABI.
        jclass    cType = env->FindClass("android/view/WindowInsets$Type");
        jmethodID mIme  = cType ? env->GetStaticMethodID(cType, "ime", "()I") : NULL;
        if (!mIme) break;
        const jint imeType = env->CallStaticIntMethod(cType, mIme);

        jclass    cIns    = env->GetObjectClass(ins);
        jmethodID mGetIns = env->GetMethodID(cIns, "getInsets", "(I)Landroid/graphics/Insets;");
        jobject   got     = mGetIns ? env->CallObjectMethod(ins, mGetIns, imeType) : NULL;
        if (!got) break;

        jclass  cI  = env->GetObjectClass(got);
        jfieldID fB = env->GetFieldID(cI, "bottom", "I");
        if (!fB) break;
        const int insetPx = (int)env->GetIntField(got, fB);

        //  As a fraction of the window, not raw pixels.
        //
        //  The caller works in the client's logical size, and RanGL_Height()
        //  reports that logical size too - not the panel's. Handing back device
        //  pixels made the two disagree by the UI scale factor and the chat flew
        //  to the top of the screen. A ratio has no units to get wrong.
        jmethodID mH = env->GetMethodID(cView, "getHeight", "()I");
        const int viewH = mH ? (int)env->CallIntMethod(decor, mH) : 0;
        if (viewH > 0) s_cached = (insetPx * 1000) / viewH;
    } while (0);

    if (env->ExceptionCheck()) env->ExceptionClear();
    g_app->activity->vm->DetachCurrentThread();
    return s_cached;
}

//  What the system says this key types, on the layout actually in use.
//
//  The table below only knows a US layout, so it can only ever produce the
//  ASCII an account name is made of. KeyCharacterMap is what the platform uses
//  itself, so it gives the right character for whatever layout is selected -
//  Thai included - and returns 0 when the key types nothing.
//
//  Cached per device: loading the map is a Java call and this is the input
//  path. A device id of -1 means the map could not be loaded, so the table is
//  used instead and nothing is worse than it was.
//
//  This is not the whole of Thai input. A keyboard that *composes* - which most
//  Thai IMEs do - commits its text through an InputConnection, and a plain
//  NativeActivity has none to commit to; that needs a Java Activity of our own
//  and a dex step in the build. This covers every layout that sends key events.
int unicodeForKey(android_app *app, int32_t deviceId, int32_t keyCode, int32_t meta) {
    if (!app || !app->activity || !app->activity->vm) return 0;

    JNIEnv *env = NULL;
    if (app->activity->vm->AttachCurrentThread(&env, NULL) != JNI_OK || !env) return 0;

    static jobject  s_map = NULL;       // global ref to the KeyCharacterMap
    static int32_t  s_mapDevice = -2;   // which device it was loaded for
    static jmethodID s_get = NULL;

    int result = 0;
    do {
        if (s_mapDevice != deviceId) {
            if (s_map) { env->DeleteGlobalRef(s_map); s_map = NULL; }
            s_get = NULL;
            s_mapDevice = deviceId;

            jclass cls = env->FindClass("android/view/KeyCharacterMap");
            if (!cls) break;
            jmethodID load = env->GetStaticMethodID(cls, "load",
                                "(I)Landroid/view/KeyCharacterMap;");
            if (!load) break;
            jobject local = env->CallStaticObjectMethod(cls, load, (jint)deviceId);
            if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
            if (!local) break;
            s_map = env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
            s_get = env->GetMethodID(cls, "get", "(II)I");
        }

        if (!s_map || !s_get) break;
        result = (int)env->CallIntMethod(s_map, s_get, (jint)keyCode, (jint)meta);
        if (env->ExceptionCheck()) { env->ExceptionClear(); result = 0; }
    } while (0);

    //  Any pending exception makes the next JNI call abort the process, so it
    //  is cleared before leaving rather than at each early exit.
    if (env->ExceptionCheck()) env->ExceptionClear();
    app->activity->vm->DetachCurrentThread();
    return result;
}

//  A codepoint as UTF-8. The client's edit boxes take UTF-8, so anything the
//  layout produces can be handed straight over.
int utf8Encode(int cp, char *out) {
    if (cp <= 0)        return 0;
    if (cp < 0x80)      { out[0] = (char)cp; return 1; }
    if (cp < 0x800)     { out[0] = (char)(0xC0 | (cp >> 6));
                          out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000)   { out[0] = (char)(0xE0 | (cp >> 12));
                          out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                          out[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

//  A key to the character it types, when the system could not say.
//
//  A US-layout table, used only as the fallback for unicodeForKey above.
char asciiFor(int32_t keyCode, int32_t meta) {
    const bool shift = (meta & AMETA_SHIFT_ON) != 0;

    if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
        const char base = (char)('a' + (keyCode - AKEYCODE_A));
        return shift ? (char)(base - 'a' + 'A') : base;
    }
    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
        static const char kShifted[10] = { ')', '!', '@', '#', '$', '%', '^', '&', '*', '(' };
        const int d = keyCode - AKEYCODE_0;
        return shift ? kShifted[d] : (char)('0' + d);
    }
    switch (keyCode) {
        case AKEYCODE_SPACE:         return ' ';
        case AKEYCODE_PERIOD:        return shift ? '>' : '.';
        case AKEYCODE_COMMA:         return shift ? '<' : ',';
        case AKEYCODE_MINUS:         return shift ? '_' : '-';
        case AKEYCODE_EQUALS:        return shift ? '+' : '=';
        case AKEYCODE_AT:            return '@';
        case AKEYCODE_SLASH:         return shift ? '?' : '/';
        case AKEYCODE_SEMICOLON:     return shift ? ':' : ';';
        case AKEYCODE_LEFT_BRACKET:  return shift ? '{' : '[';
        case AKEYCODE_RIGHT_BRACKET: return shift ? '}' : ']';
        case AKEYCODE_STAR:          return '*';
        case AKEYCODE_POUND:         return '#';
        case AKEYCODE_PLUS:          return '+';
        default: break;
    }
    return 0;
}

void goFullscreen(android_app *app) {
    ANativeActivity_setWindowFlags(app->activity,
        AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON, 0);

    JNIEnv *env = NULL;
    if (app->activity->vm->AttachCurrentThread(&env, NULL) != JNI_OK || !env) return;

    //  activity.getWindow().getDecorView().setSystemUiVisibility(flags)
    jclass    cActivity = env->GetObjectClass(app->activity->clazz);
    jmethodID mGetWindow = env->GetMethodID(cActivity, "getWindow", "()Landroid/view/Window;");
    jobject   window = mGetWindow ? env->CallObjectMethod(app->activity->clazz, mGetWindow) : NULL;

    if (window) {
        jclass    cWindow = env->GetObjectClass(window);
        jmethodID mGetDecor = env->GetMethodID(cWindow, "getDecorView", "()Landroid/view/View;");
        jobject   decor = mGetDecor ? env->CallObjectMethod(window, mGetDecor) : NULL;

        if (decor) {
            jclass    cView = env->GetObjectClass(decor);
            jmethodID mSetUi = env->GetMethodID(cView, "setSystemUiVisibility", "(I)V");
            if (mSetUi) {
                const jint kFlags =
                    0x00000002 |    // HIDE_NAVIGATION
                    0x00000004 |    // FULLSCREEN
                    0x00000100 |    // LAYOUT_STABLE
                    0x00000200 |    // LAYOUT_HIDE_NAVIGATION
                    0x00000400 |    // LAYOUT_FULLSCREEN
                    0x00001000;     // IMMERSIVE_STICKY
                env->CallVoidMethod(decor, mSetUi, kFlags);
            }
        }
    }

    //  Clear before going on. setSystemUiVisibility above throws on this
    //  device - a View method off the UI thread - and ART aborts the whole
    //  process the moment any JNI function is called with an exception still
    //  pending. That was a SIGABRT in onAppCmd on every focus change, which is
    //  what happens when the soft keyboard opens: tapping chat killed the game.
    if (env->ExceptionCheck()) env->ExceptionClear();

    //  WindowInsetsController is the supported route from Android 11 on, and
    //  setSystemUiVisibility is deprecated there. Ask for it as well: on this
    //  tablet the legacy call alone left the gesture pill on screen.
    if (window) {
        jclass    cWindow = env->GetObjectClass(window);
        jmethodID mDecorFits = env->GetMethodID(cWindow, "setDecorFitsSystemWindows", "(Z)V");
        if (mDecorFits) env->CallVoidMethod(window, mDecorFits, JNI_FALSE);
        if (env->ExceptionCheck()) env->ExceptionClear();

        jmethodID mGetCtl = env->GetMethodID(cWindow, "getInsetsController",
                                             "()Landroid/view/WindowInsetsController;");
        jobject ctl = mGetCtl ? env->CallObjectMethod(window, mGetCtl) : NULL;
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (ctl) {
            jclass cCtl = env->GetObjectClass(ctl);

            //  WindowInsets.Type.systemBars() is statusBars()|navigationBars() = 1|2.
            jmethodID mHide = env->GetMethodID(cCtl, "hide", "(I)V");
            if (mHide) env->CallVoidMethod(ctl, mHide, (jint)(1 | 2));
            if (env->ExceptionCheck()) env->ExceptionClear();

            //  BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE = 2: a swipe reveals them
            //  briefly instead of pinning them back permanently.
            jmethodID mBehav = env->GetMethodID(cCtl, "setSystemBarsBehavior", "(I)V");
            if (mBehav) env->CallVoidMethod(ctl, mBehav, (jint)2);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
    }

    //  Anything above can throw - a View method called off the UI thread is
    //  refused on some versions. Say so rather than swallowing it: this was
    //  failing silently, which is why the bars stayed and there was nothing to
    //  read. Still cleared, because leaving a pending exception would take the
    //  process down at the next JNI call.
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOGE("fullscreen: a JNI call threw - system bars may stay visible");
    }
    app->activity->vm->DetachCurrentThread();
}

// Touch becomes a mouse button. The pointer is moved before the press because
// the UI hit-tests using the current position and a touch delivers both at the
// same instant.
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
        //  Pointer ids keep fingers distinct, so the stick and a button can
        //  be held at once - which is the entire point of the layout.
        const int32_t idx = (AMotionEvent_getAction(event) &
                             AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                            AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        const int pid = (int)AMotionEvent_getPointerId(event, idx);
        const int px = (int)AMotionEvent_getX(event, idx) / scale;
        const int py = (int)AMotionEvent_getY(event, idx) / scale;

        switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                if (RanTouch_PointerDown(pid, (float)px, (float)py)) return 1;
                RanGesture_Down(px, py);
                return 1;

            case AMOTION_EVENT_ACTION_MOVE: {
                //  A move event carries every finger at once, so each is offered
                //  in turn; only an unclaimed one drives the mouse.
                const size_t count = AMotionEvent_getPointerCount(event);
                bool claimedAny = false;
                for (size_t i = 0; i < count; ++i) {
                    const int mid = (int)AMotionEvent_getPointerId(event, i);
                    const int mx = (int)AMotionEvent_getX(event, i) / scale;
                    const int my = (int)AMotionEvent_getY(event, i) / scale;
                    if (RanTouch_PointerMove(mid, (float)mx, (float)my)) { claimedAny = true; continue; }
                    RanGesture_Move(mx, my);
                }
                (void)claimedAny;
                return 1;
            }

            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                if (RanTouch_PointerUp(pid, (float)px, (float)py)) return 1;
                RanGesture_Up(px, py);
                return 1;

            default:
                return 0;
        }
    }

    if (type == AINPUT_EVENT_TYPE_KEY) {
        const int32_t action = AKeyEvent_getAction(event);
        const int32_t keyCode = AKeyEvent_getKeyCode(event);

        //  While a field is open, keys are text first.
        if (g_imeActive && action == AKEY_EVENT_ACTION_DOWN) {
            if (keyCode == AKEYCODE_DEL) { RanIME_Backspace(); return 1; }

            const int32_t meta = AKeyEvent_getMetaState(event);

            //  Ask the platform what this key types on the current layout
            //  before falling back to the US table.
            const int cp = unicodeForKey(g_app, AInputEvent_getDeviceId(event),
                                         keyCode, meta);
            if (cp > 0) {
                char sz[5] = { 0, 0, 0, 0, 0 };
                if (utf8Encode(cp, sz) > 0) {
                    RanIME_InsertUtf8(sz);
                    return 1;
                }
            }

            const char c = asciiFor(keyCode, meta);
            if (c) {
                const char sz[2] = { c, 0 };
                RanIME_InsertUtf8(sz);
                return 1;
            }
        }

        const int scan = scanCodeFor(keyCode);
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
        //  Re-applied on focus, not just once: sticky immersive lets the bars
        //  back briefly on a swipe, and anything that takes focus away (a
        //  notification, the recents switcher) restores them for good.
        case APP_CMD_GAINED_FOCUS:
            goFullscreen(app);
            break;

        //  Sound follows the activity, not the window. The frame loop already
        //  blocks when the surface goes away, but the audio thread does not -
        //  it is the mixer callback that drives it - so without this the music
        //  keeps playing over whatever the player switched to.
        case APP_CMD_PAUSE:
            RanAudioSink_Pause(1);
            break;
        case APP_CMD_RESUME:
            RanAudioSink_Pause(0);
            break;

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
                //  The overlay works in logical pixels, like the client and
                //  like the touches it is fed, which are already divided by
                //  InputScale. The frame itself is larger than that now; the
                //  overlay normalises by these numbers in its own shader, so
                //  its geometry still rasterises at the full panel resolution.
                RanTouch_Init(RanGL_LogicalWidth(), RanGL_LogicalHeight());

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
    g_app = app;
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

            //  Put something on screen before the client boots. RanApp_Boot
            //  loads for many seconds with no device of its own yet, so without
            //  this the window is black for the whole of it.
            RanSplash_Begin(root);

            const int bootOk = RanApp_Boot(root, state.width, state.height);
            RanSplash_End();
            if (bootOk) {
                state.booted = true;
            } else {
                LOGE("boot failed — stopping");
                break;
            }
        }

        if (state.booted) {
            RanGesture_Tick();
            //  One button transition per frame, so every press and release is
            //  visible to the client for at least one frame.
            RanInput_PumpButtons();
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
