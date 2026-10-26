package io.github.ms912x.helper;

/** A single worker owns ordered reports, display handoff, buttons and wheel delivery. */
final class SharedMouseRouter implements AutoCloseable {
    interface Endpoint extends AutoCloseable {
        void moveTo(float x,float y) throws Exception;
        default void enterAt(float x,float y) throws Exception {moveTo(x,y);}
        default void leave() throws Exception {}
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
    private final Check check;
    private final float sensitivityX,sensitivityY;
    private final java.util.concurrent.atomic.AtomicInteger newSide=new java.util.concurrent.atomic.AtomicInteger();
    private volatile boolean stopped;

    SharedMouseRouter(NativeMouse source,MouseRouteGeometry geometry,Endpoint main,Endpoint screen,Check check,Listener listener){
        this(source,geometry,main,screen,1,1,check,listener);
    }
    SharedMouseRouter(NativeMouse source,MouseRouteGeometry geometry,Endpoint main,Endpoint screen,float sensitivityX,float sensitivityY,Check check,Listener listener){
        this.source=source;this.geometry=geometry;this.endpoints=new Endpoint[]{main,screen};this.listener=listener;
        this.check=check;
        this.sensitivityX=sensitivityX;this.sensitivityY=sensitivityY;
        worker=new Thread(this::run,"ms912x-shared-mouse");
    }
    void start(){worker.start();}
    void reposition(int side){if(side<1||side>4)throw new IllegalArgumentException("Invalid monitor position");newSide.set(side);}
    private void run(){
        int previous=0;
        String failure="Mouse sharing stopped";
        long nextCheck=0;
        try{
            while(!stopped){
                int side=newSide.getAndSet(0);if(side!=0)geometry=geometry.reposition(side);
                long now=android.os.SystemClock.uptimeMillis();
                if(now>=nextCheck){check.check();nextCheck=now+100;}
                int[] packet=source.read();
                if(packet==null||stopped)continue;
                int next=packet[2];
                int old=geometry.active();
                boolean crossed=geometry.move(packet[0]*sensitivityX,packet[1]*sensitivityY,(previous|next)!=0);
                if(crossed)endpoints[old].leave();
                Endpoint endpoint=endpoints[geometry.active()];
                if(crossed)endpoint.enterAt(geometry.x(),geometry.y());
                else if(packet[0]!=0||packet[1]!=0)endpoint.moveTo(geometry.x(),geometry.y());
                if(next!=previous)endpoint.buttons(next);
                if(packet[3]!=0||packet[4]!=0)endpoint.scroll(packet[3],packet[4]);
                previous=next;
                if(crossed)listener.crossed(geometry.active());
            }
        }catch(Exception error){
            Throwable detail=error;while(detail instanceof java.lang.reflect.InvocationTargetException&&detail.getCause()!=null)detail=detail.getCause();
            failure=detail.getMessage()==null?"Android stopped mouse input.":"Mouse sharing stopped: "+detail.getMessage();
        }
        finally{
            stopped=true;
            // Release held state before destroying virtual devices or their display.
            for(Endpoint endpoint:endpoints){
                try{endpoint.buttons(0);}catch(Exception ignored){ }
                try{endpoint.close();}catch(Exception ignored){ }
            }
            source.close();
            listener.stopped(failure);
        }
    }
    @Override public void close(){
        stopped=true;
        source.close(); // Releases the physical device even if a framework call is stalled.
        if(Thread.currentThread()!=worker)try{worker.join(750);}catch(InterruptedException interrupted){Thread.currentThread().interrupt();}
    }
}
