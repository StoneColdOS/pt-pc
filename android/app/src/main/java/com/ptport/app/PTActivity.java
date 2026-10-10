package com.ptport.app;

import android.os.Bundle;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

import org.libsdl.app.SDLActivity;

/* SDL's activity starts SDL_main in libmain.so (the game, src/main.cpp). The hook libraries and the Turnip driver are not
   loaded here: libadrenotools opens them itself when the game starts Vulkan (src/engine/platform/android.cpp). */
public class PTActivity extends SDLActivity {
    private SecondScreen secondScreen;

    // from the game (src/engine/platform/android.cpp UpdateSecondScreen): the loop the player is on
    @SuppressWarnings("unused")
    public static void updateSecondScreen(int loop) {
        SecondScreen.update(loop);
    }

    // from the game: the Second screen setting (0 clock, 1 dim clock, 2 off)
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
        secondScreen = new SecondScreen(this);
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
