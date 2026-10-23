package io.github.ms912x.desktop;

import android.app.PendingIntent;
import android.hardware.usb.*;
import android.os.Parcel;
import java.io.ByteArrayOutputStream;
import java.util.*;

/** Device descriptors and register/transfer responses; never opens a physical USB device. */
final class AndroidFixtures {
    interface Action { void run() throws Exception; }

    static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    static void expect(Class<? extends Throwable> type, Action action) throws Exception {
        try { action.run(); }
        catch (Throwable failure) {
            if (type.isInstance(failure)) return;
            throw new AssertionError("Expected " + type.getName() + ", got " + failure, failure);
        }
        throw new AssertionError("Expected " + type.getName());
    }

    static UsbEndpoint endpoint(int address, int attributes) {
        Parcel p = Parcel.obtain();
        try {
            p.writeInt(address); p.writeInt(attributes); p.writeInt(512); p.writeInt(0);
            p.setDataPosition(0);
            return UsbEndpoint.CREATOR.createFromParcel(p);
        } finally { p.recycle(); }
    }

    static UsbInterface iface(int id, int cls, int sub, int protocol, UsbEndpoint... endpoints) {
        Parcel p = Parcel.obtain();
        try {
            p.writeInt(id); p.writeInt(0); p.writeString("fixture interface");
            p.writeInt(cls); p.writeInt(sub); p.writeInt(protocol); p.writeParcelableArray(endpoints, 0);
            p.setDataPosition(0);
            return UsbInterface.CREATOR.createFromParcel(p);
        } finally { p.recycle(); }
    }

    static UsbDevice device(int vendor, int product, UsbInterface... interfaces) {
        Parcel p = Parcel.obtain();
        try {
            p.writeInt(1); p.writeString("fixture configuration"); p.writeInt(0x80); p.writeInt(250);
            p.writeParcelableArray(interfaces, 0); p.setDataPosition(0);
            UsbConfiguration config = UsbConfiguration.CREATOR.createFromParcel(p);
            p.recycle(); p = Parcel.obtain();
            p.writeString("/dev/bus/usb/099/" + String.format(Locale.US, "%03d", product & 255));
            p.writeInt(vendor); p.writeInt(product); p.writeInt(0); p.writeInt(0); p.writeInt(0);
            p.writeString("Fixture"); p.writeString("MS912x fixture"); p.writeString("1.0");
            p.writeStrongBinder(new android.os.Binder()); p.writeParcelableArray(new UsbConfiguration[]{config}, 0);
            // Public Parcelable layout includes optional capability flags on supported API levels.
            for (int i = 0; i < 16; ++i) p.writeInt(0);
            p.setDataPosition(0);
            return UsbDevice.CREATOR.createFromParcel(p);
        } finally { p.recycle(); }
    }

    static DisplayModeOption mode640() {
        return NativeCapture.modeCatalog().stream().filter(m -> m.id == 0x40).findFirst().get();
    }

    static UsbDevice adapter() {
        return device(0x534d, 0x6021, iface(0, 255, 0, 0, endpoint(4, 2)));
    }

    static UsbDevice compositeAdapter() {
        return device(0x534d, 0x6021,
                iface(0, 3, 0, 0, endpoint(0x81, 3)),
                iface(1, 1, 1, 0), iface(2, 1, 2, 0, endpoint(3, 1)),
                iface(3, 255, 0, 0, endpoint(4, 2)),
                iface(4, 8, 6, 80, endpoint(0x82, 2), endpoint(2, 2)));
    }

    static byte[] edid(DisplayModeOption mode) {
        byte[] e = new byte[128];
        for (int i = 1; i < 7; ++i) e[i] = (byte) 255;
        e[18] = 1; e[19] = 4;
        for (int i = 38; i < 54; ++i) e[i] = 1;
        int p = 54, hblank = mode.htotal - mode.width, vblank = mode.vtotal - mode.height;
        int hfront = hblank - mode.hoffset, vfront = vblank - mode.voffset;
        int clock = mode.clockKHz / 10;
        e[p] = (byte) clock; e[p + 1] = (byte) (clock >>> 8);
        e[p + 2] = (byte) mode.width; e[p + 3] = (byte) hblank;
        e[p + 4] = (byte) ((mode.width >>> 8) << 4 | hblank >>> 8);
        e[p + 5] = (byte) mode.height; e[p + 6] = (byte) vblank;
        e[p + 7] = (byte) ((mode.height >>> 8) << 4 | vblank >>> 8);
        e[p + 8] = (byte) hfront; e[p + 9] = (byte) mode.hsyncWidth;
        e[p + 10] = (byte) ((vfront & 15) << 4 | mode.vsyncWidth & 15);
        e[p + 11] = (byte) ((hfront >>> 8) << 6 | (mode.hsyncWidth >>> 8) << 4
                | (vfront >>> 4) << 2 | mode.vsyncWidth >>> 4);
        e[p + 17] = (byte) (0x18 | mode.polarity & 6);
        e[75] = (byte) 0xfc;
        byte[] name = "Fixture LCD\n".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        System.arraycopy(name, 0, e, 77, name.length);
        checksum(e, 0);
        return e;
    }

    static void checksum(byte[] e, int block) {
        int sum = 0, base = block * 128;
        for (int i = 0; i < 127; ++i) sum += e[base + i] & 255;
        e[base + 127] = (byte) -sum;
    }

    static final class Connection implements UsbHost.Connection {
        byte[] edid;
        volatile long bulkBytes;
        int sdram = 1, calls, failAt = -1, failResult = -1, address, releases, closes, bulkCalls;
        int bulkResult = Integer.MAX_VALUE;
        boolean claimed = true, claimThrows;
        int claimFailAt = -1, claimThrowAt = -1, releaseThrowAt = -1;
        final List<Integer> claims = new ArrayList<>(), released = new ArrayList<>();
        final List<Boolean> forcedClaims = new ArrayList<>();
        final Set<Integer> owned = new HashSet<>();
        final List<byte[]> reports = new ArrayList<>();
        final ByteArrayOutputStream bulk = new ByteArrayOutputStream();
        Connection(byte[] edid) { this.edid = edid; }
        public boolean claimInterface(UsbInterface iface, boolean force) {
            claims.add(iface.getId()); forcedClaims.add(force);
            if (claimThrows || claims.size() == claimThrowAt) throw new SecurityException("Permission revoked fixture");
            if (!claimed || claims.size() == claimFailAt) return false;
            require(owned.add(iface.getId()), "Interface claimed only once");
            return true;
        }
        public void releaseInterface(UsbInterface iface) {
            ++releases; released.add(iface.getId());
            require(owned.remove(iface.getId()), "Release only owned interfaces");
            if (releases == releaseThrowAt) throw new IllegalStateException("Release failure fixture");
        }
        public void close() { ++closes; }
        public int controlTransfer(int type, int request, int value, int index,
                                   byte[] data, int length, int timeout) {
            require(length == 8 && value == 0x0300 && index == 0 && timeout == 1000, "Control envelope");
            require(owned.contains(index), "Control recipient must be claimed");
            if (++calls == failAt) return failResult;
            if (type == 0x21) {
                require(request == 9, "SET_REPORT");
                reports.add(data.clone());
                if ((data[0] & 255) == 0xb5) address = (data[1] & 255) << 8 | data[2] & 255;
                else require((data[0] & 255) == 0xa6, "Command marker");
            } else {
                require(type == 0xa1 && request == 1, "GET_REPORT");
                Arrays.fill(data, (byte) 0);
                data[3] = address == 0x30 ? (byte) sdram
                        : address >= 0xc000 && address - 0xc000 < edid.length ? edid[address - 0xc000] : 0;
            }
            return length;
        }
        public int bulkTransfer(UsbEndpoint endpoint, byte[] data, int offset, int length, int timeout) {
            require(endpoint.getAddress() == 4 && timeout > 0 && timeout <= 250, "Bulk endpoint/timeout");
            ++bulkCalls;
            if (bulkResult <= 0) return bulkResult;
            int n = Math.min(length, bulkResult);
            // Retain enough bytes for packet assertions without recording an unbounded screencast.
            int retained = Math.min(n, Math.max(0, 8 * 1024 * 1024 - bulk.size()));
            bulk.write(data, offset, retained);
            bulkBytes += n;
            return n;
        }
        List<byte[]> commands() {
            List<byte[]> result = new ArrayList<>();
            for (byte[] report : reports) if ((report[0] & 255) == 0xa6) result.add(report);
            return result;
        }
    }

    static final class Host implements UsbHost {
        final List<UsbDevice> devices = new ArrayList<>();
        AndroidFixtures.Connection connection;
        boolean permission = true, openNull, requestFails;
        PendingIntent callback;
        int opens, requests;
        Host(AndroidFixtures.Connection connection) { this.connection = connection; }
        public Collection<UsbDevice> devices() { return devices; }
        public boolean hasPermission(UsbDevice device) { return permission; }
        public void requestPermission(UsbDevice device, PendingIntent callback) {
            ++requests;
            if (requestFails) throw new IllegalStateException("permission fixture");
            this.callback = callback;
        }
        public UsbHost.Connection openDevice(UsbDevice device) { ++opens; return openNull ? null : connection; }
    }

    private AndroidFixtures() {}
}
