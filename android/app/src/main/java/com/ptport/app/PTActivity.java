package com.ptport.app;

import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Toast;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewGroup;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

import org.libsdl.app.SDLActivity;

/* SDL's activity starts SDL_main in libmain.so (the game, src/main.cpp). The hook libraries and the Turnip driver are not
   loaded here: libadrenotools opens them itself when the game starts Vulkan (src/engine/platform/android.cpp). */
public class PTActivity extends SDLActivity {
    private SecondScreen secondScreen;
    private TouchControls touchControls;
    private static PTActivity current;

    // from the game (src/engine/platform/android.cpp SetTouchControlsEnabled): the setting, any thread
    @SuppressWarnings("unused")
    public static void setTouchControlsEnabled(boolean enabled) {
        final PTActivity activity = current;
        if (activity == null) return;
        activity.runOnUiThread(() -> {
            if (activity.touchControls != null) activity.touchControls.setAllowed(enabled);
        });
    }

    private static final int PICK_DRIVER = 41;

    // from the game (Settings > Display > Import a driver; src/engine/platform/android.cpp RequestDriverImport)
    @SuppressWarnings("unused")
    public static void importDriver() {
        final PTActivity activity = current;
        if (activity == null) return;
        activity.runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            try {
                activity.startActivityForResult(intent, PICK_DRIVER);
            } catch (Exception e) {
                Toast.makeText(activity, "No file picker on this device.", Toast.LENGTH_LONG).show();
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == PICK_DRIVER) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) {
                final Uri uri = data.getData();
                new Thread(() -> {
                    String message;
                    try {
                        message = "Driver imported (" + DriverImport.importFrom(this, uri) + "). Restart P.T. to use it.";
                    } catch (Exception e) {
                        message = "Could not import that driver: " + e.getMessage();
                    }
                    final String text = message;
                    runOnUiThread(() -> Toast.makeText(this, text, Toast.LENGTH_LONG).show());
                }).start();
            }
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    // from the game (src/engine/platform/android.cpp NoteControllerEvent): a real controller was used, on the game thread
    @SuppressWarnings("unused")
    public static void controllerUsed() {
        final PTActivity activity = current;
        if (activity == null) return;
        activity.runOnUiThread(() -> {
            if (activity.touchControls != null) activity.touchControls.controllerUsed();
        });
    }

    // from the game (src/engine/platform/android.cpp UpdateSecondScreen): the loop and the subtitle line, UTF-8
    @SuppressWarnings("unused")
    public static void updateSecondScreen(int loop, byte[] line) {
        SecondScreen.update(loop, line);
    }

    // from the game: the Second screen setting (0 clock and subtitles, 1 clock, 2 dim clock, 3 off)
    @SuppressWarnings("unused")
    public static void setSecondScreenMode(int mode) {
        SecondScreen.setMode(mode);
    }

    // from the game, building its settings page: whether to offer the Second screen setting
    @SuppressWarnings("unused")
    public static boolean hasSecondScreen() {
        try {
            return SecondScreen.available();
        } catch (Throwable t) {
            return false;
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        installJavaCrashLog();
        super.onCreate(savedInstanceState);
        current = this;
        secondScreen = new SecondScreen(this);
        // the on-screen controls over the game (TouchControls.java)
        touchControls = new TouchControls(this);
        addContentView(touchControls, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    // a real controller in use puts the touch controls away (a touch brings them back); the event goes on to the game
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (touchControls != null && TouchControls.isController(event.getSource())) touchControls.controllerUsed();
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        // a stick or trigger pushed past half, not the small drift a resting stick reports
        if (touchControls != null && TouchControls.isController(event.getSource()) && event.getAction() == MotionEvent.ACTION_MOVE) {
            for (int axis : new int[] {MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ,
                     MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y, MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_RTRIGGER}) {
                if (Math.abs(event.getAxisValue(axis)) > 0.5f) {
                    touchControls.controllerUsed();
                    break;
                }
            }
        }
        return super.dispatchGenericMotionEvent(event);
    }

    // shown while the game is visible (start/stop, not resume/pause: a second-display activity can pause this one without
    // hiding it, which would take the clock down and up again)
    @Override
    protected void onStart() {
        super.onStart();
        if (secondScreen != null) {
            try {
                secondScreen.resume();
            } catch (Throwable t) {
                android.util.Log.w("pt", "second screen: " + t);
            }
        }
    }

    @Override
    protected void onStop() {
        if (secondScreen != null) {
            try {
                secondScreen.pause();
            } catch (Throwable t) {
                android.util.Log.w("pt", "second screen: " + t);
            }
        }
        super.onStop();
    }

    /* A crash on the Java side (SDL's activity) goes to Download/PT-logs/pt-java-crash.txt, or the app's own folder. */
    private void installJavaCrashLog() {
        final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            File dir = new File("/storage/emulated/0/Download/PT-logs");
            if (!dir.isDirectory() && !dir.mkdirs()) {
                dir = getExternalFilesDir(null);
            }
            try (PrintWriter out = new PrintWriter(new FileWriter(new File(dir, "pt-java-crash.txt")))) {
                out.println("thread " + thread.getName());
                error.printStackTrace(out);
            } catch (Exception ignored) {
            }
            if (previous != null) previous.uncaughtException(thread, error);
        });
    }
}
