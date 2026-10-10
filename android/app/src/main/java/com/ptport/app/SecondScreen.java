package com.ptport.app;

import android.app.Activity;
import android.app.ActivityOptions;
import android.app.Presentation;
import android.content.Context;
import android.content.Intent;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Typeface;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.util.Log;
import android.view.Display;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;


/* The bottom screen of a dual-screen handheld (AYN Thor and the like): a frozen 23:59 digital clock, white with a cold blue
   glow like the hallway's own, the loop the player is on, and (the default look) the subtitle line playing now. Drawn with Android's own Canvas, so the game's GPU
   time is not touched. Shown as a Presentation on the second display (an activity on that display where the system refuses
   a Presentation); nothing happens on a device with one screen.

   The look is the game's Second screen setting (Settings > Display; display.second_screen in pt.ini), which the game sends
   through setMode as soon as its settings are read, and again on every change: until then nothing is shown. The game sends
   the loop and the line through update (src/engine/platform/android.cpp). */
final class SecondScreen {
    private static final String TAG = "pt";
    // after a line ends, the text layout stays this long before the large clock comes back, so the screen does not jump
    // between the two in the pauses of a conversation or a broadcast
    private static final long HOLD_LAYOUT_MS = 8_000;
    private static final String[] MODES = {"subtitles", "clock", "dim", "off"};
    private static SecondScreen instance;
    private static int modeIndex = -1;
    private static int loop = 0;
    private static String line = "";

    private final Activity activity;
    private final Handler main = new Handler(Looper.getMainLooper());
    private Presentation presentation;
    private boolean usingActivity;
    private ClockView view;
    private String mode = "off";
    private final DisplayManager.DisplayListener listener = new DisplayManager.DisplayListener() {
        @Override public void onDisplayAdded(int displayId) { refresh(); }
        @Override public void onDisplayRemoved(int displayId) { refresh(); }
        @Override public void onDisplayChanged(int displayId) {}
    };

    SecondScreen(Activity activity) {
        this.activity = activity;
        instance = this;
    }

    // ---- the game's side (any thread)

    static void update(int newLoop, byte[] utf8) {
        final String text = utf8 == null ? "" : new String(utf8, java.nio.charset.StandardCharsets.UTF_8);
        final int value = newLoop;
        new Handler(Looper.getMainLooper()).post(() -> {
            loop = value;
            line = text;
            SecondScreen screen = instance;
            if (screen != null && screen.view != null) {
                screen.view.setLoop(loop);
                screen.view.setLine(line);
            }
        });
    }

    static void setMode(int index) {
        final int value = Math.max(0, Math.min(MODES.length - 1, index));
        new Handler(Looper.getMainLooper()).post(() -> {
            modeIndex = value;
            SecondScreen screen = instance;
            if (screen == null) return;
            String next = MODES[value];
            boolean showing = screen.presentation != null || screen.usingActivity;
            if (showing && !next.equals("off") && screen.view != null) {
                screen.mode = next;
                screen.view.setMode(next);
            } else {
                screen.refresh();
            }
        });
    }

    // any thread: whether there is a second display to draw on
    static boolean available() {
        SecondScreen screen = instance;
        return screen != null && screen.findSecondDisplay() != null;
    }

    // the activity fallback hands its view in here
    static void attach(ClockView view) {
        SecondScreen screen = instance;
        if (screen == null) return;
        screen.view = view;
        view.setMode(screen.mode);
        view.setLoop(loop);
        view.setLine(line);
    }

    static void detach(ClockView view) {
        SecondScreen screen = instance;
        if (screen != null && screen.view == view) screen.view = null;
    }

    // ---- the activity's side (main thread)

    void resume() {
        DisplayManager dm = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        if (dm != null) dm.registerDisplayListener(listener, main);
        refresh();
    }

    void pause() {
        DisplayManager dm = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        if (dm != null) dm.unregisterDisplayListener(listener);
        hide();
    }

    private void refresh() {
        hide();
        mode = modeIndex < 0 ? "off" : MODES[modeIndex];
        if (mode.equals("off")) return;
        Display display = findSecondDisplay();
        if (display == null) return;
        Log.i(TAG, "second screen: display " + display.getDisplayId() + " (" + display.getName() + "), mode " + mode);
        try {
            presentation = new BottomScreen(activity, display);
            presentation.show();
            return;
        } catch (Throwable t) {
            Log.w(TAG, "second screen: no Presentation on display " + display.getDisplayId() + " (" + t + "), trying an activity");
            presentation = null;
            view = null;
        }
        try {
            ActivityOptions options = ActivityOptions.makeBasic();
            options.setLaunchDisplayId(display.getDisplayId());
            Intent intent = new Intent(activity, SecondScreenActivity.class);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_MULTIPLE_TASK);
            activity.startActivity(intent, options.toBundle());
            usingActivity = true;
        } catch (Throwable t) {
            Log.w(TAG, "second screen: cannot show anything on display " + display.getDisplayId() + " (" + t + ")");
        }
    }

    private void hide() {
        if (presentation != null) {
            try {
                presentation.dismiss();
            } catch (Throwable ignored) {
            }
            presentation = null;
        }
        if (usingActivity) {
            SecondScreenActivity.finishAll();
            usingActivity = false;
        }
        view = null;
    }

    private Display findSecondDisplay() {
        DisplayManager dm = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        if (dm == null) return null;
        for (Display d : dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (d.getDisplayId() != Display.DEFAULT_DISPLAY && d.getState() != Display.STATE_OFF) return d;
        }
        for (Display d : dm.getDisplays()) {
            if (d.getDisplayId() == Display.DEFAULT_DISPLAY) continue;
            if ((d.getFlags() & Display.FLAG_PRIVATE) != 0) continue;
            if (d.getState() == Display.STATE_OFF) continue;
            return d;
        }
        return null;
    }

    private final class BottomScreen extends Presentation {
        BottomScreen(Context context, Display display) {
            super(context, display);
        }

        @Override
        protected void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);
            Window window = getWindow();
            // never takes the gamepad or touches away from the game
            window.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                    | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            ClockView clock = new ClockView(getContext());
            setContentView(clock);
            hideBars(window);
            view = clock;
            clock.setMode(mode);
            clock.setLoop(loop);
            clock.setLine(line);
        }
    }

    static void hideBars(Window window) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController c = window.getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    // ---- the drawing

    static final class ClockView extends View {
        // a seven-segment digit in a 60x100 box (a b c d e f g: top, upper right, lower right, bottom, lower left, upper left, middle)
        private static final float[][] SEGMENT_POINTS = {
            {8, 10, 14, 4, 46, 4, 52, 10, 46, 16, 14, 16},
            {50, 12, 56, 18, 56, 42, 50, 48, 44, 42, 44, 18},
            {50, 52, 56, 58, 56, 82, 50, 88, 44, 82, 44, 58},
            {8, 90, 14, 84, 46, 84, 52, 90, 46, 96, 14, 96},
            {10, 52, 16, 58, 16, 82, 10, 88, 4, 82, 4, 58},
            {10, 12, 16, 18, 16, 42, 10, 48, 4, 42, 4, 18},
            {8, 50, 14, 44, 46, 44, 52, 50, 46, 56, 14, 56},
        };
        private static final String[] DIGITS = {"abcdef", "bc", "abged", "abgcd", "fgbc", "afgcd", "afgedc", "abc", "abcdefg", "abcdfg"};
        // the hallway clock's colours: near-white segments in a cold blue glow
        private static final int SEGMENT = Color.rgb(0xEA, 0xF1, 0xFF);
        private static final int GLOW = Color.rgb(0x4A, 0x78, 0xFF);
        private static final int LABEL = Color.rgb(0x8F, 0xB0, 0xFF);
        private static final int DIM = Color.rgb(0xA9, 0xC0, 0xFF);
        private static final float SKEW = (float) Math.tan(Math.toRadians(6));

        private final Paint halo = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint lit = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint ghost = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final TextPaint label = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        private final TextPaint subtitle = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        private final Paint rule = new Paint();
        private final Handler handler = new Handler(Looper.getMainLooper());
        private final Runnable relayout = this::invalidate;
        private final Path[] segments = new Path[7];
        private final Path placed = new Path();
        private final Matrix matrix = new Matrix();

        private String mode = "clock";
        private int loopNumber;
        private String currentLine = "";
        private long lineEndedAt;

        ClockView(Context context) {
            super(context);
            Typeface typewriter;
            try {
                typewriter = getResources().getFont(R.font.special_elite);
            } catch (Exception e) {
                typewriter = Typeface.MONOSPACE;
            }
            ghost.setColor(GLOW);
            ghost.setAlpha(16);
            label.setTypeface(typewriter);
            label.setColor(LABEL);
            subtitle.setTypeface(typewriter);
            subtitle.setColor(Color.rgb(0xE8, 0xEC, 0xF4));
            rule.setColor(Color.rgb(0x1E, 0x24, 0x33));
            for (int i = 0; i < 7; i++) {
                float[] pts = SEGMENT_POINTS[i];
                Path path = new Path();
                for (int j = 0; j < pts.length; j += 2) {
                    // the mock-up's translate(6 0) skewX(-6)
                    float x = pts[j] - pts[j + 1] * SKEW + 6, y = pts[j + 1];
                    if (j == 0) path.moveTo(x, y); else path.lineTo(x, y);
                }
                path.close();
                segments[i] = path;
            }
            setBackgroundColor(Color.BLACK);
        }

        void setMode(String value) {
            mode = value;
            invalidate();
        }

        void setLoop(int loop) {
            loopNumber = loop;
            invalidate();
        }

        void setLine(String text) {
            String next = text == null ? "" : text.trim();
            if (next.equals(currentLine)) return;
            if (next.isEmpty()) {
                lineEndedAt = SystemClock.uptimeMillis();
                handler.removeCallbacks(relayout);
                handler.postDelayed(relayout, HOLD_LAYOUT_MS + 50);
            }
            currentLine = next;
            invalidate();
        }

        @Override
        protected void onDetachedFromWindow() {
            handler.removeCallbacks(relayout);
            super.onDetachedFromWindow();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            final float w = getWidth(), h = getHeight();
            final float s = Math.min(w / 620f, h / 540f);
            if (mode.equals("dim")) {
                float height = 106 * s;
                drawClock(canvas, (w - clockWidth(height)) / 2f, (h - height) / 2f, height, true);
                return;
            }
            final boolean textLayout = mode.equals("subtitles")
                    && (!currentLine.isEmpty() || (lineEndedAt > 0 && SystemClock.uptimeMillis() - lineEndedAt < HOLD_LAYOUT_MS));
            if (textLayout) {
                drawWithSubtitle(canvas, w, h, s);
                return;
            }
            // the clock large and centred, the loop under it
            final String loopText = String.format("LOOP %02d", Math.max(0, Math.min(99, loopNumber)));
            float height = 160 * s, labelSize = 20 * s, gap = 36 * s;
            float y0 = (h - (height + gap + labelSize)) / 2f;
            drawClock(canvas, (w - clockWidth(height)) / 2f, y0, height, false);
            label.setTextSize(labelSize);
            label.setLetterSpacing(0.5f);
            label.setShadowLayer(8 * s, 0, 0, GLOW);
            canvas.drawText(loopText, (w - label.measureText(loopText)) / 2f, y0 + height + gap + labelSize * 0.8f, label);
        }

        // the clock small at the top, the loop at its right, the line now under a rule (only the line now: an older one under it
        // breaks the reading)
        private void drawWithSubtitle(Canvas canvas, float w, float h, float s) {
            final String loopText = String.format("LOOP %02d", Math.max(0, Math.min(99, loopNumber)));
            float padX = 48 * s, padTop = 44 * s, height = 80 * s;
            drawClock(canvas, padX, padTop, height, false);
            label.setTextSize(16 * s);
            label.setLetterSpacing(0.4f);
            label.setShadowLayer(6 * s, 0, 0, GLOW);
            canvas.drawText(loopText, w - padX - label.measureText(loopText), padTop + height / 2f + 6 * s, label);
            float ruleY = padTop + height + 28 * s;
            canvas.drawRect(padX, ruleY, w - padX, ruleY + Math.max(1f, s), rule);
            if (currentLine.isEmpty()) return;
            subtitle.setTextSize(26 * s);
            int width = (int) Math.max(1, w - 2 * padX);
            StaticLayout layout = StaticLayout.Builder.obtain(currentLine, 0, currentLine.length(), subtitle, width)
                    .setAlignment(Layout.Alignment.ALIGN_NORMAL)
                    .setLineSpacing(0f, 1.3f)
                    .setIncludePad(false)
                    .build();
            float top = ruleY + 32 * s, bottom = h - 36 * s;
            int lines = layout.getLineCount();
            while (lines > 1 && top + layout.getLineBottom(lines - 1) > bottom) lines--;
            canvas.save();
            canvas.translate(padX, top);
            canvas.clipRect(0, 0, width, layout.getLineBottom(lines - 1));
            layout.draw(canvas);
            canvas.restore();
        }

        private static float clockWidth(float height) {
            float digit = height * 0.6f, gap = height * 14f / 160f, colon = height * 22f / 160f;
            return digit * 4 + colon + gap * 4;
        }

        private void drawClock(Canvas canvas, float x, float y, float height, boolean dim) {
            String time = "2359";
            float digit = height * 0.6f, gap = height * 14f / 160f, colon = height * 22f / 160f;
            float unit = height / 100f;
            // glow in screen pixels: a wide soft blue halo under a tight one on near-white segments (the matrix places the
            // segments, never a scaled canvas, so the blur radii stay in pixels)
            if (dim) {
                lit.setColor(DIM);
                lit.setAlpha(56);
                lit.clearShadowLayer();
            } else {
                lit.setColor(SEGMENT);
                lit.setShadowLayer(6 * unit, 0, 0, GLOW);
                halo.setColor(GLOW);
                halo.setAlpha(90);
                halo.setShadowLayer(22 * unit, 0, 0, GLOW);
            }
            for (int pass = dim ? 1 : 0; pass < 2; pass++) {
                Paint paint = pass == 0 ? halo : lit;
                float cx = x;
                for (int i = 0; i < 4; i++) {
                    if (i == 2) {
                        float r = 5 * unit;
                        canvas.drawCircle(cx + colon * 0.5f, y + 32 * unit, r, paint);
                        canvas.drawCircle(cx + colon * 0.36f, y + 70 * unit, r, paint);
                        cx += colon + gap;
                    }
                    String on = DIGITS[time.charAt(i) - '0'];
                    matrix.setScale(unit, unit);
                    matrix.postTranslate(cx, y);
                    for (int seg = 0; seg < 7; seg++) {
                        boolean isOn = on.indexOf((char) ('a' + seg)) >= 0;
                        if (isOn) {
                            segments[seg].transform(matrix, placed);
                            canvas.drawPath(placed, paint);
                        } else if (pass == 1 && !dim) {
                            segments[seg].transform(matrix, placed);
                            canvas.drawPath(placed, ghost);
                        }
                    }
                    cx += digit + gap;
                }
            }
        }
    }
}
