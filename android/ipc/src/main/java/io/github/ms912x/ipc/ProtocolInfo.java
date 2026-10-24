package io.github.ms912x.ipc;

import android.os.Parcel;
import android.os.Parcelable;

/** Shared, typed Binder wire value. Field order is part of protocol version 1. */
public final class ProtocolInfo implements Parcelable {
    public final int version;
    public final String buildDigest;
    public final int uid;
    public final int api;
    public ProtocolInfo(int version, String buildDigest, int uid, int api) {
        this.version = version;
        this.buildDigest = buildDigest;
        this.uid = uid;
        this.api = api;
    }
    private ProtocolInfo(Parcel in) { this(in.readInt(), in.readString(), in.readInt(), in.readInt()); }
    @Override public void writeToParcel(Parcel out, int flags) {
        out.writeInt(version);
        out.writeString(buildDigest);
        out.writeInt(uid);
        out.writeInt(api);
    }
    @Override public int describeContents() { return 0; }
    public static final Creator<ProtocolInfo> CREATOR = new Creator<ProtocolInfo>() {
        @Override public ProtocolInfo createFromParcel(Parcel in) { return new ProtocolInfo(in); }
        @Override public ProtocolInfo[] newArray(int size) { return new ProtocolInfo[size]; }
    };
}
