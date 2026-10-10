package com.ptport.app;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.os.StatFs;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;

/* The first screen: starts the game when its files are in place, otherwise sets them up. The player picks their P.T. .pkg
   with Android's file picker (no storage permission needed for that), libptsetup.so takes the three game archives out of it
   into the app's own folder (src/pkginstall), and the game starts. Players who already have the three files can put them in
   Download instead, which needs All files access. Everything is reachable with a gamepad: the d-pad moves, A presses. */
public class SetupActivity extends Activity {
    private static final int PICK_PKG = 1;
    private static final long NEEDED_BYTES = 1_400_000_000L;
    private static final String[] SHARED_GAME_DIRS = {
        "/storage/emulated/0/Download/CUSA01127", "/storage/emulated/0/Download/PT", "/storage/emulated/0/Download",
        "/storage/emulated/0/PT/CUSA01127", "/storage/emulated/0/PT", "/storage/emulated/0/CUSA01127"};

    private LinearLayout panel;
    private volatile boolean cancelled;
    private long lastUiUpdate;
    private boolean waitingForAccess;

    private native String nativeExtract(int fd, String destination);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN, WindowManager.LayoutParams.FLAG_FULLSCREEN);
        if (gameInstalled()) {
            startGame();
            return;
        }
        System.loadLibrary("ptsetup");
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(8, 8, 9));
        ScrollView scroll = new ScrollView(this);
        panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setGravity(Gravity.CENTER_HORIZONTAL);
        int pad = dp(28);
        panel.setPadding(pad, pad, pad, pad);
        scroll.addView(panel, new ScrollView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(Math.min(dp(640), getResources().getDisplayMetrics().widthPixels),
                ViewGroup.LayoutParams.MATCH_PARENT, Gravity.CENTER_HORIZONTAL);
        root.addView(scroll, lp);
        setContentView(root);
        showWelcome();
    }

    @Override
    protected void onResume() {
        super.onResume();
        // back from Android's permission screen: the files in Download are readable now
        if (waitingForAccess && gameInstalled()) {
            waitingForAccess = false;
            startGame();
        }
    }

    // ---- screens

    private void showWelcome() {
        panel.removeAllViews();
        title("P.T.");
        text("This app needs your own copy of the game.\n\n"
                + "Choose your P.T. .pkg file (a fake PKG made from your own dump). The setup takes the game files out of it "
                + "once, which takes a minute or two and needs about 1.4 GB of free space. After that the game starts straight "
                + "away every time.");
        Button pick = button("Choose your P.T. .pkg", v -> pickPackage());
        button("I already have the extracted files", v -> showExtractedFiles());
        button("Quit", v -> finish());
        pick.requestFocus();
    }

    private void showExtractedFiles() {
        panel.removeAllViews();
        title("Extracted files");
        text("Copy these three files from your CUSA01127 dump into the Download folder (loose, or in a CUSA01127 folder "
                + "inside it):\n\n    chunk1.psarc\n    texture.qar\n    pathid_list_ps4.bin\n\n"
                + "Then allow P.T. \"All files access\" so it can read them there.");
        Button allow = button("Allow access", v -> requestAllFilesAccess());
        button("Back", v -> showWelcome());
        allow.requestFocus();
    }

    private void showProgress() {
        panel.removeAllViews();
        title("Setting up");
        TextView status = text("Opening the package...");
        status.setTag("status");
        ProgressBar bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        bar.setTag("bar");
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(12));
        lp.setMargins(0, dp(8), 0, dp(20));
        panel.addView(bar, lp);
        Button cancel = button("Cancel", v -> {
            cancelled = true;
            v.setEnabled(false);
        });
        cancel.requestFocus();
    }

    private void showError(String message) {
        panel.removeAllViews();
        title("That did not work");
        text(message);
        Button again = button("Choose another file", v -> pickPackage());
        button("Back", v -> showWelcome());
        again.requestFocus();
    }

    private void showDone(Uri packageUri) {
        panel.removeAllViews();
        title("Ready");
        text("P.T. is set up. The .pkg is not needed any more: deleting it frees about 1.3 GB.");
        Button start = button("Start P.T.", v -> startGame());
        button("Delete the .pkg and start", v -> {
            boolean deleted = false;
            try {
                deleted = DocumentsContract.deleteDocument(getContentResolver(), packageUri);
            } catch (Exception ignored) {
            }
            if (!deleted) {
                panel.removeAllViews();
                title("Ready");
                text("The .pkg could not be deleted from here. You can remove it yourself with a file manager.");
                button("Start P.T.", w -> startGame()).requestFocus();
                return;
            }
            startGame();
        });
        start.requestFocus();
    }

    // ---- actions

    private void pickPackage() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        try {
            startActivityForResult(intent, PICK_PKG);
        } catch (Exception e) {
            showError("This device has no file picker for choosing the package.");
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_PKG || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        extract(data.getData());
    }

    private void extract(Uri uri) {
        File destination = new File(getExternalFilesDir(null), "game/CUSA01127");
        destination.mkdirs();
        long free = new StatFs(destination.getAbsolutePath()).getAvailableBytes();
        if (free < NEEDED_BYTES) {
            showError("There is not enough free space: the game files need about 1.4 GB, and " + (free / 1_000_000) + " MB are free.");
            return;
        }
        final ParcelFileDescriptor pfd;
        try {
            pfd = getContentResolver().openFileDescriptor(uri, "r");
        } catch (Exception e) {
            showError("Could not open the chosen file: " + e.getMessage());
            return;
        }
        if (pfd == null) {
            showError("Could not open the chosen file.");
            return;
        }
        cancelled = false;
        showProgress();
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        new Thread(() -> {
            String error;
            try {
                error = nativeExtract(pfd.getFd(), destination.getAbsolutePath());
            } catch (Throwable t) {
                error = "The setup failed: " + t;
            }
            try {
                pfd.close();
            } catch (Exception ignored) {
            }
            final String result = error;
            runOnUiThread(() -> {
                getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                if (result == null) {
                    showDone(uri);
                } else if (result.isEmpty()) {
                    showWelcome();  // cancelled
                } else {
                    showError(result);
                }
            });
        }, "pt-setup").start();
    }

    // called from libptsetup.so on the setup thread; false stops the extraction
    @SuppressWarnings("unused")
    private boolean onExtractProgress(long done, long total, String stage) {
        long now = System.currentTimeMillis();
        if (now - lastUiUpdate >= 100 || done == total) {
            lastUiUpdate = now;
            runOnUiThread(() -> {
                TextView status = panel.findViewWithTag("status");
                ProgressBar bar = panel.findViewWithTag("bar");
                if (status == null || bar == null) return;
                int permille = total > 0 ? (int) (done * 1000 / total) : 0;
                bar.setProgress(permille);
                status.setText(stage + "...  " + (permille / 10) + "%");
            });
        }
        return !cancelled;
    }

    private void requestAllFilesAccess() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R || Environment.isExternalStorageManager()) {
            if (gameInstalled()) {
                startGame();
            } else {
                showError("The three files were not found in Download. Check their names and that they finished copying.");
            }
            return;
        }
        waitingForAccess = true;
        try {
            Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
            intent.setData(Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Exception e) {
            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        }
    }

    private boolean gameInstalled() {
        File own = new File(getExternalFilesDir(null), "game/CUSA01127");
        if (hasGameFiles(own)) return true;
        boolean canReadShared = Build.VERSION.SDK_INT < Build.VERSION_CODES.R || Environment.isExternalStorageManager();
        if (!canReadShared) return false;
        for (String dir : SHARED_GAME_DIRS) {
            if (hasGameFiles(new File(dir))) return true;
        }
        return false;
    }

    private static boolean hasGameFiles(File dir) {
        return new File(dir, "chunk1.psarc").isFile() && new File(dir, "texture.qar").isFile();
    }

    private void startGame() {
        startActivity(new Intent(this, PTActivity.class));
        finish();
    }

    // ---- gamepad: A presses the focused button, B goes back

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BUTTON_A) {
            KeyEvent center = new KeyEvent(event.getDownTime(), event.getEventTime(), event.getAction(), KeyEvent.KEYCODE_DPAD_CENTER,
                    event.getRepeatCount(), event.getMetaState(), event.getDeviceId(), event.getScanCode(), event.getFlags(), event.getSource());
            return super.dispatchKeyEvent(center);
        }
        if (event.getKeyCode() == KeyEvent.KEYCODE_BUTTON_B) {
            if (event.getAction() == KeyEvent.ACTION_UP) onBackPressed();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    // ---- views

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void title(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextColor(Color.rgb(222, 218, 208));
        view.setTextSize(34);
        view.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
        view.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, dp(12), 0, dp(18));
        panel.addView(view, lp);
    }

    private TextView text(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextColor(Color.rgb(176, 172, 164));
        view.setTextSize(16);
        view.setLineSpacing(0, 1.2f);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, 0, 0, dp(20));
        panel.addView(view, lp);
        return view;
    }

    private Button button(String label, View.OnClickListener onClick) {
        Button view = new Button(this);
        view.setText(label);
        view.setAllCaps(false);
        view.setTextSize(17);
        view.setTextColor(Color.rgb(230, 226, 216));
        view.setFocusable(true);
        GradientDrawable normal = new GradientDrawable();
        normal.setColor(Color.rgb(28, 28, 30));
        normal.setCornerRadius(dp(6));
        normal.setStroke(dp(2), Color.rgb(28, 28, 30));
        GradientDrawable focused = new GradientDrawable();
        focused.setColor(Color.rgb(44, 42, 40));
        focused.setCornerRadius(dp(6));
        focused.setStroke(dp(2), Color.rgb(200, 190, 170));
        android.graphics.drawable.StateListDrawable states = new android.graphics.drawable.StateListDrawable();
        states.addState(new int[] {android.R.attr.state_focused}, focused);
        states.addState(new int[] {android.R.attr.state_pressed}, focused);
        states.addState(new int[] {}, normal);
        view.setBackground(states);
        view.setOnClickListener(onClick);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(56));
        lp.setMargins(0, 0, 0, dp(12));
        panel.addView(view, lp);
        return view;
    }
}
