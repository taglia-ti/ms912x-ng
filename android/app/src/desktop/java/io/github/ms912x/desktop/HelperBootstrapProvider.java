package io.github.ms912x.desktop;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;

/** Bootstrap only: no file access, shell commands, or pixel operations are exported. */
public final class HelperBootstrapProvider extends ContentProvider {
    @Override public boolean onCreate() { return true; }
    @Override public Bundle call(String operation, String arg, Bundle extras) {
        return HelperEnrollment.call(getContext(), Binder.getCallingUid(), operation, extras);
    }
    @Override public Cursor query(Uri uri, String[] projection, String selection, String[] args, String order) {
        throw new UnsupportedOperationException("Bootstrap calls only");
    }
    @Override public String getType(Uri uri) { return null; }
    @Override public Uri insert(Uri uri, ContentValues values) { throw new UnsupportedOperationException(); }
    @Override public int delete(Uri uri, String where, String[] args) { throw new UnsupportedOperationException(); }
    @Override public int update(Uri uri, ContentValues values, String where, String[] args) { throw new UnsupportedOperationException(); }
}
