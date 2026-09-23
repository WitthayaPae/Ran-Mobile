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

//  extern "C" on every entry point, and it is not decoration: ran_ios_main.mm
//  declares these inside an extern "C" block, so a C++-linkage definition here
//  gives the linker a mangled name to look for and an unmangled one to find.
//  That was five undefined symbols at the end of an otherwise complete iOS
//  build. Android never noticed because android_main.cpp includes this header
//  and agreed with it either way.

//  Called once the GL context exists.
extern "C" void RanTouch_Init(int surfaceWidth, int surfaceHeight);
extern "C" void RanTouch_Shutdown(void);

//  The surface changed size.
extern "C" void RanTouch_Resize(int surfaceWidth, int surfaceHeight);

//  Pointer events, in surface pixels. Return non-zero when the touch UI has
//  taken the event and the client should not see it.
//
//  `id` is the platform's pointer id, so multiple fingers stay distinct: the
//  stick and the attack ring can be held at once, which is the whole point.
extern "C" int RanTouch_PointerDown(int id, float x, float y);
extern "C" int RanTouch_PointerMove(int id, float x, float y);
extern "C" int RanTouch_PointerUp(int id, float x, float y);

//  Per-frame update; drives held buttons and re-targets the stick.
extern "C" void RanTouch_Frame(float elapsedSeconds);

//  Draws the controls. Called after the client's frame, before the swap.
extern "C" void RanTouch_Render(void);

//  What the game side reads each frame. Kept as plain C so the guarded hook in
//  the client can call them without pulling in any of this header's neighbours.
extern "C" int RanTouch_GetStick(float *outX, float *outY, float *outMag);
extern "C" int RanTouch_ConsumeButton(int *outSlot);

//  What ConsumeButton reports. Non-negative values are quick-slot indices, so
//  the special buttons take negative codes and the caller can switch on one
//  field.
#define RANTOUCH_SLOT_ATTACK      (-1)
//  The skill pages, one button each. They were two arrows that stepped a
//  counter; F1..F4 is what the tray itself listens for, so a button per page
//  says where you are as well as where you are going.
#define RANTOUCH_SLOT_F1          (-2)
#define RANTOUCH_SLOT_F2          (-3)
#define RANTOUCH_SLOT_AUTO        (-4)
#define RANTOUCH_SLOT_PK          (-5)
#define RANTOUCH_SLOT_PICKUP      (-6)
#define RANTOUCH_SLOT_CAMLOCK     (-7)
#define RANTOUCH_SLOT_VEHICLE     (-8)
#define RANTOUCH_SLOT_MENU        (-9)
#define RANTOUCH_SLOT_F3          (-10)
#define RANTOUCH_SLOT_F4          (-11)
#define RANTOUCH_SLOT_CHAT        (-12)

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

//  Where to put the ride button, as a fraction of the surface, and whether to
//  show it at all.
//
//  The client owns the position because the button belongs beside the chat and
//  the chat is dragged and resized; the overlay owns the size and the drawing,
//  so it comes out identical to the attack ring and the mode toggles rather
//  than merely similar to them.
extern "C" void RanTouch_SetVehicleButton(float cx, float cy, int show);

//  Where to put the chat button, and which one it is.
//
//  mode 0 hides it, 1 draws the collapse control that folds the chat away, and
//  2 the chat icon that brings it back. One button in two states rather than
//  two buttons, so it stays in the same place across the fold: the icon comes
//  up under the thumb that just put the chat away.
//  In mode 1 the button is a small plate on the window frame rather than a
//  round control, and this is its half-width as a multiple of the half-height
//  the client passes in `r`. Shared because the client places it by its RIGHT
//  edge - 5 units in from the chat's - and only the overlay knows how wide it
//  draws.
#define RANTOUCH_CHATBAR_ASPECT 1.75f

extern "C" void RanTouch_SetChatButton(float cx, float cy, float r, int mode);

//  Which skill page the tray is showing, 1..4, for the page readout.
extern "C" void RanTouch_SetSkillPage(int page);

//  The skill icons, drawn by the overlay so they can be cropped to their round
//  button. Positions and radii are in surface pixels; the UVs come straight
//  off the control the icon was taken from.
//  The potion tray's slots, drawn round to match the skill slots. Same shape
//  of hand-over: the client owns the slots and says where they are.
extern "C" void RanTouch_SetPotionIcons(int count, const unsigned *tex,
                                        const float *cx, const float *cy, const float *r,
                                        const float *u0, const float *v0,
                                        const float *u1, const float *v1);

//  The corner icons - quest box, small party frame - and what the editor did
//  to them. Set by the client each frame, read back by it the next.
extern "C" void RanTouch_SetCornerBox(int i, float cx, float cy, float r);
extern "C" void RanTouch_GetCornerAdjust(int i, float *dx, float *dy, float *scale);

//  How big the player has asked the skill slots to be.
extern "C" float RanTouch_GetSkillScale(void);

//  Per-slot size, on top of the group's: every button sizes on its own.
extern "C" float RanTouch_GetSkillSlotScale(int i);
extern "C" float RanTouch_GetPotionSlotScale(int i);

//  Where potion slot i has been dragged, in pixels, on top of the row.
extern "C" void RanTouch_GetPotionSlotOffset(int i, float *dx, float *dy);

//  The box the skill slots occupy, so the potion row can sit on top of them.
extern "C" int  RanTouch_GetSkillBounds(float *cx, float *top, float *width);

//  Where the player has dragged the potion row in the HUD editor.
extern "C" void RanTouch_GetPotionAdjust(float *dx, float *dy, float *scale);

//  The painted sheet the controls are drawn from: one texture, sixteen cells
//  in a 4 x 4 grid. Handed over by the client, which owns the texture. Pass 0
//  and the overlay falls back to drawing the controls as shapes.
extern "C" void RanTouch_SetHudSheet(unsigned tex, int w, int h);

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
extern "C" void RanTouch_SetActive(int active);
extern "C" int  RanTouch_IsActive(void);

//  The player's HUD arrangement: move, size and opacity per control group,
//  edited on screen. Layout is kGrpCount * 4 floats (dx, dy, size, opacity).
extern "C" void RanTouch_SetEditMode(int on);
extern "C" int  RanTouch_IsEditMode(void);
extern "C" int  RanTouch_GetHudLayout(float *out, int max);
extern "C" void RanTouch_SetHudLayout(const float *in, int n);
extern "C" int  RanTouch_HudSavedGeneration(void);
//  Per-slot nudge for the client's skill slots, as a fraction of the surface.
extern "C" void RanTouch_GetSkillSlotOffset(int slot, float *fx, float *fy);
//  The editor's layer, drawn after the client's interface so it is on top.
extern "C" void RanTouch_RenderEditTop(void);
