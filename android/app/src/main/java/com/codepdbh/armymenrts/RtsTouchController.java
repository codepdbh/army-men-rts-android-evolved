package com.codepdbh.armymenrts;

/** Gesture arbitration independent of Android, so transitions can be tested on the JVM. */
public final class RtsTouchController {
    public interface Sink {
        void move(float x,float y);
        void button(float x,float y,int button,boolean down);
        void click(float x,float y,int button);
        void wheel(int steps);
        void feedback();
    }
    private enum Mode {IDLE,PENDING,SELECTING,TWO_PENDING,PANNING,PINCHING,SUPPRESSED}
    private final Sink sink;
    private final float width,height,slop,pinchSlop,sensitivity;
    private Mode mode=Mode.IDLE;
    private float startX,startY,lastX,lastY,centerX,centerY,span,zoomRemainder;
    private long startTime;
    private static final long HOLD_MS=450;

    public RtsTouchController(Sink sink,float width,float height,float density,float sensitivity){
        this.sink=sink;this.width=Math.max(width,1);this.height=Math.max(height,1);
        slop=10*Math.max(density,1);pinchSlop=14*Math.max(density,1);
        this.sensitivity=Math.max(.25f,Math.min(sensitivity,2));
    }
    private float nx(float x){return Math.max(0,Math.min(x/width,1));}
    private float ny(float y){return Math.max(0,Math.min(y/height,1));}
    private static float distance(float x,float y){return (float)Math.hypot(x,y);}
    public void down(float x,float y,long time){
        cancel();mode=Mode.PENDING;startX=lastX=x;startY=lastY=y;startTime=time;
        sink.move(nx(x),ny(y));
    }
    public void second(float x0,float y0,float x1,float y1){
        if(mode==Mode.SELECTING){sink.button(nx(lastX),ny(lastY),0,false);mode=Mode.SUPPRESSED;return;}
        if(mode!=Mode.PENDING){cancel();mode=Mode.SUPPRESSED;return;}
        mode=Mode.TWO_PENDING;centerX=(x0+x1)/2;centerY=(y0+y1)/2;
        lastX=centerX;lastY=centerY;span=distance(x1-x0,y1-y0);zoomRemainder=0;
    }
    public void move(float x,float y){
        if(mode==Mode.PENDING&&distance(x-startX,y-startY)>slop){
            mode=Mode.SELECTING;sink.button(nx(startX),ny(startY),0,true);
        }
        if(mode==Mode.SELECTING)sink.move(nx(x),ny(y));
        lastX=x;lastY=y;
    }
    public void multiMove(float x0,float y0,float x1,float y1){
        float x=(x0+x1)/2,y=(y0+y1)/2,currentSpan=distance(x1-x0,y1-y0);
        if(mode==Mode.TWO_PENDING){
            float translation=distance(x-centerX,y-centerY),scale=Math.abs(currentSpan-span);
            if(scale>pinchSlop&&scale>translation*1.2f){mode=Mode.PINCHING;sink.feedback();}
            else if(translation>slop){mode=Mode.PANNING;sink.button(nx(centerX),ny(centerY),1,true);}
        }
        if(mode==Mode.PANNING){
            // Accumulate in display coordinates; never issue a left command during a pan.
            lastX+=(x-centerX)*sensitivity;lastY+=(y-centerY)*sensitivity;
            sink.move(nx(lastX),ny(lastY));centerX=x;centerY=y;
        }else if(mode==Mode.PINCHING){
            zoomRemainder+=(currentSpan-span)/(pinchSlop*1.8f);int steps=(int)zoomRemainder;
            if(steps!=0){steps=Math.max(-3,Math.min(steps,3));sink.wheel(steps);zoomRemainder-=steps;}
            span=currentSpan;
        }
    }
    public void pointerUp(){
        if(mode==Mode.PANNING)sink.button(nx(lastX),ny(lastY),1,false);
        else if(mode==Mode.TWO_PENDING){sink.click(nx(centerX),ny(centerY),1);sink.feedback();}
        mode=Mode.SUPPRESSED;
    }
    public void up(float x,float y,long time){
        if(mode==Mode.PENDING){sink.click(nx(x),ny(y),time-startTime>=HOLD_MS?1:0);sink.feedback();}
        else if(mode==Mode.SELECTING)sink.button(nx(x),ny(y),0,false);
        else if(mode==Mode.PANNING)sink.button(nx(lastX),ny(lastY),1,false);
        mode=Mode.IDLE;
    }
    public void cancel(){
        if(mode==Mode.SELECTING)sink.button(nx(lastX),ny(lastY),0,false);
        if(mode==Mode.PANNING)sink.button(nx(lastX),ny(lastY),1,false);
        mode=Mode.IDLE;
    }
}
