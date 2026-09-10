//  The touch-to-mouse gesture layer, shared by every platform.
//
//  This used to live inside platform/android/android_main.cpp, which meant iOS
//  had none of it: a touch there pressed the left button on the way down and
//  released it on the way up, so there was no long press, no drag, no camera
//  look, no window dragging and no pinch. Everything the Android build had
//  learned about touch was in a file iOS does not compile.
//
//  Nothing in here is Android-specific. The platform layer's whole job is to
//  turn its own events into coordinates and call these four functions.
#ifndef RAN_TOUCH_GESTURE_H
#define RAN_TOUCH_GESTURE_H

#ifdef __cplusplus
extern "C" {
#endif

//  Coordinates are in the space the frame is laid out in - panel pixels divided
//  by RanGL_InputScale() - which is the same space RanTouch_Init was given and
//  the client's own hit tests use.
//
//  Call Down/Move/Up only for touches the overlay did NOT claim; RanTouch_*
//  gets first refusal, exactly as before.
void RanGesture_Down ( int x, int y );
void RanGesture_Move ( int x, int y );
void RanGesture_Up   ( int x, int y );

//  Once a frame. A finger that rests still generates no events at all, so this
//  is the only place a hold can be noticed.
void RanGesture_Tick ( void );

//  Whether an edit box currently has the keyboard up. A press outside the field
//  being edited puts it away; the client only ends an edit when focus moves to
//  another box, so without this the keyboard sits over half the screen.
void RanGesture_SetImeActive ( int active );

#ifdef __cplusplus
}
#endif

#endif
