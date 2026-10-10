package com.ptport.app;

import android.app.Activity;
import android.os.Bundle;
import android.view.WindowManager;

import java.lang.ref.WeakReference;

/* The bottom screen's clock as an activity on the second display, for devices whose system will not take a Presentation
   there (SecondScreen). It never takes the gamepad or touches from the game. */
public class SecondScreenActivity extends Activity {
    private static WeakReference<SecondScreenActivity> current = new WeakReference<>(null);
    private SecondScreen.ClockView view;

    static void finishAll() {
        SecondScreenActivity activity = current.get();
        if (activity != null) activity.finish();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        current = new WeakReference<>(this);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        view = new SecondScreen.ClockView(this);
        setContentView(view);
        SecondScreen.hideBars(getWindow());
        SecondScreen.attach(view);
    }

    @Override
    protected void onDestroy() {
        SecondScreen.detach(view);
        if (current.get() == this) current = new WeakReference<>(null);
        super.onDestroy();
    }
}
