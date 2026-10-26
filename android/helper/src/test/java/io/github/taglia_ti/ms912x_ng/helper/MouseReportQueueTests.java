package io.github.taglia_ti.ms912x_ng.helper;

public final class MouseReportQueueTests {
    private static void require(boolean value,String message){if(!value)throw new AssertionError(message);}
    private static MouseReportQueue.Frame frame(int display,float x,int buttons,boolean crossed,boolean changed,int wheel){
        return new MouseReportQueue.Frame(display,x,20,buttons,0,wheel,true,crossed,changed);
    }
    private static void capacityAndShutdown() throws Exception {
        MouseReportQueue queue=new MouseReportQueue();
        for(int i=0;i<MouseReportQueue.LIMIT-1;i++)queue.offer(frame(0,i,i%2,false,true,0));
        queue.offer(frame(0,1,0,false,false,0));
        require(queue.offer(frame(0,99,0,false,false,0)),"Replacing motion at capacity must not overflow");
        try{queue.offer(frame(0,100,0,false,false,1));throw new AssertionError("A wheel barrier cannot evict queued buttons");}
        catch(IllegalStateException expected){ }
        for(int i=0;i<MouseReportQueue.LIMIT-1;i++){
            MouseReportQueue.Frame next=queue.take();
            require(next.x==i&&next.buttons==i%2&&next.buttonChanged,"Overflow must preserve every queued transition");
        }
        require(queue.take().x==99&&queue.take()==null,"Newest motion survives capacity rejection");
        queue.offer(new MouseReportQueue.Frame(0,100,20,0,1,0,true,false,false));
        queue.offer(frame(0,101,0,false,false,0));
        require(queue.take().horizontal==1&&queue.take().x==101,"Horizontal wheel cannot coalesce into motion");
        queue.offer(frame(0,102,1,false,true,0));
        queue.close();queue.close();
        require(!queue.offer(frame(0,103,0,false,true,0))&&queue.take()==null,"Close discards stale buttons and rejects new input");
        Exception disconnected=new java.io.IOException("device removed");
        queue=new MouseReportQueue();
        queue.offer(frame(0,104,1,false,true,0));
        queue.fail(disconnected);
        require(!queue.offer(frame(0,105,0,false,true,0)),"Failure rejects new input");
        try{queue.take();throw new AssertionError("Failure must reach the consumer before any stale input");}
        catch(Exception actual){require(actual==disconnected,"The original disconnect failure must propagate");}
    }
    public static void main(String[] args) throws Exception {
        capacityAndShutdown();
        MouseReportQueue queue=new MouseReportQueue();
        for(int i=0;i<100_000;i++)queue.offer(frame(0,i,0,false,false,0));
        require(queue.take().x==99_999&&queue.take()==null,"A stalled consumer gets the newest absolute position in bounded storage");
        queue.offer(frame(0,10,1,false,true,0));
        for(int i=11;i<=30;i++)queue.offer(frame(0,i,1,false,false,0));
        queue.offer(frame(0,31,0,false,true,0));
        queue.offer(frame(0,32,0,false,false,1));
        queue.offer(frame(1,0,0,true,false,0));
        queue.offer(frame(1,20,0,false,false,0));
        queue.offer(frame(0,639,0,true,false,0));
        require(queue.take().x==10,"Press position survives following motion");
        require(queue.take().x==30,"Held motion coalesces with the button still down");
        require(queue.take().buttons==0,"Release remains ordered");
        require(queue.take().vertical==1,"Wheel remains ordered");
        require(queue.take().display==1&&queue.take().x==20&&queue.take().display==0,"Both crossings survive coalescing");
        MouseRouteGeometry geometry=new MouseRouteGeometry(640,480,160,640,480,160,2,620,240);
        geometry.move(-1000,0,false);queue.offer(frame(0,geometry.x(),0,false,false,0));
        geometry.move(20,0,false);queue.offer(frame(0,geometry.x(),0,false,false,0));
        require(queue.take().x==20,"Resolve clipping before coalescing direction reversals");
        for(int i=0;i<MouseReportQueue.LIMIT;i++)queue.offer(frame(0,0,i%2,false,true,0));
        try{queue.offer(frame(0,0,0,false,true,0));throw new AssertionError("Button overflow must fail explicitly");}
        catch(IllegalStateException expected){ }
        queue.fail(new java.io.IOException("disconnected"));
        try{queue.take();throw new AssertionError("Disconnect must discard stale input");}
        catch(java.io.IOException expected){ }
        System.out.println("PASS: high-rate motion, held state, wheel/crossing barriers, clipping, capacity replacement and shutdown/failure");
    }
}
