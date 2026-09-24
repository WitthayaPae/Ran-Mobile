//  Package is com.ran.launcher, not com.ran.native: 'native' is a Java
//  reserved word and cannot be a package segment. The APPLICATION id stays
//  com.ran.native - that is an Android identifier, not a Java one - so
//  getPackageName() below still returns it.
package com.ran.launcher;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ComponentName;
import android.content.Context;
import android.content.BroadcastReceiver;
import android.content.Intent;
import android.content.IntentFilter;
import android.app.PendingIntent;
import android.content.pm.PackageInstaller;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Shader;
import android.view.View;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.security.KeyFactory;
import java.security.PublicKey;
import java.security.Signature;
import java.security.spec.X509EncodedKeySpec;
import java.net.URL;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

/*  The patcher, and the first thing that runs.
 *
 *  It reconciles /sdcard/ran against a manifest on the patch host, then hands
 *  off to NativeActivity. The game itself knows nothing about any of this.
 *
 *  Why Java rather than the native side: HTTP, a progress UI, the storage
 *  permission prompt and the APK install prompt are all a few lines here and
 *  a fight in C++ through JNI. This layer is also what the Thai composing IME
 *  needs, so it earns its place twice.
 */
public class RanLauncher extends Activity {

    /*  The patch host. Everything else about publishing is derived from this. */
    private static final String BASE_DEFAULT = "https://ran-legacy-m.com/launcher_mobile/";

    /*  An override, read from /sdcard/ran/.patchbase when it exists. Testing a
     *  patch against a local server otherwise means rebuilding the APK to
     *  change one string, which is slow enough that it does not get done. */
    private static String base() {
        try {
            java.io.File f = new java.io.File(ROOT, ".patchbase");
            if (f.exists()) {
                String s = new String(readAll(new FileInputStream(f)), "UTF-8").trim();
                if (s.length() > 0) return s.endsWith("/") ? s : s + "/";
            }
        } catch (Throwable t) { }
        return BASE_DEFAULT;
    }

    /*  Where the game reads its data. The manifest's paths are relative to
     *  this, so an entry's "path" is literally where it lands - no mapping. */
    /*  Where the game data lives.
     *
     *  This used to be /sdcard/ran, which is shared storage: readable and
     *  writable by any app holding a storage permission. Game data is parsed by
     *  the C++ client, whose loaders are not hardened against hostile input, so
     *  another app editing a .rcc in place was a way into this process - and
     *  .patchbase sitting there meant any app could also redirect the patcher.
     *
     *  The app's own external files directory is not reachable by other apps on
     *  Android 11 and later, needs no permission for us to use, and is still
     *  visible over adb, which is why the data lives here now. The old location
     *  is migrated on first run and kept as a fallback the native loader still
     *  recognises, so an adb-pushed test tree keeps working.                  */
    private static String ROOT = "/sdcard/ran";        //  replaced in onCreate

    private static final String LEGACY_ROOT = "/sdcard/ran";

    /*  Written only after every file in a manifest has been verified. If we
     *  are killed part way through, this still names the OLD version, so the
     *  next launch reconciles again and finishes. That one ordering rule is
     *  what makes the whole thing crash-safe. */
    private static final String VER_FILE = ".patchver";

    /*  path \t size \t mtime \t sha256, one line per file. Without it every
     *  launch would hash 1.7 GB to discover that nothing changed. */
    private static final String INDEX_FILE = ".patchindex";

    private static final String TAG = "RanPatch";

    private TextView status, detail;
    private PatchBar bar;
    private final Handler ui = new Handler(Looper.getMainLooper());

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);

        /*  The game's own loading screen, as the patch screen.
         *
         *  The player used to meet a bare dark panel, then a moment later the
         *  client's loading art - two screens for one wait. This is the same
         *  lobby art the in-game loader shows (loading_002.dds), with the RAN
         *  mark from the login page over it and the progress along the bottom,
         *  so the launcher and the first frame of the game are one continuous
         *  screen.
         *
         *  The art is a drawable in the APK rather than a file read from the
         *  data root, and it has to be: on a fresh install this screen is what
         *  is drawn *while* that root is being downloaded, so nothing in it can
         *  come from there.                                                    */
        FrameLayout root = new FrameLayout(this);
        mPage = root;
        root.setBackgroundColor(Color.parseColor("#0B0E10"));

        /*  Laid out like the game's own loading screen.
         *
         *  LoadingThread.cpp works in a 1024x768 virtual space: ld_top drawn
         *  1024x128 at (0,0), the art 1024x512 at (0,128), ld_under 1024x128 at
         *  (0,640). So each band is 128/768 of the height whatever the panel
         *  is, and the art has the middle two thirds. Matching that is the
         *  point - the player sees this screen and then, a moment later, the
         *  client's map loader draws the same one.                            */
        final int bandH = Math.round(screenHeightPx() * 128.0f / 768.0f);

        ImageView art = new ImageView(this);
        setDrawable(art, "ran_loading");
        //  Fill the middle band and crop, rather than letterbox: black bars
        //  around it look like a broken asset.
        art.setScaleType(ImageView.ScaleType.CENTER_CROP);
        FrameLayout.LayoutParams alp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        alp.topMargin = bandH;
        alp.bottomMargin = bandH;
        root.addView(art, alp);

        //  The bands themselves are stretched to width, as the client does -
        //  they are a frame, not a picture, and their ends are what has to meet
        //  the edges of the screen.
        ImageView topBand = new ImageView(this);
        setDrawable(topBand, "ld_top");
        topBand.setScaleType(ImageView.ScaleType.FIT_XY);
        FrameLayout.LayoutParams tlp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, bandH);
        tlp.gravity = Gravity.TOP;
        root.addView(topBand, tlp);

        ImageView underBand = new ImageView(this);
        setDrawable(underBand, "ld_under");
        underBand.setScaleType(ImageView.ScaleType.FIT_XY);
        FrameLayout.LayoutParams ulp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, bandH);
        ulp.gravity = Gravity.BOTTOM;
        root.addView(underBand, ulp);

        ImageView mark = new ImageView(this);
        setDrawable(mark, "ran_mark");
        mark.setAdjustViewBounds(true);
        //  170dp, not the 230 the old mark used: that one was a wide wordmark
        //  at 177x96, this one is square, and 230dp square is a third of the
        //  height of the page.
        /*  In the top band, which is empty by design - it is where the client
         *  puts the map name on its own loading screen. Below it the logo
         *  landed on the group's heads, which read as clutter rather than a
         *  title. Sized off the band rather than in dp so it keeps its margin
         *  on any panel.                                                      */
        final int markH = Math.round(bandH * 0.82f);
        FrameLayout.LayoutParams mlp = new FrameLayout.LayoutParams(markH, markH);
        mlp.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
        mlp.topMargin = (bandH - markH) / 2;
        root.addView(mark, mlp);

        /*  The text and the bar sit in a band along the bottom. A little scrim
         *  behind them, because the art is bright sky in places and white text
         *  on it is unreadable.                                                */
        /*  The status sits on the bottom band now, so it needs no scrim of its
         *  own - the band is already dark, and a second dark rectangle over it
         *  only made a seam.                                                  */
        LinearLayout band = new LinearLayout(this);
        band.setOrientation(LinearLayout.VERTICAL);
        band.setGravity(Gravity.CENTER_VERTICAL);
        band.setPadding(dp(24), 0, dp(24), 0);
        FrameLayout.LayoutParams blp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, bandH);
        blp.gravity = Gravity.BOTTOM;
        root.addView(band, blp);

        status = new TextView(this);
        status.setTextColor(Color.parseColor("#F0F4F6"));
        status.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        status.setGravity(Gravity.CENTER);
        status.setText("กำลังเริ่ม");
        band.addView(status);

        bar = new PatchBar(this);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(8));
        lp.topMargin = dp(10);
        bar.setLayoutParams(lp);
        band.addView(bar);

        detail = new TextView(this);
        detail.setTextColor(Color.parseColor("#AEB8BE"));
        detail.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        detail.setGravity(Gravity.CENTER);
        detail.setPadding(0, dp(8), 0, 0);
        band.addView(detail);

        setContentView(root);

        /*  A first install downloads 4.7 GB, and this Activity is what holds the
         *  process alive while it happens. Let the screen sleep and Android
         *  eventually kills a backgrounded process mid-download.              */
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        /*  The same immersive flags the game sets in goFullscreen.
         *
         *  Not cosmetic. This page is handed to the client as a picture and
         *  drawn over the whole game surface, and that surface is the whole
         *  panel. Laid out inside the navigation bar instead, the page is 1568
         *  px tall against 1600 and the handover stretches it by 2% - a small
         *  but visible jerk at the one moment the two are meant to be
         *  indistinguishable. Same insets, same picture.                      */
        getWindow().getDecorView().setSystemUiVisibility(
                  0x00000002      //  HIDE_NAVIGATION
                | 0x00000004      //  FULLSCREEN
                | 0x00000100      //  LAYOUT_STABLE
                | 0x00000200      //  LAYOUT_HIDE_NAVIGATION
                | 0x00000400      //  LAYOUT_FULLSCREEN
                | 0x00001000);    //  IMMERSIVE_STICKY

        /*  Storage permission is never asked for, and never blocks anything.
         *
         *  The data lives in this app's own external files directory, which
         *  needs no permission at all. All-files access is good for exactly one
         *  thing: spotting a pre-private-root install under /sdcard/ran and
         *  moving it instead of re-downloading it. An updating player already
         *  granted it to the old build and the grant survives the update, so
         *  they get the migration for free. A fresh install has nothing to
         *  migrate and no use for the permission.
         *
         *  Prompting anyway was actively harmful, and measured: the Settings
         *  screen put this Activity in the background just as the download
         *  started, and the permission change then killed the process -
         *  "Killing com.ran.native (adj 900): MANAGE_EXTERNAL_STORAGE changed".
         *  The one case it helped, it broke.                                  */

        //  Claim the guard here, not just in onResume: onCreate is followed
        //  immediately by onResume, and without this both start a patch
        //  thread and the game is launched twice.
        started = true;
        new Thread(new Runnable() { public void run() { patchThenPlay(); } }).start();
    }

    @Override protected void onResume() {
        super.onResume();
        /*  Coming back from the storage-permission screen - granted or not. */
        if (!started) {
            started = true;
            new Thread(new Runnable() { public void run() { patchThenPlay(); } }).start();
        }
    }
    private boolean started = false;

    /*  The page, kept so it can be handed to the game as a picture. */
    private FrameLayout mPage;

    /*  Rasterise this page for the client to keep showing.
     *
     *  The client boots for a couple of seconds after this Activity goes away,
     *  behind a window it has not drawn to yet, and what has to be on screen
     *  for that time is this page - unchanged, band and status line included.
     *  Drawing it again on the native side would mean building the band and its
     *  text in GL before the client has a font, and it would drift from this
     *  layout the first time either was touched. Handing over the actual pixels
     *  cannot drift.
     *
     *  Half resolution: it is a photograph behind a caption, it is stretched
     *  back to full size by a linear filter, and this keeps the file to about
     *  4 MB and the write under a frame. cache/ is not in the patch manifest,
     *  so a patch never fights over it.                                       */
    private void handOverPage() {
        final java.util.concurrent.CountDownLatch done =
                new java.util.concurrent.CountDownLatch(1);
        ui.post(new Runnable() { public void run() {
            try {
                final int w = mPage.getWidth() / 2, h = mPage.getHeight() / 2;
                if (w <= 0 || h <= 0) return;

                android.graphics.Bitmap bmp = android.graphics.Bitmap.createBitmap(
                        w, h, android.graphics.Bitmap.Config.ARGB_8888);
                android.graphics.Canvas c = new android.graphics.Canvas(bmp);
                c.scale(0.5f, 0.5f);
                mPage.draw(c);

                java.nio.ByteBuffer buf = java.nio.ByteBuffer.allocate(w * h * 4);
                bmp.copyPixelsToBuffer(buf);        //  ARGB_8888 is RGBA in memory
                bmp.recycle();

                File dir = new File(ROOT, "cache");
                if (!dir.isDirectory() && !dir.mkdirs()) return;
                File tmp = new File(dir, "bootcover.tmp");
                FileOutputStream os = new FileOutputStream(tmp);
                try {
                    //  "RANC", then width and height, little-endian.
                    os.write(new byte[] { 82, 65, 78, 67 });
                    writeLE(os, w);
                    writeLE(os, h);
                    os.write(buf.array());
                } finally { os.close(); }
                //  Rename last: a half-written file must never be picked up.
                File dst = new File(dir, "bootcover.bin");
                dst.delete();
                if (!tmp.renameTo(dst)) tmp.delete();
            } catch (Throwable t) {
                //  Not fatal - the client composes its own boot screen instead.
                Log.w(TAG, "boot cover: " + t);
            } finally { done.countDown(); }
        }});
        try { done.await(2, java.util.concurrent.TimeUnit.SECONDS); }
        catch (InterruptedException e) { Thread.currentThread().interrupt(); }
    }

    private static void writeLE(FileOutputStream os, int v) throws Exception {
        os.write(new byte[] { (byte) v, (byte) (v >> 8), (byte) (v >> 16), (byte) (v >> 24) });
    }

    private int dp(int v) { return (int) (v * getResources().getDisplayMetrics().density); }

    /*  The whole panel, not the part left over after the system bars: this
     *  window is immersive and the picture it hands to the client covers the
     *  game surface, which is the whole panel.                                */
    private int screenHeightPx() {
        android.util.DisplayMetrics dm = new android.util.DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(dm);
        return dm.heightPixels > 0 ? dm.heightPixels
                                   : getResources().getDisplayMetrics().heightPixels;
    }

    /*  By name, not by R.drawable: this APK is linked by aapt2 without --java,
     *  so there is no generated R class to compile against. A missing drawable
     *  leaves the view empty rather than throwing - a launcher that crashes
     *  because of its own wallpaper would be a poor trade.                    */
    private void setDrawable(ImageView v, String name) {
        try {
            int id = getResources().getIdentifier(name, "drawable", getPackageName());
            if (id != 0) v.setImageResource(id);
            else Log.w(TAG, "drawable " + name + " not in the APK");
        } catch (Throwable t) { Log.w(TAG, "drawable " + name + ": " + t); }
    }

    private void say(final String s, final String d, final int permille) {
        if (s != null || d != null) Log.i(TAG, (s == null ? "" : s) + (d == null ? "" : "  |  " + d));
        ui.post(new Runnable() { public void run() {
            if (s != null) status.setText(s);
            if (d != null) detail.setText(d);
            bar.setPermille(permille);
        }});
    }

    /*  The progress bar, which never stops moving.
     *
     *  It was a plain horizontal ProgressBar: a filled rectangle and nothing
     *  else. A patch spends most of its time inside one file - a 400 MB blob is
     *  one step of the count - and during that the fill does not move at all,
     *  so the screen looks frozen and the player force-closes a download that
     *  was working. This draws the same fill and sweeps a highlight along it,
     *  every frame, whatever the number is doing: the page says "still here"
     *  without claiming progress it has not made.
     *
     *  With no number to show at all (busy), the highlight sweeps the whole
     *  track instead, which is the indeterminate case the old bar had.
     */
    private static final class PatchBar extends View {
        private float mFill = 0.0f;              //  0..1, -1 while busy
        private boolean mBusy = true;
        private long mT0 = 0;
        private final Paint mPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF mR = new RectF();

        PatchBar(Context c) { super(c); }

        /*  Redrawn from a ticker, not from inside onDraw.
         *
         *  postInvalidateOnAnimation() at the end of onDraw is the obvious way
         *  to write this and it does not work here: measured on the device,
         *  the bar's pixels were byte for byte identical across four frames
         *  while the countdown text beside it changed on every one. An
         *  invalidate issued while the view is being drawn is swallowed. A
         *  posted Runnable is outside that pass, so it schedules the next
         *  frame properly, and it stops when the view leaves the window.     */
        private final Runnable mTick = new Runnable() {
            public void run() { invalidate(); postDelayed(this, 16); }
        };

        @Override protected void onAttachedToWindow() {
            super.onAttachedToWindow();
            post(mTick);
        }

        @Override protected void onDetachedFromWindow() {
            super.onDetachedFromWindow();
            removeCallbacks(mTick);
        }

        void setPermille(int permille) {
            if (permille < 0) { mBusy = true; }
            else { mBusy = false; mFill = permille / 1000.0f; }
        }

        @Override protected void onDraw(Canvas cv) {
            final float w = getWidth(), h = getHeight();
            if (w <= 0.0f || h <= 0.0f) return;
            if (mT0 == 0) mT0 = System.nanoTime();
            final float t = (float)((System.nanoTime() - mT0) / 1e9);
            final float r = h * 0.5f;

            //  The track.
            mPaint.setShader(null);
            mPaint.setColor(0xFF1B2126);
            mR.set(0, 0, w, h);
            cv.drawRoundRect(mR, r, r, mPaint);

            //  The progress, in the game's own gold. Honest: it is the number
            //  the patcher gave, and nothing animates it.
            final float fill = mBusy ? 0.0f : w * (mFill < 0.0f ? 0.0f : mFill);
            if (fill > 1.0f) {
                mPaint.setColor(0xFFC9962B);
                mR.set(0, 0, fill, h);
                cv.drawRoundRect(mR, r, r, mPaint);
            }

            //  And the part still to do carries the movement.
            //
            //  One bar doing both jobs: the fill says how far along this is,
            //  the sweep runs along what is LEFT of it, so the page is visibly
            //  alive without ever claiming progress it has not made. A patch
            //  spends minutes inside one big file with the number motionless,
            //  and that is exactly when a player decides it has hung.
            //
            //  Before a number exists at all the fill is zero, so the sweep has
            //  the whole track - which is the indeterminate case, for free.
            final float rest = w - fill;
            if (rest > 2.0f) {
                cv.save();
                mR.set(fill, 0, w, h);
                cv.clipRect(mR);

                final float band = Math.min(rest, w * 0.28f);
                final float span = rest + band;
                final float x = fill + ((t / 1.5f) % 1.0f) * span - band;
                mPaint.setShader(new LinearGradient(x, 0, x + band, 0,
                        new int[] { 0x00C9962B, 0x55C9962B, 0x00C9962B },
                        new float[] { 0.0f, 0.5f, 1.0f }, Shader.TileMode.CLAMP));
                mR.set(0, 0, w, h);
                cv.drawRoundRect(mR, r, r, mPaint);
                mPaint.setShader(null);
                cv.restore();
            }

            //  A bright head where the two meet, so the eye finds the number's
            //  edge even while the sweep is at the far end.
            if (fill > 1.0f && fill < w - 1.0f) {
                mPaint.setColor(0xFFF2D493);
                mR.set(fill - h * 0.35f, 0, fill, h);
                cv.drawRect(mR, mPaint);
            }
        }
    }

    /* --------------------------------------------------------------- patch */

    private void patchThenPlay() {
        /*  The game does not start until the patch has been applied.
         *
         *  It used to: a failure here with the data already on disk said "could
         *  not reach the update server" for a second and then played anyway. On
         *  a client whose version does not match the server's that is a worse
         *  outcome than waiting - the player gets in, plays against packets
         *  they do not understand, and reads the symptoms as the game being
         *  broken. And whether the versions match is exactly what this screen
         *  cannot answer while the server is unreachable: the local .patchver
         *  is the version we last applied, not the one that is current.
         *
         *  So a failure waits and tries again, with the number of seconds on
         *  screen so it is visibly waiting rather than stuck, and the wait
         *  grows to half a minute. It clears itself the moment the connection
         *  comes back; nothing here needs the player to do anything but be
         *  online. fail() is the other ending - the APK being too old is a
         *  hard stop, and it has already said so.                            */
        int wait = 5;
        for (;;) {
            try {
                adoptPrivateRoot();
                patch();
                break;
            } catch (Throwable t) {
                /*  The reason goes to the log, not to the screen. It carries
                 *  URLs, host names and file paths, and this screen is the
                 *  first thing a player screenshots when something goes wrong.
                 *
                 *  What does go on screen is the class name and, for an HTTP
                 *  failure, the status - enough to tell a refused certificate
                 *  from a full disk from a 404 without a round trip, and it
                 *  names no host and no path.                                */
                Log.e(TAG, "patch failed", t);
                if (mFatal) return;            //  fail() has already spoken
                final String why = reasonCode(t);
                for (int left = wait; left > 0; --left) {
                    say("เชื่อมต่อเซิร์ฟเวอร์อัปเดตไม่ได้",
                        "จะลองใหม่ใน " + left + " วินาที กรุณาตรวจสอบอินเทอร์เน็ต  (" + why + ")", -1);
                    sleep(1000);
                }
                say("กำลังเชื่อมต่ออีกครั้ง", null, -1);
                if (wait < 30) wait += 5;
            }
        }
        play();
    }

    /*  Choose the private root, and move an old install into it.
     *
     *  The move is a rename per top-level entry, which is a metadata operation
     *  on the same volume - 1.7 GB arrives instantly rather than being
     *  re-downloaded. If it cannot be done (a different volume, or the
     *  permission is gone) nothing is lost: the data stays where it is and the
     *  native loader still accepts the old location.                          */
    private void adoptPrivateRoot() {
        File priv = getExternalFilesDir(null);
        if (priv == null) priv = getFilesDir();          //  no external storage
        if (priv == null) return;                        //  keep the old root
        if (!priv.exists() && !priv.mkdirs()) return;

        final String target = priv.getAbsolutePath();

        File legacy = new File(LEGACY_ROOT);
        boolean privHasData = new File(priv, "config.ini").exists();
        boolean legacyHasData = new File(legacy, "config.ini").exists();

        if (!privHasData && legacyHasData) {
            if (!migrate(legacy, priv)) {
                /*  Half a data tree is worse than the old one. Leave the legacy
                 *  root in charge; the native loader still accepts it.        */
                Log.w(TAG, "migration incomplete, staying on " + LEGACY_ROOT);
                return;
            }
        }

        ROOT = target;
        Log.i(TAG, "data root: " + ROOT);
    }

    /*  Move an existing install into the private root.
     *
     *  Renaming would be instant, and is tried first, but Android does not
     *  allow a rename from shared storage into Android/data/<package> whatever
     *  permissions are held - measured, with MANAGE_EXTERNAL_STORAGE granted:
     *  0 of 33 entries moved. So it falls back to copying, which for this data
     *  is over a gigabyte and takes minutes. It happens once.
     *
     *  config.ini is copied LAST and is what marks the tree complete: an
     *  interrupted migration leaves the private root without it, so the next
     *  run starts again rather than running against half a tree.             */
    private boolean migrate(File legacy, File priv) {
        say("กำลังย้ายข้อมูลเกม", "ครั้งเดียว ไปยังพื้นที่ส่วนตัวของแอป", -1);

        String[] names = legacy.list();
        if (names == null) return false;

        /*  Everything except the marker, and the marker after it. */
        List<String> order = new ArrayList<String>();
        for (String n : names) if (!n.equals("config.ini")) order.add(n);
        for (String n : names) if (n.equals("config.ini")) order.add(n);

        long need = 0;
        for (String n : names) need += sizeOf(new File(legacy, n));
        long free = priv.getUsableSpace();
        if (free < need + (64L << 20)) {
            Log.w(TAG, "migration needs " + mb(need) + ", only " + mb(free) + " free");
            say("พื้นที่ว่างไม่พอ", "ต้องการ " + mb(need) + " มีอยู่ " + mb(free), 1000);
            sleep(2500);
            return false;
        }

        int done = 0;
        for (String n : order) {
            File from = new File(legacy, n), to = new File(priv, n);
            if (from.renameTo(to)) { done++; continue; }      //  instant, when allowed
            try {
                copyTree(from, to);
            } catch (Throwable t) {
                Log.e(TAG, "migration failed on " + n, t);
                return false;
            }
            done++;
            say(null, done + " / " + order.size() + "   " + n, done * 1000 / order.size());
        }

        if (!new File(priv, "config.ini").exists()) return false;

        /*  Only once the new tree is known good. */
        for (String n : names) deleteTree(new File(legacy, n));
        Log.i(TAG, "data root migration complete: " + done + " entries");
        return true;
    }

    private static long sizeOf(File f) {
        if (f.isFile()) return f.length();
        File[] kids = f.listFiles();
        long n = 0;
        if (kids != null) for (File k : kids) n += sizeOf(k);
        return n;
    }

    private static void copyTree(File from, File to) throws Exception {
        if (from.isDirectory()) {
            if (!to.exists() && !to.mkdirs()) throw new Exception("cannot create " + to);
            File[] kids = from.listFiles();
            if (kids != null) for (File k : kids) copyTree(k, new File(to, k.getName()));
            return;
        }
        File tmp = new File(to.getPath() + ".part");
        InputStream in = new FileInputStream(from);
        OutputStream out = new FileOutputStream(tmp);
        try {
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        } finally { out.close(); in.close(); }
        if (to.exists() && !to.delete()) throw new Exception("cannot replace " + to);
        if (!tmp.renameTo(to)) throw new Exception("cannot rename " + tmp);
    }

    private static void deleteTree(File f) {
        if (f.isDirectory()) {
            File[] kids = f.listFiles();
            if (kids != null) for (File k : kids) deleteTree(k);
        }
        f.delete();
    }

    private void patch() throws Exception {
        //  The host is not the player's business, and a screenshot of this
        //  screen should not hand anyone the patch address. It goes to the log,
        //  which is where someone diagnosing a patch failure is looking anyway.
        Log.i(TAG, "patch base " + base());
        say("กำลังตรวจสอบอัปเดต", null, -1);

        /*  Fetched as bytes and checked before being parsed: a JSON parser is
         *  the first thing an attacker reaches, so it must not run on anything
         *  unverified.                                                        */
        byte[] body = httpGet(base() + "manifest.json");
        byte[] sig;
        try {
            sig = httpGet(base() + "manifest.sig");
        } catch (Exception e) {
            throw new Exception("no manifest signature on the server (" + e.getMessage() + ")");
        }
        verifyManifest(body, sig);

        JSONObject m = new JSONObject(new String(body, "UTF-8"));
        int version = m.getInt("version");
        int minApk = m.optInt("minApk", 0);

        int myApk = getPackageManager().getPackageInfo(getPackageName(), 0).versionCode;

        /*  A new binary, if the manifest offers one, before any data is
         *  fetched: data can depend on code, never the other way round, and an
         *  install restarts the process anyway.
         *
         *  Offered BEFORE the minApk gate, not after. The gate used to come
         *  first, so a phone below minApk was stopped with "install the new
         *  APK" and never shown the download that would have fixed it - the
         *  only way out was sideloading by hand. Now the install is offered
         *  first, and the gate only stops a phone that is still too old after
         *  that (declined, no permission, failed download).                 */
        mApkRequired = (minApk > myApk);
        if (offerApk(m.optJSONObject("apk"), myApk)) return;

        if (minApk > myApk) {
            /*  A data patch cannot fix a client whose packet layout is stale,
             *  so this is a hard stop rather than a warning. Reopening the game
             *  offers the install again. */
            fail("ต้องอัปเดตแอปก่อนเล่น",
                 "เซิร์ฟเวอร์ต้องการแอปเวอร์ชัน " + minApk + " แต่เครื่องนี้เป็น " + myApk +
                 "\nกรุณาเปิดเกมใหม่แล้วกดติดตั้งอัปเดต");
            throw new Exception("apk too old");
        }

        File rootDir = new File(ROOT);
        if (!rootDir.exists() && !rootDir.mkdirs())
            throw new Exception("cannot create " + ROOT);

        int localVersion = readVersion();
        if (localVersion == version) { say("เป็นเวอร์ชันล่าสุด", "เวอร์ชัน " + version, 1000); return; }

        /*  Never go backwards.
         *
         *  A signature stops an attacker writing a manifest, but not replaying
         *  one you signed yourself - an old manifest is still validly signed
         *  forever. Without this, anyone able to answer for the host could pin
         *  clients to a version whose bugs they know. To publish old content
         *  deliberately, republish it under a higher number.                  */
        if (localVersion >= 0 && version < localVersion)
            throw new Exception("server offers version " + version +
                                ", older than the installed " + localVersion);

        JSONArray arr = m.getJSONArray("files");
        Map<String, String> index = readIndex();          //  path -> "size:mtime:sha"
        List<String[]> todo = new ArrayList<String[]>();  //  {path, sha, size}
        long todoBytes = 0;

        say("กำลังตรวจสอบไฟล์", arr.length() + " ไฟล์", 0);
        for (int i = 0; i < arr.length(); i++) {
            JSONObject e = arr.getJSONObject(i);
            String p = e.getString("path");
            String sha = e.getString("sha256");
            long size = e.getLong("size");

            File f = safeDest(rootDir, p);
            boolean ok = false;
            /*  A seeded file belongs to the player once it exists.
             *
             *  option.ini is written by the client every time settings are
             *  saved, so its hash stops matching the manifest immediately and
             *  the normal path would replace it - resetting graphics, sound
             *  and gameplay options on every patch. Seeding installs it when
             *  absent, which is what gives a fresh install sane defaults, and
             *  leaves it alone forever after. Content is never compared: any
             *  existing file, whatever is in it, is the player's.            */
            if (e.optBoolean("seed", false) && f.exists()) ok = true;
            else if (f.exists() && f.length() == size) {
                String key = index.get(p);
                String want = size + ":" + f.lastModified() + ":" + sha;
                if (key != null && key.equals(want)) ok = true;      //  trusted
                else ok = sha.equalsIgnoreCase(sha256(f));           //  verify
            }
            if (!ok) {
                JSONArray parts = e.optJSONArray("parts");
                todo.add(new String[]{ p, sha, String.valueOf(size),
                                       parts == null ? null : parts.toString() });
                todoBytes += size;
            }
            if ((i & 255) == 0) say(null, "checked " + i + " / " + arr.length(), i * 1000 / arr.length());
        }

        if (todo.isEmpty()) {
            writeIndexFrom(arr, rootDir);
            writeVersion(version);
            say("เป็นเวอร์ชันล่าสุด", "เวอร์ชัน " + version, 1000);
            return;
        }

        say("กำลังดาวน์โหลดอัปเดต", todo.size() + " ไฟล์, " + mb(todoBytes), 0);
        downloadAll(todo, rootDir, todoBytes, base() + "blobs/");

        writeIndexFrom(arr, rootDir);
        writeVersion(version);                 //  last, always
        say("อัปเดตเสร็จแล้ว", "เวอร์ชัน " + version, 1000);
    }

    /*  The key the manifest must be signed with.
     *
     *  P-256 public key, X.509 SubjectPublicKeyInfo, base64. The private half
     *  lives in MOBILE/tools/patch/keys/ and is gitignored; make-manifest.js
     *  signs manifest.json with it and writes manifest.sig beside it.
     *
     *  This is what makes the patcher safe over plain HTTP. Every blob is
     *  verified against a hash out of the manifest, so whoever writes the
     *  manifest decides what lands on the device - and until now that was
     *  anyone on the network path, because the manifest arrived unauthenticated
     *  and the SHA-256 check only ever caught corruption. An attacker who
     *  cannot sign now cannot publish, whatever they do to the transport or the
     *  host.                                                                  */
    private static final String MANIFEST_PUBKEY =
        "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE/kqyu7XQLuP/WlBSpgnfKrN91qevUOtyVEMA3nL6hMX+lBTv9K7PHs/tQ1t1BZgpb9ugHasRVkTOk8b1F93jUQ==";

    /*  Fails closed: a missing, malformed or wrong signature is a hard stop,
     *  never a warning. A check that can be skipped by deleting a file is not a
     *  check.                                                                 */
    private void verifyManifest(byte[] body, byte[] sigText) throws Exception {
        byte[] der;
        try {
            der = android.util.Base64.decode(new String(sigText, "UTF-8").trim(),
                                             android.util.Base64.DEFAULT);
        } catch (Throwable t) {
            throw new Exception("manifest signature is not valid base64");
        }

        PublicKey pk = KeyFactory.getInstance("EC").generatePublic(
            new X509EncodedKeySpec(android.util.Base64.decode(MANIFEST_PUBKEY,
                                                              android.util.Base64.DEFAULT)));
        Signature v = Signature.getInstance("SHA256withECDSA");
        v.initVerify(pk);
        v.update(body);
        if (!v.verify(der))
            throw new Exception("manifest signature does not verify - refusing this update");
    }

    /*  Where a manifest entry is allowed to land.
     *
     *  "path" is used directly as a destination under /sdcard/ran, and it comes
     *  off the network. Nothing checked it: an entry of "../../../../x" wrote
     *  outside the data root, and this app holds MANAGE_EXTERNAL_STORAGE, so
     *  "outside" means anywhere on shared storage. The download path also
     *  deletes the destination before renaming over it, so a hostile manifest
     *  could remove files as well as create them.
     *
     *  Reaching that needs a manifest an attacker controls, which plain HTTP
     *  hands to anyone on the network path - so this is not theoretical, it is
     *  one hop away. Checked here rather than at the call sites so there is one
     *  place that decides, and it is applied on both passes.                  */
    private File safeDest(File root, String rel) throws Exception {
        if (rel == null || rel.length() == 0)
            throw new Exception("empty path in manifest");
        final char first = rel.charAt(0);
        if (first == '/' || first == '\\')
            throw new Exception("absolute path in manifest: " + rel);
        if (rel.length() > 1 && rel.charAt(1) == ':')
            throw new Exception("drive-qualified path in manifest: " + rel);
        if (rel.indexOf('\\') >= 0)
            throw new Exception("backslash in manifest path: " + rel);
        for (String seg : rel.split("/"))
            if (seg.equals(".."))
                throw new Exception("path escapes the data root: " + rel);

        /*  Belt and braces: symlinks and anything the checks above did not
         *  anticipate still have to resolve to somewhere under the root.      */
        File f = new File(root, rel);
        String base = root.getCanonicalPath();
        String got = f.getCanonicalPath();
        if (!got.equals(base) && !got.startsWith(base + File.separator))
            throw new Exception("path escapes the data root: " + rel);
        return f;
    }

    /* --------------------------------------------------------------- state */

    private int readVersion() {
        try {
            byte[] b = readAll(new FileInputStream(new File(ROOT, VER_FILE)));
            return Integer.parseInt(new String(b, "UTF-8").trim());
        } catch (Throwable t) { return -1; }
    }

    private void writeVersion(int v) throws Exception {
        FileOutputStream o = new FileOutputStream(new File(ROOT, VER_FILE));
        try { o.write(String.valueOf(v).getBytes("UTF-8")); } finally { o.close(); }
    }

    private Map<String, String> readIndex() {
        Map<String, String> m = new HashMap<String, String>();
        try {
            String s = new String(readAll(new FileInputStream(new File(ROOT, INDEX_FILE))), "UTF-8");
            for (String line : s.split("\n")) {
                int a = line.indexOf('\t');
                if (a > 0) m.put(line.substring(0, a), line.substring(a + 1).trim());
            }
        } catch (Throwable t) { /* no index yet: everything gets hashed once */ }
        return m;
    }

    private void writeIndexFrom(JSONArray arr, File rootDir) throws Exception {
        StringBuilder sb = new StringBuilder(1 << 18);
        for (int i = 0; i < arr.length(); i++) {
            JSONObject e = arr.getJSONObject(i);
            String p = e.getString("path");
            //  A seeded file's local content is the player's and will not match
            //  the manifest hash, so recording it here would only produce an
            //  entry that never validates. It is skipped on every run anyway.
            if (e.optBoolean("seed", false)) continue;
            File f = new File(rootDir, p);
            sb.append(p).append('\t').append(f.length()).append(':')
              .append(f.lastModified()).append(':').append(e.getString("sha256")).append('\n');
        }
        FileOutputStream o = new FileOutputStream(new File(ROOT, INDEX_FILE));
        try { o.write(sb.toString().getBytes("UTF-8")); } finally { o.close(); }
    }

    /* ---------------------------------------------------------------- http */

    /*  The manifest is read whole into memory, so it needs a ceiling: without
     *  one, a server that streams forever is an out-of-memory kill rather than
     *  an error message. The real manifest is 1.2 MB.                         */
    private static final int MANIFEST_MAX = 64 << 20;

    /*  How many files download at once.
     *
     *  A fresh install is 23,368 files, and for most of them the time goes to
     *  the round trip each request costs, not to bytes: one at a time, 1,351
     *  small files took 43.8 s on LDPlayer (32 ms a file). Riot measured eight
     *  connections as the point past which more stopped helping their patcher,
     *  and the CDN serves any number.                                          */
    private static final int DL_THREADS = 8;

    private void downloadAll(final List<String[]> todo, final File rootDir,
                             final long todoBytes, final String blobBase) throws Exception {
        /*  The keep-alive pool holds 5 idle sockets by default, so three of the
         *  eight workers would reconnect - a fresh TLS handshake - every file.  */
        System.setProperty("http.maxConnections", String.valueOf(DL_THREADS));

        final AtomicInteger next = new AtomicInteger(0);
        final AtomicInteger files = new AtomicInteger(0);
        final AtomicLong bytes = new AtomicLong(0);
        final AtomicReference<Exception> failed = new AtomicReference<Exception>();

        ExecutorService pool = Executors.newFixedThreadPool(DL_THREADS);
        for (int w = 0; w < DL_THREADS; w++) {
            pool.execute(new Runnable() { public void run() {
                int i;
                while (failed.get() == null && (i = next.getAndIncrement()) < todo.size()) {
                    String[] t = todo.get(i);
                    try {
                        downloadOne(rootDir, blobBase, t);
                        files.incrementAndGet();
                        bytes.addAndGet(Long.parseLong(t[2]));
                    } catch (Exception e) {
                        failed.compareAndSet(null, e);
                    }
                }
            }});
        }
        pool.shutdown();

        /*  Progress is reported from here, four times a second, not once per
         *  file from the workers: that was 23,000 UI posts and log lines.       */
        while (!pool.awaitTermination(250, TimeUnit.MILLISECONDS)) {
            long b = bytes.get();
            say(null, files.get() + " / " + todo.size() + "   " + mb(b) + " of " + mb(todoBytes),
                (int) (todoBytes == 0 ? 1000 : b * 1000 / todoBytes));
        }
        /*  A failure stops new files from starting; the ones already running
         *  finish or fail on their own. Nothing half-written is ever renamed
         *  into place, so the next launch resumes cleanly.                     */
        if (failed.get() != null) throw failed.get();
        say(null, files.get() + " / " + todo.size() + "   " + mb(bytes.get()) + " of " + mb(todoBytes), 1000);
    }

    private void downloadOne(File rootDir, String blobBase, String[] t) throws Exception {
        File dest = safeDest(rootDir, t[0]);
        File parent = dest.getParentFile();
        /*  Two workers can create the same directory at the same moment, and
         *  mkdirs() then returns false for one of them - not a failure.        */
        if (parent != null && !parent.exists() && !parent.mkdirs() && !parent.isDirectory())
            throw new Exception("cannot create " + parent);

        File tmp = new File(dest.getPath() + ".tmp");
        if (t[3] != null) downloadParts(dest, tmp, blobBase, t);
        else httpToFile(blobBase + t[1], tmp, Long.parseLong(t[2]));

        String got = sha256(tmp);
        if (!got.equalsIgnoreCase(t[1])) {
            tmp.delete();
            throw new Exception("checksum failed for " + t[0]);
        }
        /*  Replace only once the bytes are known good, so being killed
         *  mid-download can never leave a corrupt file behind. */
        if (dest.exists() && !dest.delete()) throw new Exception("cannot replace " + t[0]);
        if (!tmp.renameTo(dest)) throw new Exception("cannot rename " + t[0]);
    }

    /*  A file the store also keeps as slices ("parts" in its manifest entry).
     *
     *  Cloudflare caches nothing over 512 MB, and Map.rcc is 548 MB: whole, it
     *  came from the origin at about 1 MB/s; as 64 MB parts it comes out of the
     *  cache like everything else. Each part is its own blob, downloaded
     *  resumably, checked against its own hash, and appended; the caller then
     *  checks the joined file against the whole-file hash before it is renamed
     *  into place, so a wrong part list cannot install anything either.
     *
     *  A part is deleted once appended, which keeps the peak at the file plus
     *  one part. If the launcher is killed part way, the next launch starts the
     *  join again - the parts already fetched come back out of the cache.      */
    private void downloadParts(File dest, File tmp, String blobBase, String[] t) throws Exception {
        JSONArray parts = new JSONArray(t[3]);
        long total = 0;
        for (int k = 0; k < parts.length(); k++) {
            String psha = parts.getJSONObject(k).getString("sha256");
            if (!psha.matches("[0-9a-fA-F]{64}")) throw new Exception("bad part hash for " + t[0]);
            total += parts.getJSONObject(k).getLong("size");
        }
        if (total != Long.parseLong(t[2])) throw new Exception("parts do not add up for " + t[0]);

        tmp.delete();
        OutputStream out = new FileOutputStream(tmp);
        boolean joined = false;
        try {
            byte[] buf = new byte[1 << 16];
            for (int k = 0; k < parts.length(); k++) {
                String psha = parts.getJSONObject(k).getString("sha256");
                long psize = parts.getJSONObject(k).getLong("size");
                File pf = new File(dest.getPath() + ".part" + k);
                httpToFile(blobBase + psha, pf, psize);
                if (pf.length() != psize || !psha.equalsIgnoreCase(sha256(pf))) {
                    pf.delete();
                    throw new Exception("checksum failed for part " + k + " of " + t[0]);
                }
                InputStream in = new FileInputStream(pf);
                try {
                    int n;
                    while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                } finally { in.close(); }
                pf.delete();
            }
            joined = true;
        } finally {
            out.close();
            /*  A failed join is restarted from nothing next time, so the
             *  partial file is only disk space - up to 548 MB of it.          */
            if (!joined) tmp.delete();
        }
    }

    private byte[] httpGet(String url) throws Exception {
        HttpURLConnection c = open(url);
        try {
            byte[] b = readAll(c.getInputStream(), MANIFEST_MAX);
            return b;
        } finally { c.disconnect(); }
    }

    /*  Resumable: a 574 MB pack over mobile data will be interrupted, and
     *  starting again from zero each time never finishes. */
    private void httpToFile(String url, File tmp, long expected) throws Exception {
        long have = tmp.exists() ? tmp.length() : 0;
        /*  A part-file bigger than the whole is not a resume point.           */
        if (expected >= 0 && have > expected) { tmp.delete(); have = 0; }

        HttpURLConnection c = open(url);
        if (have > 0) c.setRequestProperty("Range", "bytes=" + have + "-");
        boolean bad = false;
        try {
            int code = c.getResponseCode();
            boolean append = (code == 206);
            if (!append && have > 0) have = 0;          //  server ignored Range
            if (code != 200 && code != 206) throw new Exception("HTTP " + code);

            InputStream in = c.getInputStream();
            OutputStream out = new FileOutputStream(tmp, append);
            long written = have;
            try {
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) {
                    /*  Stop at the size the manifest promised. The checksum
                     *  would reject the result anyway, but only after the whole
                     *  body had been written - and a body with no end fills the
                     *  device long before that.                               */
                    written += n;
                    if (expected >= 0 && written > expected) {
                        bad = true;
                        throw new Exception("oversize body");
                    }
                    out.write(buf, 0, n);
                }
            } finally { out.close(); in.close(); }
        } catch (Exception e) {
            if (bad) tmp.delete();
            c.disconnect();
            throw e;
        }
        /*  No disconnect() on success. The body was read to its end and closed,
         *  which hands the socket back to the keep-alive pool; disconnect() may
         *  close it instead, and then every file pays a new TCP and TLS
         *  handshake - 130 ms a file through Cloudflare instead of 75.          */
    }

    private HttpURLConnection open(String url) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(15000);
        c.setReadTimeout(30000);
        c.setInstanceFollowRedirects(true);
        return c;
    }

    /* --------------------------------------------------------------- utils */

    private static byte[] readAll(InputStream in) throws Exception {
        return readAll(in, Integer.MAX_VALUE);
    }

    private static byte[] readAll(InputStream in, int max) throws Exception {
        java.io.ByteArrayOutputStream o = new java.io.ByteArrayOutputStream();
        byte[] buf = new byte[1 << 16];
        int n;
        long total = 0;
        try {
            while ((n = in.read(buf)) > 0) {
                total += n;
                if (total > max) throw new Exception("response larger than " + max + " bytes");
                o.write(buf, 0, n);
            }
        } finally { in.close(); }
        return o.toByteArray();
    }

    private static String sha256(File f) throws Exception {
        MessageDigest d = MessageDigest.getInstance("SHA-256");
        FileInputStream in = new FileInputStream(f);
        try {
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) d.update(buf, 0, n);
        } finally { in.close(); }
        byte[] h = d.digest();
        StringBuilder sb = new StringBuilder(64);
        for (byte x : h) sb.append(Character.forDigit((x >> 4) & 0xF, 16))
                           .append(Character.forDigit(x & 0xF, 16));
        return sb.toString();
    }

    private static String mb(long b) { return String.format("%.1f MB", b / 1048576.0); }
    private static void sleep(long ms) { try { Thread.sleep(ms); } catch (InterruptedException e) {} }

    /*  The shortest thing that says WHICH failure this was.
     *
     *  Class name, plus the HTTP status when the message carries one, plus the
     *  cause's class where there is one - an SSLHandshakeException wrapped in
     *  an IOException is the difference between "this phone does not trust the
     *  certificate" and "this phone has no network". No message text is
     *  copied, only the type and, where present, three digits, so nothing here
     *  can put a path or a host on a screenshot.                             */
    private static String reasonCode(Throwable t) {
        StringBuilder sb = new StringBuilder();
        Throwable c = t;
        for (int depth = 0; c != null && depth < 3; ++depth) {
            if (sb.length() > 0) sb.append(" / ");
            sb.append(c.getClass().getSimpleName());
            String msg = c.getMessage();
            if (msg != null) {
                java.util.regex.Matcher mm =
                    java.util.regex.Pattern.compile("HTTP (\\d{3})").matcher(msg);
                if (mm.find()) sb.append(' ').append(mm.group(1));
            }
            c = c.getCause();
        }
        return sb.toString();
    }

    /*  Set by fail(): this run is over, and the retry loop must not restart it. */
    private volatile boolean mFatal = false;

    private void fail(final String title, final String msg) {
        mFatal = true;
        ui.post(new Runnable() { public void run() {
            bar.setVisibility(ViewGroup.INVISIBLE);
            new AlertDialog.Builder(RanLauncher.this)
                .setTitle(title).setMessage(msg).setCancelable(false)
                .setPositiveButton("ปิด", null).show();
            status.setText(title);
            detail.setText(msg);
        }});
    }



    /* ------------------------------------------------------------ apk update
     *
     *  Native code cannot ride the payload. Since Android 10 an app targeting
     *  API 29 or above may not dlopen a library out of its own writable
     *  storage - W^X - and this one targets 34. A code fix therefore reaches a
     *  player only as a new APK, and this is what installs it.
     *
     *  Every step of that is somewhere to be careful, so:
     *
     *  *  The APK's hash comes out of manifest.json, which is verified against
     *     a key compiled into this APK before it is even parsed. The bytes are
     *     authenticated, not merely un-corrupted, and that holds over plain
     *     HTTP to a bare IP - the property the data blobs already have.
     *
     *  *  It is fetched from blobs/<sha256>, content-addressed like everything
     *     else. The manifest never names a path here, so there is no traversal
     *     surface and nothing new to validate.
     *
     *  *  The bytes stream straight into a PackageInstaller session and are
     *     hashed on the way through. They are never a file on disk that this
     *     app, or any other, could swap between the check and the install -
     *     which is the hole every download-verify-install sequence has.
     *
     *  *  A mismatch abandons the session, so a wrong or truncated body is
     *     discarded rather than handed to the installer.
     *
     *  *  Only a strictly newer versionCode is offered. An old manifest stays
     *     validly signed forever, so without this a replay could walk a player
     *     back to a version whose bugs are known.
     *
     *  *  Android's own check is the second anchor, and the one that cannot be
     *     talked around: an APK signed with a different key from the installed
     *     app is refused outright. The manifest signature says "the publisher
     *     meant this"; the platform signature says "this is the same app".
     *
     *  Returns true when the install went ahead - the process is about to be
     *  replaced, so there is nothing further to do this run.                  */
    private static final String INSTALL_ACTION = "com.ran.launcher.INSTALL_RESULT";

    /*  Set by patch() when the installed APK is below minApk: this update is
     *  not optional, and the screen must not promise the old version will do. */
    private volatile boolean mApkRequired = false;

    private String keepOld() {
        return mApkRequired ? "ต้องติดตั้งอัปเดตนี้ก่อนจึงจะเล่นได้"
                            : "จะเล่นด้วยเวอร์ชันที่ติดตั้งไว้";
    }

    private boolean offerApk(JSONObject apk, int myApk) throws Exception {
        if (apk == null) return false;

        final int want = apk.getInt("versionCode");
        if (want <= myApk) return false;              //  never sideways, never back

        final String sha  = apk.getString("sha256");
        final long   size = apk.getLong("size");
        final String name = apk.optString("versionName", "");
        final String what = "version " + want + (name.length() == 0 ? "" : " (" + name + ")");

        /*  Installing needs the player's consent once, in Settings. Asking is
         *  all this can do, and being refused is not a reason to keep them out
         *  of the game.                                                       */
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                && !getPackageManager().canRequestPackageInstalls()) {
            say("มีอัปเดตใหม่", what + " พร้อมแล้ว แต่แอปนี้ติดตั้งเองไม่ได้\n" +
                "อนุญาตใน ติดตั้งแอปที่ไม่รู้จัก แล้วเปิดเกมใหม่", -1);
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                                         Uri.parse("package:" + getPackageName())));
            } catch (Throwable t) { /* no such screen; the message stands */ }
            sleep(4000);
            return false;
        }

        say("กำลังดาวน์โหลดอัปเดต", what + ", " + mb(size), 0);

        PackageInstaller pi = getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams sp =
            new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        sp.setAppPackageName(getPackageName());
        try { sp.setSize(size); } catch (Throwable t) { }

        final int sessionId = pi.createSession(sp);
        PackageInstaller.Session session = pi.openSession(sessionId);
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            HttpURLConnection c = open(base() + "blobs/" + sha);
            try {
                int code = c.getResponseCode();
                if (code != 200) throw new Exception("HTTP " + code + " for the apk");
                InputStream in = c.getInputStream();
                OutputStream out = session.openWrite("apk", 0, size);
                long written = 0;
                try {
                    byte[] buf = new byte[1 << 16];
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        written += n;
                        /*  The size the manifest promised is a ceiling. The hash
                         *  would reject an overlong body anyway, but only after
                         *  all of it had been written.                         */
                        if (written > size) throw new Exception("oversize apk body");
                        md.update(buf, 0, n);
                        out.write(buf, 0, n);
                        say(null, mb(written) + " of " + mb(size),
                            (int) (size == 0 ? 1000 : written * 1000 / size));
                    }
                    if (written != size) throw new Exception("short apk body");
                    session.fsync(out);
                } finally { try { out.close(); } finally { in.close(); } }
            } finally { c.disconnect(); }

            String got = hex(md.digest());
            if (!got.equalsIgnoreCase(sha)) throw new Exception("checksum failed for the apk");
        } catch (Throwable t) {
            session.abandon();
            Log.e(TAG, "apk update failed", t);   //  reason to the log, not the screen
            say("อัปเดตไม่สำเร็จ", keepOld(), -1);
            sleep(2500);
            return false;                              //  the old binary still works
        }

        return commitInstall(session, sessionId, what);
    }

    /*  Commit, and wait for the player to answer the system's install prompt.
     *
     *  The result arrives as a broadcast, and the first one is normally
     *  STATUS_PENDING_USER_ACTION carrying the confirmation Intent that has to
     *  be started from here: a session commits, it does not install by itself.
     *  The patch thread blocks on the latch so a declined install falls through
     *  to the data patch rather than racing it.                               */
    private boolean commitInstall(PackageInstaller.Session session, int sessionId,
                                  final String what) throws Exception {
        final CountDownLatch done = new CountDownLatch(1);
        final int[] status = { PackageInstaller.STATUS_FAILURE };
        final String[] why = { "" };

        BroadcastReceiver rx = new BroadcastReceiver() {
            public void onReceive(Context ctx, Intent i) {
                int st = i.getIntExtra(PackageInstaller.EXTRA_STATUS,
                                       PackageInstaller.STATUS_FAILURE);
                if (st == PackageInstaller.STATUS_PENDING_USER_ACTION) {
                    Intent confirm = (Intent) i.getParcelableExtra(Intent.EXTRA_INTENT);
                    if (confirm != null) {
                        confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                        try { startActivity(confirm); return; }      //  still pending
                        catch (Throwable t) { why[0] = String.valueOf(t.getMessage()); }
                    }
                    st = PackageInstaller.STATUS_FAILURE;
                }
                status[0] = st;
                if (why[0].length() == 0)
                    why[0] = String.valueOf(i.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE));
                done.countDown();
            }
        };

        IntentFilter filter = new IntentFilter(INSTALL_ACTION);
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(rx, filter, Context.RECEIVER_NOT_EXPORTED);
        else                             registerReceiver(rx, filter);

        try {
            /*  Addressed to this package explicitly: an implicit broadcast would
             *  let any app listening on the action see the install result.     */
            Intent i = new Intent(INSTALL_ACTION).setPackage(getPackageName());
            int flags = PendingIntent.FLAG_UPDATE_CURRENT;
            if (Build.VERSION.SDK_INT >= 31) flags |= PendingIntent.FLAG_MUTABLE;
            PendingIntent pe = PendingIntent.getBroadcast(this, sessionId, i, flags);

            say("กำลังติดตั้งอัปเดต", what + "\nกดยืนยันการติดตั้งเมื่อระบบถาม", -1);
            session.commit(pe.getIntentSender());
            session.close();

            /*  Generous: the prompt waits on a human. If it expires the app is
             *  simply left as it was.                                         */
            if (!done.await(5, TimeUnit.MINUTES)) {
                say("ยังไม่ได้ยืนยันการติดตั้ง", keepOld(), -1);
                sleep(2000);
                return false;
            }
        } finally {
            try { unregisterReceiver(rx); } catch (Throwable t) { }
        }

        if (status[0] == PackageInstaller.STATUS_SUCCESS) {
            say("อัปเดตเสร็จแล้ว", "ติดตั้ง " + what + " แล้ว", 1000);
            return true;                     //  the process is about to be replaced
        }
        Log.w(TAG, "install not completed: status " + status[0] + " " + why[0]);
        say("ติดตั้งอัปเดตไม่สำเร็จ", why[0] + "\n" + keepOld(), -1);
        sleep(2500);
        return false;
    }

    private static String hex(byte[] b) {
        StringBuilder sb = new StringBuilder(b.length * 2);
        for (byte x : b) sb.append(Character.forDigit((x >> 4) & 0xF, 16))
                           .append(Character.forDigit(x & 0xF, 16));
        return sb.toString();
    }


    /* ---------------------------------------------------------------- play */

    private void play() {
        //  A bare Intent plus setComponent. Intent(Context, Class) builds the
        //  ComponentName from the class immediately, so passing null there
        //  throws before setComponent can replace it.
        //  Before the window goes: the client shows these pixels while it boots.
        handOverPage();

        Intent i = new Intent();
        i.setComponent(new ComponentName(getPackageName(), "com.ran.launcher.RanActivity"));
        i.addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION);
        startActivity(i);
        finish();
    }
}
