package com.codepdbh.armymenrts;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import java.io.File;

/** Continuous ARM64 game session with RTS-specific touch gestures. */
public final class GameProbeActivity extends Activity implements SurfaceHolder.Callback {
    static {System.loadLibrary("armymen_rts_android");}
    private static native long nativeCreateSession();
    private static native void nativeCloseSession(long session);
    private static native void nativePointer(long session,float x,float y,int button,boolean down);
    private static native void nativeKey(long session,int scan,boolean down);
    private static native void nativeWheel(long session,int steps);
    private static native int nativeFrames(long session);
    private static native String nativeVisualProbe(String folder,Surface surface,long session);
    private final Handler handler=new Handler(Looper.getMainLooper());
    private TextView status;
    private SurfaceView game;
    private Button menu;
    private PopupWindow popup;
    private RtsTouchController touch;
    private SharedPreferences preferences;
    private volatile long session;
    private Thread worker;
    private boolean resumed,finished;
    private float pointerX=.5f,pointerY=.5f;
    private Runnable mouseRelease;
    private long pulseSession;
    private int pulseButton;
    private float pulseX,pulseY;
    private int dp(float value){return Math.round(value*getResources().getDisplayMetrics().density);}
    SurfaceView touchSurface(){return game;}
    int presentedFrames(){return nativeFrames(session);}
    boolean runFinished(){return finished;}

    @Override public void onCreate(Bundle state){
        super.onCreate(state);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        preferences=getSharedPreferences("rts_touch",MODE_PRIVATE);
        FrameLayout frame=new FrameLayout(this);game=new SurfaceView(this);
        game.getHolder().addCallback(this);frame.addView(game);
        game.setOnTouchListener((view,event)->handleTouch(event));
        status=new TextView(this);status.setTextColor(Color.WHITE);status.setBackgroundColor(0xb0162b22);
        status.setTextSize(15);status.setPadding(dp(12),dp(8),dp(12),dp(8));
        FrameLayout.LayoutParams textPlacement=new FrameLayout.LayoutParams(-2,-2,Gravity.TOP|Gravity.CENTER_HORIZONTAL);
        textPlacement.topMargin=dp(12);frame.addView(status,textPlacement);
        menu=button("≡",()->showMenu());menu.setTextSize(25);menu.setContentDescription("Controles y menu del juego");
        FrameLayout.LayoutParams placement=new FrameLayout.LayoutParams(dp(48),dp(48),Gravity.TOP|Gravity.LEFT);
        placement.leftMargin=dp(12);placement.topMargin=dp(12);frame.addView(menu,placement);
        setContentView(frame);immersive();
        if(!preferences.getBoolean("gestures_seen",false)){
            preferences.edit().putBoolean("gestures_seen",true).apply();
            handler.postDelayed(()->{if(resumed&&!isFinishing())showGestures();},600);
        }
    }
    private void immersive(){
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY|View.SYSTEM_UI_FLAG_FULLSCREEN|View.SYSTEM_UI_FLAG_HIDE_NAVIGATION|View.SYSTEM_UI_FLAG_LAYOUT_STABLE|View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN|View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);if(focus)immersive();}
    private GradientDrawable background(){
        GradientDrawable drawable=new GradientDrawable();drawable.setColor(0xe6162b22);drawable.setCornerRadius(dp(10));drawable.setStroke(dp(1),0xffb99b59);return drawable;
    }
    private Button button(String label,Runnable action){
        Button button=new Button(this);button.setText(label);button.setAllCaps(false);button.setTextColor(0xfff4ecd7);
        button.setTypeface(Typeface.DEFAULT,Typeface.BOLD);button.setTextSize(14);button.setMinHeight(dp(46));
        button.setPadding(dp(12),0,dp(12),0);button.setBackground(background());button.setOnClickListener(view->action.run());return button;
    }
    private void rebuildTouch(){
        if(touch!=null)touch.cancel();
        touch=new RtsTouchController(new RtsTouchController.Sink(){
            public void move(float x,float y){pointerX=x;pointerY=y;nativePointer(session,x,y,3,false);}
            public void button(float x,float y,int button,boolean down){releaseMousePulse();pointerX=x;pointerY=y;nativePointer(session,x,y,button,down);}
            public void click(float x,float y,int button){clickMouse(x,y,button);}
            public void wheel(int steps){nativeWheel(session,steps);}
            public void feedback(){game.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);}
        },game.getWidth(),game.getHeight(),getResources().getDisplayMetrics().density,preferences.getFloat("sensitivity",1));
    }
    private boolean handleTouch(MotionEvent event){
        if(touch==null)rebuildTouch();
        int action=event.getActionMasked(),count=event.getPointerCount();
        if(count>2){touch.cancel();return true;}
        switch(action){
            case MotionEvent.ACTION_DOWN:touch.down(event.getX(),event.getY(),event.getEventTime());break;
            case MotionEvent.ACTION_POINTER_DOWN:touch.second(event.getX(0),event.getY(0),event.getX(1),event.getY(1));break;
            case MotionEvent.ACTION_MOVE:
                if(count==2)touch.multiMove(event.getX(0),event.getY(0),event.getX(1),event.getY(1));else touch.move(event.getX(),event.getY());break;
            case MotionEvent.ACTION_POINTER_UP:touch.pointerUp();break;
            case MotionEvent.ACTION_UP:touch.up(event.getX(),event.getY(),event.getEventTime());break;
            case MotionEvent.ACTION_CANCEL:touch.cancel();releaseMousePulse();break;
        }
        return true;
    }
    private void releaseMousePulse(){
        if(mouseRelease!=null){handler.removeCallbacks(mouseRelease);nativePointer(pulseSession,pulseX,pulseY,pulseButton,false);mouseRelease=null;}
    }
    private void clickMouse(float x,float y,int button){
        releaseMousePulse();pointerX=x;pointerY=y;pulseX=x;pulseY=y;pulseButton=button;pulseSession=session;
        final long current=session;nativePointer(current,x,y,button,true);
        mouseRelease=()->{nativePointer(current,x,y,button,false);mouseRelease=null;};handler.postDelayed(mouseRelease,90);
    }
    private void key(int scan){
        final long current=session;nativeKey(current,scan,true);handler.postDelayed(()->nativeKey(current,scan,false),90);
    }
    private void showMenu(){
        if(touch!=null)touch.cancel();releaseMousePulse();
        LinearLayout actions=new LinearLayout(this);actions.setOrientation(LinearLayout.VERTICAL);actions.setPadding(dp(8),dp(8),dp(8),dp(8));
        String[] labels={"Menu y objetivos","Cancelar seleccion","Detener unidades","Cuartel general","Ultimo combate","Pelotones","Intro","Gestos y sensibilidad"};
        Runnable[] callbacks={()->key(1),()->clickMouse(pointerX,pointerY,1),()->key(31),()->key(35),()->key(57),()->showSquads(),()->key(28),()->showGestures()};
        for(int i=0;i<labels.length;i++){final int index=i;Button action=button(labels[i],()->{popup.dismiss();callbacks[index].run();});
            LinearLayout.LayoutParams row=new LinearLayout.LayoutParams(-1,dp(46));row.bottomMargin=dp(4);actions.addView(action,row);}
        ScrollView scroll=new ScrollView(this);scroll.addView(actions);popup=new PopupWindow(scroll,dp(240),Math.min(dp(420),game.getHeight()-dp(76)),true);
        popup.setBackgroundDrawable(background());popup.setElevation(dp(12));popup.setOutsideTouchable(true);popup.showAsDropDown(menu,0,dp(6));
    }
    private void showSquads(){
        String[] groups=new String[9];for(int i=0;i<9;i++)groups[i]="Peloton "+(i+1);
        new AlertDialog.Builder(this).setTitle("Elegir peloton").setItems(groups,(dialog,index)->{
            new AlertDialog.Builder(this).setTitle(groups[index]).setItems(new String[]{"Seleccionar","Asignar unidades seleccionadas"},(choice,operation)->{
                final long current=session;if(operation==1)nativeKey(current,29,true);nativeKey(current,index+2,true);
                handler.postDelayed(()->{nativeKey(current,index+2,false);if(operation==1)nativeKey(current,29,false);},90);
            }).show();
        }).show();
    }
    private void showGestures(){
        LinearLayout content=new LinearLayout(this);content.setOrientation(LinearLayout.VERTICAL);content.setPadding(dp(20),dp(8),dp(20),0);
        TextView guide=new TextView(this);guide.setText("Un toque: seleccionar, mover, atacar o usar el HUD.\nArrastrar un dedo: seleccionar un grupo.\nArrastrar dos dedos: mover la camara.\nPellizcar: acercar o alejar.\nToque con dos dedos o mantener un dedo: cancelar.\nEl minimapa y la construccion se usan directamente.\n\nSensibilidad de la camara");guide.setTextSize(16);content.addView(guide);
        SeekBar sensitivity=new SeekBar(this);sensitivity.setMax(175);sensitivity.setProgress(Math.round(preferences.getFloat("sensitivity",1)*100)-25);content.addView(sensitivity);
        new AlertDialog.Builder(this).setTitle("Controles RTS").setView(content).setPositiveButton("Listo",(dialog,which)->{preferences.edit().putFloat("sensitivity",(sensitivity.getProgress()+25)/100f).apply();rebuildTouch();}).show();
    }
    private void startRun(){
        if(!resumed||session!=0||!game.getHolder().getSurface().isValid())return;
        long current=nativeCreateSession();session=current;finished=false;Surface surface=game.getHolder().getSurface();Thread previous=worker;
        status.setVisibility(View.VISIBLE);status.setText("Cargando Army Men RTS...");rebuildTouch();
        worker=new Thread(()->{
            if(previous!=null){try{previous.join();}catch(InterruptedException exception){Thread.currentThread().interrupt();return;}}
            if(session!=current)return;String folder=new File(Environment.getExternalStorageDirectory(),"armymenrts").getAbsolutePath();
            String result=nativeVisualProbe(folder,surface,current);
            runOnUiThread(()->{if(!isDestroyed()&&session==current){finished=true;status.setVisibility(View.VISIBLE);status.setText(result);}});
        },"amrts-game");worker.start();
        handler.postDelayed(new Runnable(){public void run(){if(session!=current||finished)return;if(nativeFrames(current)>2)status.setVisibility(View.GONE);else handler.postDelayed(this,200);}},200);
    }
    private void stopRun(){if(touch!=null)touch.cancel();releaseMousePulse();long current=session;session=0;if(current!=0)nativeCloseSession(current);if(popup!=null)popup.dismiss();}
    @Override protected void onResume(){super.onResume();resumed=true;startRun();}
    @Override protected void onPause(){resumed=false;stopRun();super.onPause();}
    @Override public void surfaceCreated(SurfaceHolder holder){startRun();}
    @Override public void surfaceChanged(SurfaceHolder holder,int format,int width,int height){rebuildTouch();}
    @Override public void surfaceDestroyed(SurfaceHolder holder){stopRun();}
    @Override public boolean onKeyDown(int key,KeyEvent event){
        if(key==KeyEvent.KEYCODE_ENTER||key==KeyEvent.KEYCODE_ESCAPE){nativeKey(session,key==KeyEvent.KEYCODE_ENTER?28:1,true);return true;}return super.onKeyDown(key,event);
    }
    @Override public boolean onKeyUp(int key,KeyEvent event){
        if(key==KeyEvent.KEYCODE_ENTER||key==KeyEvent.KEYCODE_ESCAPE){nativeKey(session,key==KeyEvent.KEYCODE_ENTER?28:1,false);return true;}return super.onKeyUp(key,event);
    }
    @Override public void onBackPressed(){key(1);}
}
