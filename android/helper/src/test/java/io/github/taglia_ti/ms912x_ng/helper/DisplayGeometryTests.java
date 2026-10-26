package io.github.taglia_ti.ms912x_ng.helper;

public final class DisplayGeometryTests {
    public static void main(String[] args){
        int cases=0;
        for(int[] source:new int[][]{{640,480},{1920,1200},{480,640},{1,1}})
            for(int[] output:new int[][]{{640,480},{1920,1080},{800,800}})
                for(int rotation=0;rotation<4;rotation++){
                    DisplayGeometry g=new DisplayGeometry(source[0],source[1],output[0],output[1],rotation);
                    if(g.left<0||g.top<0||g.left+g.width>output[0]||g.top+g.height>output[1])throw new AssertionError("Fit outside output");
                    float x=g.pointerX(31,-17),y=g.pointerY(31,-17);
                    float rx=rotation==1?-y:rotation==2?-x:rotation==3?y:x;
                    float ry=rotation==1?x:rotation==2?-y:rotation==3?-x:y;
                    float renderedX=rx*g.width/(rotation%2==0?source[0]:source[1]);
                    float renderedY=ry*g.height/(rotation%2==0?source[1]:source[0]);
                    if(Math.abs(renderedX-31)>.001||Math.abs(renderedY+17)>.001)throw new AssertionError("Pointer inverse mismatch");
                    cases++;
                }
        System.out.println("PASS: "+cases+" fitted display/pointer rotation round trips");
    }
}
