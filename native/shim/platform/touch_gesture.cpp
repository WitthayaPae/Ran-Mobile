//  Touch-to-mouse gestures. Moved here verbatim from android_main.cpp so that
//  every platform gets the same behaviour; the comments are the ones written
//  when each rule was worked out on a device, and they are kept because each
//  one records a bug that is easy to reintroduce.
#include "touch_gesture.h"
#include "ran_plat.h"

#include <time.h>
#include <stdint.h>

extern "C" {
void RanInput_PointerMove ( int x, int y );
void RanInput_PointerButton ( int button, int down );
int  RanTouch_IsPinching ( void );
int  RanUI_PointInControl ( int x, int y );
void RanUI_EndEditIfOutside ( int x, int y );
}

namespace {

//  A touch screen has no second button, so a long press stands in for it.
//
//  The press cannot be sent on touch-down, because by the time the hold is long
//  enough to count a left click would already have happened. So it is deferred:
//  a finger that moves is a drag and presses left as soon as it moves, a finger
//  that lifts early presses left then releases, and a finger that stays put
//  presses right when the timer expires. The pointer still moves on touch-down,
//  so hover and tooltips behave exactly as before.
struct TouchGesture {
    bool    active  = false;
    bool    pressed = false;    // a button is down for this touch
    bool    moved   = false;    // travelled far enough that this is not a hold
    int     button  = 0;        // which one
    int     x = 0, y = 0;       // where it started
    int64_t downMs  = 0;
} g_gesture;

bool g_imeActive = false;

//  Long enough not to fire on a normal tap, short enough not to feel stuck.
const int64_t kLongPressMs = 450;

//  Past this the touch is a drag, not a hold or a tap, however long it lasts.
//
//  16 was far too tight. A finger resting on glass wanders further than that
//  just from the contact patch shifting, so ordinary taps were being promoted to
//  drags - which is why tapping a window's close button dragged the window
//  instead of closing it.
const int kDragSlop = 30;

//  Movement that rules out a hold, well before it counts as a drag.
//
//  The hold used to fire on time alone: hold still for 450 ms and the right
//  button goes down wherever the finger is. Turning the camera slowly does not
//  cover 30 px in 450 ms, so the hold fired first - and with a crowd on screen
//  the finger is almost always over another player, so dragging to look around
//  opened that player's menu instead. The two gestures start identically and
//  only movement tells them apart, so any real movement now cancels the hold
//  and leaves the touch to become a drag when it crosses kDragSlop.
//
//  Below the resting wander a fingertip shows anyway (the contact patch shifts
//  several pixels without the finger moving), or a genuine long press would
//  stop working.
const int kHoldSlop = 10;

int64_t nowMs() {
    struct timespec ts;
    clock_gettime ( CLOCK_MONOTONIC, &ts );
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

//  Press at the point the finger went DOWN, not wherever it is now.
//
//  The client records a window's grab offset on the button-down, so pressing at
//  the current position after the finger had already travelled made the window
//  jump by however far that was. Putting the pointer back first reproduces a
//  real press-then-drag: down where you touched, then movement.
void gesturePress ( int button ) {
    g_gesture.pressed = true;
    g_gesture.button  = button;

    //  One line per touch, and the only place that says which of the three
    //  gestures a touch turned into. Reading it off the screen does not work:
    //  a tap on a player and a long press on a player open menus that look the
    //  same in a screenshot, and a crowd of moving characters swamps any
    //  attempt to see the camera turn by comparing frames.
    RanPlat_Log ( RANLOG_INFO, "RanTouch",
                  "GESTURE %s at (%d,%d) after %dms, moved=%d",
                  button == 0 ? "left(tap/drag)" :
                  button == 1 ? "right(hold)"    : "middle(camera)",
                  g_gesture.x, g_gesture.y,
                  (int)( nowMs () - g_gesture.downMs ), g_gesture.moved ? 1 : 0 );

    RanInput_PointerMove ( g_gesture.x, g_gesture.y );
    RanInput_PointerButton ( button, 1 );
}

}   // namespace

extern "C" void RanGesture_SetImeActive ( int active ) { g_imeActive = ( active != 0 ); }

extern "C" void RanGesture_Tick ( void ) {
    if (!g_gesture.active || g_gesture.pressed) return;
    if (g_gesture.moved) return;        // a drag in progress, not a hold
    if (nowMs() - g_gesture.downMs < kLongPressMs) return;
    gesturePress ( 1 );                 // right
}

extern "C" void RanGesture_Down ( int x, int y ) {
    //  A second finger means a pinch is starting. Anything already dragging has
    //  to let go now, before the pinch moves.
    if (RanTouch_IsPinching() && g_gesture.pressed) {
        RanInput_PointerButton ( g_gesture.button, 0 );
        g_gesture.pressed = false;
        g_gesture.active  = false;
    }

    RanInput_PointerMove ( x, y );
    //  A press outside the field being edited puts the keyboard away. Nothing
    //  in the client does this: it only ends an edit when you move to another
    //  box, so the keyboard would sit there over half the screen.
    if (g_imeActive) RanUI_EndEditIfOutside ( x, y );

    //  No button yet - see TouchGesture. The move alone is what drives hover
    //  and tooltips.
    g_gesture.active  = true;
    g_gesture.pressed = false;
    g_gesture.moved   = false;
    g_gesture.x       = x;
    g_gesture.y       = y;
    g_gesture.downMs  = nowMs();
}

extern "C" void RanGesture_Move ( int x, int y ) {
    RanInput_PointerMove ( x, y );

    //  A pinch is two fingers moving, and that movement would otherwise cross
    //  the drag threshold and press the middle button - which is the camera
    //  rotate binding. Zooming turned the view at the same time. A pinch is a
    //  zoom and nothing else.
    if (RanTouch_IsPinching()) {
        if (g_gesture.pressed) {
            //  Already dragging when the second finger landed: let go, or the
            //  rotation continues through the whole pinch.
            RanInput_PointerButton ( g_gesture.button, 0 );
            g_gesture.pressed = false;
        }
        g_gesture.active = false;
        return;
    }

    if (g_gesture.active && !g_gesture.pressed) {
        const int dx = x - g_gesture.x, dy = y - g_gesture.y;
        //  Past the resting wander: whatever this turns out to be, it is not a
        //  long press. Noted before the drag test, because the gap between the
        //  two thresholds is exactly where a slow camera drag used to be
        //  mistaken for a hold.
        if (dx * dx + dy * dy > kHoldSlop * kHoldSlop) g_gesture.moved = true;
        if (dx * dx + dy * dy > kDragSlop * kDragSlop) {
            //  Moved far enough to be a drag. What that means depends on what
            //  is under the finger:
            //
            //    on a control - left, so items and scrollbars drag;
            //    on the world - middle, which is what DxViewPort reads for
            //    camera rotation. That is the free look.
            //
            //  Ask where the finger IS, not where the pointer was.
            //  RanUI_MouseInControl answers for the end of the last frame, and
            //  on touch that is wherever the previous tap left the pointer - so
            //  a drag starting on a window title read as "not on a control" and
            //  pressed the middle button, which turns the camera. That is why
            //  no window could be dragged unless something had already been
            //  tapped inside it.
            gesturePress ( RanUI_PointInControl ( g_gesture.x, g_gesture.y ) ? 0 : 2 );
            RanInput_PointerMove ( x, y );
        }
    }
}

extern "C" void RanGesture_Up ( int x, int y ) {
    //  Lifted before the hold expired and without moving: an ordinary tap, so
    //  the left click happens now, at the point the finger went down rather
    //  than the pixel it left from. The shim holds the release back until the
    //  press has been polled, so a quick tap cannot fall between two frames and
    //  vanish.
    if (g_gesture.active && !g_gesture.pressed) gesturePress ( 0 );
    else                                        RanInput_PointerMove ( x, y );
    if (g_gesture.pressed) RanInput_PointerButton ( g_gesture.button, 0 );
    g_gesture.active  = false;
    g_gesture.pressed = false;
}
