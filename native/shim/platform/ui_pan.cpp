//  Getting the field you are typing into out from under the soft keyboard.
//
//  The window never resizes: windowSoftInputMode is adjustNothing, deliberately,
//  because letting Android resize it churns the surface mid-frame and the client
//  is not built to be resized while it runs. So when the keyboard opens, nothing
//  moves, and whatever is in the bottom half of the screen is simply buried.
//  On a phone the client's own windows are centred and a keyboard clears them;
//  on the tablet the keyboard is a far larger share of the screen and it covers
//  the split window's number box.
//
//  This works out how far, and RanUI_LiftFocusedWindow moves the one window
//  that needs it - see the note on that function.
//
//  It began as a whole-layer pan, which is what Android's adjustPan does, and
//  that was wrong for a game: the health bars, the minimap and the icon bar all
//  rode up for a keyboard that had nothing to do with them. Only the window you
//  are typing into should move.
#include "ui_pan.h"

extern "C" {
int  RanUI_FocusedEditRect ( float *pTop, float *pBottom );     //  Lib_ClientUI
void RanUI_LiftFocusedWindow ( float fLift );                    //  Lib_ClientUI
int  RanPlat_ImeInsetPerMille ( void );                         //  platform layer
int  RanGL_LogicalHeight ( void );
}

namespace {

//  How far up the 2D layer is drawn, in the client's logical pixels.
float g_pan = 0.0f;

}   // namespace

extern "C" void RanUIPan_Update ( void )
{
    const int nPerMille = RanPlat_ImeInsetPerMille();

    float fTop = 0.0f, fBottom = 0.0f;
    if ( nPerMille <= 0 || !RanUI_FocusedEditRect ( &fTop, &fBottom ) )
    {
        g_pan = 0.0f;
        RanUI_LiftFocusedWindow ( 0.0f );
        return;
    }

    const float fH = (float) RanGL_LogicalHeight();
    if ( fH <= 0.0f ) { g_pan = 0.0f; RanUI_LiftFocusedWindow ( 0.0f ); return; }

    //  The inset arrives as a fraction of the window rather than pixels: the
    //  client works in logical size and the keyboard is measured in the panel's,
    //  and handing raw pixels across that gap is what once threw the chat off
    //  the top of the screen. A ratio has no units to get wrong.
    const float fKeyboard = fH * (float) nPerMille / 1000.0f;
    const float fVisible  = fH - fKeyboard;

    //  A little air under the box, so it is not flush against the keyboard.
    const float fMargin = fH * 0.02f;

    float fWant = ( fBottom + fMargin ) - fVisible;
    if ( fWant <= 0.0f )    { g_pan = 0.0f; RanUI_LiftFocusedWindow ( 0.0f ); return; }   //  already clear

    //  Never so far that the box itself leaves the top of the screen. If the
    //  keyboard is tall enough that the field cannot be shown whole, showing
    //  its top is the useful half - that is where the caret starts.
    if ( fWant > fTop )     fWant = fTop;

    g_pan = fWant;
    RanUI_LiftFocusedWindow ( fWant );
}

extern "C" float RanUIPan_Y ( void ) { return g_pan; }
