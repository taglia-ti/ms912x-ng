package io.github.ms912x.helper;

public final class MouseRouteGeometryTests {
    private static void check(boolean value,String reason){if(!value)throw new AssertionError(reason);}
    private static void near(float actual,float expected){check(Math.abs(actual-expected)<.02f,actual+" != "+expected);}

    private static void diagonalHandoffs(){
        // At the seam, 30/80 maps to 15/40. Remaining motion doubles with density.
        // On return, 25/40 maps to 50/80, then the remaining motion is halved.
        float[][] cases={
                {100,81,60,41,10,20,-20,20,40,35,30,-15,5,47.5f},
                {100,81,60,41,90,20,20,20,20,35,-30,-15,95,47.5f},
                {81,100,41,60,20,10,20,-20,35,40,-15,30,47.5f,5},
                {81,100,41,60,20,90,20,20,35,20,-15,-30,47.5f,95}
        };
        for(int side=1;side<=4;side++){
            float[] c=cases[side-1];
            MouseRouteGeometry g=new MouseRouteGeometry((int)c[0],(int)c[1],100,(int)c[2],(int)c[3],200,side,c[4],c[5]);
            check(g.move(c[6],c[7],false)&&g.active()==1,"Diagonal crossing failed for side "+side);
            near(g.x(),c[8]);near(g.y(),c[9]);
            check(g.move(c[10],c[11],false)&&g.active()==0,"Diagonal return failed for side "+side);
            near(g.x(),c[12]);near(g.y(),c[13]);
        }
    }

    private static void unsharedEdgesAndSinglePixels(){
        int[][] sharedEdges={{1,2},{2,1},{3,4},{4,3}};
        int[][] motion={{-1000,0},{1000,0},{0,-1000},{0,1000}};
        int[][] expected={{0,40},{99,40},{50,0},{50,79}};
        for(int side=1;side<=4;side++){
          for(int display=0;display<=1;display++){
            MouseRouteGeometry g=new MouseRouteGeometry(100,80,160,100,80,160,side,50,40);
            for(int edge=1;edge<=4;edge++){
                if(edge==sharedEdges[side-1][display])continue;
                g.position(display,50,40);
                check(!g.move(motion[edge-1][0],motion[edge-1][1],false)&&g.active()==display,
                        "An unshared edge changed displays");
                near(g.x(),expected[edge-1][0]);near(g.y(),expected[edge-1][1]);
            }
          }
            for(int[] sizes:new int[][]{{1,1,1,1},{1,1,100,80},{100,80,1,1}}){
                MouseRouteGeometry g=new MouseRouteGeometry(sizes[0],sizes[1],160,sizes[2],sizes[3],240,side,0,0);
                int[] out=motion[side-1];
                check(g.move(out[0],out[1],false)&&g.active()==1,"Single-pixel crossing failed");
                check(Float.isFinite(g.x())&&Float.isFinite(g.y()),"Single-pixel mapping is not finite");
                check(g.x()>=0&&g.x()<sizes[2]&&g.y()>=0&&g.y()<sizes[3],"Single-pixel crossing escaped bounds");
                check(g.move(-out[0],-out[1],false)&&g.active()==0,"Single-pixel return failed");
                check(Float.isFinite(g.x())&&Float.isFinite(g.y()),"Single-pixel return is not finite");
                check(g.x()>=0&&g.x()<sizes[0]&&g.y()>=0&&g.y()<sizes[1],"Single-pixel return escaped bounds");
            }
        }
    }

    public static void main(String[] args){
        diagonalHandoffs();
        unsharedEdgesAndSinglePixels();
        for(int side=1;side<=4;side++){
            MouseRouteGeometry g=new MouseRouteGeometry(1920,1080,160,1920,1080,160,side,960,540);
            float dx=side==1?-1000:side==2?1000:0,dy=side==3?-600:side==4?600:0;
            check(g.move(dx,dy,false)&&g.active()==1,"Failed crossing side "+side);
            check(g.move(-dx,-dy,false)&&g.active()==0,"Failed return side "+side);
            near(g.x(),960);near(g.y(),540);
            check(!g.move(dx,dy,true)&&g.active()==0,"Held drag changed display");
            check(g.move(dx,dy,false)&&g.active()==1,"Release did not permit crossing");
        }
        MouseRouteGeometry unequal=new MouseRouteGeometry(1920,1080,240,1280,720,160,2,1919,540);
        check(unequal.move(4,0,false),"Mixed-density edge failed");near(unequal.y(),540f*719/1079);near(unequal.x(),2);
        MouseRouteGeometry gap=new MouseRouteGeometry(1920,1200,160,1280,720,160,2,1919,30);
        check(gap.move(200,0,false),"Full-edge handoff failed");near(gap.y(),30f*719/1199);
        gap.position(0,1800,100);
        check(gap.move(300,200,false),"Diagonal edge handoff failed");
        for(int side=1;side<=4;side++){
            MouseRouteGeometry proportional=new MouseRouteGeometry(1920,1080,160,1280,720,160,side,
                    side==1?0:side==2?1919:480,side==3?0:side==4?1079:270);
            float dx=side==1?-1:side==2?1:0,dy=side==3?-1:side==4?1:0;
            check(proportional.move(dx,dy,false),"Unequal-resolution handoff failed");
            if(dx!=0)near(proportional.y(),270f*719/1079);else near(proportional.x(),480f*1279/1919);
            check(proportional.move(-dx*2,-dy*2,false),"Unequal-resolution return failed");
            if(dx!=0)near(proportional.y(),270);else near(proportional.x(),480);
        }
        gap.position(0,1900,600);check(gap.move(100000,0,false),"Large movement failed");near(gap.x(),1279);
        check(gap.move(-100000,0,false),"Large return failed");near(gap.x(),0);
        gap.move(-100000,0,false);gap.move(1,0,false);near(gap.x(),1);
        boolean rejected=false;try{gap.move(Float.NaN,0,false);}catch(IllegalArgumentException expected){rejected=true;}
        check(rejected,"Invalid motion accepted");
        MouseRouteGeometry relocated=new MouseRouteGeometry(640,480,160,640,480,160,2,320,240);
        relocated.move(500,0,false);float originalX=relocated.x(),originalY=relocated.y();
        relocated=relocated.reposition(1);check(relocated.active()==1,"Layout update changed the active screen");
        near(relocated.x(),originalX);near(relocated.y(),originalY);
        check(relocated.move(500,0,false)&&relocated.active()==0,"Live layout did not update the crossing edge");
        System.out.println("PASS: four-edge/diagonal handoff, unequal densities, unshared edges, single-pixel displays, held drags and bounded overshoot");
    }
}
