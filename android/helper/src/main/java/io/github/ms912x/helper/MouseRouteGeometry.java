package io.github.ms912x.helper;

/** Two adjacent display rectangles in dp; only the shared part of an edge permits crossing. */
final class MouseRouteGeometry {
    static final int LEFT=1, RIGHT=2, TOP=3, BOTTOM=4;
    private final float[] left=new float[2],top=new float[2],width=new float[2],height=new float[2],scale=new float[2];
    private final int[] pixelsX,pixelsY;
    private int active;
    private float x,y;

    MouseRouteGeometry(int mainWidth,int mainHeight,int mainDensity,int screenWidth,int screenHeight,int screenDensity,int side,float startX,float startY) {
        if(mainWidth<1||mainHeight<1||screenWidth<1||screenHeight<1||mainDensity<1||screenDensity<1||side<LEFT||side>BOTTOM)
            throw new IllegalArgumentException("Invalid mouse display geometry");
        pixelsX=new int[]{mainWidth,screenWidth};pixelsY=new int[]{mainHeight,screenHeight};
        scale[0]=mainDensity/160f;scale[1]=screenDensity/160f;
        for(int i=0;i<2;i++){width[i]=pixelsX[i]/scale[i];height[i]=pixelsY[i]/scale[i];}
        left[1]=(width[0]-width[1])/2;top[1]=(height[0]-height[1])/2;
        if(side==LEFT)left[1]=-width[1];
        if(side==RIGHT)left[1]=width[0];
        if(side==TOP)top[1]=-height[1];
        if(side==BOTTOM)top[1]=height[0];
        position(0,startX,startY);
    }
    int active(){return active;}
    MouseRouteGeometry reposition(int side){
        MouseRouteGeometry next=new MouseRouteGeometry(pixelsX[0],pixelsY[0],Math.round(scale[0]*160),
                pixelsX[1],pixelsY[1],Math.round(scale[1]*160),side,0,0);
        next.position(active,x(),y());return next;
    }
    float x(){return (x-left[active])*scale[active];}
    float y(){return (y-top[active])*scale[active];}
    void position(int display,float px,float py){
        if(display<0||display>1||!Float.isFinite(px)||!Float.isFinite(py))throw new IllegalArgumentException("Invalid mouse position");
        active=display;x=left[active]+clamp(px,0,pixelsX[active]-1)/scale[active];y=top[active]+clamp(py,0,pixelsY[active]-1)/scale[active];
    }
    boolean move(float dx,float dy,boolean held){
        if(!Float.isFinite(dx)||!Float.isFinite(dy))throw new IllegalArgumentException("Invalid mouse motion");
        float nextX=x+dx/scale[active],nextY=y+dy/scale[active];
        int other=1-active;
        boolean cross=false;
        if(!held){
            if(left[other]>=left[active]+width[active]-.001f&&nextX>=left[other]&&dx>0)
                cross=verticalHit(left[other],nextX,nextY,other);
            else if(left[other]+width[other]<=left[active]+.001f&&nextX<left[active]&&dx<0)
                cross=verticalHit(left[active],nextX,nextY,other);
            else if(top[other]>=top[active]+height[active]-.001f&&nextY>=top[other]&&dy>0)
                cross=horizontalHit(top[other],nextX,nextY,other);
            else if(top[other]+height[other]<=top[active]+.001f&&nextY<top[active]&&dy<0)
                cross=horizontalHit(top[active],nextX,nextY,other);
        }
        if(cross)active=other;
        x=clamp(nextX,left[active],left[active]+(pixelsX[active]-1)/scale[active]);
        y=clamp(nextY,top[active],top[active]+(pixelsY[active]-1)/scale[active]);
        return cross;
    }
    private boolean verticalHit(float edge,float nextX,float nextY,int other){
        float at=y+(nextY-y)*(edge-x)/(nextX-x);
        return at>=Math.max(top[active],top[other])&&at<Math.min(top[active]+height[active],top[other]+height[other]);
    }
    private boolean horizontalHit(float edge,float nextX,float nextY,int other){
        float at=x+(nextX-x)*(edge-y)/(nextY-y);
        return at>=Math.max(left[active],left[other])&&at<Math.min(left[active]+width[active],left[other]+width[other]);
    }
    private static float clamp(float value,float min,float max){return Math.max(min,Math.min(max,value));}
}
