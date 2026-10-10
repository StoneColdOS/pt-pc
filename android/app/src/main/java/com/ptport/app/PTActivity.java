package com.ptport.app;

import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;

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
        requestStorageAccessIfNeeded();
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

    /* The game files may sit in the shared Download folder (Android/data is closed to file managers on Android 11+). Reading
       them there takes All files access; asked for only while the app's own folder has no copy. */
    private void requestStorageAccessIfNeeded() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R || Environment.isExternalStorageManager()) {
            return;
        }
        File own = new File(getExternalFilesDir(null), "game/CUSA01127/chunk1.psarc");
        if (own.exists()) {
            return;
        }
        try {
            Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
            intent.setData(Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Exception e) {
            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        }
    }
}
