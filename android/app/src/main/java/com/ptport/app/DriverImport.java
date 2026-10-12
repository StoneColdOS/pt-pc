package com.ptport.app;

import android.content.Context;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.database.Cursor;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/* Custom graphics drivers: an AdrenoTools driver package (a .zip with meta.json naming its libvulkan_*.so, as K11MCH1 and
   others publish them) or a bare .so, picked with Android's file picker. The library goes into getFilesDir()/drivers, the one
   kind of place a library may be loaded from, custom.txt names it, and select.txt tells the game to use it from the next start
   (src/engine/platform/android.cpp: the "custom" driver, TakePendingDriverChoice). */
final class DriverImport {
    private DriverImport() {
    }

    static File driverDir(Context context) {
        File dir = new File(context.getFilesDir(), "drivers");
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        return dir;
    }

    /** Imports the picked file; returns the driver's file name, or throws with a message for the player. */
    static String importFrom(Context context, Uri uri) throws Exception {
        String display = displayName(context, uri);
        File dir = driverDir(context);
        String library;
        if (display.toLowerCase(java.util.Locale.ROOT).endsWith(".so")) {
            library = sanitize(display);
            try (InputStream in = context.getContentResolver().openInputStream(uri)) {
                copy(in, new File(dir, library));
            }
        } else {
            library = fromZip(context, uri, dir);
        }
        writeText(new File(dir, "custom.txt"), library);
        select(context, "custom");
        return library;
    }

    /** The driver the game takes at its next start: "turnip", "system" or "custom". */
    static void select(Context context, String choice) throws Exception {
        writeText(new File(driverDir(context), "select.txt"), choice);
    }

    static boolean hasCustom(Context context) {
        File list = new File(driverDir(context), "custom.txt");
        return list.isFile();
    }

    private static String fromZip(Context context, Uri uri, File dir) throws Exception {
        // first pass: meta.json for the library name; the libraries themselves are kept in memory meanwhile (a few MB)
        String named = null;
        java.util.Map<String, byte[]> libraries = new java.util.HashMap<>();
        try (InputStream raw = context.getContentResolver().openInputStream(uri); ZipInputStream zip = new ZipInputStream(raw)) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                if (entry.isDirectory()) continue;
                String name = new File(entry.getName()).getName();
                if (name.equals("meta.json")) {
                    JSONObject meta = new JSONObject(new String(readAll(zip), "UTF-8"));
                    named = meta.optString("libraryName", null);
                } else if (name.endsWith(".so")) {
                    libraries.put(name, readAll(zip));
                }
            }
        }
        if (libraries.isEmpty()) throw new Exception("That file has no driver in it (no .so inside the .zip).");
        String library = named != null && libraries.containsKey(named) ? named : null;
        if (library == null) {
            for (String name : libraries.keySet()) {
                if (name.startsWith("libvulkan")) library = name;
            }
        }
        if (library == null) library = libraries.keySet().iterator().next();
        String target = sanitize(library);
        try (OutputStream out = new FileOutputStream(new File(dir, target))) {
            out.write(libraries.get(library));
        }
        return target;
    }

    private static String displayName(Context context, Uri uri) {
        try (Cursor cursor = context.getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                String name = cursor.getString(0);
                if (name != null) return name;
            }
        } catch (Exception ignored) {
        }
        return "driver.zip";
    }

    // libadrenotools takes a plain file name; keep it simple and recognisably a library
    private static String sanitize(String name) {
        String clean = new File(name).getName().replaceAll("[^A-Za-z0-9._-]", "_");
        if (!clean.endsWith(".so")) clean += ".so";
        if (!clean.startsWith("lib")) clean = "lib" + clean;
        return clean;
    }

    private static byte[] readAll(InputStream in) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[64 * 1024];
        int n;
        while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
        return out.toByteArray();
    }

    private static void copy(InputStream in, File target) throws Exception {
        if (in == null) throw new Exception("The file could not be opened.");
        try (OutputStream out = new FileOutputStream(target)) {
            byte[] buffer = new byte[64 * 1024];
            int n;
            while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
        }
    }

    private static void writeText(File file, String text) throws Exception {
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes("UTF-8"));
        }
    }
}
