// DirectInput, fed by Android touch and key events.
//
// The engine reads input the way it always has — CreateDevice, SetDataFormat,
// Acquire, then GetDeviceData for a buffered stream of DIDEVICEOBJECTDATA, or
// GetDeviceState for the immediate state. Rather than divert DxInputDevice, the
// device it asks for is provided here and the platform layer posts events into
// it, so the whole engine-side input path (key repeat, click timing, cursor
// tracking, the UI's hit testing) runs exactly as on PC.
//
// Touch maps to the left mouse button, which is what the outer UI listens for:
//   ACTION_DOWN  -> move the pointer, then press
//   ACTION_MOVE  -> move
//   ACTION_UP    -> release
// The pointer is moved BEFORE the press because the UI hit-tests on the button
// event using the current position, and a phone gives both at once.

#include "windows.h"
#include <dinput.h>

#include <android/log.h>
#include <pthread.h>
#include <string.h>
#include <deque>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "RanInput", __VA_ARGS__)

namespace {

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

struct Lock {
    Lock() { pthread_mutex_lock(&g_lock); }
    ~Lock() { pthread_mutex_unlock(&g_lock); }
};

// Buffered events waiting for the next GetDeviceData, plus the immediate state
// GetDeviceState reports.
std::deque<DIDEVICEOBJECTDATA> g_mouseQueue;
std::deque<DIDEVICEOBJECTDATA> g_keyQueue;

DIMOUSESTATE2 g_mouseState;
BYTE          g_keyState[256];

int  g_pointerX = 0, g_pointerY = 0;
DWORD g_sequence = 1;

void pushMouse(DWORD ofs, DWORD data) {
    DIDEVICEOBJECTDATA e;
    memset(&e, 0, sizeof(e));
    e.dwOfs = ofs;
    e.dwData = data;
    e.dwTimeStamp = GetTickCount();
    e.dwSequence = g_sequence++;
    g_mouseQueue.push_back(e);
    if (g_mouseQueue.size() > 256) g_mouseQueue.pop_front();
}

void pushKey(BYTE scanCode, bool down) {
    DIDEVICEOBJECTDATA e;
    memset(&e, 0, sizeof(e));
    e.dwOfs = scanCode;
    e.dwData = down ? 0x80 : 0x00;
    e.dwTimeStamp = GetTickCount();
    e.dwSequence = g_sequence++;
    g_keyQueue.push_back(e);
    if (g_keyQueue.size() > 256) g_keyQueue.pop_front();
}

class RanInputDevice : public IDirectInputDevice8A {
public:
    LONG m_ref;
    bool m_isMouse;

    explicit RanInputDevice(bool isMouse) : m_ref(1), m_isMouse(isMouse) {}

    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    HRESULT __stdcall GetCapabilities(LPDIDEVCAPS) { return DI_OK; }
    HRESULT __stdcall EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA, LPVOID, DWORD) { return DI_OK; }
    HRESULT __stdcall GetProperty(REFGUID, LPDIPROPHEADER) { return DI_OK; }
    HRESULT __stdcall SetProperty(REFGUID, LPCDIPROPHEADER) { return DI_OK; }
    HRESULT __stdcall Acquire() { return DI_OK; }
    HRESULT __stdcall Unacquire() { return DI_OK; }

    HRESULT __stdcall GetDeviceState(DWORD cbData, LPVOID lpvData) {
        if (!lpvData) return DIERR_INVALIDPARAM;
        Lock lk;
        if (m_isMouse) {
            if (cbData < sizeof(DIMOUSESTATE2)) return DIERR_INVALIDPARAM;
            memcpy(lpvData, &g_mouseState, sizeof(DIMOUSESTATE2));
            // Relative axes are consumed by reading them.
            g_mouseState.lX = g_mouseState.lY = g_mouseState.lZ = 0;
        } else {
            if (cbData > sizeof(g_keyState)) cbData = sizeof(g_keyState);
            memcpy(lpvData, g_keyState, cbData);
        }
        return DI_OK;
    }

    HRESULT __stdcall GetDeviceData(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod,
                                    LPDWORD pdwInOut, DWORD dwFlags) {
        if (!pdwInOut) return DIERR_INVALIDPARAM;
        if (cbObjectData != sizeof(DIDEVICEOBJECTDATA)) return DIERR_INVALIDPARAM;

        Lock lk;
        std::deque<DIDEVICEOBJECTDATA> &q = m_isMouse ? g_mouseQueue : g_keyQueue;

        // A null buffer means "how many are waiting"; DIGDD_PEEK means "do not
        // consume". Both are part of the contract the engine relies on.
        if (!rgdod) { *pdwInOut = (DWORD)q.size(); return DI_OK; }

        DWORD want = *pdwInOut;
        DWORD n = 0;
        for (; n < want && n < q.size(); ++n) rgdod[n] = q[n];
        if (!(dwFlags & DIGDD_PEEK)) q.erase(q.begin(), q.begin() + n);
        *pdwInOut = n;
        return DI_OK;
    }

    HRESULT __stdcall SetDataFormat(LPCDIDATAFORMAT) { return DI_OK; }
    HRESULT __stdcall SetEventNotification(HANDLE) { return DI_OK; }
    HRESULT __stdcall SetCooperativeLevel(HWND, DWORD) { return DI_OK; }
    HRESULT __stdcall GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA, DWORD, DWORD) { return DI_OK; }
    HRESULT __stdcall GetDeviceInfo(LPDIDEVICEINSTANCEA) { return DI_OK; }
    HRESULT __stdcall RunControlPanel(HWND, DWORD) { return DI_OK; }
    HRESULT __stdcall Initialize(HINSTANCE, DWORD, REFGUID) { return DI_OK; }
    HRESULT __stdcall CreateEffect(REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT *, LPUNKNOWN) { return DIERR_UNSUPPORTED; }
    HRESULT __stdcall EnumEffects(LPDIENUMEFFECTSCALLBACKA, LPVOID, DWORD) { return DI_OK; }
    HRESULT __stdcall GetEffectInfo(LPDIEFFECTINFOA, REFGUID) { return DI_OK; }
    HRESULT __stdcall GetForceFeedbackState(LPDWORD) { return DIERR_UNSUPPORTED; }
    HRESULT __stdcall SendForceFeedbackCommand(DWORD) { return DIERR_UNSUPPORTED; }
    HRESULT __stdcall EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, LPVOID, DWORD) { return DI_OK; }
    HRESULT __stdcall Escape(LPDIEFFESCAPE) { return DI_OK; }
    HRESULT __stdcall Poll() { return DI_OK; }
    HRESULT __stdcall SendDeviceData(DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD) { return DI_OK; }
    HRESULT __stdcall EnumEffectsInFile(LPCSTR, LPDIENUMEFFECTSINFILECALLBACK, LPVOID, DWORD) { return DI_OK; }
    HRESULT __stdcall WriteEffectToFile(LPCSTR, DWORD, LPDIFILEEFFECT, DWORD) { return DI_OK; }
    HRESULT __stdcall BuildActionMap(LPDIACTIONFORMATA, LPCSTR, DWORD) { return DI_OK; }
    HRESULT __stdcall SetActionMap(LPDIACTIONFORMATA, LPCSTR, DWORD) { return DI_OK; }
    HRESULT __stdcall GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA) { return DI_OK; }
};

class RanDirectInput : public IDirectInput8A {
public:
    LONG m_ref;
    RanDirectInput() : m_ref(1) {}

    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    HRESULT __stdcall CreateDevice(REFGUID rguid, LPDIRECTINPUTDEVICE8A *lplpDirectInputDevice,
                                   LPUNKNOWN) {
        if (!lplpDirectInputDevice) return DIERR_INVALIDPARAM;
        // GUID_SysMouse vs GUID_SysKeyboard differ in their first DWORD.
        const bool isMouse = (rguid.Data1 == 0x6f1d2b60);
        *lplpDirectInputDevice = new RanInputDevice(isMouse);
        return DI_OK;
    }
    HRESULT __stdcall EnumDevices(DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD) { return DI_OK; }
    HRESULT __stdcall GetDeviceStatus(REFGUID) { return DI_OK; }
    HRESULT __stdcall RunControlPanel(HWND, DWORD) { return DI_OK; }
    HRESULT __stdcall Initialize(HINSTANCE, DWORD) { return DI_OK; }
    HRESULT __stdcall FindDevice(REFGUID, LPCSTR, LPGUID) { return DIERR_NOTFOUND; }
    HRESULT __stdcall EnumDevicesBySemantics(LPCSTR, LPDIACTIONFORMATA, LPDIENUMDEVICESBYSEMANTICSCBA,
                                             LPVOID, DWORD) { return DI_OK; }
    HRESULT __stdcall ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK, LPDICONFIGUREDEVICESPARAMSA,
                                       DWORD, LPVOID) { return DI_OK; }
};

} // namespace

// ------------------------------------------------------- platform entry points
extern "C" void RanInput_PointerMove(int x, int y) {
    Lock lk;
    // The engine tracks both an absolute position and a relative delta.
    g_mouseState.lX += x - g_pointerX;
    g_mouseState.lY += y - g_pointerY;
    g_pointerX = x;
    g_pointerY = y;
    pushMouse(DIMOFS_X, (DWORD)(x - g_pointerX));
    pushMouse(DIMOFS_Y, (DWORD)(y - g_pointerY));
}

extern "C" void RanInput_PointerButton(int button, int down) {
    Lock lk;
    if (button < 0 || button > 2) return;
    g_mouseState.rgbButtons[button] = down ? 0x80 : 0x00;
    pushMouse(DIMOFS_BUTTON0 + button, down ? 0x80 : 0x00);
}

extern "C" void RanInput_Key(int scanCode, int down) {
    Lock lk;
    if (scanCode < 0 || scanCode > 255) return;
    g_keyState[scanCode] = down ? 0x80 : 0x00;
    pushKey((BYTE)scanCode, down != 0);
}

extern "C" void RanInput_PointerAbsolute(int *x, int *y) {
    Lock lk;
    if (x) *x = g_pointerX;
    if (y) *y = g_pointerY;
}

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE, DWORD, REFIID, LPVOID *ppvOut, LPUNKNOWN) {
    if (!ppvOut) return E_POINTER;
    *ppvOut = new RanDirectInput();
    return DI_OK;
}
