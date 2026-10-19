import com.codepdbh.armymenrts.GameInput;
import com.codepdbh.armymenrts.RtsTouchController;
import java.util.ArrayList;

public final class RtsTouchControllerTest {
    static final class Sink implements RtsTouchController.Sink {
        ArrayList<String> events=new ArrayList<>();
        float restX=-1,restY=-1,cameraX,cameraY,velocityX,velocityY,zoom=1;
        public void move(float x,float y){events.add("move");}
        public void button(float x,float y,int button,boolean down){events.add(button+":"+down);}
        public void click(float x,float y,int button){events.add("click:"+button);}
        public void camera(int phase,float x,float y,float z,float vx,float vy){
            if(phase==RtsTouchController.MOVE)zoom=z;
            events.add(new String[]{"begin","drag","end","stop"}[phase]);
            if(phase!=RtsTouchController.CANCEL){cameraX=x;cameraY=y;}
            if(phase==RtsTouchController.END){velocityX=vx;velocityY=vy;}
        }
        public void rotate(int direction){events.add("rotate:"+direction);}
        public void feedback(){events.add("feedback");}
        public void rest(float x,float y){events.add("rest");restX=x;restY=y;}
        void clear(){events.clear();}
    }
    static final class Guest implements GameInput.Target {
        ArrayList<String> events=new ArrayList<>();int frame;
        public void pointer(float x,float y,int button,boolean down){events.add(button==3?"move":button+":"+down+"@"+frame);}
        public void key(int scan,boolean down){events.add("key"+scan+":"+down+"@"+frame);}
        public void motion(float dx,float dy){events.add("motion");}
        public void wheel(int steps){events.add("wheel");}
        public int frames(){return frame;}
    }
    static void require(boolean value,String message){if(!value)throw new AssertionError(message);}
    static long count(ArrayList<String> events,String value){return events.stream().filter(event->event.equals(value)).count();}
    static boolean any(ArrayList<String> events,String prefix){return events.stream().anyMatch(event->event.startsWith(prefix));}
    public static void main(String[] args){
        Sink sink=new Sink();RtsTouchController touch=new RtsTouchController(sink,1000,500,1);
        touch.down(100,100,0);touch.up(100,100,80);
        require(sink.events.contains("click:0"),"Tap did not issue the contextual left click");
        require(sink.events.indexOf("click:0")<sink.events.indexOf("rest"),"Cursor parked before the click");
        require(sink.events.get(1).equals("stop"),"Touching did not stop camera inertia");sink.clear();
        // Taps on the HUD/minimap at the screen border must not leave the cursor in the edge-scroll band.
        touch.down(999,499,0);touch.up(999,499,80);
        require(sink.events.contains("click:0"),"Edge tap lost its click");
        require(sink.restX<=1-RtsTouchController.EDGE&&sink.restY<=1-RtsTouchController.EDGE,"Cursor rests in the edge-scroll band");sink.clear();
        // One finger drag: anchored at the touch point, then the absolute finger position.
        touch.down(500,250,0);touch.move(520,250,16);
        require(sink.events.indexOf("begin")>=0&&sink.events.indexOf("begin")<sink.events.indexOf("drag"),"Drag did not anchor before moving");
        touch.move(700,300,32);require(sink.cameraX==.7f&&sink.cameraY==.6f,"Camera does not follow the finger");
        // Finger rested 200 ms before lifting: no inertia.
        touch.up(700,300,250);
        require(sink.events.contains("end")&&sink.velocityX==0&&sink.velocityY==0,"Still release kept inertia");
        require(!any(sink.events,"0:")&&!any(sink.events,"1:")&&!any(sink.events,"click"),"Pan issued mouse buttons or orders");sink.clear();
        // Flick: release while moving keeps the measured velocity (screen units/s).
        touch.down(200,250,0);for(int t=16;t<=96;t+=16)touch.move(200+t*5,250,t);touch.up(700,250,100);
        require(Math.abs(sink.velocityX-5f)<.3f&&Math.abs(sink.velocityY)<.01f,"Flick velocity wrong: "+sink.velocityX);sink.clear();
        // Hold, then drag: selection rectangle, no camera.
        touch.down(100,100,0);touch.hold(RtsTouchController.HOLD_MS);touch.move(200,200,400);touch.up(200,200,900);
        require(sink.events.contains("feedback"),"Hold gave no feedback");
        require(sink.events.indexOf("0:true")<sink.events.indexOf("0:false"),"Selection rectangle button order changed");
        require(!sink.events.contains("click:0")&&!sink.events.contains("begin"),"Selection issued an extra order/pan");sink.clear();
        // Hold and release: cancel. A quicker press is still a tap.
        // Holding and releasing without moving is still a tap, never a cancel.
        touch.down(100,100,0);touch.up(100,100,RtsTouchController.HOLD_MS+50);require(sink.events.contains("click:0")&&!sink.events.contains("click:1"),"Slow tap turned into a cancel");sink.clear();
        touch.down(100,100,0);touch.hold(RtsTouchController.HOLD_MS);touch.up(100,100,RtsTouchController.HOLD_MS+900);require(sink.events.contains("click:0")&&!sink.events.contains("click:1"),"Long hold turned into a cancel");sink.clear();
        touch.down(100,100,0);touch.up(100,100,RtsTouchController.HOLD_MS-50);require(sink.events.contains("click:0"),"Short press did not select");sink.clear();
        // Two finger drag pans from the centroid; pinch zooms; two finger tap cancels once.
        touch.down(100,100,0);touch.second(100,100,200,100);touch.multiMove(160,150,260,150,30);touch.pointerUp();touch.up(160,150,200);
        require(count(sink.events,"begin")==1&&sink.events.contains("drag")&&count(sink.events,"end")==1,"Two finger pan failed: "+sink.events);
        require(Math.abs(sink.cameraX-.21f)<1e-4f&&Math.abs(sink.cameraY-.3f)<1e-4f,"Two finger pan does not follow the centroid");
        require(!any(sink.events,"0:")&&!sink.events.contains("click:0"),"Two finger pan issued a selection/order");sink.clear();
        // Spreading two fingers zooms continuously around their midpoint (no wheel steps).
        touch.down(100,100,0);touch.second(100,100,200,100);touch.multiMove(50,100,250,100);touch.multiMove(0,100,300,100);touch.pointerUp();touch.up(0,100,200);
        require(sink.events.contains("begin")&&Math.abs(sink.zoom-3f)<1e-4f&&Math.abs(sink.cameraX-.15f)<1e-4f,"Pinch did not zoom around the midpoint: zoom="+sink.zoom);
        require(!sink.events.contains("click:0")&&!sink.events.contains("click:1"),"Pinch issued an order");sink.clear();
        touch.down(100,100,0);touch.second(100,100,200,100);touch.pointerUp();touch.up(100,100,100);
        require(count(sink.events,"click:1")==1,"Two finger tap cancellation was duplicated");sink.clear();
        // Two finger twist rotates a structure in steps; no pan, zoom or order.
        touch.down(400,250,0);touch.second(400,250,600,250);
        for(int degrees=5;degrees<=70;degrees+=5){double r=Math.toRadians(degrees);float cx=500,cy=250;
            touch.multiMove(cx-100*(float)Math.cos(r),cy-100*(float)Math.sin(r),cx+100*(float)Math.cos(r),cy+100*(float)Math.sin(r));}
        touch.pointerUp();touch.up(400,250,500);
        require(count(sink.events,"rotate:1")==2&&!sink.events.contains("begin")&&!sink.events.contains("click:1"),"Twist did not rotate cleanly: "+sink.events);sink.clear();
        // A one finger pan joined by a second finger re-anchors on the midpoint and can zoom at once.
        touch.down(100,100,0);touch.move(200,100,16);touch.second(200,100,300,100);touch.multiMove(230,120,330,120,40);
        require(count(sink.events,"begin")==2&&sink.zoom==1f,"Second finger did not re-anchor");
        touch.multiMove(150,120,450,120,60);touch.pointerUp();touch.up(150,120,400);
        require(Math.abs(sink.zoom-3f)<1e-4f&&count(sink.events,"end")==1&&sink.velocityX==0,"Pan into pinch did not zoom: "+sink.events);sink.clear();
        touch.down(100,100,0);touch.hold(RtsTouchController.HOLD_MS);touch.move(200,200);touch.cancel();touch.up(200,200,300);
        require(count(sink.events,"0:false")==1,"Cancelled selection left a stuck button");sink.clear();
        touch.down(100,100,0);touch.move(200,200);touch.cancel();
        require(sink.events.get(sink.events.size()-1).equals("stop"),"Cancelled pan kept moving the camera");
        System.out.println("RTS touch gesture tests passed");

        // Frame-synchronised delivery: a click spans an engine input poll at any frame rate.
        Guest guest=new Guest();GameInput input=new GameInput(guest);
        input.click(.5f,.5f,0);input.move(.6f,.6f);input.pump(0);
        require(guest.events.equals(java.util.List.of("0:true@0")),"Release was not held for a frame: "+guest.events);
        guest.frame=1;input.pump(10);require(guest.events.size()==1,"Released after a single present");
        guest.frame=2;input.pump(20);require(guest.events.contains("0:false@2")&&guest.events.get(2).equals("move"),"Click/rest order wrong: "+guest.events);
        guest.events.clear();input.click(.5f,.5f,0);input.pump(30);input.pump(330);
        require(guest.events.contains("0:false@2"),"Stalled game kept the button pressed");
        guest.events.clear();input.button(.5f,.5f,1,true);input.pump(400);input.clear();
        require(guest.events.contains("1:false@2"),"Clear did not release a held button");
        guest.events.clear();input.shiftClick(.5f,.5f);for(int i=0;i<8;i++){guest.frame++;input.pump(500+i);}
        require(guest.events.get(0).startsWith("key42:true")&&guest.events.get(guest.events.size()-1).startsWith("key42:false"),"Shift did not wrap the click");
        System.out.println("Frame-synchronised input tests passed");
    }
}
