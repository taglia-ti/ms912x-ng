package io.github.ms912x.helper;

public final class MouseRouteGeometryTests {
    private static void check(boolean value,String reason){if(!value)throw new AssertionError(reason);}
    private static void near(float actual,float expected){check(Math.abs(actual-expected)<.02f,actual+" != "+expected);}
    public static void main(String[] args){
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
        System.out.println("PASS: four-edge handoff, proportional resolution mapping, densities, held drags and bounded overshoot");
    }
}
