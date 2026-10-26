package io.github.taglia_ti.ms912x_ng.ipc;

import android.os.Parcel;
import android.os.Parcelable;

/** Authenticated router state; device values are opaque identities, not array indices. */
public final class MouseSharingResult implements Parcelable {
    public static final int OFF=0,ACTIVE=1,UNAVAILABLE=2,FAILED=3;
    public final long sessionId,generation,requestId;
    public final int state,side;
    public final String message;
    public final String[] devices;
    public MouseSharingResult(long session,long generation,long request,int state,int side,String message,String[] devices){
        this.sessionId=session;this.generation=generation;this.requestId=request;this.state=state;this.side=side;
        this.message=message;this.devices=devices==null?new String[0]:devices.clone();
    }
    private MouseSharingResult(Parcel in){this(in.readLong(),in.readLong(),in.readLong(),in.readInt(),in.readInt(),in.readString(),in.createStringArray());}
    @Override public void writeToParcel(Parcel out,int flags){out.writeLong(sessionId);out.writeLong(generation);out.writeLong(requestId);out.writeInt(state);out.writeInt(side);out.writeString(message);out.writeStringArray(devices);}
    @Override public int describeContents(){return 0;}
    public static final Creator<MouseSharingResult> CREATOR=new Creator<MouseSharingResult>(){
        public MouseSharingResult createFromParcel(Parcel in){return new MouseSharingResult(in);}
        public MouseSharingResult[] newArray(int size){return new MouseSharingResult[size];}
    };
}
