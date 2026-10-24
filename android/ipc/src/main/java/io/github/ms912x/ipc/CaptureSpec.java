package io.github.ms912x.ipc;

import android.os.Parcel;
import android.os.Parcelable;

/** Shared, typed Binder wire value. Field order is part of protocol version 1. */
public final class CaptureSpec implements Parcelable {
    public final long sessionId;
    public final long requestId;
    public final long generation;
    public final int sourceId;
    public final String sourceIdentity;
    public final int outputWidth;
    public final int outputHeight;
    public final int modeId;
    public final int rotation;
    public CaptureSpec(long sessionId, long requestId, long generation, int sourceId, String sourceIdentity, int outputWidth, int outputHeight, int modeId, int rotation) {
        this.sessionId = sessionId;
        this.requestId = requestId;
        this.generation = generation;
        this.sourceId = sourceId;
        this.sourceIdentity = sourceIdentity;
        this.outputWidth = outputWidth;
        this.outputHeight = outputHeight;
        this.modeId = modeId;
        this.rotation = rotation;
    }
    private CaptureSpec(Parcel in) { this(in.readLong(), in.readLong(), in.readLong(), in.readInt(), in.readString(), in.readInt(), in.readInt(), in.readInt(), in.readInt()); }
    @Override public void writeToParcel(Parcel out, int flags) {
        out.writeLong(sessionId);
        out.writeLong(requestId);
        out.writeLong(generation);
        out.writeInt(sourceId);
        out.writeString(sourceIdentity);
        out.writeInt(outputWidth);
        out.writeInt(outputHeight);
        out.writeInt(modeId);
        out.writeInt(rotation);
    }
    @Override public int describeContents() { return 0; }
    public static final Creator<CaptureSpec> CREATOR = new Creator<CaptureSpec>() {
        @Override public CaptureSpec createFromParcel(Parcel in) { return new CaptureSpec(in); }
        @Override public CaptureSpec[] newArray(int size) { return new CaptureSpec[size]; }
    };
}
