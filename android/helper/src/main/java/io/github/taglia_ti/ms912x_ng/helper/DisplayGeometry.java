package io.github.taglia_ti.ms912x_ng.helper;

/** Shared fit and inverse pointer transform, without framework or pixel ownership. */
final class DisplayGeometry {
    final int left,top,width,height;
    private final int rotation;
    private final float scaleX,scaleY;

    DisplayGeometry(int sourceWidth,int sourceHeight,int outputWidth,int outputHeight,int rotation){
        if(sourceWidth<=0||sourceHeight<=0||outputWidth<=0||outputHeight<=0||rotation<0||rotation>3)
            throw new IllegalArgumentException("Invalid display geometry");
        this.rotation=rotation;
        int rotatedWidth=rotation%2==0?sourceWidth:sourceHeight;
        int rotatedHeight=rotation%2==0?sourceHeight:sourceWidth;
        double fit=Math.min((double)outputWidth/rotatedWidth,(double)outputHeight/rotatedHeight);
        width=Math.max(1,Math.min(outputWidth,(int)Math.round(rotatedWidth*fit)));
        height=Math.max(1,Math.min(outputHeight,(int)Math.round(rotatedHeight*fit)));
        left=(outputWidth-width)/2;top=(outputHeight-height)/2;
        scaleX=(float)rotatedWidth/width;scaleY=(float)rotatedHeight/height;
    }

    float pointerX(float dx,float dy){
        switch(rotation){case 1:return dy*scaleY;case 2:return -dx*scaleX;case 3:return -dy*scaleY;default:return dx*scaleX;}
    }
    float pointerY(float dx,float dy){
        switch(rotation){case 1:return -dx*scaleX;case 2:return -dy*scaleY;case 3:return dx*scaleX;default:return dy*scaleY;}
    }
}
