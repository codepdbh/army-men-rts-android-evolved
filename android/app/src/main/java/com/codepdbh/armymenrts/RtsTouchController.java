package com.codepdbh.armymenrts;

/**
 * Touch gestures for the RTS map, independent of Android so every transition
 * is testable on the JVM.
 *
 *   tap                      left click (select / contextual order)
 *   one finger drag          camera: the touched terrain stays under the finger,
 *                            with inertia after a flick
 *   hold, then drag          selection rectangle (left drag)
 *   hold, release            still a tap: a slow tap must never cancel
 *   two finger tap           right click
 *   two fingers              camera follows the midpoint while the spread
 *                            sets the camera height (pan and zoom together)
 *   two finger twist         rotate a structure being placed (, and .)
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
        /** Gesture finished; leave the cursor here so it cannot trigger edge scrolling. */
        void rest(float x,float y);
    }
    public static final int BEGIN=0,MOVE=1,END=2,CANCEL=3;
    private enum Mode {IDLE,PENDING,HELD,SELECTING,TWO_PENDING,PANNING,ROTATING,SUPPRESSED}
    /** Normalised border kept free of the resting cursor (engine edge scroll band). */
    public static final float EDGE=.035f;
    /** Arms the selection rectangle; long enough that unhurried taps stay taps. */
    public static final long HOLD_MS=500;
    /** Velocity is measured over the last part of the drag only. */
    static final long VELOCITY_WINDOW_MS=80,STILL_MS=60;
    static final double TWIST_STEP=Math.toRadians(30);
    private final Sink sink;
    private final float width,height,slop,pinchSlop;
    private Mode mode=Mode.IDLE;
    private float startX,startY,lastX,lastY,centerX,centerY,span,baseSpan;
    private boolean twoFinger;
    private double angle;
    private long startTime,lastTime;
    private final float[] sampleX=new float[16],sampleY=new float[16];
    private final long[] sampleTime=new long[16];
    private int samples;

    public RtsTouchController(Sink sink,float width,float height,float density){
        this.sink=sink;this.width=Math.max(width,1);this.height=Math.max(height,1);
        slop=8*Math.max(density,1);pinchSlop=14*Math.max(density,1);
    }
    private float nx(float x){return Math.max(0,Math.min(x/width,1));}
    private float ny(float y){return Math.max(0,Math.min(y/height,1));}
    private static float inside(float value){return Math.max(EDGE,Math.min(value,1-EDGE));}
    private static float distance(float x,float y){return (float)Math.hypot(x,y);}
    public boolean waitingForHold(){return mode==Mode.PENDING;}
    public void down(float x,float y,long time){
        cancel();mode=Mode.PENDING;startX=lastX=x;startY=lastY=y;startTime=lastTime=time;
        sink.move(nx(x),ny(y));
        // A touch stops any camera inertia, as on every mobile map.
        sink.camera(CANCEL,0,0,1,0,0);
    }
    /** Long-press timer; also evaluated on release in case the timer ran late. */
    public void hold(long time){
        if(mode==Mode.PENDING&&time-startTime>=HOLD_MS){mode=Mode.HELD;sink.feedback();}
    }
    private void sample(float x,float y,long time){
        int slot=samples++%sampleX.length;sampleX[slot]=x/width;sampleY[slot]=y/height;sampleTime[slot]=time;
    }
    /** Release velocity in screen units per second; zero if the finger had stopped. */
    private float[] velocity(long time){
        if(samples<2)return new float[2];
        int newest=(samples-1)%sampleX.length;
        if(time-sampleTime[newest]>STILL_MS)return new float[2];
        int oldest=newest;
        for(int i=1;i<Math.min(samples,sampleX.length);i++){
            int slot=Math.floorMod(newest-i,sampleX.length);
            if(sampleTime[newest]-sampleTime[slot]>VELOCITY_WINDOW_MS)break;oldest=slot;
        }
        long elapsed=sampleTime[newest]-sampleTime[oldest];if(elapsed<8)return new float[2];
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
    public void second(float x0,float y0,float x1,float y1){
        if(mode==Mode.SELECTING){sink.button(nx(lastX),ny(lastY),0,false);sink.rest(inside(nx(lastX)),inside(ny(lastY)));mode=Mode.SUPPRESSED;return;}
        float x=(x0+x1)/2,y=(y0+y1)/2;
        angle=Math.atan2(y1-y0,x1-x0);span=distance(x1-x0,y1-y0);centerX=x;centerY=y;
        if(mode==Mode.PANNING){beginPan(x,y,lastTime,true,span);return;}// Re-anchor on the midpoint.
        if(mode!=Mode.PENDING&&mode!=Mode.HELD){cancel();mode=Mode.SUPPRESSED;return;}
        mode=Mode.TWO_PENDING;
    }
    public void move(float x,float y){move(x,y,lastTime);}
    public void move(float x,float y,long time){
        lastTime=time;
        if(mode==Mode.PENDING&&distance(x-startX,y-startY)>slop)beginPan(startX,startY,startTime,false,1);
        else if(mode==Mode.HELD&&distance(x-startX,y-startY)>slop){mode=Mode.SELECTING;sink.button(nx(startX),ny(startY),0,true);}
        if(mode==Mode.SELECTING)sink.move(nx(x),ny(y));
        else if(mode==Mode.PANNING)panTo(x,y,1,time);
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
    public void pointerUp(){
        // Lifting one of two fingers ends the gesture without inertia; the
        // remaining finger does nothing until it is lifted too.
        if(mode==Mode.PANNING)endPan(centerX,centerY,lastTime,false);
        else if(mode==Mode.TWO_PENDING){sink.click(nx(centerX),ny(centerY),1);sink.feedback();sink.rest(inside(nx(centerX)),inside(ny(centerY)));}
        else if(mode==Mode.ROTATING)sink.rest(inside(nx(centerX)),inside(ny(centerY)));
        mode=Mode.SUPPRESSED;
    }
    public void up(float x,float y,long time){
        hold(time);
        if(mode==Mode.PENDING){sink.click(nx(x),ny(y),0);sink.rest(inside(nx(x)),inside(ny(y)));}
        else if(mode==Mode.HELD){sink.click(nx(x),ny(y),0);sink.rest(inside(nx(x)),inside(ny(y)));}
        else if(mode==Mode.SELECTING){sink.button(nx(x),ny(y),0,false);sink.rest(inside(nx(x)),inside(ny(y)));}
        else if(mode==Mode.PANNING){sample(x,y,time);endPan(x,y,time,true);}
        mode=Mode.IDLE;
    }
    public void cancel(){
        if(mode==Mode.SELECTING)sink.button(nx(lastX),ny(lastY),0,false);
        if(mode==Mode.PANNING)sink.camera(CANCEL,0,0,1,0,0);
        mode=Mode.IDLE;
    }
}
