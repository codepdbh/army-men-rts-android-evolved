package com.codepdbh.armymenrts;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;

/** Start screen: checks the player's game copy and launches the session. */
public final class LauncherActivity extends Activity {
    static { System.loadLibrary("armymen_rts_android"); }
    private static native String nativeProbe(String dataPath);
    static native String nativeBootProbe(String dataPath);
    private static final int GOLD=0xffd8b25a,INK=0xff14241c,CREAM=0xfff4ecd7,OK=0xff7bd88f,BAD=0xffe0705a;
    private TextView access,files,gpu,hint;
    private Button play,grant;
    private String report="";
    private File gameFolder(){return new File(Environment.getExternalStorageDirectory(),"armymenrts");}
    private int dp(float value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private boolean storageAllowed(){
        return Build.VERSION.SDK_INT>=30?Environment.isExternalStorageManager():
            checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE)==PackageManager.PERMISSION_GRANTED;
    }
    private GradientDrawable panel(int color,int stroke,float radius){
        GradientDrawable drawable=new GradientDrawable();drawable.setColor(color);drawable.setCornerRadius(dp(radius));
        if(stroke!=0)drawable.setStroke(dp(1.5f),stroke);return drawable;
    }
    private TextView text(String value,float size,int color,boolean bold){
        TextView view=new TextView(this);view.setText(value);view.setTextSize(size);view.setTextColor(color);
        if(bold)view.setTypeface(Typeface.DEFAULT_BOLD);view.setShadowLayer(dp(3),0,dp(1),0xcc000000);return view;
    }
    private Button action(String label,boolean primary,Runnable run){
        Button button=new Button(this);button.setText(label);button.setAllCaps(false);button.setTypeface(Typeface.DEFAULT_BOLD);
        button.setTextSize(primary?22:15);button.setTextColor(primary?INK:CREAM);
        button.setBackground(panel(primary?GOLD:0xcc14241c,primary?0xfffff0c0:GOLD,primary?14:10));
        button.setStateListAnimator(null);button.setOnClickListener(view->run.run());return button;
    }
    @Override protected void onCreate(Bundle state){
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
        FrameLayout root=new FrameLayout(this);root.setBackgroundColor(Color.BLACK);
        ImageView backdrop=new ImageView(this);backdrop.setImageResource(R.drawable.launcher_backdrop);backdrop.setScaleType(ImageView.ScaleType.CENTER_CROP);
        root.addView(backdrop,new FrameLayout.LayoutParams(-1,-1));
        LinearLayout content=new LinearLayout(this);content.setOrientation(LinearLayout.HORIZONTAL);content.setGravity(Gravity.CENTER_VERTICAL);
        content.setPadding(dp(32),dp(20),dp(32),dp(20));
        ImageView badge=new ImageView(this);badge.setImageResource(R.drawable.icono);badge.setAdjustViewBounds(true);
        content.addView(badge,new LinearLayout.LayoutParams(0,-1,0.9f));
        LinearLayout side=new LinearLayout(this);side.setOrientation(LinearLayout.VERTICAL);side.setGravity(Gravity.CENTER_VERTICAL);
        side.setPadding(dp(28),0,0,0);
        side.addView(text("ARMY MEN RTS",34,CREAM,true));
        String version="";try{version=getPackageManager().getPackageInfo(getPackageName(),0).versionName;}catch(Exception ignored){}
        side.addView(text("Android Evolved · v"+version,16,GOLD,true));
        LinearLayout checks=new LinearLayout(this);checks.setOrientation(LinearLayout.VERTICAL);checks.setPadding(dp(16),dp(12),dp(16),dp(12));
        checks.setBackground(panel(0xb014241c,0x66d8b25a,12));
        access=text("",15,CREAM,false);files=text("",15,CREAM,false);gpu=text("",15,CREAM,false);
        checks.addView(access);checks.addView(files);checks.addView(gpu);
        LinearLayout.LayoutParams checksPlacement=new LinearLayout.LayoutParams(-1,-2);checksPlacement.topMargin=dp(16);side.addView(checks,checksPlacement);
        play=action("▶  JUGAR",true,()->startActivity(new Intent(this,GameProbeActivity.class)));
        LinearLayout.LayoutParams playPlacement=new LinearLayout.LayoutParams(-1,dp(64));playPlacement.topMargin=dp(18);side.addView(play,playPlacement);
        LinearLayout row=new LinearLayout(this);row.setOrientation(LinearLayout.HORIZONTAL);
        grant=action("Dar acceso a archivos",false,this::requestAccess);
        Button details=action("Diagnóstico",false,this::showDiagnostics);
        LinearLayout.LayoutParams half=new LinearLayout.LayoutParams(0,dp(48),1);half.rightMargin=dp(8);row.addView(grant,half);
        row.addView(details,new LinearLayout.LayoutParams(0,dp(48),1));
        LinearLayout.LayoutParams rowPlacement=new LinearLayout.LayoutParams(-1,-2);rowPlacement.topMargin=dp(10);side.addView(row,rowPlacement);
        hint=text("",13,0xffd3dfd5,false);LinearLayout.LayoutParams hintPlacement=new LinearLayout.LayoutParams(-1,-2);hintPlacement.topMargin=dp(10);side.addView(hint,hintPlacement);
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.addView(side);
        content.addView(scroll,new LinearLayout.LayoutParams(0,-1,1.1f));
        root.addView(content,new FrameLayout.LayoutParams(-1,-1));
        setContentView(root);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY|View.SYSTEM_UI_FLAG_FULLSCREEN|View.SYSTEM_UI_FLAG_HIDE_NAVIGATION|View.SYSTEM_UI_FLAG_LAYOUT_STABLE|View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN|View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        if(getIntent().getBooleanExtra("visual_probe",false))startActivity(new Intent(this,GameProbeActivity.class));
    }
    private void requestAccess(){
        if(Build.VERSION.SDK_INT>=30)startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,Uri.parse("package:"+getPackageName())));
        else requestPermissions(new String[]{android.Manifest.permission.READ_EXTERNAL_STORAGE},1);
    }
    private void showDiagnostics(){
        TextView body=new TextView(this);body.setText(report.isEmpty()?"Sin acceso a los archivos todavía.":report);body.setTextSize(14);body.setPadding(dp(20),dp(12),dp(20),dp(12));
        body.setTextIsSelectable(true);ScrollView scroll=new ScrollView(this);scroll.addView(body);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("Diagnóstico").setView(scroll)
            .setNeutralButton("Probar arranque",null).setPositiveButton("Cerrar",null).create();
        dialog.setOnShowListener(shown->dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(view->{
            view.setEnabled(false);body.append("\n\nProbando el arranque (hasta 30 s)...");
            new Thread(()->{String result=nativeBootProbe(gameFolder().getAbsolutePath());
                runOnUiThread(()->{body.append("\nArranque: "+result);view.setEnabled(true);});},"amrts-boot-probe").start();
        }));
        dialog.show();
    }
    private void mark(TextView view,boolean ok,String label){view.setText((ok?"✓  ":"✗  ")+label);view.setTextColor(ok?OK:BAD);}
    @Override protected void onResume(){
        super.onResume();
        boolean allowed=storageAllowed();
        report=allowed?nativeProbe(gameFolder().getAbsolutePath()):"";
        boolean data=allowed&&report.contains("AMRTS.EXE: OK")&&report.contains("base.x: OK");
        boolean vulkan=report.contains("Vulkan 1.1")||report.contains("Vulkan 1.2")||report.contains("Vulkan 1.3")||report.contains("Vulkan 1.4");
        String device="";for(String line:report.split("\n"))if(line.startsWith("GPU: "))device=line.substring(5);
        mark(access,allowed,allowed?"Acceso a los archivos concedido":"Falta el acceso a los archivos");
        mark(files,data,data?"Copia del juego encontrada en Memoria interna/armymenrts":"Copia del juego no encontrada en Memoria interna/armymenrts");
        mark(gpu,vulkan||!allowed,vulkan?device:allowed?"GPU sin Vulkan 1.1":"GPU: se comprobará al dar acceso");
        if(!allowed)gpu.setTextColor(0xffd3dfd5);
        play.setEnabled(data);play.setAlpha(data?1f:.45f);
        grant.setVisibility(allowed?View.GONE:View.VISIBLE);
        hint.setText(data?"Consejo: toca ≡ dentro del juego para ver los controles táctiles."
            :"Copia los archivos de tu Army Men RTS para PC (AMRTS.EXE, base.x, carpetas missions, worlds...) en Memoria interna/armymenrts.");
    }
}
