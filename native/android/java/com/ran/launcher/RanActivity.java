package com.ran.launcher;

import android.app.NativeActivity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.text.InputType;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.widget.FrameLayout;

/*  The game activity, with something for the keyboard to type into.
 *
 *  The client used to be a plain NativeActivity, and raising the keyboard was
 *  done by calling InputMethodManager.showSoftInput() on the window's decor
 *  view. That has stopped working:
 *
 *      mHaveConnection=true  mBoundToMethod=true
 *      mServedInputConnection=null
 *
 *  The IME binds and the system reports it as shown, but a decor view is not an
 *  editor, so there is no InputConnection and nothing is drawn. SHOW_FORCED,
 *  which used to paper over this, was deprecated at API 33 and no longer
 *  forces anything. On Android 14 the result is a keyboard that is "shown" and
 *  invisible - which is why nothing could be typed into the shop's quantity
 *  field, or any other text box.
 *
 *  The fix is an actual text editor. ImeView below is focusable, answers
 *  onCheckIsTextEditor, and hands back an InputConnection; the IME then has a
 *  real target. Committed text and deletions are forwarded straight to the
 *  client's own edit buffer through the JNI entry points the native side
 *  already exposes for hardware keys.
 */
public class RanActivity extends NativeActivity {

    private static final String TAG = "RanIME";

    /*  NativeActivity dlopens the library itself, and that is invisible to the
     *  runtime's JNI lookup - which only knows about libraries brought in by
     *  System.loadLibrary. Without this the class links fine and every native
     *  call throws at the first keystroke:
     *
     *      UnsatisfiedLinkError: No implementation found for
     *      void com.ran.launcher.RanActivity.nativeCommitText(String)
     *
     *  even though the symbol is present and exported in libran.so. Loading it
     *  here as well costs nothing - dlopen refcounts the same mapping - and
     *  gives the runtime the handle it needs to resolve against. */
    static { System.loadLibrary("ran"); }

    private ImeView mIme;
    private boolean mNumeric = false;

    /*  Text the keyboard produced, handed to CUIEditBox's buffer. These are the
     *  same two entry points the hardware-key path already uses, so soft and
     *  hard keyboards end up in exactly the same place. */
    private static native void nativeCommitText(String text);
    private static native void nativeBackspace();
    /*  Return, for sending a chat line. The soft keyboard's action key used to
     *  just close the keyboard, so anything typed was dropped on the floor. */
    private static native void nativeEnter();

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        mIme = new ImeView(this);
        //  Deliberately NOT attached here - see attach() below.
    }

    /*  The view is attached only while a field is being typed into.
     *
     *  NativeActivity renders straight to the window surface through EGL. Adding
     *  any View to that window puts a View hierarchy on top of it, and the
     *  window stops being a candidate for the cheap composition path - measured
     *  on a Tab S9 as a drop from 120 fps to 36 with nothing on screen but a
     *  1x1 invisible view. Attaching only for the duration of an edit keeps the
     *  cost where it is invisible: while the keyboard is up the game is behind
     *  a keyboard anyway. */
    private boolean mAttached = false;

    private void attach() {
        if (mAttached) return;
        addContentView(mIme, new FrameLayout.LayoutParams(1, 1));
        mAttached = true;
    }

    private void detach() {
        if (!mAttached) return;
        ViewGroup parent = (ViewGroup) mIme.getParent();
        if (parent != null) parent.removeView(mIme);
        mAttached = false;
    }

    /*  Called from the native side when an edit box takes focus. */
    public void ranShowKeyboard(final boolean numeric) {
        runOnUiThread(new Runnable() { public void run() {
            mNumeric = numeric;
            attach();
            mIme.setVisibility(View.VISIBLE);
            mIme.setFocusableInTouchMode(true);
            mIme.requestFocus();
            //  restartInput so the IME picks up the input type for this field
            //  rather than whatever the previous one asked for.
            InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.restartInput(mIme);
                imm.showSoftInput(mIme, InputMethodManager.SHOW_IMPLICIT);
            }
        }});
    }

    /*  Hand a link to whatever browser the phone uses.
     *
     *  ACTION_VIEW rather than an in-game browser: the client's embedded web
     *  window is a Windows control the port does not have, and its fallback is
     *  ShellExecute, which the shim stubs out. A top-up page also wants the
     *  real browser anyway - it is where the player is already signed in to
     *  their bank or wallet app.
     *
     *  NEW_TASK because the link opens from an Activity context into another
     *  app's task; without it Android refuses the start.
     */
    public void ranOpenUrl(final String url) {
        runOnUiThread(new Runnable() { public void run() {
            try {
                Intent i = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(i);
            } catch (Exception e) {
                //  No browser, or a malformed link: say so rather than die. The
                //  button is not worth a crash.
                Log.e("RanActivity", "ranOpenUrl failed: " + e);
            }
        }});
    }

    public void ranHideKeyboard() {
        runOnUiThread(new Runnable() { public void run() {
            InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.hideSoftInputFromWindow(mIme.getWindowToken(), 0);
            mIme.clearFocus();
            mIme.setVisibility(View.INVISIBLE);
            detach();
        }});
    }

    /* ------------------------------------------------------------ the editor */

    private class ImeView extends View {
        ImeView(Context c) {
            super(c);
            setFocusable(true);
            setFocusableInTouchMode(true);
            setVisibility(View.INVISIBLE);
        }

        @Override public boolean onCheckIsTextEditor() { return true; }

        /*  Injected key events, which is how a test types.
         *
         *  RanInputConnection.sendKeyEvent below covers keys that arrive
         *  THROUGH an IME. "adb shell input text" does not go that way: it
         *  injects into the focused window, so the events reach the view and
         *  nothing was listening. With the emulator IME shown-but-dead - which
         *  is exactly what LDPlayer does now - there was no way to type at all,
         *  and the login script that had worked for weeks silently entered
         *  nothing. Handling them here makes typing work with no IME involved,
         *  on any device, and costs a real keyboard nothing.  */
        @Override public boolean onKeyDown(int keyCode, KeyEvent event) {
            if (keyCode == KeyEvent.KEYCODE_DEL)   { nativeBackspace(); return true; }
            if (keyCode == KeyEvent.KEYCODE_ENTER) { nativeEnter();     return true; }
            final int u = event.getUnicodeChar();
            if (u > 0) { nativeCommitText(String.valueOf((char) u)); return true; }
            return super.onKeyDown(keyCode, event);
        }

        /*  A string injected in one go arrives as ACTION_MULTIPLE with the
         *  characters attached rather than as separate key codes.  */
        @Override public boolean onKeyMultiple(int keyCode, int repeatCount, KeyEvent event) {
            final String chars = event.getCharacters();
            if (chars != null && chars.length() > 0) { nativeCommitText(chars); return true; }
            return super.onKeyMultiple(keyCode, repeatCount, event);
        }

        @Override public InputConnection onCreateInputConnection(EditorInfo out) {
            out.inputType = mNumeric
                ? (InputType.TYPE_CLASS_NUMBER)
                : (InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
            /*  No fullscreen editor in landscape: it would cover the game with
             *  the IME's own text field, and the client is already drawing one. */
            out.imeOptions = EditorInfo.IME_ACTION_DONE
                           | EditorInfo.IME_FLAG_NO_FULLSCREEN
                           | EditorInfo.IME_FLAG_NO_EXTRACT_UI;
            out.initialSelStart = 0;
            out.initialSelEnd = 0;
            return new RanInputConnection(this);
        }
    }

    private class RanInputConnection extends BaseInputConnection {
        RanInputConnection(View target) {
            /*  fullEditor=false: there is no Editable behind this, we forward
             *  everything to the client instead of maintaining a local buffer. */
            super(target, false);
        }

        /*  What the client is currently holding from the composition in flight.
         *
         *  A soft keyboard does not send the letter just typed: it sends the
         *  WHOLE composition again on every keystroke. Appending each one was
         *  the bug - pressing x twice sent "x" and then "xx", the client
         *  appended both and ended up holding "xxx". A composition has to
         *  REPLACE the previous one, and the client has no notion of composing
         *  text - it has an edit buffer and a backspace and nothing else - so
         *  the last composition is remembered here and taken back a character
         *  at a time before the new one goes in.                             */
        private String mComposing = "";

        /*  Code points, not String.length(): nativeBackspace removes one whole
         *  UTF-8 character, while length() counts UTF-16 units and would leave
         *  half of any character outside the BMP behind.                     */
        private void dropComposing() {
            final int n = mComposing.codePointCount(0, mComposing.length());
            for (int i = 0; i < n; i++) nativeBackspace();
            mComposing = "";
        }

        @Override public boolean commitText(CharSequence text, int newCursorPosition) {
            dropComposing();
            if (text != null && text.length() > 0) nativeCommitText(text.toString());
            return true;
        }

        /*  Most keyboards send each keystroke as composing text and only commit
         *  on a word break, so this is the path ordinary typing runs through. */
        @Override public boolean setComposingText(CharSequence text, int newCursorPosition) {
            dropComposing();
            if (text != null && text.length() > 0) {
                nativeCommitText(text.toString());
                mComposing = text.toString();
            }
            return true;
        }

        /*  The IME is editing around the composition, so what is remembered no
         *  longer describes what the client holds.                           */
        @Override public boolean deleteSurroundingText(int beforeLength, int afterLength) {
            mComposing = "";
            for (int i = 0; i < beforeLength; i++) nativeBackspace();
            return true;
        }

        @Override public boolean sendKeyEvent(KeyEvent event) {
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                final int k = event.getKeyCode();
                //  A real backspace ends the composition: what it removes is a
                //  character the client already has, not one to take back again.
                if (k == KeyEvent.KEYCODE_DEL) { mComposing = ""; nativeBackspace(); return true; }
                /*  Send it, and leave the keyboard alone: CUIEditBox::EndEdit
                  *  calls RanIME_Hide itself once the client closes the line,
                  *  so hiding here as well only fought with it - and hiding
                  *  *instead* of sending is what dropped the message. */
                if (k == KeyEvent.KEYCODE_ENTER) { mComposing = ""; nativeEnter(); return true; }
                final int u = event.getUnicodeChar();
                if (u > 0) { nativeCommitText(String.valueOf((char) u)); return true; }
            }
            return true;
        }

        /*  Most keyboards deliver their action key as an editor action rather
         *  than as a KeyEvent, so this is the path that actually runs for the
         *  blue Done/Send key. Without it the button did nothing at all. */
        @Override public boolean performEditorAction(int actionCode) {
            mComposing = "";
            nativeEnter();
            return true;
        }

        /*  The composition is settled text now as far as the IME is concerned.
         *  The client already holds those characters, so nothing is sent - only
         *  the memory of what could still have been taken back is dropped.    */
        @Override public boolean finishComposingText() { mComposing = ""; return true; }
    }
}
