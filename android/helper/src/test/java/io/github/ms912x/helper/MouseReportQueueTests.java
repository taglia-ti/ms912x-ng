package io.github.ms912x.helper;

public final class MouseReportQueueTests {
    private static void require(boolean value,String message){if(!value)throw new AssertionError(message);}
    private static MouseReportQueue.Frame frame(int display,float x,int buttons,boolean crossed,boolean changed,int wheel){
        return new MouseReportQueue.Frame(display,x,20,buttons,0,wheel,true,crossed,changed);
    }
    public static void main(String[] args) throws Exception {
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
        System.out.println("PASS: high-rate motion, held state, wheel/crossing barriers, clipping and bounded failure");
    }
}
