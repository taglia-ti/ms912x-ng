package io.github.taglia_ti.ms912x_ng.ipc;

import android.os.Parcel;
import android.os.Parcelable;

/** Shared, typed Binder wire value. Field order is part of protocol version 1. */
public final class DisplaySourceInfo implements Parcelable {
    public final int displayId;
    public final String identity;
    public final String name;
    public final int width;
    public final int height;
    public final int rotation;
    public final float refreshHz;
    public final int layerStack;
    public DisplaySourceInfo(int displayId, String identity, String name, int width, int height, int rotation, float refreshHz, int layerStack) {
        this.displayId = displayId;
        this.identity = identity;
        this.name = name;
        this.width = width;
        this.height = height;
        this.rotation = rotation;
        this.refreshHz = refreshHz;
        this.layerStack = layerStack;
    }
    private DisplaySourceInfo(Parcel in) { this(in.readInt(), in.readString(), in.readString(), in.readInt(), in.readInt(), in.readInt(), in.readFloat(), in.readInt()); }
    @Override public void writeToParcel(Parcel out, int flags) {
        out.writeInt(displayId);
        out.writeString(identity);
        out.writeString(name);
        out.writeInt(width);
        out.writeInt(height);
        out.writeInt(rotation);
        out.writeFloat(refreshHz);
        out.writeInt(layerStack);
    }
    @Override public int describeContents() { return 0; }
    public static final Creator<DisplaySourceInfo> CREATOR = new Creator<DisplaySourceInfo>() {
        @Override public DisplaySourceInfo createFromParcel(Parcel in) { return new DisplaySourceInfo(in); }
        @Override public DisplaySourceInfo[] newArray(int size) { return new DisplaySourceInfo[size]; }
    };
}
