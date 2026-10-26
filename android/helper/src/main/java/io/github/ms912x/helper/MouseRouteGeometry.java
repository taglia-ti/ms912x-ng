package io.github.ms912x.helper;

/** Full-edge handoff with proportional coordinates along the adjoining edge. */
final class MouseRouteGeometry {
    static final int LEFT=1,RIGHT=2,TOP=3,BOTTOM=4;
    private final int[] width,height,density;
    private final int side;
    private int active;
    private float x,y;
    MouseRouteGeometry(int mainWidth,int mainHeight,int mainDensity,int screenWidth,int screenHeight,int screenDensity,int side,float startX,float startY){
        if(mainWidth<1||mainHeight<1||screenWidth<1||screenHeight<1||mainDensity<1||screenDensity<1||side<LEFT||side>BOTTOM)
            throw new IllegalArgumentException("Invalid mouse display geometry");
        width=new int[]{mainWidth,screenWidth};height=new int[]{mainHeight,screenHeight};density=new int[]{mainDensity,screenDensity};this.side=side;
        position(0,startX,startY);
    }
    int active(){return active;}
    int side(){return side;}
    float x(){return x;}
    float y(){return y;}
    void position(int display,float px,float py){
        if(display<0||display>1||!Float.isFinite(px)||!Float.isFinite(py))throw new IllegalArgumentException("Invalid mouse position");
        active=display;x=clamp(px,0,width[active]-1);y=clamp(py,0,height[active]-1);
    }
    MouseRouteGeometry reposition(int nextSide){
        MouseRouteGeometry result=new MouseRouteGeometry(width[0],height[0],density[0],width[1],height[1],density[1],nextSide,0,0);
        result.position(active,x,y);return result;
    }
    boolean move(float dx,float dy,boolean held){
        if(!Float.isFinite(dx)||!Float.isFinite(dy))throw new IllegalArgumentException("Invalid mouse motion");
        float nx=x+dx,ny=y+dy;int other=1-active;
        int exit=active==0?side:side==LEFT?RIGHT:side==RIGHT?LEFT:side==TOP?BOTTOM:TOP;
        boolean crossed=!held&&(exit==LEFT&&nx<0&&dx<0||exit==RIGHT&&nx>=width[active]&&dx>0
                ||exit==TOP&&ny<0&&dy<0||exit==BOTTOM&&ny>=height[active]&&dy>0);
        if(crossed){
            float ratio=(float)density[other]/density[active];
            if(exit==LEFT||exit==RIGHT){
                float edge=exit==LEFT?0:width[active];
                float hit=y+dy*(edge-x)/dx;
                ny=clamp(hit,0,height[active]-1)*(height[other]-1)/Math.max(1,height[active]-1)+(ny-hit)*ratio;
                nx=exit==LEFT?width[other]+nx*ratio:(nx-width[active])*ratio;
            }else{
                float edge=exit==TOP?0:height[active];
                float hit=x+dx*(edge-y)/dy;
                nx=clamp(hit,0,width[active]-1)*(width[other]-1)/Math.max(1,width[active]-1)+(nx-hit)*ratio;
                ny=exit==TOP?height[other]+ny*ratio:(ny-height[active])*ratio;
            }
            active=other;
        }
        x=clamp(nx,0,width[active]-1);y=clamp(ny,0,height[active]-1);return crossed;
    }
    private static float clamp(float value,float min,float max){return Math.max(min,Math.min(max,value));}
}
