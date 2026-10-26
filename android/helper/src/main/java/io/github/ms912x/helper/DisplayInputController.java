package io.github.ms912x.helper;

import android.os.Handler;
import android.view.MotionEvent;
import io.github.ms912x.ipc.ScreenInputCommand;
import java.util.ArrayDeque;

/** Serializes, bounds and cancels input directed to one owned desktop display. */
final class DisplayInputController implements AutoCloseable {
    interface Error { void onError(long sequence,String detail); }
    interface Home { void open() throws Exception; }
    private static final int LIMIT=64;
    private final Handler handler;
    private final FrameworkInputCompat input;
    private final PointerOverlay pointer;
    private final int displayId,width,height;
    private final DisplayGeometry geometry;
    private final ArrayDeque<ScreenInputCommand> pending=new ArrayDeque<>();
    private final Runnable drain=this::drain;
    private final Error error;
    private final Home home;
    private float x,y;
    private int buttonMask;
    private long lastSequence;
    private boolean supported=true,closed,errorReported;

    DisplayInputController(Handler handler,FrameworkInputCompat input,PointerOverlay pointer,int displayId,int width,int height,int rotation,Home home,Error error) throws Exception {
        this.handler=handler;this.input=input;this.pointer=pointer;this.displayId=displayId;this.width=width;this.height=height;this.home=home;this.error=error;
        x=width/2f;y=height/2f;
        geometry=new DisplayGeometry(width,height,width,height,rotation);
        // A hover probe can be rejected before Android has focused a window on
        // the newly created display. Wait for real touchpad input to validate
        // injection so screen creation does not disable the touchpad up front.
        supported=true;
        if(pointer!=null)pointer.moveTo(x,y);
    }

    synchronized boolean submit(ScreenInputCommand event) {
        if(closed||!supported||event.sequence<=lastSequence||event.sequence<=0)return false;
        if(!valid(event))return false;
        lastSequence=event.sequence;
        if(event.type==ScreenInputCommand.MOVE&&!pending.isEmpty()){
            ScreenInputCommand previous=pending.peekLast();
            if(previous.type==ScreenInputCommand.MOVE){
                pending.removeLast();
                pending.addLast(new ScreenInputCommand(event.sessionId,event.generation,event.sequence,event.type,
                        clamp(previous.x+event.x,-4096,4096),clamp(previous.y+event.y,-4096,4096),0));
                return true;
            }
        }
        if(pending.size()>=LIMIT){
            pending.clear();pending.addLast(new ScreenInputCommand(event.sessionId,event.generation,event.sequence,
                    ScreenInputCommand.CANCEL,0,0,0));
            if(!handler.hasCallbacks(drain))handler.post(drain);
            report(event.sequence,"Input queue overflow; active pointer was cancelled");
            return false;
        }
        pending.addLast(event);
        if(!handler.hasCallbacks(drain)&&!handler.post(drain)){pending.clear();return false;}
        return true;
    }
    synchronized boolean supported(){return supported&&!closed;}
    void suspendPointer(boolean value){if(pointer!=null)pointer.suspend(value);}
    SharedMouseRouter.Endpoint mouseEndpoint(){
        synchronized(this){pending.clear();handler.removeCallbacks(drain);releaseButton();}
        return new SharedMouseRouter.Endpoint(){
            private int buttons;
            public void moveTo(float px,float py) throws Exception {
                x=px;y=py;input.move(displayId,x,y,buttons);if(pointer!=null)pointer.moveTo(x,y);
            }
            public void enterAt(float px,float py) throws Exception {suspendPointer(false);moveTo(px,py);}
            public void leave(){suspendPointer(true);}
            public void buttons(int next) throws Exception {
                if(!input.buttons(displayId,x,y,buttons,next))throw new IllegalStateException("Android refused mouse buttons");
                buttons=next;
            }
            public void scroll(int h,int v) throws Exception {input.scroll(displayId,x,y,h,v);}
            public void close(){}
        };
    }

    private boolean valid(ScreenInputCommand event){
        if(!Float.isFinite(event.x)||!Float.isFinite(event.y))return false;
        switch(event.type){
            case ScreenInputCommand.MOVE:return absBound(event.x,4096)&&absBound(event.y,4096)&&event.button==0;
            case ScreenInputCommand.CLICK:return event.x==0&&event.y==0&&(event.button==1||event.button==2);
            case ScreenInputCommand.SCROLL:return absBound(event.x,24)&&absBound(event.y,24)&&event.button==0;
            case ScreenInputCommand.DRAG_START:case ScreenInputCommand.DRAG_END:
            case ScreenInputCommand.BACK:case ScreenInputCommand.HOME:case ScreenInputCommand.CANCEL:
                return event.x==0&&event.y==0&&event.button==0;
            default:return false;
        }
    }
    private static boolean absBound(float value,float max){return Math.abs(value)<=max;}
    private static float clamp(float value,float min,float max){return Math.max(min,Math.min(max,value));}

    private void drain(){
        for(int processed=0;processed<16;processed++){
            ScreenInputCommand event;
            synchronized(this){event=pending.pollFirst();if(event==null||closed)return;}
            try{
                switch(event.type){
                    case ScreenInputCommand.MOVE:
                        x=clamp(x+geometry.pointerX(event.x,event.y),0,width-1);
                        y=clamp(y+geometry.pointerY(event.x,event.y),0,height-1);
                        // A window transition can temporarily leave no target.
                        // A rejected event is not a loss of shell permission.
                        input.move(displayId,x,y,buttonMask);
                        if(pointer!=null)pointer.moveTo(x,y);
                        break;
                    case ScreenInputCommand.CLICK:
                        releaseButton();
                        int button=event.button==1?MotionEvent.BUTTON_PRIMARY:MotionEvent.BUTTON_SECONDARY;
                        try{input.button(displayId,x,y,button,true);}
                        finally{input.button(displayId,x,y,button,false);}
                        break;
                    case ScreenInputCommand.SCROLL:
                        input.scroll(displayId,x,y,event.x,event.y);
                        break;
                    case ScreenInputCommand.DRAG_START:
                        if(buttonMask==0&&input.button(displayId,x,y,MotionEvent.BUTTON_PRIMARY,true))buttonMask=MotionEvent.BUTTON_PRIMARY;
                        break;
                    case ScreenInputCommand.DRAG_END:case ScreenInputCommand.CANCEL:
                        releaseButton();break;
                    case ScreenInputCommand.BACK:
                        releaseButton();input.back(displayId);
                        break;
                    case ScreenInputCommand.HOME:
                        releaseButton();
                        home.open();
                        break;
                    default:throw new IllegalArgumentException("Unknown input event");
                }
            }catch(Exception failure){
                synchronized(this){supported=false;pending.clear();releaseButton();}
                report(event.sequence,failure.getClass().getSimpleName()+": display input unavailable");
                return;
            }
        }
        synchronized(this){if(!pending.isEmpty()&&!closed)handler.post(drain);}
    }

    private void releaseButton(){
        if(buttonMask!=0){try{input.button(displayId,x,y,buttonMask,false);}catch(Exception ignored){ }
            buttonMask=0;}
    }
    private void report(long sequence,String detail){if(!errorReported){errorReported=true;error.onError(sequence,detail);}}
    @Override public synchronized void close(){
        if(closed)return;closed=true;pending.clear();handler.removeCallbacks(drain);releaseButton();
        if(pointer!=null)pointer.close();
    }
}
