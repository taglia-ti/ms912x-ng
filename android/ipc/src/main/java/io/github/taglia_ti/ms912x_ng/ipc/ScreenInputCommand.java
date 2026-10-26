package io.github.taglia_ti.ms912x_ng.ipc;

import android.os.Parcel;
import android.os.Parcelable;

/** Typed, bounded input event scoped to one helper-created display generation. */
public final class ScreenInputCommand implements Parcelable {
    public static final int MOVE=1, CLICK=2, SCROLL=3, DRAG_START=4, DRAG_END=5, BACK=6, HOME=7, CANCEL=8;
    public final long sessionId, generation, sequence;
    public final int type, button;
    public final float x, y;

    public ScreenInputCommand(long sessionId,long generation,long sequence,int type,float x,float y,int button) {
        this.sessionId=sessionId;this.generation=generation;this.sequence=sequence;this.type=type;
        this.x=x;this.y=y;this.button=button;
    }
    private ScreenInputCommand(Parcel in) {
        this(in.readLong(),in.readLong(),in.readLong(),in.readInt(),in.readFloat(),in.readFloat(),in.readInt());
    }
    @Override public void writeToParcel(Parcel out,int flags) {
        out.writeLong(sessionId);out.writeLong(generation);out.writeLong(sequence);out.writeInt(type);
        out.writeFloat(x);out.writeFloat(y);out.writeInt(button);
    }
    @Override public int describeContents(){return 0;}
    public static final Creator<ScreenInputCommand> CREATOR=new Creator<ScreenInputCommand>() {
        @Override public ScreenInputCommand createFromParcel(Parcel in){return new ScreenInputCommand(in);}
        @Override public ScreenInputCommand[] newArray(int size){return new ScreenInputCommand[size];}
    };
}
