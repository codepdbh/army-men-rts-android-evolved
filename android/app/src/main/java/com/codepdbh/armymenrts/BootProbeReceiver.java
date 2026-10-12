package com.codepdbh.armymenrts;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Environment;
import android.util.Log;
import java.io.File;

/** ADB-only diagnostic entry point; does not open an activity. */
public final class BootProbeReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        final PendingResult pending = goAsync();
        final String requestId = intent.getStringExtra("request_id");
        new Thread(() -> {
            try {
                File folder = new File(Environment.getExternalStorageDirectory(), "armymenrts");
                String result = LauncherActivity.nativeBootProbe(folder.getAbsolutePath());
                Log.i("AMRTS", "Probe result [" + requestId + "]: " + result);
            } catch (Throwable error) {
                Log.e("AMRTS", "Probe result [" + requestId + "]: " + error);
            } finally { pending.finish(); }
        }, "amrts-headless-probe").start();
    }
}
