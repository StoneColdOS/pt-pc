package com.ptport.app;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;

/* On-screen controls for players without a controller, laid over the game. They reach the game as a virtual gamepad
   (src/engine/platform/android.cpp PumpTouchControls), so the game reads them like any other pad.

   Left half: a floating stick to walk (it appears where the thumb lands). Right half: a floating stick to look. Buttons on
   the right: Use (A), Zoom (right stick click, P.T.'s zoom), Back (B); at the top, Pause (Start) and Name (both triggers,
   the voice part without the microphone). The overlay shows on a touch and fades out when a real controller is used. */
public class TouchControls extends View {
    private static final int A = 1, B = 2, START = 4, ZOOM = 8, NAME = 16;

    private static native void nativeState(float lx, float ly, float rx, float ry, int buttons);
    // tells the game whether the overlay is up, so that it puts it away when a real controller is used (NoteControllerEvent)
    private static native void nativeVisible(boolean visible);

    private static final class Button {
        final int bit;
        final String label;
        final RectF area = new RectF();
        int pointer = -1;

        Button(int bit, String label) {
            this.bit = bit;
            this.label = label;
        }
    }

    private static final class Stick {
        int pointer = -1;
        float originX, originY, x, y;

        float valueX(float radius) {
            return pointer < 0 ? 0f : clamp((x - originX) / radius);
        }

        float valueY(float radius) {
            return pointer < 0 ? 0f : clamp((y - originY) / radius);
        }
    }

    private final Button[] buttons = {
        new Button(A, "Use"), new Button(ZOOM, "Zoom"), new Button(B, "Back"), new Button(START, "Pause"), new Button(NAME, "Name"),
    };
    private final Stick move = new Stick();
    private final Stick look = new Stick();
    private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
    private boolean shown;
    // the setting (display.touch_controls): off keeps the overlay away
    private boolean allowed = true;
    // put away after this long without a finger on the screen
    private static final long HIDE_AFTER_MS = 5000;
    private final Runnable hideIdle = () -> {
        if (!anyFingerDown()) hide();
    };
    private int lastButtons = -1;
    private float lastLx, lastLy, lastRx, lastRy;

    public TouchControls(Context context) {
        super(context);
        ring.setStyle(Paint.Style.STROKE);
        ring.setColor(Color.argb(110, 234, 241, 255));
        fill.setColor(Color.argb(60, 234, 241, 255));
        label.setColor(Color.argb(170, 234, 241, 255));
        label.setTextAlign(Paint.Align.CENTER);
        Typeface typewriter = null;
        try {
            typewriter = context.getResources().getFont(R.font.special_elite);
        } catch (Throwable ignored) {
        }
        label.setTypeface(typewriter != null ? typewriter : Typeface.MONOSPACE);
        // shown at the start only when no controller is connected (a handheld's own pad counts)
        setShown(!controllerConnected());
        if (shown) postDelayed(hideIdle, HIDE_AFTER_MS * 2);
    }

    private void setShown(boolean value) {
        shown = value;
        try {
            nativeVisible(value);
        } catch (UnsatisfiedLinkError ignored) {
            // the game library is not loaded yet
        }
        postInvalidate();
    }

    private float dp(float value) {
        return value * getResources().getDisplayMetrics().density;
    }

    private float stickRadius() {
        return dp(56);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        float size = dp(64), gap = dp(14), margin = dp(28);
        // Use bottom right, Zoom left of it, Back above Use
        float right = w - margin, bottom = h - margin;
        buttons[0].area.set(right - size, bottom - size, right, bottom);
        buttons[1].area.set(right - 2 * size - gap, bottom - size * 0.75f, right - size - gap, bottom + size * 0.25f);
        buttons[2].area.set(right - size, bottom - 2 * size - gap, right, bottom - size - gap);
        float small = dp(48), top = dp(20);
        buttons[3].area.set(w - margin - small * 1.6f, top, w - margin, top + small * 0.8f);
        buttons[4].area.set(w - margin - small * 3.4f, top, w - margin - small * 1.8f, top + small * 0.8f);
    }

    void setAllowed(boolean value) {
        allowed = value;
        setVisibility(value ? VISIBLE : GONE);
        if (!value) hide();
    }

    private boolean anyFingerDown() {
        if (move.pointer >= 0 || look.pointer >= 0) return true;
        for (Button b : buttons) {
            if (b.pointer >= 0) return true;
        }
        return false;
    }

    private void hide() {
        removeCallbacks(hideIdle);
        if (!shown) return;
        setShown(false);
        move.pointer = look.pointer = -1;
        for (Button b : buttons) b.pointer = -1;
        send();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!allowed) return false;
        removeCallbacks(hideIdle);
        if (!shown) setShown(true);
        final int action = event.getActionMasked();
        final int index = event.getActionIndex();
        switch (action) {
        case MotionEvent.ACTION_DOWN:
        case MotionEvent.ACTION_POINTER_DOWN:
            press(event.getPointerId(index), event.getX(index), event.getY(index));
            break;
        case MotionEvent.ACTION_MOVE:
            for (int i = 0; i < event.getPointerCount(); i++) {
                int id = event.getPointerId(i);
                if (move.pointer == id) {
                    move.x = event.getX(i);
                    move.y = event.getY(i);
                } else if (look.pointer == id) {
                    look.x = event.getX(i);
                    look.y = event.getY(i);
                }
            }
            break;
        case MotionEvent.ACTION_UP:
        case MotionEvent.ACTION_POINTER_UP:
            release(event.getPointerId(index));
            break;
        case MotionEvent.ACTION_CANCEL:
            move.pointer = look.pointer = -1;
            for (Button b : buttons) b.pointer = -1;
            break;
        default:
            break;
        }
        send();
        invalidate();
        if (!anyFingerDown()) postDelayed(hideIdle, HIDE_AFTER_MS);
        return true;
    }

    private void press(int id, float x, float y) {
        for (Button b : buttons) {
            RectF hit = new RectF(b.area);
            hit.inset(-dp(8), -dp(8));
            if (hit.contains(x, y)) {
                b.pointer = id;
                return;
            }
        }
        Stick stick = x < getWidth() / 2f ? move : look;
        if (stick.pointer >= 0) return;
        stick.pointer = id;
        stick.originX = stick.x = x;
        stick.originY = stick.y = y;
    }

    private void release(int id) {
        if (move.pointer == id) move.pointer = -1;
        if (look.pointer == id) look.pointer = -1;
        for (Button b : buttons) {
            if (b.pointer == id) b.pointer = -1;
        }
    }

    private void send() {
        float r = stickRadius();
        int held = 0;
        for (Button b : buttons) {
            if (b.pointer >= 0) held |= b.bit;
        }
        float lx = move.valueX(r), ly = move.valueY(r), rx = look.valueX(r), ry = look.valueY(r);
        if (held == lastButtons && lx == lastLx && ly == lastLy && rx == lastRx && ry == lastRy) return;
        lastButtons = held;
        lastLx = lx;
        lastLy = ly;
        lastRx = rx;
        lastRy = ry;
        try {
            nativeState(lx, ly, rx, ry, held);
        } catch (UnsatisfiedLinkError ignored) {
            // the game library is not loaded yet
        }
    }

    // a real controller in use: the overlay steps aside until the screen is touched again
    void controllerUsed() {
        hide();
    }

    static boolean isController(int source) {
        return (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    private static boolean controllerConnected() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device != null && !device.isVirtual() && isController(device.getSources())) return true;
        }
        return false;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (!shown) return;
        float r = stickRadius();
        ring.setStrokeWidth(dp(2));
        drawStick(canvas, move, r, getWidth() * 0.18f, getHeight() * 0.68f, "MOVE");
        drawStick(canvas, look, r, getWidth() * 0.62f, getHeight() * 0.68f, "LOOK");
        for (Button b : buttons) {
            boolean down = b.pointer >= 0;
            float cx = b.area.centerX(), cy = b.area.centerY();
            boolean round = b.bit == A || b.bit == B || b.bit == ZOOM;
            if (round) {
                float radius = b.area.width() / 2f;
                if (down) canvas.drawCircle(cx, cy, radius, fill);
                canvas.drawCircle(cx, cy, radius, ring);
            } else {
                if (down) canvas.drawRoundRect(b.area, dp(10), dp(10), fill);
                canvas.drawRoundRect(b.area, dp(10), dp(10), ring);
            }
            label.setTextSize(round ? dp(15) : dp(13));
            canvas.drawText(b.label, cx, cy - (label.descent() + label.ascent()) / 2f, label);
        }
    }

    // a resting hint where the stick usually is; the live stick where the thumb went down
    private void drawStick(Canvas canvas, Stick stick, float r, float hintX, float hintY, String name) {
        if (stick.pointer < 0) {
            ring.setAlpha(50);
            canvas.drawCircle(hintX, hintY, r, ring);
            label.setTextSize(dp(12));
            label.setAlpha(80);
            canvas.drawText(name, hintX, hintY - (label.descent() + label.ascent()) / 2f, label);
            label.setAlpha(170);
            ring.setAlpha(110);
            return;
        }
        canvas.drawCircle(stick.originX, stick.originY, r, ring);
        float dx = stick.x - stick.originX, dy = stick.y - stick.originY;
        float length = (float) Math.hypot(dx, dy);
        if (length > r) {
            dx = dx / length * r;
            dy = dy / length * r;
        }
        canvas.drawCircle(stick.originX + dx, stick.originY + dy, r * 0.42f, fill);
    }

    private static float clamp(float v) {
        return Math.max(-1f, Math.min(1f, v));
    }
}
