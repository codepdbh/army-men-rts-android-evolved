package com.codepdbh.armymenrts;

/**
 * Touch gestures for the RTS, independent of Android so every transition is
 * testable on the JVM.
 *
 * On the map:
 *   tap                      left click (select / contextual order)
 *   one finger drag          camera (direct, with inertia after a flick)
 *   hold, then drag          selection rectangle; with box mode on, any drag
 *   hold, release            still a tap: a slow tap must never cancel
 *   two finger tap           right click
 *   two fingers              camera pan and zoom together
 *   two finger twist         rotate a structure being placed (, and .)
 * While placing a structure:
 *   tap                      move the structure there (no click)
 *   one finger drag          nudge the structure relatively, like a trackpad,
 *                            so the finger never covers it
 * On the interface (HUD panels, minimap, build column):
 *   tap                      left click
 *   drag                     left drag (the minimap scrolls the view)
 *   hold, release            right click (remove from a production queue)
 */
public final class RtsTouchController {
    public interface Sink {
        /** Absolute cursor position, normalised to the game surface. */
        void move(float x,float y);
        void button(float x,float y,int button,boolean down);
        void click(float x,float y,int button);
        /**
         * Direct camera drag in normalised screen coordinates: BEGIN anchors the
         * terrain under (x, y), MOVE keeps it under the finger, END releases with
         * a velocity (screen units per second) for inertia, CANCEL stops at once.
         * `zoom` is the two-finger spread relative to BEGIN (1 for one finger).
         */
        void camera(int phase,float x,float y,float zoom,float vx,float vy);
        /** Twist step: +1 clockwise, -1 counter-clockwise. */
        void rotate(int direction);
        void feedback();
        /** Gesture finished; park the cursor here. */
        void rest(float x,float y);
    }
    /** Which part of the screen a normalised point belongs to. */
    public interface Zones {int zone(float x,float y);}
    public static final int WORLD=0,HUD=1,MINIMAP=2;
    public static final int BEGIN=0,MOVE=1,END=2,CANCEL=3;
    private enum Mode {IDLE,PENDING,HELD,SELECTING,TWO_PENDING,PANNING,ROTATING,AIMING,HUD_DRAG,SUPPRESSED}
    /** Normalised border kept free of the resting cursor. */
    public static final float EDGE=.035f;
    /** Where the cursor rests after using the interface: over the map, so a
     *  structure picked from a menu appears where it can be seen and moved. */
    public static final float REST_X=.5f,REST_Y=.45f;
    /** Arms the selection rectangle; long enough that unhurried taps stay taps. */
    public static final long HOLD_MS=500;
    /** Velocity is measured over the last part of the drag only. */
    static final long VELOCITY_WINDOW_MS=80,STILL_MS=60;
    static final double TWIST_STEP=Math.toRadians(30);
    private final Sink sink;
    private final Zones zones;
    private final float width,height,slop,pinchSlop;
    private Mode mode=Mode.IDLE;
    private float startX,startY,lastX,lastY,centerX,centerY,span,baseSpan,aimX=REST_X,aimY=REST_Y;
    private boolean twoFinger,placing,boxMode;
    private int startZone;
    private double angle;
    private long startTime,lastTime;
    private final float[] sampleX=new float[16],sampleY=new float[16];
    private final long[] sampleTime=new long[16];
    private int samples;

    public RtsTouchController(Sink sink,Zones zones,float width,float height,float density){
        this.sink=sink;this.zones=zones;this.width=Math.max(width,1);this.height=Math.max(height,1);
        // Android's own touch slop is 8 dp; RTS taps on small units need more
        // tolerance before a tap turns into a camera drag.
        slop=12*Math.max(density,1);pinchSlop=14*Math.max(density,1);
    }
    /** The game is placing a structure (engine construct cursor shown). */
    public void setPlacing(boolean value){
        if(value&&!placing){aimX=REST_X;aimY=REST_Y;}
        placing=value;
    }
    public boolean placing(){return placing;}
    public void setBoxMode(boolean value){boxMode=value;}
    public boolean boxMode(){return boxMode;}
    public float aimX(){return aimX;}
    public float aimY(){return aimY;}
    private float nx(float x){return Math.max(0,Math.min(x/width,1));}
    private float ny(float y){return Math.max(0,Math.min(y/height,1));}
    private static float inside(float value){return Math.max(EDGE,Math.min(value,1-EDGE));}
    private static float distance(float x,float y){return (float)Math.hypot(x,y);}
    public boolean waitingForHold(){return mode==Mode.PENDING;}
    public void down(float x,float y,long time){
        cancel();mode=Mode.PENDING;startX=lastX=x;startY=lastY=y;startTime=lastTime=time;
        startZone=zones.zone(nx(x),ny(y));
        // While placing, the cursor is the structure: it only moves deliberately.
        if(!(placing&&startZone==WORLD))sink.move(nx(x),ny(y));
        // A touch stops any camera inertia, as on every mobile map.
        sink.camera(CANCEL,0,0,1,0,0);
    }
    /** Long-press timer; also evaluated on release in case the timer ran late. */
    public void hold(long time){
        if(mode==Mode.PENDING&&time-startTime>=HOLD_MS&&!(placing&&startZone==WORLD)){mode=Mode.HELD;sink.feedback();}
    }
    private void sample(float x,float y,long time){
        int slot=samples++%sampleX.length;sampleX[slot]=x/width;sampleY[slot]=y/height;sampleTime[slot]=time;
    }
    /** Release velocity in screen units per second; zero if the finger had stopped. */
    private float[] velocity(long time){
        if(samples<3)return new float[2];
        int newest=(samples-1)%sampleX.length;
        if(time-sampleTime[newest]>STILL_MS)return new float[2];
        int oldest=newest;
        for(int i=1;i<Math.min(samples,sampleX.length);i++){
            int slot=Math.floorMod(newest-i,sampleX.length);
            if(sampleTime[newest]-sampleTime[slot]>VELOCITY_WINDOW_MS)break;oldest=slot;
        }
        long elapsed=sampleTime[newest]-sampleTime[oldest];if(elapsed<16)return new float[2];
        return new float[]{(sampleX[newest]-sampleX[oldest])*1000f/elapsed,(sampleY[newest]-sampleY[oldest])*1000f/elapsed};
    }
    private void beginPan(float x,float y,long time,boolean fingers,float spread){
        mode=Mode.PANNING;twoFinger=fingers;baseSpan=Math.max(spread,1);samples=0;sample(x,y,time);
        sink.camera(BEGIN,x/width,y/height,1,0,0);
    }
    private float zoom(float spread){return twoFinger?spread/baseSpan:1;}
    private void panTo(float x,float y,float spread,long time){sample(x,y,time);sink.camera(MOVE,x/width,y/height,zoom(spread),0,0);}
    private void endPan(float x,float y,long time,boolean inertia){
        float[] v=inertia?velocity(time):new float[2];
        sink.camera(END,x/width,y/height,zoom(span),v[0],v[1]);
    }
    private void aimTo(float x,float y){
        aimX=Math.max(.02f,Math.min(x,.98f));aimY=Math.max(.02f,Math.min(y,.98f));sink.move(aimX,aimY);
    }
    public void second(float x0,float y0,float x1,float y1){
        if(mode==Mode.SELECTING||mode==Mode.HUD_DRAG){sink.button(nx(lastX),ny(lastY),0,false);sink.rest(inside(nx(lastX)),inside(ny(lastY)));mode=Mode.SUPPRESSED;return;}
        float x=(x0+x1)/2,y=(y0+y1)/2;
        angle=Math.atan2(y1-y0,x1-x0);span=distance(x1-x0,y1-y0);centerX=x;centerY=y;
        if(mode==Mode.PANNING){beginPan(x,y,lastTime,true,span);return;}// Re-anchor on the midpoint.
        if(mode!=Mode.PENDING&&mode!=Mode.HELD&&mode!=Mode.AIMING){cancel();mode=Mode.SUPPRESSED;return;}
        mode=Mode.TWO_PENDING;
    }
    public void move(float x,float y){move(x,y,lastTime);}
    public void move(float x,float y,long time){
        lastTime=time;
        boolean moved=distance(x-startX,y-startY)>slop;
        if(mode==Mode.PENDING&&moved){
            if(startZone!=WORLD){mode=Mode.HUD_DRAG;sink.button(nx(startX),ny(startY),0,true);}
            else if(placing)mode=Mode.AIMING;
            else if(boxMode){mode=Mode.SELECTING;sink.button(nx(startX),ny(startY),0,true);}
            else beginPan(startX,startY,startTime,false,1);
        }else if(mode==Mode.HELD&&moved){
            mode=startZone==WORLD?Mode.SELECTING:Mode.HUD_DRAG;sink.button(nx(startX),ny(startY),0,true);
        }
        if(mode==Mode.SELECTING||mode==Mode.HUD_DRAG)sink.move(nx(x),ny(y));
        else if(mode==Mode.PANNING)panTo(x,y,1,time);
        else if(mode==Mode.AIMING)aimTo(aimX+(x-lastX)/width,aimY+(y-lastY)/height);
        lastX=x;lastY=y;
    }
    public void multiMove(float x0,float y0,float x1,float y1){multiMove(x0,y0,x1,y1,lastTime);}
    public void multiMove(float x0,float y0,float x1,float y1,long time){
        lastTime=time;
        float x=(x0+x1)/2,y=(y0+y1)/2,currentSpan=distance(x1-x0,y1-y0);
        double currentAngle=Math.atan2(y1-y0,x1-x0),turn=Math.IEEEremainder(currentAngle-angle,2*Math.PI);
        if(mode==Mode.TWO_PENDING){
            float translation=distance(x-centerX,y-centerY),scale=Math.abs(currentSpan-span);
            if(Math.abs(turn)>Math.toRadians(20)&&translation<slop*2&&scale<pinchSlop*2){mode=Mode.ROTATING;sink.feedback();}
            else if(translation>slop||scale>pinchSlop)beginPan(centerX,centerY,time,true,span);
        }
        if(mode==Mode.PANNING&&twoFinger){panTo(x,y,currentSpan,time);centerX=x;centerY=y;span=currentSpan;}
        else if(mode==Mode.ROTATING){
            if(Math.abs(turn)>=TWIST_STEP){int direction=turn>0?1:-1;sink.rotate(direction);angle+=direction*TWIST_STEP;}
        }
    }
    private void restAfter(float x,float y){
        if(placing)return;// The cursor is the structure; leave it.
        if(startZone==WORLD)sink.rest(inside(nx(x)),inside(ny(y)));
        else sink.rest(REST_X,REST_Y);
    }
    public void pointerUp(){
        // Lifting one of two fingers ends the gesture without inertia; the
        // remaining finger does nothing until it is lifted too.
        if(mode==Mode.PANNING)endPan(centerX,centerY,lastTime,false);
        else if(mode==Mode.TWO_PENDING){
            if(placing)sink.click(aimX,aimY,1);// Cancel the structure, not whatever is under the fingers.
            else{sink.click(nx(centerX),ny(centerY),1);sink.rest(inside(nx(centerX)),inside(ny(centerY)));}
            sink.feedback();
        }
        mode=Mode.SUPPRESSED;
    }
    public void up(float x,float y,long time){
        hold(time);
        switch(mode){
            case PENDING:
                if(placing&&startZone==WORLD)aimTo(nx(x),ny(y));
                else{sink.click(nx(x),ny(y),0);restAfter(x,y);}
                break;
            case HELD:
                // On the interface a long press is the right click (queues);
                // on the map it is just a slow tap.
                sink.click(nx(x),ny(y),startZone==WORLD?0:1);restAfter(x,y);break;
            case SELECTING:case HUD_DRAG:sink.button(nx(x),ny(y),0,false);restAfter(x,y);break;
            case PANNING:sample(x,y,time);endPan(x,y,time,true);break;
            default:break;
        }
        mode=Mode.IDLE;
    }
    public void cancel(){
        if(mode==Mode.SELECTING||mode==Mode.HUD_DRAG)sink.button(nx(lastX),ny(lastY),0,false);
        if(mode==Mode.PANNING)sink.camera(CANCEL,0,0,1,0,0);
        mode=Mode.IDLE;
    }
}
