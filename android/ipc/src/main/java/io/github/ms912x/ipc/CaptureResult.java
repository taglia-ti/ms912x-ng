package io.github.ms912x.ipc;

import android.os.Parcel;
import android.os.Parcelable;
import android.graphics.Rect;

/** Shared, typed Binder wire value. Field order is part of protocol version 2. */
public final class CaptureResult implements Parcelable {
    public final long sessionId;
    public final long requestId;
    public final long generation;
    public final int code;
    public final int mirrorId;
    public final String backend;
    public final String detail;
    public final DisplaySourceInfo source;
    public final Rect contentBounds;
    public final int outputRotation;
    public CaptureResult(long sessionId, long requestId, long generation, int code, int mirrorId, String backend, String detail) {
        this(sessionId, requestId, generation, code, mirrorId, backend, detail, null, null, 0);
    }
    public CaptureResult(long sessionId, long requestId, long generation, int code, int mirrorId,
                         String backend, String detail, DisplaySourceInfo source, Rect bounds, int rotation) {
        this.sessionId = sessionId;
        this.requestId = requestId;
        this.generation = generation;
        this.code = code;
        this.mirrorId = mirrorId;
        this.backend = backend;
        this.detail = detail;
        this.source = source;
        this.contentBounds = bounds == null ? null : new Rect(bounds);
        this.outputRotation = rotation;
    }
    private CaptureResult(Parcel in) {
        this(in.readLong(), in.readLong(), in.readLong(), in.readInt(), in.readInt(), in.readString(), in.readString(),
                in.readTypedObject(DisplaySourceInfo.CREATOR), in.readTypedObject(Rect.CREATOR), in.readInt());
    }
    @Override public void writeToParcel(Parcel out, int flags) {
        out.writeLong(sessionId);
        out.writeLong(requestId);
        out.writeLong(generation);
        out.writeInt(code);
        out.writeInt(mirrorId);
        out.writeString(backend);
        out.writeString(detail);
        out.writeTypedObject(source, flags);
        out.writeTypedObject(contentBounds, flags);
        out.writeInt(outputRotation);
    }
    @Override public int describeContents() { return 0; }
    public static final Creator<CaptureResult> CREATOR = new Creator<CaptureResult>() {
        @Override public CaptureResult createFromParcel(Parcel in) { return new CaptureResult(in); }
        @Override public CaptureResult[] newArray(int size) { return new CaptureResult[size]; }
    };
}
