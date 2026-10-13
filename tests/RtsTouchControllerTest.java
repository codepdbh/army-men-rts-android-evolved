import com.codepdbh.armymenrts.RtsTouchController;
import java.util.ArrayList;

public final class RtsTouchControllerTest {
    static final class Sink implements RtsTouchController.Sink {
        ArrayList<String> events=new ArrayList<>();
        public void move(float x,float y){events.add("move");}
        public void button(float x,float y,int button,boolean down){events.add(button+":"+down);}
        public void click(float x,float y,int button){events.add("click:"+button);}
        public void wheel(int steps){events.add("wheel:"+steps);}
        public void feedback(){}
    }
    static void require(boolean value,String message){if(!value)throw new AssertionError(message);}
    public static void main(String[] args){
        Sink sink=new Sink();RtsTouchController touch=new RtsTouchController(sink,1000,500,1,1);
        touch.down(100,100,0);touch.up(100,100,80);
        require(sink.events.contains("click:0"),"Tap did not issue the native contextual left click");sink.events.clear();
        touch.down(100,100,0);touch.move(200,200);touch.up(200,200,200);
        require(sink.events.indexOf("0:true")<sink.events.indexOf("0:false"),"Selection rectangle button order changed");
        require(!sink.events.contains("click:0"),"Drag issued an extra order");sink.events.clear();
        touch.down(100,100,0);touch.second(100,100,200,100);touch.multiMove(160,150,260,150);touch.pointerUp();touch.up(160,150,200);
        require(sink.events.contains("1:true")&&sink.events.contains("1:false"),"Camera drag did not release its button");
        require(!sink.events.contains("0:true")&&!sink.events.contains("click:0"),"Two finger camera gesture issued a selection/order");sink.events.clear();
        touch.down(100,100,0);touch.second(100,100,200,100);touch.multiMove(50,100,250,100);touch.pointerUp();touch.up(50,100,200);
        require(sink.events.stream().anyMatch(event->event.startsWith("wheel:")),"Pinch did not generate zoom");
        require(!sink.events.contains("1:true")&&!sink.events.contains("click:0"),"Pinch issued a camera click/order");sink.events.clear();
        touch.down(100,100,0);touch.second(100,100,200,100);touch.pointerUp();touch.up(100,100,100);
        require(sink.events.stream().filter(event->event.equals("click:1")).count()==1,"Two finger tap cancellation was duplicated");sink.events.clear();
        touch.down(100,100,0);touch.up(100,100,600);require(sink.events.contains("click:1"),"Hold did not cancel");sink.events.clear();
        touch.down(100,100,0);touch.move(200,200);touch.cancel();touch.up(200,200,300);
        require(sink.events.stream().filter(event->event.equals("0:false")).count()==1,"Cancelled drag left a stuck button");
        System.out.println("RTS touch gesture tests passed");
    }
}
