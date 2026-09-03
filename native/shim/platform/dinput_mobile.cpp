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
#include "ran_plat.h"
#include <dinput.h>

#include <pthread.h>
#include <string.h>
#include <deque>

#define LOGI(...) RanPlat_Log(RANLOG_INFO, "RanInput", __VA_ARGS__)

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

//  Button transitions are queued and applied one per poll.
//
//  The UI turns a press into a click only when it sees the button down on one
//  poll and up on a later one, so both halves have to be visible to a poll. Two
//  ways that fails with touch:
//
//    * A fast tap delivers down and up between two polls and the click is lost
//      entirely. This is what made the skill slots look broken - an
//      `adb shell input tap` on one did nothing, until a held press proved the
//      hit test had been right all along.
//
//    * A double click delivers down/up/down/up in a hurry. Anything that just
//      holds the latest state collapses that into one long press, and the
//      double click never happens - which broke picking a server from the list.
//
//  Applying exactly one transition per poll fixes both: every down and every up
//  is seen by at least one poll, in the order it happened. A double click then
//  takes four polls, about 130ms at 30fps, which is well inside the client's
//  double-click window.
//  A button event carries the pointer position it happened at.
//
//  Position is applied the moment it arrives; button events wait their turn,
//  one per frame. On a quick drag that means the press was delivered after the
//  finger had already moved on, so the control under the press point never saw
//  the mouse over it - a window title got a press at a point outside itself and
//  never started dragging. Travelling with its position, a press lands where it
//  was made whatever the queue did in between.
struct BtnEvent { int button; int down; int x; int y; };
BtnEvent g_btnQueue[16];
int      g_btnQueueCount = 0;

void queueButton(int button, int down);           // defined below g_pointerX


BYTE          g_keyState[256];

int  g_pointerX = 0, g_pointerY = 0;
DWORD g_sequence = 1;

void queueButton(int button, int down) {          // caller holds the lock
    if (g_btnQueueCount >= (int)(sizeof(g_btnQueue) / sizeof(g_btnQueue[0]))) return;
    g_btnQueue[g_btnQueueCount].button = button;
    g_btnQueue[g_btnQueueCount].down   = down;
    g_btnQueue[g_btnQueueCount].x      = g_pointerX;
    g_btnQueue[g_btnQueueCount].y      = g_pointerY;
    ++g_btnQueueCount;
}

//  Where the pointer really is, while a queued event borrows it for one frame.
bool g_posBorrowed = false;
int  g_posRealX = 0, g_posRealY = 0;

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
    //
    // The delta has to be taken BEFORE the origin moves. Computing it after
    // made every queued DIMOFS_X/Y event zero, so anything reading buffered
    // motion through GetDeviceData  camera drag-rotate  saw the mouse as
    // perfectly still even while it moved. The accumulated lX/lY below was
    // right, which is why it went unnoticed.
    //  While a queued button borrows the pointer for a frame, the real position
    //  lives in g_posReal*; measure and write there, so the borrow neither eats
    //  a movement nor invents one.
    int& rX = g_posBorrowed ? g_posRealX : g_pointerX;
    int& rY = g_posBorrowed ? g_posRealY : g_pointerY;

    const int dx = x - rX;
    const int dy = y - rY;
    g_mouseState.lX += dx;
    g_mouseState.lY += dy;
    rX = x;
    rY = y;
    if (dx) pushMouse(DIMOFS_X, (DWORD)dx);
    if (dy) pushMouse(DIMOFS_Y, (DWORD)dy);
}

//  The wheel. DxViewPort::FrameMoveMAX reads it as dz from GetMouseMove and
//  feeds it straight to CameraZoom, so a pinch gesture becomes a zoom by
//  arriving here. There was no wheel path at all before.
extern "C" void RanInput_PointerWheel(int dz) {
    if (!dz) return;
    Lock lk;
    g_mouseState.lZ += dz;
    pushMouse(DIMOFS_Z, (DWORD)dz);
}

extern "C" void RanInput_PointerButton(int button, int down) {
    Lock lk;
    if (button < 0 || button > 2) return;
    queueButton(button, down != 0);
}

//  Called once per frame, before the client runs.
//
//  Draining inside GetDeviceState looked natural - that is the poll, after all -
//  but it ties input delivery to the client happening to call that particular
//  API. The outer stages read the mouse a different way, so the queue never
//  drained there and the server list stopped responding to clicks entirely.
//  Pumping from the frame loop makes it independent of how any given stage reads
//  its input.
//  See RanInput_KeyTap: a soft-keyboard key held for a couple of pumps so the
//  game thread is certain to see it down before it is released.
static int g_tapScan = -1;
static int g_tapHold = 0;

extern "C" void RanInput_PumpButtons(void) {
    {
        Lock lk;
        if (g_tapScan >= 0 && --g_tapHold <= 0) {
            const int scan = g_tapScan;
            g_tapScan = -1;
            //  Released outside the lock below would be neater, but RanInput_Key
            //  takes the same non-recursive lock, so it is done inline.
            g_keyState[scan] = 0x00;
            pushKey((BYTE)scan, false);
        }
    }
    Lock lk;
    //  Hand the pointer back before anything else: last frame's event may have
    //  borrowed it. Straight assignment, no delta and no DIMOFS event - the
    //  motion between the two points was accumulated when it arrived, and
    //  counting it a second time here would spin the camera.
    if (g_posBorrowed) {
        g_pointerX = g_posRealX;
        g_pointerY = g_posRealY;
        g_posBorrowed = false;
    }

    if (g_btnQueueCount <= 0) return;

    const BtnEvent ev = g_btnQueue[0];
    for (int i = 1; i < g_btnQueueCount; ++i) g_btnQueue[i - 1] = g_btnQueue[i];
    --g_btnQueueCount;

    if (ev.x != g_pointerX || ev.y != g_pointerY) {
        g_posRealX = g_pointerX;
        g_posRealY = g_pointerY;
        g_posBorrowed = true;
        g_pointerX = ev.x;
        g_pointerY = ev.y;
    }

    g_mouseState.rgbButtons[ev.button] = ev.down ? 0x80 : 0x00;
    pushMouse(DIMOFS_BUTTON0 + ev.button, ev.down ? 0x80 : 0x00);
}

//  Return, latched from the moment it goes down until something asks.
//
//  CIMEEdit::CheckEnterKeyDown is what the client uses to tell "the user pressed
//  Return in this edit box" apart from "Return is down somewhere". On Windows the
//  IME sets it; here nothing did, so the shim's stub returned false forever and
//  chat could never be sent - BasicChatRightBody requires BOTH DIK_RETURN down
//  and CheckEnterKeyDown before it calls SEND_CHAT_MESSAGE.
//
//  Latched rather than sampled, because the down edge and the client's query do
//  not necessarily land in the same frame.
static bool g_enterLatched = false;

extern "C" int RanInput_TakeEnter(void) {
    Lock lk;
    const bool was = g_enterLatched;
    g_enterLatched = false;
    return was ? 1 : 0;
}

//  A key the soft keyboard sent, which has no separate release.
//
//  A hardware key arrives as a down now and an up frames later, and that gap is
//  what the client reads: BasicChatRightBody wants DIK_RETURN *down* on a poll.
//  The IME hands over a single action instead, so pressing and releasing it here
//  in one call would leave g_keyState clear again before the client ever looked,
//  and the chat would still not send. Held for two pumps instead - one to be
//  sure the game thread polls it, one for slack - then released like any key.
extern "C" void RanInput_Key(int scanCode, int down);   //  defined just below

extern "C" void RanInput_KeyTap(int scanCode) {
    RanInput_Key(scanCode, 1);
    Lock lk;
    g_tapScan = scanCode;
    g_tapHold = 2;
}

extern "C" void RanInput_Key(int scanCode, int down) {
    Lock lk;
    if (scanCode < 0 || scanCode > 255) return;
    g_keyState[scanCode] = down ? 0x80 : 0x00;
    //  0x1C DIK_RETURN, 0x9C DIK_NUMPADENTER - both send.
    if (down && (scanCode == 0x1C || scanCode == 0x9C)) g_enterLatched = true;
    pushKey((BYTE)scanCode, down != 0);
}

//  Move the pointer without it counting as movement.
//
//  DxInputDevice::HoldCursor pins the cursor while the camera is being dragged,
//  so each frame delta is measured from the pin rather than from wherever the
//  drag started. That only works if SetCursorPos actually moves what
//  GetCursorPos reads - and it did not: GetCursorPos returned the live touch
//  position while SetCursorPos wrote a separate variable nothing read. The pin
//  was a no-op, so every frame reported the same non-zero delta and the camera
//  kept turning while a finger rested still on the screen.
//
//  No DIMOFS events and no lX/lY accumulation: this is a teleport, not a move.
extern "C" void RanInput_WarpPointer(int x, int y) {
    Lock lk;
    //  A pin overrides a borrow: the client is placing the pointer itself.
    g_posBorrowed = false;
    g_pointerX = x;
    g_pointerY = y;
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
