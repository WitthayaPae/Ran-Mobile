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
import android.text.Editable;
import android.text.Selection;
import android.text.SpannableStringBuilder;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.CompletionInfo;
import android.view.inputmethod.CorrectionInfo;
import android.view.inputmethod.TextAttribute;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.widget.FrameLayout;

import java.io.File;
import java.io.FileInputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

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

    /*  Crash reports, sent home (native: RanCrash_Begin -> RanPlat_UploadCrashReports).
     *
     *  The native side writes each report as a text file in crash_pending/;
     *  this POSTs them to the site, oldest first, and deletes each one the
     *  server answers 200 for. Anything else stops the round and leaves the
     *  files for the next launch - no retry loop, it is not worth the battery.
     *  Its own thread: called from the game thread during boot, which must
     *  not wait on a network.
     *
     *  iOS does the same in ran_ios_plat.mm: same URL, same headers, same
     *  order, same rules.                                                   */
    private static final String CRASH_URL = "https://ran-legacy-m.com/crash/upload.php";

    public void ranUploadCrashReports(final String dir) {
        String app = "?";
        try {
            app = String.valueOf(getPackageManager().getPackageInfo(getPackageName(), 0).versionCode);
        } catch (Exception e) { /* keep "?" */ }
        final String appVer = app;
        new Thread(new Runnable() { public void run() {
            File[] files = new File(dir).listFiles();
            if (files == null || files.length == 0) return;
            java.util.Arrays.sort(files);
            for (File f : files) {
                if (!f.getName().endsWith(".txt")) continue;
                HttpURLConnection c = null;
                try {
                    byte[] body = new byte[(int) Math.min(f.length(), 600 * 1024)];
                    FileInputStream in = new FileInputStream(f);
                    int got = 0;
                    while (got < body.length) {
                        int k = in.read(body, got, body.length - got);
                        if (k <= 0) break;
                        got += k;
                    }
                    in.close();

                    c = (HttpURLConnection) new URL(CRASH_URL).openConnection();
                    c.setConnectTimeout(15000);
                    c.setReadTimeout(20000);
                    c.setDoOutput(true);
                    c.setRequestMethod("POST");
                    c.setRequestProperty("Content-Type", "text/plain; charset=utf-8");
                    c.setRequestProperty("X-Ran-Crash", "1");
                    c.setRequestProperty("X-Ran-App", appVer);
                    c.setFixedLengthStreamingMode(got);
                    OutputStream out = c.getOutputStream();
                    out.write(body, 0, got);
                    out.close();
                    final int code = c.getResponseCode();
                    Log.i("RanCrash", "sent " + f.getName() + " -> HTTP " + code);
                    if (code != 200) break;
                    f.delete();
                } catch (Throwable t) {
                    Log.w("RanCrash", "upload failed: " + t);
                    break;
                } finally {
                    if (c != null) c.disconnect();
                }
            }
        }}, "RanCrashUpload").start();
    }

    public void ranHideKeyboard() {
        runOnUiThread(new Runnable() { public void run() {
            InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.hideSoftInputFromWindow(mIme.getWindowToken(), 0);
            mIme.clearFocus();
            mIme.setVisibility(View.INVISIBLE);
            mConn = null;
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
            final RanInputConnection c = mConn;
            if (keyCode == KeyEvent.KEYCODE_DEL)   { if (c != null) c.backspace(); else nativeBackspace(); return true; }
            if (keyCode == KeyEvent.KEYCODE_ENTER) { if (c != null) c.enter();     else nativeEnter();     return true; }
            final int u = event.getUnicodeChar();
            if (u > 0) {
                final String t = new String(Character.toChars(u));
                if (c != null) c.type(t); else nativeCommitText(t);
                return true;
            }
            return super.onKeyDown(keyCode, event);
        }

        /*  A string injected in one go arrives as ACTION_MULTIPLE with the
         *  characters attached rather than as separate key codes.  */
        @Override public boolean onKeyMultiple(int keyCode, int repeatCount, KeyEvent event) {
            final String chars = event.getCharacters();
            if (chars != null && chars.length() > 0) {
                if (mConn != null) mConn.type(chars); else nativeCommitText(chars);
                return true;
            }
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
            mConn = new RanInputConnection(this);
            return mConn;
        }
    }

    /*  The connection the keyboard is typing through, for keys that reach the
     *  view directly (injected by a test) so they go through the same mirror. */
    private RanInputConnection mConn;

    /*  A mirror of the field, which the keyboard edits however it likes, and a
     *  diff of it into the client.
     *
     *  The client's edit box can do two things: insert at the caret and delete
     *  before it. The caret is always at the end on mobile (RanIME_CaretToEnd;
     *  nothing moves it). Keyboards do much more than append: at a word break
     *  - an '@' or '.' in an email - they reach back and REPLACE the word they
     *  already sent (setComposingRegion + commitText, replaceText, a
     *  correction). The old connection kept no text, so each of those was
     *  forwarded as an append and the word appeared twice; and every call it
     *  did not override fell into BaseInputConnection's "dummy editor", which
     *  re-sends its buffer as key events - more text the client never lost.
     *
     *  Here BaseInputConnection runs as a full editor on mText, so every IME
     *  operation - replace, recompose, delete around the cursor - lands on real
     *  text exactly as it would in an EditText. After each one, sync() sends the
     *  client the difference: backspace to where the old and new text part,
     *  then type the rest. Right whatever the keyboard did, because the result
     *  is compared, not the operation.
     *
     *  Text the field held before the keyboard opened is not in the mirror; a
     *  delete with nothing in the mirror goes straight to the client, as it
     *  always did, so that text can still be erased.                        */
    private class RanInputConnection extends BaseInputConnection {
        private final SpannableStringBuilder mText = new SpannableStringBuilder();
        /*  What the client holds of the mirror, as of the last sync. */
        private String mSent = "";
        private int mBatch = 0;

        RanInputConnection(View target) {
            super(target, true);
        }

        @Override public Editable getEditable() { return mText; }

        @Override public boolean beginBatchEdit() { mBatch++; return true; }
        @Override public boolean endBatchEdit() {
            if (mBatch > 0) mBatch--;
            if (mBatch == 0) sync();
            return mBatch > 0;
        }

        private boolean changed(boolean r) { if (mBatch == 0) sync(); return r; }

        @Override public boolean commitText(CharSequence text, int newCursorPosition) {
            return changed(super.commitText(text, newCursorPosition));
        }
        @Override public boolean setComposingText(CharSequence text, int newCursorPosition) {
            return changed(super.setComposingText(text, newCursorPosition));
        }
        @Override public boolean setComposingRegion(int start, int end) {
            return changed(super.setComposingRegion(start, end));
        }
        @Override public boolean finishComposingText() {
            return changed(super.finishComposingText());
        }
        @Override public boolean setSelection(int start, int end) {
            return changed(super.setSelection(start, end));
        }
        @Override public boolean commitCompletion(CompletionInfo text) {
            return changed(super.commitCompletion(text));
        }
        @Override public boolean commitCorrection(CorrectionInfo info) {
            return changed(super.commitCorrection(info));
        }
        @Override public boolean replaceText(int start, int end, CharSequence text,
                                             int newCursorPosition, TextAttribute attr) {
            return changed(super.replaceText(start, end, text, newCursorPosition, attr));
        }
        @Override public boolean deleteSurroundingTextInCodePoints(int beforeLength, int afterLength) {
            if (mText.length() == 0) { for (int i = 0; i < beforeLength; i++) nativeBackspace(); return true; }
            return changed(super.deleteSurroundingTextInCodePoints(beforeLength, afterLength));
        }
        @Override public boolean deleteSurroundingText(int beforeLength, int afterLength) {
            if (mText.length() == 0) { for (int i = 0; i < beforeLength; i++) nativeBackspace(); return true; }
            return changed(super.deleteSurroundingText(beforeLength, afterLength));
        }

        @Override public boolean sendKeyEvent(KeyEvent event) {
            if (event.getAction() != KeyEvent.ACTION_DOWN) return true;
            final int k = event.getKeyCode();
            if (k == KeyEvent.KEYCODE_DEL)   { backspace(); return true; }
            /*  Send it, and leave the keyboard alone: CUIEditBox::EndEdit calls
             *  RanIME_Hide itself once the client closes the line. */
            if (k == KeyEvent.KEYCODE_ENTER) { enter(); return true; }
            final int u = event.getUnicodeChar();
            if (u > 0) type(new String(Character.toChars(u)));
            return true;
        }

        /*  Most keyboards deliver the blue Done/Send key this way. */
        @Override public boolean performEditorAction(int actionCode) { enter(); return true; }

        void type(String t) {
            int a = Selection.getSelectionStart(mText), b = Selection.getSelectionEnd(mText);
            if (a < 0 || b < 0) { a = b = mText.length(); }
            mText.replace(Math.min(a, b), Math.max(a, b), t);
            changed(true);
        }

        void backspace() {
            int a = Selection.getSelectionStart(mText), b = Selection.getSelectionEnd(mText);
            if (a < 0 || b < 0) { a = b = mText.length(); }
            if (a != b) { mText.delete(Math.min(a, b), Math.max(a, b)); changed(true); return; }
            if (a > 0)  { mText.delete(Character.offsetByCodePoints(mText, a, -1), a); changed(true); return; }
            nativeBackspace();          //  text from before the keyboard opened
        }

        /*  Return sends the line and the client empties the field, so the
         *  mirror starts again from nothing - otherwise the next line would be
         *  diffed against the last one. */
        void enter() {
            mText.clear();
            mText.clearSpans();
            mSent = "";
            nativeEnter();
            report();
        }

        private void sync() {
            final String now = mText.toString();
            int p = 0;
            final int n = Math.min(now.length(), mSent.length());
            while (p < n && now.charAt(p) == mSent.charAt(p)) p++;
            if (p > 0 && Character.isHighSurrogate(now.charAt(p - 1))) p--;

            final int dels = clientChars(mSent.substring(p));
            for (int i = 0; i < dels; i++) nativeBackspace();
            final String add = now.substring(p);
            if (add.length() > 0) nativeCommitText(add);
            mSent = now;
            report();
        }

        /*  Tell the keyboard where the cursor and composition are, as an
         *  EditText would. Without it a keyboard works from its own guess of the
         *  cursor, which is what made reaching back go wrong.                 */
        private void report() {
            InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm == null) return;
            imm.updateSelection(mIme,
                Selection.getSelectionStart(mText), Selection.getSelectionEnd(mText),
                getComposingSpanStart(mText), getComposingSpanEnd(mText));
        }
    }

    /*  How many characters the client keeps of a string. RanIME_InsertUtf8
     *  (shell_mobile.cpp, appendCp874) drops what CP874 cannot hold - an emoji,
     *  a CJK character - so a backspace is sent only for those it kept. The
     *  old code counted every code point and deleted a neighbour instead.     */
    static int clientChars(String s) {
        int n = 0;
        for (int i = 0; i < s.length(); ) {
            final int cp = s.codePointAt(i);
            i += Character.charCount(cp);
            if (cp < 0x80 || (cp >= 0x0E01 && cp <= 0x0E5B)) { n++; continue; }
            switch (cp) {
                case 0x20AC: case 0x00A0: case 0x2026: case 0x2018: case 0x2019:
                case 0x201C: case 0x201D: case 0x2022: case 0x2013: case 0x2014:
                    n++;
                    break;
                default:
                    break;
            }
        }
        return n;
    }
}
