package com.codepdbh.armymenrts;

import android.app.Activity;
import android.os.Bundle;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
import android.content.Intent;
import android.net.Uri;
import android.graphics.Color;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.ScrollView;
import java.io.File;

/** Early Android port diagnostics against the owner's game files. */
public final class LauncherActivity extends Activity {
    static { System.loadLibrary("armymen_rts_android"); }
    private static native String nativeProbe(String dataPath);
    static native String nativeBootProbe(String dataPath);
    private TextView report;
    private File gameFolder() {
        return new File(Environment.getExternalStorageDirectory(), "armymenrts");
    }
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout=new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER_VERTICAL);
        layout.setPadding(48,32,48,32);
        layout.setBackgroundColor(Color.rgb(17,36,29));
        TextView heading=new TextView(this);
        heading.setText("Army Men RTS · Android Evolved");
        heading.setTextSize(28);heading.setTextColor(Color.WHITE);
        layout.addView(heading);
        TextView folder=new TextView(this);
        folder.setText("Copia los archivos de tu juego a Memoria interna/armymenrts");
        folder.setTextSize(18);folder.setTextColor(0xffd3dfd5);
        layout.addView(folder);
        report=new TextView(this);
        report.setTextSize(17);report.setTextColor(Color.WHITE);
        report.setPadding(0,28,0,28);layout.addView(report);
        Button access=new Button(this);access.setText("Dar acceso a archivos");
        access.setOnClickListener(view->{
            if(Build.VERSION.SDK_INT>=30)
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:"+getPackageName())));
            else requestPermissions(new String[]{android.Manifest.permission.READ_EXTERNAL_STORAGE},1);
        });layout.addView(access);
        Button boot=new Button(this);boot.setText("Diagnostico de inicio");
        boot.setOnClickListener(view->{
            boot.setEnabled(false);
            new Thread(()->{
                String result=nativeBootProbe(gameFolder().getAbsolutePath());
                runOnUiThread(()->{
                    report.append("\nArranque: "+result);
                    boot.setEnabled(true);
                });
            },"amrts-boot-probe").start();
        });layout.addView(boot);
        Button visual=new Button(this);visual.setText("Iniciar (experimental)");
        visual.setOnClickListener(view->startActivity(new Intent(this,GameProbeActivity.class)));layout.addView(visual);
        ScrollView scroll=new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(layout);
        setContentView(scroll);
        if(getIntent().getBooleanExtra("visual_probe",false))startActivity(new Intent(this,GameProbeActivity.class));
    }
    @Override protected void onResume(){super.onResume();
        boolean allowed=Build.VERSION.SDK_INT>=30?Environment.isExternalStorageManager():
                checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE)==
                        android.content.pm.PackageManager.PERMISSION_GRANTED;
        report.setText(allowed?nativeProbe(gameFolder().getAbsolutePath()):
                "Hace falta acceso a Memoria interna/armymenrts para comprobar los datos.");
    }
}
