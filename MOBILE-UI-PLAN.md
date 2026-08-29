# Mobile touch UI — investigation and plan

Joystick, on-screen attack controls, pinch zoom and real text input.

The point of this document is the first section: the client's own mechanisms for
movement, zoom and text entry, read out of the PC source. Everything else follows
from those, and guessing at them is how this kind of work goes wrong.

---

## 1. What the client already does

### Movement is destination-based, and there is a clean entry point

    GLCharacter::ActionMoveTo(float fTime,
                              D3DXVECTOR3 vFromPt, D3DXVECTOR3 vTargetPt,
                              BOOL bContinue, BOOL bREACT)
    // Lib_Client/G-Logic/GLCharacter.cpp:1428

It takes a **world-space ray**, resolves it against the navmesh, and on success
sets `m_sTargetID.vPos`, turns on `GLAT_MOVE` and sends `SNETPC_GOTO`
(`vCurPos` + `vTarPos`). `Actor::Update` then walks the character there over the
following frames.

Existing callers pass a short vertical ray at the destination:

    ActionMoveTo(0.0f, vMoveTo + D3DXVECTOR3(0,+5,0),
                       vMoveTo + D3DXVECTOR3(0,-5,0), FALSE, TRUE);
    // GLCharacterMsg.cpp:6642, GLCharactorReq.cpp:4497,
    // ItemSearchResultWindow.cpp:291

**So a joystick does not need to synthesise screen clicks.** It computes a point
offset from the player in the stick's direction and calls the same function:

    D3DXVECTOR3 dest = playerPos + dir * kStickReach;
    pChar->ActionMoveTo(0.0f, dest + D3DXVECTOR3(0,+5,0),
                              dest + D3DXVECTOR3(0,-5,0), FALSE, TRUE);

Two things already handled for us:

* `ActionMoveTo` bails when `!m_actorMove.PathIsActive()`, so pushing the stick
  into a wall simply does not move rather than looping a run animation in place.
  (There is a comment there about exactly that bug.)
* `GLCharacter` keeps `m_sLastMsgMove` / `m_sLastMsgMoveSend` and only sends when
  they differ, so re-issuing the same destination every frame does **not** spam
  the server. Re-issuing a *changing* destination will, so the stick should
  re-target on a timer or on a meaningful direction change, not per frame.

One thing to suppress: `RANPARAM::bClickEffect` spawns a click sparkle at the
destination. Correct for a mouse, wrong for a stick.

### Zoom is the mouse wheel

`DxViewPort::FrameMoveMAX` reads `dxInputDev.GetMouseMove(dx, dy, dz)` and feeds
`dz` into `CameraZoom` (`DxViewPortSetCamera.cpp:111`, `DxViewPort.cpp:360`),
with limits already applied in `CameraLimitCheck`.

`dz` comes from `m_MouseMoveDZ = m_MouseLocateZ - m_OldMouseLocateZ`, and
`m_MouseLocateZ` accumulates `DIMOFS_Z` events from DirectInput.

The shim's DirectInput emulation (`shim/platform/dinput_mobile.cpp`) has no wheel
path at all — only `RanInput_PointerMove`, `PointerButton`, `Key`. Pinch zoom is
therefore a small addition:

    extern "C" void RanInput_PointerWheel(int dz);   // pushMouse(DIMOFS_Z, dz)

### Text entry has a focus signal and a way in

`CUIEditBoxMan` (`Lib_ClientUI/Interface/UIEditBoxMan.h`) exposes:

    void    BeginEdit();                                  // focus gained
    void    EndEdit();                                    // focus lost
    void    SetEditString(UIGUID id, const CString& s);   // set text directly
    CString GetEditString();
    bool    IsMODE_NATIVE();  void DoMODE_TOGGLE();       // native-language mode

`BeginEdit` / `EndEdit` are the show/hide signal for the soft keyboard, and
`SetEditString` means composed text can be delivered as a **string** rather than
replayed as fake keystrokes — which matters, because Thai cannot be expressed as
a sequence of DIK scan codes.

---

## 2. Where the touch UI should live

**In the platform/shim layer, above the client — not in `CUIControl`.**

* These controls are input transducers. Their job is to turn a touch into
  something the client already understands, which is the shim's job.
* It keeps `SOURCE` clean; the "byte-identical unless `#ifdef RAN_MOBILE`"
  discipline is what has kept this port tractable.
* The client renders at 1280x800 and the display upscales it (`gl_context.cpp`
  picks the scale). An overlay drawn at the panel's real resolution is **sharp**
  while the world stays soft — a real gain for controls that are always on screen.
* It cannot be hidden or z-fought by a game window.

---

## 3. Phases

**Phase 0 — a bug found while investigating.** `RanInput_PointerMove` computes
its queued deltas after updating the origin:

    g_pointerX = x;
    pushMouse(DIMOFS_X, (DWORD)(x - g_pointerX));   // always zero

The accumulated `g_mouseState.lX/lY` is right, but every buffered motion event
says zero, so anything reading `GetDeviceData` for motion — camera drag-rotate —
receives nothing. Fix before building anything that relies on motion.

**Phase 1 — overlay framework.** A full-resolution layer drawn after the client's
frame: touch routing (a control claims touches inside it, everything else passes
through untouched), and two primitives — round button and thumbstick.

**Phase 2 — joystick.** Direction to `ActionMoveTo` as above, re-targeting on a
timer rather than per frame, click effect suppressed. Walk/run via the existing
`EM_ACT_RUN` state.

**Phase 3 — attack ring.** The quarter-circle at bottom right is a radial
arrangement of the Phase 1 button. Bind to the existing attack and quick-slot
paths so skills reuse it.

**Phase 4 — pinch zoom.** `RanInput_PointerWheel` plus a two-finger gesture.
Small, given the client already clamps the range.

**Phase 5 — text input.** The fiddly one. `NativeActivity` has no usable IME
story, so proper Thai entry needs a small Java layer with a hidden `EditText`
whose composed text is passed over JNI to `SetEditString`. `BeginEdit`/`EndEdit`
drive show/hide. Fallback if this is deferred: the client's own on-screen
keyboard already works — Latin only.

---

## 4. Worth fixing first

Item tooltips do not render (see STATUS.md). On a touch UI there is no hover at
all, so a long-press gesture has to drive tooltips anyway — and that bug sits in
the middle of that path.
