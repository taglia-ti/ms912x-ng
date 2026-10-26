package io.github.ms912x.helper;

import java.util.ArrayDeque;

/** Bounded delivery mailbox; geometry is resolved before superseded motion is replaced. */
final class MouseReportQueue {
    static final int LIMIT=256;
    static final class Frame {
        final int display,buttons,horizontal,vertical;
        final float x,y;
        final boolean moved,crossed,buttonChanged;
        Frame(int display,float x,float y,int buttons,int horizontal,int vertical,boolean moved,boolean crossed,boolean buttonChanged){
            this.display=display;this.x=x;this.y=y;this.buttons=buttons;this.horizontal=horizontal;this.vertical=vertical;
            this.moved=moved;this.crossed=crossed;this.buttonChanged=buttonChanged;
        }
        boolean motionOnly(){return moved&&!crossed&&!buttonChanged&&horizontal==0&&vertical==0;}
    }
    private final ArrayDeque<Frame> frames=new ArrayDeque<>();
    private boolean closed;
    private Exception failure;
    synchronized boolean offer(Frame frame){
        if(closed)return false;
        Frame last=frames.peekLast();
        if(last!=null&&last.motionOnly()&&frame.motionOnly()&&last.display==frame.display&&last.buttons==frame.buttons)frames.removeLast();
        if(frames.size()>=LIMIT)throw new IllegalStateException("Mouse button/wheel queue overflow");
        frames.addLast(frame);notifyAll();return true;
    }
    synchronized Frame take() throws Exception {
        if(!closed&&frames.isEmpty())wait(16);
        if(failure!=null)throw failure;
        return frames.pollFirst();
    }
    synchronized void fail(Exception error){failure=error;close();}
    synchronized void close(){closed=true;frames.clear();notifyAll();}
}
