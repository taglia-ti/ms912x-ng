package io.github.taglia_ti.ms912x_ng.desktop;

/** An evaluated source identity and capability, independent of dropdown position. */
final class SourceCapability {
    final int id,width,height,rotation;
    final String identity,name,reason;
    final boolean capture;
    SourceCapability(int id,String identity,String name,int width,int height,int rotation,boolean capture,String reason){
        this.id=id;this.identity=identity;this.name=name;this.width=width;this.height=height;
        this.rotation=rotation;this.capture=capture;this.reason=reason;
    }
}
