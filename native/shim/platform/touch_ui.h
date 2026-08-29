#pragma once
//  The on-screen touch controls: a movement stick, an attack ring, and pinch to
//  zoom.
//
//  These live above the client rather than inside its UI system. They are input
//  transducers - their whole job is to turn a touch into something the client
//  already understands - and keeping them out of the game code means SOURCE
//  stays byte-identical to the PC build.
//
//  Touches are offered here first. A control that owns a touch claims it and the
//  client never sees it; everything else falls through unchanged, so tapping the
//  world or the game's own windows still works.

//  Called once the GL context exists.
void RanTouch_Init(int surfaceWidth, int surfaceHeight);
void RanTouch_Shutdown(void);

//  The surface changed size.
void RanTouch_Resize(int surfaceWidth, int surfaceHeight);

//  Pointer events, in surface pixels. Return non-zero when the touch UI has
//  taken the event and the client should not see it.
//
//  `id` is the platform's pointer id, so multiple fingers stay distinct: the
//  stick and the attack ring can be held at once, which is the whole point.
int RanTouch_PointerDown(int id, float x, float y);
int RanTouch_PointerMove(int id, float x, float y);
int RanTouch_PointerUp(int id, float x, float y);

//  Per-frame update; drives held buttons and re-targets the stick.
void RanTouch_Frame(float elapsedSeconds);

//  Draws the controls. Called after the client's frame, before the swap.
void RanTouch_Render(void);

//  What the game side reads each frame. Kept as plain C so the guarded hook in
//  the client can call them without pulling in any of this header's neighbours.
extern "C" int RanTouch_GetStick(float *outX, float *outY, float *outMag);
extern "C" int RanTouch_ConsumeButton(int *outSlot);

//  What ConsumeButton reports. Non-negative values are quick-slot indices, so
//  the special buttons take negative codes and the caller can switch on one
//  field.
#define RANTOUCH_SLOT_ATTACK      (-1)
#define RANTOUCH_SLOT_PAGE_PREV   (-2)
#define RANTOUCH_SLOT_PAGE_NEXT   (-3)
#define RANTOUCH_SLOT_AUTO        (-4)
#define RANTOUCH_SLOT_PK          (-5)
#define RANTOUCH_SLOT_PICKUP      (-6)
#define RANTOUCH_SLOT_CAMLOCK     (-7)

//  Where the client put its quick-skill slots, as fractions of the surface, so
//  the overlay can draw a round rim over each one.
//
//  The slots are the client's own square controls. Their frame art is switched
//  off and this rim is drawn on top, thick enough to cover the corners of the
//  square icon underneath - which leaves a circular window onto the icon. It has
//  to be an overdraw rather than a mask because the overlay renders after the
//  client, so there is no way to put anything behind the icon.
#define RANTOUCH_MAX_SKILL_CIRCLES 10
//  filled[i] != 0 means the slot holds a skill and gets a solid face; a zero
//  draws it as glass - an outline you can still drop onto, with the game
//  showing through.
//  cool[i] is how much of the skill's delay is still to run, 0..1. It is drawn
//  as the client draws it: a dark wipe over the bottom of the button, shrinking
//  as the skill comes back.
extern "C" void RanTouch_SetSkillCircles(int count, const float *cx,
                                         const float *cy, const float *r,
                                         const int *filled, const float *cool);

//  Light a toggle button up. The overlay does not decide whether auto-target or
//  PK is on - the client owns that - so it has to be told what to draw.
extern "C" void RanTouch_SetToggle(int slot, int on);

//  Which skill page the tray is showing, 1..4, for the page readout.
extern "C" void RanTouch_SetSkillPage(int page);

//  The skill icons, drawn by the overlay so they can be cropped to their round
//  button. Positions and radii are in surface pixels; the UVs come straight
//  off the control the icon was taken from.
extern "C" void RanTouch_SetSkillIcons(int count, const unsigned *tex,
                                       const float *cx, const float *cy, const float *r,
                                       const float *u0, const float *v0,
                                       const float *u1, const float *v1);

//  Where the attack button sits, as a fraction of the surface: cx of the width,
//  cy and r of the height. Fractions rather than pixels so the client can lay
//  its own controls out around the button in its own coordinate space without
//  either side knowing the other's resolution.
//
//  This is how the quick-skill tray finds its arc. The tray is the client's
//  own control - it draws the skill icons, the cooldowns and the keys - so the
//  overlay does not duplicate it, it just tells it where to go.
extern "C" void RanTouch_GetAttackCircle(float *cx, float *cy, float *r);

//  Is a two-finger pinch in progress?
//
//  The touch layer has to know, because a pinch is two fingers moving and the
//  drag gesture would otherwise read that movement as a camera drag - so zooming
//  rotated the view at the same time.
extern "C" int RanTouch_IsPinching(void);

//  Whether the controls should be shown at all. They are hidden outside the
//  world - there is nothing to steer on the login screen, and a stick sitting
//  over the server list would only eat taps.
void RanTouch_SetActive(int active);
int  RanTouch_IsActive(void);
