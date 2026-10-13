package com.codepdbh.armymenrts;

import android.app.Instrumentation;
import android.content.Intent;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.KeyEvent;
import android.view.MotionEvent;

/** Exercises real MotionEvent transitions against the running ARM64/Vulkan game. */
public final class TouchInstrumentation extends Instrumentation {
    private GameProbeActivity activity;
    private long downTime;
    @Override public void onCreate(Bundle arguments){super.onCreate(arguments);start();}
    private void event(int action,float... coordinates){
        int count=coordinates.length/2;
        MotionEvent.PointerProperties[] properties=new MotionEvent.PointerProperties[count];
        MotionEvent.PointerCoords[] points=new MotionEvent.PointerCoords[count];
        for(int i=0;i<count;i++){
            properties[i]=new MotionEvent.PointerProperties();properties[i].id=i;properties[i].toolType=MotionEvent.TOOL_TYPE_FINGER;
            points[i]=new MotionEvent.PointerCoords();points[i].x=coordinates[i*2]*activity.touchSurface().getWidth();points[i].y=coordinates[i*2+1]*activity.touchSurface().getHeight();points[i].pressure=1;points[i].size=.1f;
        }
        long now=SystemClock.uptimeMillis();if(action==MotionEvent.ACTION_DOWN)downTime=now;
        MotionEvent event=MotionEvent.obtain(downTime,now,action,count,properties,points,0,0,1,1,0,0,0x1002,0);
        runOnMainSync(()->activity.touchSurface().dispatchTouchEvent(event));event.recycle();
    }
    private void tap(float x,float y){event(MotionEvent.ACTION_DOWN,x,y);SystemClock.sleep(100);event(MotionEvent.ACTION_UP,x,y);}
    private int frames(){final int[] value={0};runOnMainSync(()->value[0]=activity.presentedFrames());return value[0];}
    private void waitFrames(int count,long timeout){
        int target=frames()+count;long deadline=SystemClock.uptimeMillis()+timeout;
        while(frames()<target){
            final boolean[] stopped={false};runOnMainSync(()->stopped[0]=activity.runFinished());
            if(stopped[0])throw new AssertionError("Guest stopped during gesture test");
            if(SystemClock.uptimeMillis()>deadline)throw new AssertionError("Guest stopped presenting frames");SystemClock.sleep(30);
        }
    }
    @Override public void onStart(){
        Bundle result=new Bundle();
        try{
            getTargetContext().getSharedPreferences("rts_touch",0).edit().putBoolean("gestures_seen",true).commit();
            activity=(GameProbeActivity)startActivitySync(new Intent(getTargetContext(),GameProbeActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            waitFrames(15,15000);tap(.075f,.91f);waitFrames(10,10000);tap(.78f,.8f);waitFrames(20,20000);
            sendKeyDownUpSync(KeyEvent.KEYCODE_ENTER);waitFrames(20,15000);
            // One finger selection drag.
            event(MotionEvent.ACTION_DOWN,.38f,.32f);event(MotionEvent.ACTION_MOVE,.5f,.45f);event(MotionEvent.ACTION_UP,.6f,.6f);waitFrames(5,5000);
            // Pinch: no mouse button is pressed, only DirectInput wheel input.
            event(MotionEvent.ACTION_DOWN,.4f,.4f);event(MotionEvent.ACTION_POINTER_DOWN|(1<<8),.4f,.4f,.6f,.4f);
            event(MotionEvent.ACTION_MOVE,.33f,.4f,.67f,.4f);SystemClock.sleep(100);
            event(MotionEvent.ACTION_MOVE,.28f,.4f,.72f,.4f);event(MotionEvent.ACTION_POINTER_UP|(1<<8),.28f,.4f,.72f,.4f);event(MotionEvent.ACTION_UP,.28f,.4f);waitFrames(10,10000);
            // Two finger camera pan with a stable span.
            event(MotionEvent.ACTION_DOWN,.35f,.4f);event(MotionEvent.ACTION_POINTER_DOWN|(1<<8),.35f,.4f,.55f,.4f);
            event(MotionEvent.ACTION_MOVE,.45f,.45f,.65f,.45f);SystemClock.sleep(100);
            event(MotionEvent.ACTION_MOVE,.5f,.5f,.7f,.5f);event(MotionEvent.ACTION_POINTER_UP|(1<<8),.5f,.5f,.7f,.5f);event(MotionEvent.ACTION_UP,.5f,.5f);waitFrames(10,10000);
            // Lifecycle cancellation path while a selection button is held.
            event(MotionEvent.ACTION_DOWN,.3f,.3f);event(MotionEvent.ACTION_MOVE,.45f,.45f);event(MotionEvent.ACTION_CANCEL,.45f,.45f);waitFrames(5,5000);
            result.putString("stream","RTS device gestures passed: menu, mission, selection, pinch, camera, cancellation; frames="+frames());finish(0,result);
        }catch(Throwable error){result.putString("stream",error.toString());finish(1,result);}
    }
}
