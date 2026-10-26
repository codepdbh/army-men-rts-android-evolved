package com.codepdbh.armymenrts;

import java.util.ArrayDeque;

/**
 * Frame-synchronised delivery of mouse/keyboard actions to the guest.
 *
 * The engine samples DirectInput once per frame. A press and its release must
 * therefore be separated by a presented frame, whatever the frame rate; a
 * fixed delay loses clicks when the game slows down. Barriers in the queue
 * wait for the next presented frame (bounded so a stalled game cannot wedge
 * input forever).
 */
public final class GameInput {
    public interface Target {
        void pointer(float x,float y,int button,boolean down);
        void key(int scan,boolean down);
        void motion(float dx,float dy);
        void wheel(int steps);
        int frames();
    }
    private static final int POINTER=0,KEY=1,MOTION=2,WHEEL=3,BARRIER=4;
    static final long BARRIER_TIMEOUT_MS=250;
    private static final class Op {int kind,button,scan;boolean down;float x,y;Op(int kind){this.kind=kind;}}
    private final Target target;
    private final ArrayDeque<Op> queue=new ArrayDeque<>();
    private int barrierFrame=-1;
    private long barrierStart;
    private float cursorX=.5f,cursorY=.5f;
    private final boolean[] pressed=new boolean[3];
    private final boolean[] keys=new boolean[256];

    public GameInput(Target target){this.target=target;}
    public float cursorX(){return cursorX;}
    public float cursorY(){return cursorY;}
    public boolean idle(){return queue.isEmpty();}

    private void push(Op op){queue.add(op);}
    private void barrier(){Op last=queue.peekLast();if(last==null||last.kind!=BARRIER)push(new Op(BARRIER));}
    public void move(float x,float y){
        // Consecutive moves collapse: only the newest position matters.
        Op last=queue.peekLast();
        if(last!=null&&last.kind==POINTER&&last.button==3){last.x=x;last.y=y;return;}
        Op op=new Op(POINTER);op.button=3;op.x=x;op.y=y;push(op);
    }
    public void button(float x,float y,int button,boolean down){
        if(!down)barrier();
        Op op=new Op(POINTER);op.button=button;op.down=down;op.x=x;op.y=y;push(op);
        if(down)barrier();
    }
    public void click(float x,float y,int button){button(x,y,button,true);button(x,y,button,false);}
    /** Click while Shift is held (adds to the current selection). */
    public void shiftClick(float x,float y){key(42,true);barrier();click(x,y,0);key(42,false);}
    public void motion(float dx,float dy){
        Op last=queue.peekLast();
        if(last!=null&&last.kind==MOTION){last.x+=dx;last.y+=dy;return;}
        Op op=new Op(MOTION);op.x=dx;op.y=dy;push(op);
    }
    /**
     * Park the cursor once the engine has consumed everything queued before.
     * The engine resolves a release at the cursor position of the frame that
     * reads it: moving in the same frame would, for example, drop a structure
     * picked from a menu at the parking spot.
     */
    public void restAt(float x,float y){barrier();move(x,y);}
    public void wheel(int steps){Op op=new Op(WHEEL);op.button=steps;push(op);}
    public void key(int scan,boolean down){Op op=new Op(KEY);op.scan=scan;op.down=down;push(op);}
    public void tapKey(int scan){key(scan,true);barrier();key(scan,false);}
    public void chord(int modifier,int scan){key(modifier,true);key(scan,true);barrier();key(scan,false);key(modifier,false);}
    /** Drop queued input and release everything the guest still sees pressed. */
    public void clear(){
        queue.clear();barrierFrame=-1;
        for(int i=0;i<3;i++)if(pressed[i]){pressed[i]=false;target.pointer(cursorX,cursorY,i,false);}
        for(int i=0;i<256;i++)if(keys[i]){keys[i]=false;target.key(i,false);}
    }
    /** Run from the UI thread every few milliseconds. */
    public void pump(long now){
        while(!queue.isEmpty()){
            Op op=queue.peekFirst();
            if(op.kind==BARRIER){
                int frame=target.frames();
                if(barrierFrame<0){barrierFrame=frame;barrierStart=now;}
                // Two presents guarantee one complete engine input poll in between.
                if(frame<barrierFrame+2&&now-barrierStart<BARRIER_TIMEOUT_MS)return;
                barrierFrame=-1;queue.pollFirst();continue;
            }
            queue.pollFirst();
            switch(op.kind){
                case POINTER:cursorX=op.x;cursorY=op.y;if(op.button<3)pressed[op.button]=op.down;target.pointer(op.x,op.y,op.button,op.down);break;
                case KEY:keys[op.scan&255]=op.down;target.key(op.scan,op.down);break;
                case MOTION:target.motion(op.x,op.y);break;
                case WHEEL:target.wheel(op.button);break;
            }
        }
    }
}
