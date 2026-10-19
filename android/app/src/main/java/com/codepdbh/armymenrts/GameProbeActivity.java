package com.codepdbh.armymenrts;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.media.AudioManager;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
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
    private static native void nativeMotion(long session,float dx,float dy);
    private static native void nativeCameraDrag(long session,int phase,float x,float y,float zoom,float vx,float vy);
    private static native void nativeKey(long session,int scan,boolean down);
    private static native void nativeWheel(long session,int steps);
    private static native void nativeVolume(long session,int volume);
    private static native int nativeFrames(long session);
    private static native String nativeVisualProbe(String folder,Surface surface,long session);
    // DirectInput scan codes used by the overlay.
    // Bindings from the game's own if_game_keybind.cfg (client/p/ingame.x).
    private static final int ESCAPE=1,ENTER=28,CONTROL=29,SHIFT=42,ALT=56,SPACE=57,
        E_SELECT_ALL=18,G_SELECT_TYPE=34,S_STOP=31,H_HEADQUARTERS=35,U_UPGRADE=22,P_PAUSE=25,C_COMMS=46,D_KEY=32,
        PREV_UNIT=26,NEXT_UNIT=27,PREV_DIR=51,NEXT_DIR=52,F1=59,F8=66,F9=67,F10=68;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private TextView status;
    private SurfaceView game;
    private TouchMarks marks;
    private Button menu,additive;
    private LinearLayout tools;
    private PopupWindow popup;
    private RtsTouchController touch;
    private SharedPreferences preferences;
    private volatile long session;
    private Thread worker;
    private boolean resumed,finished,addMode;
    private final GameInput input=new GameInput(new GameInput.Target(){
        public void pointer(float x,float y,int button,boolean down){nativePointer(session,x,y,button,down);}
        public void key(int scan,boolean down){nativeKey(session,scan,down);}
        public void motion(float dx,float dy){nativeMotion(session,dx,dy);}
        public void wheel(int steps){nativeWheel(session,steps);}
        public int frames(){return nativeFrames(session);}
    });
    private final Runnable inputPump=new Runnable(){public void run(){
        if(session!=0)input.pump(SystemClock.uptimeMillis());
        handler.postDelayed(this,4);
    }};
    private final Runnable holdCheck=()->{if(touch!=null&&touch.waitingForHold())touch.hold(SystemClock.uptimeMillis());};
    private int dp(float value){return Math.round(value*getResources().getDisplayMetrics().density);}
    SurfaceView touchSurface(){return game;}
    int presentedFrames(){return nativeFrames(session);}
    boolean runFinished(){return finished;}

    /** Short-lived rings where taps landed, since the game cursor is tiny on a phone. */
    private static final class TouchMarks extends View {
        private final float[] x=new float[8],y=new float[8];private final long[] time=new long[8];private final int[] color=new int[8];
        private int next;private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
        TouchMarks(Context context){super(context);paint.setStyle(Paint.Style.STROKE);}
        void mark(float px,float py,int argb){x[next]=px;y[next]=py;color[next]=argb;time[next]=SystemClock.uptimeMillis();next=(next+1)%8;invalidate();}
        @Override protected void onDraw(Canvas canvas){
            long now=SystemClock.uptimeMillis();boolean live=false;float density=getResources().getDisplayMetrics().density;
            for(int i=0;i<8;i++){long age=now-time[i];if(time[i]==0||age>350)continue;live=true;float t=age/350f;
                paint.setColor(color[i]);paint.setAlpha(Math.round(220*(1-t)));paint.setStrokeWidth(3*density);
                canvas.drawCircle(x[i],y[i],(10+18*t)*density,paint);}
            if(live)postInvalidateOnAnimation();
        }
    }

    @Override public void onCreate(Bundle state){
        super.onCreate(state);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);setVolumeControlStream(AudioManager.STREAM_MUSIC);
        preferences=getSharedPreferences("rts_touch",MODE_PRIVATE);
        FrameLayout frame=new FrameLayout(this);game=new SurfaceView(this);
        game.getHolder().addCallback(this);frame.addView(game);
        game.setOnTouchListener((view,event)->handleTouch(event));
        marks=new TouchMarks(this);frame.addView(marks,new FrameLayout.LayoutParams(-1,-1));
        status=new TextView(this);status.setTextColor(Color.WHITE);status.setBackgroundColor(0xb0162b22);
        status.setTextSize(15);status.setPadding(dp(12),dp(8),dp(12),dp(8));
        FrameLayout.LayoutParams textPlacement=new FrameLayout.LayoutParams(-2,-2,Gravity.TOP|Gravity.CENTER_HORIZONTAL);
        textPlacement.topMargin=dp(12);frame.addView(status,textPlacement);
        menu=button("≡",()->showMenu());menu.setTextSize(25);menu.setContentDescription("Menu del juego y controles");
        FrameLayout.LayoutParams placement=new FrameLayout.LayoutParams(dp(48),dp(48),Gravity.TOP|Gravity.LEFT);
        placement.leftMargin=dp(12);placement.topMargin=dp(12);frame.addView(menu,placement);
        // Top row beside the menu: the original HUD occupies the bottom and the top-right corner.
        tools=new LinearLayout(this);tools.setOrientation(LinearLayout.HORIZONTAL);
        addTool("✕","Cancelar o deseleccionar (clic derecho)",()->{cancelGesture();input.click(input.cursorX(),input.cursorY(),1);});
        additive=addTool("+","Anadir a la seleccion (Mayus)",()->setAddMode(!addMode));
        addTool("Todo","Seleccionar todas las unidades (E)",()->input.tapKey(E_SELECT_ALL));
        addTool("Tipo","Seleccionar las del mismo tipo (G)",()->input.tapKey(G_SELECT_TYPE));
        addTool("■","Detener unidades (S)",()->input.tapKey(S_STOP));
        addTool("⌂","Ir al cuartel general (H)",()->input.tapKey(H_HEADQUARTERS));
        addTool("⚑","Ir al ultimo combate (Espacio)",()->input.tapKey(SPACE));
        for(int i=0;i<4;i++)addSquad(i);
        FrameLayout.LayoutParams toolPlacement=new FrameLayout.LayoutParams(-2,-2,Gravity.TOP|Gravity.LEFT);
        toolPlacement.leftMargin=dp(12+48+16);toolPlacement.topMargin=dp(12);frame.addView(tools,toolPlacement);
        tools.setVisibility(preferences.getBoolean("tools",true)?View.VISIBLE:View.GONE);
        setContentView(frame);immersive();
        if(!preferences.getBoolean("gestures_seen_v2",false)){
            preferences.edit().putBoolean("gestures_seen_v2",true).apply();
            handler.postDelayed(()->{if(resumed&&!isFinishing())showGestures();},600);
        }
    }
    private Button addTool(String label,String description,Runnable action){
        Button tool=button(label,action);tool.setTextSize(label.length()>1?13:19);tool.setPadding(0,0,0,0);tool.setAlpha(.8f);tool.setContentDescription(description);
        tool.setMinHeight(0);tool.setMinimumHeight(0);tool.setMinWidth(0);tool.setMinimumWidth(0);
        LinearLayout.LayoutParams size=new LinearLayout.LayoutParams(dp(label.length()>1?52:44),dp(44));size.rightMargin=dp(6);tools.addView(tool,size);return tool;
    }
    /**
     * Squad slot N (the game has four): tap selects (N), a second quick tap
     * centres the camera on it (Shift+N), holding stores the selection
     * (Ctrl+N), or adds to it (Alt+N) while + is active.
     */
    private void addSquad(int index){
        int key=2+index;Button squad=addTool(String.valueOf(index+1),"Peloton "+(index+1),()->{});
        if(index==0)((LinearLayout.LayoutParams)squad.getLayoutParams()).leftMargin=dp(10);
        final long[] lastTap={0};final boolean[] held={false};
        Runnable hold=()->{held[0]=true;squad.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
            if(addMode)input.chord(ALT,key);else input.chord(CONTROL,key);
            toast((addMode?"Anadido al peloton ":"Peloton ")+(index+1)+(addMode?"":" asignado"));};
        squad.setOnClickListener(null);
        squad.setOnTouchListener((view,event)->{
            switch(event.getActionMasked()){
                case MotionEvent.ACTION_DOWN:held[0]=false;view.setPressed(true);handler.postDelayed(hold,500);return true;
                case MotionEvent.ACTION_UP:view.setPressed(false);handler.removeCallbacks(hold);if(held[0])return true;
                    long now=SystemClock.uptimeMillis();
                    if(now-lastTap[0]<350){input.chord(SHIFT,key);lastTap[0]=0;}else{input.tapKey(key);lastTap[0]=now;}
                    return true;
                case MotionEvent.ACTION_CANCEL:view.setPressed(false);handler.removeCallbacks(hold);return true;
            }
            return true;
        });
    }
    private void toast(String text){android.widget.Toast.makeText(this,text,android.widget.Toast.LENGTH_SHORT).show();}
    private void setAddMode(boolean enabled){
        addMode=enabled;GradientDrawable drawable=background();if(enabled)drawable.setColor(0xe6b99b59);additive.setBackground(drawable);
        additive.setTextColor(enabled?0xff162b22:0xfff4ecd7);
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
            public void move(float x,float y){input.move(x,y);}
            public void button(float x,float y,int button,boolean down){input.button(x,y,button,down);}
            public void click(float x,float y,int button){
                if(button==0&&addMode)input.shiftClick(x,y);else input.click(x,y,button);
                marks.mark(x*game.getWidth(),y*game.getHeight(),button==0?0xfff4ecd7:0xffe05a47);
            }
            public void camera(int phase,float x,float y,float zoom,float vx,float vy){nativeCameraDrag(session,phase,x,y,zoom,vx,vy);}
            public void rotate(int direction){input.tapKey(direction>0?NEXT_DIR:PREV_DIR);}
            public void feedback(){game.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);}
            public void rest(float x,float y){input.move(x,y);}
        },game.getWidth(),game.getHeight(),getResources().getDisplayMetrics().density);
    }
    private void cancelGesture(){handler.removeCallbacks(holdCheck);if(touch!=null)touch.cancel();}
    private boolean handleTouch(MotionEvent event){
        if(touch==null)rebuildTouch();
        int action=event.getActionMasked(),count=event.getPointerCount();
        if(count>2){touch.cancel();return true;}
        switch(action){
            case MotionEvent.ACTION_DOWN:touch.down(event.getX(),event.getY(),event.getEventTime());
                handler.removeCallbacks(holdCheck);handler.postDelayed(holdCheck,RtsTouchController.HOLD_MS);break;
            case MotionEvent.ACTION_POINTER_DOWN:handler.removeCallbacks(holdCheck);touch.second(event.getX(0),event.getY(0),event.getX(1),event.getY(1));break;
            case MotionEvent.ACTION_MOVE:
                // Feed batched historical samples too: smoother tracking and a
                // better release velocity for inertia.
                for(int h=0;h<event.getHistorySize();h++){
                    if(count==2)touch.multiMove(event.getHistoricalX(0,h),event.getHistoricalY(0,h),event.getHistoricalX(1,h),event.getHistoricalY(1,h),event.getHistoricalEventTime(h));
                    else touch.move(event.getHistoricalX(h),event.getHistoricalY(h),event.getHistoricalEventTime(h));
                }
                if(count==2)touch.multiMove(event.getX(0),event.getY(0),event.getX(1),event.getY(1),event.getEventTime());else touch.move(event.getX(),event.getY(),event.getEventTime());break;
            case MotionEvent.ACTION_POINTER_UP:touch.pointerUp();break;
            case MotionEvent.ACTION_UP:handler.removeCallbacks(holdCheck);touch.up(event.getX(),event.getY(),event.getEventTime());break;
            case MotionEvent.ACTION_CANCEL:cancelGesture();break;
        }
        return true;
    }
    private void showMenu(){
        cancelGesture();
        LinearLayout actions=new LinearLayout(this);actions.setOrientation(LinearLayout.VERTICAL);actions.setPadding(dp(8),dp(8),dp(8),dp(8));
        String[] labels={"Menu del juego y objetivos (Esc)","Pausa (P)","Mejorar (U)","Unidad anterior ( [ )","Unidad siguiente ( ] )","Altura de camara","Guardado rapido (F9)","Carga rapida (F10)","Guardar / cargar (F8)","Comunicaciones (C)","Autodestruir seleccion","Aceptar (Intro)","Volumen",tools.getVisibility()==View.VISIBLE?"Ocultar botones de accion":"Mostrar botones de accion","Controles y sensibilidad"};
        Runnable[] callbacks={()->input.tapKey(ESCAPE),()->input.tapKey(P_PAUSE),()->input.tapKey(U_UPGRADE),()->input.tapKey(PREV_UNIT),()->input.tapKey(NEXT_UNIT),
            ()->showCameraHeights(),()->input.tapKey(F9),()->input.tapKey(F10),()->input.tapKey(F8),()->input.tapKey(C_COMMS),()->confirmSelfDestruct(),
            ()->input.tapKey(ENTER),()->showVolume(),()->{
            boolean show=tools.getVisibility()!=View.VISIBLE;tools.setVisibility(show?View.VISIBLE:View.GONE);preferences.edit().putBoolean("tools",show).apply();},()->showGestures()};
        for(int i=0;i<labels.length;i++){final int index=i;Button action=button(labels[i],()->{popup.dismiss();callbacks[index].run();});
            LinearLayout.LayoutParams row=new LinearLayout.LayoutParams(-1,dp(46));row.bottomMargin=dp(4);actions.addView(action,row);}
        ScrollView scroll=new ScrollView(this);scroll.addView(actions);popup=new PopupWindow(scroll,dp(260),Math.min(dp(380),game.getHeight()-dp(76)),true);
        popup.setBackgroundDrawable(background());popup.setElevation(dp(12));popup.setOutsideTouchable(true);popup.showAsDropDown(menu,0,dp(6));
    }
    /** Miles master volume 0..127 sent to the guest; -1 follows the game's saved profile. */
    private int masterVolume(){return preferences.getBoolean("follow_game_volume",false)?-1:Math.round(preferences.getInt("volume",100)*1.27f);}
    private void showVolume(){
        LinearLayout content=new LinearLayout(this);content.setOrientation(LinearLayout.VERTICAL);content.setPadding(dp(20),dp(8),dp(20),0);
        TextView label=new TextView(this);label.setTextSize(15);content.addView(label);
        SeekBar volume=new SeekBar(this);volume.setMax(100);volume.setProgress(preferences.getInt("volume",100));content.addView(volume);
        android.widget.CheckBox follow=new android.widget.CheckBox(this);follow.setText("Usar el volumen general guardado en la partida");follow.setChecked(preferences.getBoolean("follow_game_volume",false));content.addView(follow);
        TextView note=new TextView(this);note.setTextSize(13);note.setText("La musica, las voces y los efectos se ajustan tambien en las opciones del juego. Los botones de volumen del telefono controlan el sonido.");content.addView(note);
        Runnable update=()->{label.setText("Volumen general: "+volume.getProgress()+" %");volume.setEnabled(!follow.isChecked());
            preferences.edit().putInt("volume",volume.getProgress()).putBoolean("follow_game_volume",follow.isChecked()).apply();nativeVolume(session,masterVolume());};
        volume.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            public void onProgressChanged(SeekBar bar,int value,boolean user){update.run();}
            public void onStartTrackingTouch(SeekBar bar){}public void onStopTrackingTouch(SeekBar bar){}});
        follow.setOnCheckedChangeListener((box,checked)->update.run());update.run();
        new AlertDialog.Builder(this).setTitle("Sonido").setView(content).setPositiveButton("Listo",null).show();
    }
    private void showCameraHeights(){
        String[] heights={"Muy baja (F1)","Normal (F2)","Alta (F3)","Muy alta (F4)"};
        new AlertDialog.Builder(this).setTitle("Altura de camara").setItems(heights,(dialog,index)->input.tapKey(F1+index)).show();
    }
    private void confirmSelfDestruct(){
        new AlertDialog.Builder(this).setTitle("Autodestruir").setMessage("Las unidades seleccionadas se destruiran.")
            .setPositiveButton("Destruir",(dialog,which)->input.chord(SHIFT,D_KEY)).setNegativeButton("Cancelar",null).show();
    }
    private void showGestures(){
        LinearLayout content=new LinearLayout(this);content.setOrientation(LinearLayout.VERTICAL);content.setPadding(dp(20),dp(8),dp(20),0);
        TextView guide=new TextView(this);guide.setText(
            "Tocar: seleccionar, o dar la orden (mover, atacar, recoger) con algo seleccionado.\n"+
            "Doble toque sobre una unidad: todas las de ese tipo.\n"+
            "Arrastrar un dedo: mover la camara (el terreno sigue al dedo; deslizar y soltar le da inercia; tocar la detiene).\n"+
            "Mantener hasta que vibre y arrastrar: recuadro de seleccion.\n"+
            "Tocar con dos dedos o ✕: cancelar o deseleccionar.\n"+
            "Pellizcar: acercar o alejar.\n"+
            "Girar dos dedos: rotar el edificio al colocarlo.\n"+
            "+ : los toques se anaden a la seleccion (Mayus).\n"+
            "Todo: todas las unidades.  Tipo: las del mismo tipo.  ■ Detener  ⌂ Cuartel general  ⚑ Ultimo combate\n"+
            "1-4 pelotones: tocar selecciona, doble toque centra la camara, mantener asigna la seleccion (con + la anade).\n"+
            "El minimapa, la construccion y los pelotones del juego se usan tocandolos.");
        guide.setTextSize(15);content.addView(guide);
        ScrollView scroll=new ScrollView(this);scroll.addView(content);
        new AlertDialog.Builder(this).setTitle("Controles").setView(scroll).setPositiveButton("Listo",null).show();
    }
    private void startRun(){
        if(!resumed||session!=0||!game.getHolder().getSurface().isValid())return;
        long current=nativeCreateSession();session=current;finished=false;nativeVolume(current,masterVolume());Surface surface=game.getHolder().getSurface();Thread previous=worker;
        status.setVisibility(View.VISIBLE);status.setText("Cargando Army Men RTS...");rebuildTouch();
        handler.removeCallbacks(inputPump);handler.post(inputPump);
        worker=new Thread(()->{
            if(previous!=null){try{previous.join();}catch(InterruptedException exception){Thread.currentThread().interrupt();return;}}
            if(session!=current)return;String folder=new File(Environment.getExternalStorageDirectory(),"armymenrts").getAbsolutePath();
            String result=nativeVisualProbe(folder,surface,current);
            runOnUiThread(()->{if(!isDestroyed()&&session==current){finished=true;status.setVisibility(View.VISIBLE);status.setText(result);}});
        },"amrts-game");worker.start();
        handler.postDelayed(new Runnable(){public void run(){if(session!=current||finished)return;if(nativeFrames(current)>2)status.setVisibility(View.GONE);else handler.postDelayed(this,200);}},200);
    }
    private void stopRun(){
        cancelGesture();input.clear();handler.removeCallbacks(inputPump);
        long current=session;session=0;if(current!=0)nativeCloseSession(current);if(popup!=null)popup.dismiss();
    }
    @Override protected void onResume(){super.onResume();resumed=true;startRun();}
    @Override protected void onPause(){resumed=false;stopRun();super.onPause();}
    @Override public void surfaceCreated(SurfaceHolder holder){startRun();}
    @Override public void surfaceChanged(SurfaceHolder holder,int format,int width,int height){rebuildTouch();}
    @Override public void surfaceDestroyed(SurfaceHolder holder){stopRun();}
    @Override public boolean onKeyDown(int key,KeyEvent event){
        if(key==KeyEvent.KEYCODE_ENTER||key==KeyEvent.KEYCODE_ESCAPE){input.key(key==KeyEvent.KEYCODE_ENTER?ENTER:ESCAPE,true);return true;}return super.onKeyDown(key,event);
    }
    @Override public boolean onKeyUp(int key,KeyEvent event){
        if(key==KeyEvent.KEYCODE_ENTER||key==KeyEvent.KEYCODE_ESCAPE){input.key(key==KeyEvent.KEYCODE_ENTER?ENTER:ESCAPE,false);return true;}return super.onKeyUp(key,event);
    }
    @Override public void onBackPressed(){input.tapKey(ESCAPE);}
}
