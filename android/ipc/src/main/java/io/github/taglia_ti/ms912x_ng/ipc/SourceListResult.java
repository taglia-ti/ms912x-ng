package io.github.taglia_ti.ms912x_ng.ipc;

import android.os.Parcel;
import android.os.Parcelable;

/** Correlated asynchronous inventory result. No capture or pixel ownership crosses this value. */
public final class SourceListResult implements Parcelable {
    public final long sessionId, requestId;
    public final int code;
    public final String detail;
    public final DisplaySourceInfo[] sources;
    public SourceListResult(long sessionId, long requestId, int code, String detail, DisplaySourceInfo[] sources) {
        this.sessionId = sessionId; this.requestId = requestId; this.code = code; this.detail = detail;
        this.sources = sources == null ? new DisplaySourceInfo[0] : sources.clone();
    }
    private SourceListResult(Parcel in) {
        this(in.readLong(), in.readLong(), in.readInt(), in.readString(), in.createTypedArray(DisplaySourceInfo.CREATOR));
    }
    @Override public void writeToParcel(Parcel out, int flags) {
        out.writeLong(sessionId); out.writeLong(requestId); out.writeInt(code); out.writeString(detail);
        out.writeTypedArray(sources, flags);
    }
    @Override public int describeContents() { return 0; }
    public static final Creator<SourceListResult> CREATOR = new Creator<SourceListResult>() {
        @Override public SourceListResult createFromParcel(Parcel in) { return new SourceListResult(in); }
        @Override public SourceListResult[] newArray(int size) { return new SourceListResult[size]; }
    };
}
