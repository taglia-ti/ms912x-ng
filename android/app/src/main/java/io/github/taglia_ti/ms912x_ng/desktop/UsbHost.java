package io.github.taglia_ti.ms912x_ng.desktop;

import android.app.PendingIntent;
import android.content.Context;
import android.hardware.usb.*;
import java.util.Collection;

/** Framework boundary shared by adapter probing, output, and deterministic fixtures. */
interface UsbHost {
    Collection<UsbDevice> devices();
    boolean hasPermission(UsbDevice device);
    void requestPermission(UsbDevice device, PendingIntent callback);
    Connection openDevice(UsbDevice device);

    interface Connection extends AutoCloseable {
        boolean claimInterface(UsbInterface iface, boolean force);
        void releaseInterface(UsbInterface iface);
        int controlTransfer(int type, int request, int value, int index,
                            byte[] data, int length, int timeout);
        int bulkTransfer(UsbEndpoint endpoint, byte[] data, int offset, int length, int timeout);
        @Override void close();
    }

    static UsbHost fromContext(Context context) {
        UsbManager manager = context.getSystemService(UsbManager.class);
        if (manager == null) return null;
        return new UsbHost() {
            public Collection<UsbDevice> devices() { return manager.getDeviceList().values(); }
            public boolean hasPermission(UsbDevice device) { return manager.hasPermission(device); }
            public void requestPermission(UsbDevice device, PendingIntent callback) {
                manager.requestPermission(device, callback);
            }
            public Connection openDevice(UsbDevice device) {
                UsbDeviceConnection connection = manager.openDevice(device);
                if (connection == null) return null;
                return new Connection() {
                    public boolean claimInterface(UsbInterface iface, boolean force) {
                        return connection.claimInterface(iface, force);
                    }
                    public void releaseInterface(UsbInterface iface) { connection.releaseInterface(iface); }
                    public int controlTransfer(int type, int request, int value, int index,
                                               byte[] data, int length, int timeout) {
                        return connection.controlTransfer(type, request, value, index, data, length, timeout);
                    }
                    public int bulkTransfer(UsbEndpoint endpoint, byte[] data, int offset,
                                            int length, int timeout) {
                        return connection.bulkTransfer(endpoint, data, offset, length, timeout);
                    }
                    public void close() { connection.close(); }
                };
            }
        };
    }
}
