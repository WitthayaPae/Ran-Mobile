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
import android.widget.ProgressBar;
import android.widget.TextView;

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
    private static final String BASE_DEFAULT = "http://143.14.11.244:1521/launcher_mobile/";

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
    private ProgressBar bar;
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
        root.setBackgroundColor(Color.parseColor("#0B0E10"));

        ImageView art = new ImageView(this);
        setDrawable(art, "ran_loading");
        //  Fill the panel and crop, rather than letterbox: the art is 2:1 and
        //  a tablet is not, and black bars around it look like a broken asset.
        art.setScaleType(ImageView.ScaleType.CENTER_CROP);
        root.addView(art, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        ImageView mark = new ImageView(this);
        setDrawable(mark, "ran_mark");
        mark.setAdjustViewBounds(true);
        FrameLayout.LayoutParams mlp = new FrameLayout.LayoutParams(
                dp(230), ViewGroup.LayoutParams.WRAP_CONTENT);
        mlp.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
        mlp.topMargin = dp(30);
        root.addView(mark, mlp);

        /*  The text and the bar sit in a band along the bottom. A little scrim
         *  behind them, because the art is bright sky in places and white text
         *  on it is unreadable.                                                */
        LinearLayout band = new LinearLayout(this);
        band.setOrientation(LinearLayout.VERTICAL);
        band.setBackgroundColor(Color.parseColor("#B4000000"));
        band.setPadding(dp(24), dp(12), dp(24), dp(14));
        FrameLayout.LayoutParams blp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        blp.gravity = Gravity.BOTTOM;
        root.addView(band, blp);

        status = new TextView(this);
        status.setTextColor(Color.parseColor("#F0F4F6"));
        status.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        status.setGravity(Gravity.CENTER);
        status.setText("Starting");
        band.addView(status);

        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(6));
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

    private int dp(int v) { return (int) (v * getResources().getDisplayMetrics().density); }

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
            if (permille >= 0) { bar.setIndeterminate(false); bar.setProgress(permille); }
            else bar.setIndeterminate(true);
        }});
    }

    /* --------------------------------------------------------------- patch */

    private void patchThenPlay() {
        try {
            adoptPrivateRoot();
            patch();
        } catch (Throwable t) {
            /*  A patch failure must not be fatal when the game is already
             *  installed - a player on a bad connection should still get in. */
            Log.e(TAG, "patch failed", t);
            final String msg = t.getMessage() == null ? t.toString() : t.getMessage();
            if (new File(ROOT, "data/glogic/GLogic.rcc").exists()) {
                say("Could not reach the patch server", msg + "\nStarting with the data already installed.", 1000);
                sleep(1800);
            } else {
                fail("Could not download the game data", msg);
                return;
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
        say("Moving game data", "one-off, into private storage", -1);

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
            say("Not enough free space", "need " + mb(need) + ", have " + mb(free), 1000);
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
        say("Checking for updates", base(), -1);

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
        if (minApk > myApk) {
            /*  A data patch cannot fix a client whose packet layout is stale,
             *  so this is a hard stop rather than a warning. */
            fail("This version of RAN is out of date",
                 "The server needs app version " + minApk + ", this is " + myApk +
                 ".\nDownload the new APK and install it over this one.");
            throw new Exception("apk too old");
        }

        /*  A new binary, if the manifest offers one, before any data is
         *  fetched: data can depend on code, never the other way round, and an
         *  install restarts the process anyway.                              */
        if (offerApk(m.optJSONObject("apk"), myApk)) return;

        File rootDir = new File(ROOT);
        if (!rootDir.exists() && !rootDir.mkdirs())
            throw new Exception("cannot create " + ROOT);

        int localVersion = readVersion();
        if (localVersion == version) { say("Up to date", "version " + version, 1000); return; }

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

        say("Checking files", arr.length() + " files", 0);
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
            if (!ok) { todo.add(new String[]{ p, sha, String.valueOf(size) }); todoBytes += size; }
            if ((i & 255) == 0) say(null, "checked " + i + " / " + arr.length(), i * 1000 / arr.length());
        }

        if (todo.isEmpty()) {
            writeIndexFrom(arr, rootDir);
            writeVersion(version);
            say("Up to date", "version " + version, 1000);
            return;
        }

        say("Downloading update", todo.size() + " files, " + mb(todoBytes), 0);
        long done = 0;
        for (int i = 0; i < todo.size(); i++) {
            String[] t = todo.get(i);
            File dest = safeDest(rootDir, t[0]);
            File parent = dest.getParentFile();
            if (parent != null && !parent.exists()) parent.mkdirs();

            File tmp = new File(dest.getPath() + ".tmp");
            httpToFile(base() + "blobs/" + t[1], tmp, Long.parseLong(t[2]));

            String got = sha256(tmp);
            if (!got.equalsIgnoreCase(t[1])) {
                tmp.delete();
                throw new Exception("checksum failed for " + t[0]);
            }
            /*  Replace only once the bytes are known good, so being killed
             *  mid-download can never leave a corrupt file behind. */
            if (dest.exists() && !dest.delete()) throw new Exception("cannot replace " + t[0]);
            if (!tmp.renameTo(dest)) throw new Exception("cannot rename " + t[0]);

            done += Long.parseLong(t[2]);
            say(null, (i + 1) + " / " + todo.size() + "   " + mb(done) + " of " + mb(todoBytes),
                (int) (todoBytes == 0 ? 1000 : done * 1000 / todoBytes));
        }

        writeIndexFrom(arr, rootDir);
        writeVersion(version);                 //  last, always
        say("Updated", "version " + version, 1000);
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
            if (code != 200 && code != 206) throw new Exception("HTTP " + code + " for " + url);

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
                        throw new Exception("oversize body for " + url);
                    }
                    out.write(buf, 0, n);
                }
            } finally { out.close(); in.close(); }
        } catch (Exception e) {
            if (bad) tmp.delete();
            throw e;
        } finally { c.disconnect(); }
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

    private void fail(final String title, final String msg) {
        ui.post(new Runnable() { public void run() {
            bar.setVisibility(ViewGroup.INVISIBLE);
            new AlertDialog.Builder(RanLauncher.this)
                .setTitle(title).setMessage(msg).setCancelable(false)
                .setPositiveButton("Close", null).show();
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
            say("Update available", what + " is ready, but this app may not install it.\n" +
                "Allow it under Install unknown apps, then restart.", -1);
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                                         Uri.parse("package:" + getPackageName())));
            } catch (Throwable t) { /* no such screen; the message stands */ }
            sleep(4000);
            return false;
        }

        say("Downloading update", what + ", " + mb(size), 0);

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
            Log.e(TAG, "apk update failed", t);
            say("Update failed", t.getMessage() + "\nContinuing on version " + myApk, -1);
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

            say("Installing update", what + "\nConfirm the install when asked.", -1);
            session.commit(pe.getIntentSender());
            session.close();

            /*  Generous: the prompt waits on a human. If it expires the app is
             *  simply left as it was.                                         */
            if (!done.await(5, TimeUnit.MINUTES)) {
                say("Update not confirmed", "Continuing on the installed version", -1);
                sleep(2000);
                return false;
            }
        } finally {
            try { unregisterReceiver(rx); } catch (Throwable t) { }
        }

        if (status[0] == PackageInstaller.STATUS_SUCCESS) {
            say("Updated", what + " installed", 1000);
            return true;                     //  the process is about to be replaced
        }
        Log.w(TAG, "install not completed: status " + status[0] + " " + why[0]);
        say("Update not installed", why[0] + "\nContinuing on the installed version", -1);
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
        Intent i = new Intent();
        i.setComponent(new ComponentName(getPackageName(), "com.ran.launcher.RanActivity"));
        i.addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION);
        startActivity(i);
        finish();
    }
}
