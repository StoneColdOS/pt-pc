package com.ptport.app;

import android.os.Bundle;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

import org.libsdl.app.SDLActivity;

/* SDL's activity starts SDL_main in libmain.so (the game, src/main.cpp). The hook libraries and the Turnip driver are not
   loaded here: libadrenotools opens them itself when the game starts Vulkan (src/engine/platform/android.cpp). */
public class PTActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        installJavaCrashLog();
        super.onCreate(savedInstanceState);
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
