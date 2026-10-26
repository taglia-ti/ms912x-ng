package io.github.taglia_ti.ms912x_ng.helper;

/** Read evdev independently of Binder delivery; retain every button, wheel and crossing boundary. */
final class SharedMouseRouter implements AutoCloseable {
    interface Endpoint extends AutoCloseable {
        void moveTo(float x,float y) throws Exception;
        default void enterAt(float x,float y) throws Exception {moveTo(x,y);}
        default void leave() throws Exception {}
        default void layout(int side) throws Exception {}
        void buttons(int mask) throws Exception;
        void scroll(int horizontal,int vertical) throws Exception;
        @Override void close() throws Exception;
    }
    interface Listener {void stopped(String reason);default void crossed(int display){}}
    interface Check {void check() throws Exception;}
    private final NativeMouse source;
    private MouseRouteGeometry geometry;
    private final Endpoint[] endpoints;
    private final Listener listener;
    private final Thread worker;
    private final Thread reader;
    private final MouseReportQueue reports=new MouseReportQueue();
    private final Check check;
    private final float sensitivityX,sensitivityY;
    private final java.util.concurrent.atomic.AtomicInteger newSide=new java.util.concurrent.atomic.AtomicInteger();
    private volatile boolean stopped;
    private volatile int placement;

    SharedMouseRouter(NativeMouse source,MouseRouteGeometry geometry,Endpoint main,Endpoint screen,Check check,Listener listener){
        this(source,geometry,main,screen,1,1,check,listener);
    }
    SharedMouseRouter(NativeMouse source,MouseRouteGeometry geometry,Endpoint main,Endpoint screen,float sensitivityX,float sensitivityY,Check check,Listener listener){
        this.source=source;this.geometry=geometry;this.endpoints=new Endpoint[]{main,screen};this.listener=listener;
        this.check=check;
        this.sensitivityX=sensitivityX;this.sensitivityY=sensitivityY;
        placement=geometry.side();
        worker=new Thread(this::run,"ms912x-shared-mouse");
        reader=new Thread(this::read,"ms912x-mouse-reader");
    }
    void start(){worker.start();}
    void reposition(int side){if(side<1||side>4)throw new IllegalArgumentException("Invalid monitor position");placement=side;newSide.set(side);}
    private void read(){
        int previous=0;
        try{
            while(!stopped){
                int side=newSide.getAndSet(0);if(side!=0)geometry=geometry.reposition(side);
                int[] packet=source.read();
                if(packet==null||stopped)continue;
                if(packet.length>5&&packet[5]!=0)android.util.Log.w("MS912xMouse","Recovered evdev overflow; button state resynchronized");
                int next=packet[2];
                boolean moved=packet[0]!=0||packet[1]!=0;
                boolean crossed=geometry.move(packet[0]*sensitivityX,packet[1]*sensitivityY,(previous|next)!=0);
                if(moved||crossed||next!=previous||packet[3]!=0||packet[4]!=0){
                    if(!reports.offer(new MouseReportQueue.Frame(geometry.active(),geometry.x(),geometry.y(),next,
                            packet[3],packet[4],moved,crossed,next!=previous)))break;
                }
                previous=next;
            }
        }catch(Exception failure){reports.fail(failure);source.close();}
    }
    private void run(){
        int previous=0,display=0,appliedSide=0;
        String failure="Mouse sharing stopped";
        long nextCheck=0;
        try{
            reader.start();
            while(!stopped){
                int desiredSide=placement;if(desiredSide!=appliedSide){endpoints[0].layout(desiredSide);appliedSide=desiredSide;}
                long now=android.os.SystemClock.uptimeMillis();
                if(now>=nextCheck){check.check();nextCheck=now+100;}
                MouseReportQueue.Frame frame=reports.take();
                if(frame==null||stopped)continue;
                boolean crossed=frame.display!=display;
                if(crossed)endpoints[display].leave();
                display=frame.display;Endpoint endpoint=endpoints[display];
                if(crossed)endpoint.enterAt(frame.x,frame.y);
                else if(frame.moved)endpoint.moveTo(frame.x,frame.y);
                if(frame.buttons!=previous)endpoint.buttons(frame.buttons);
                if(frame.horizontal!=0||frame.vertical!=0)endpoint.scroll(frame.horizontal,frame.vertical);
                previous=frame.buttons;
                if(crossed)listener.crossed(display);
            }
        }catch(Exception error){
            Throwable detail=error;while(detail instanceof java.lang.reflect.InvocationTargetException&&detail.getCause()!=null)detail=detail.getCause();
            failure=detail.getMessage()==null?"Android stopped mouse input.":"Mouse sharing stopped: "+detail.getMessage();
        }
        finally{
            stopped=true;
            reports.close();
            // Release held state before destroying virtual devices or their display.
            for(Endpoint endpoint:endpoints){
                try{endpoint.buttons(0);}catch(Exception ignored){ }
                try{endpoint.close();}catch(Exception ignored){ }
            }
            source.close();
            try{reader.join(100);}catch(InterruptedException interrupted){Thread.currentThread().interrupt();}
            listener.stopped(failure);
        }
    }
    @Override public void close(){
        stopped=true;
        reports.close();
        source.releaseGrab(); // Release physical input immediately; keep uinput alive for cursor restoration.
        if(Thread.currentThread()!=worker)try{worker.join(750);}catch(InterruptedException interrupted){Thread.currentThread().interrupt();}
    }
}
