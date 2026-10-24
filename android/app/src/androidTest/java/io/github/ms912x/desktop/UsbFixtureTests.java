package io.github.ms912x.desktop;

import android.hardware.usb.*;
import java.io.*;
import java.nio.ByteBuffer;
import java.util.*;
import java.util.concurrent.atomic.AtomicLong;
import static io.github.ms912x.desktop.AndroidFixtures.*;

final class UsbFixtureTests {
    static void bulk() throws Exception {
        ByteBuffer source = ByteBuffer.allocateDirect(40_003);
        for (int i = 0; i < source.capacity(); ++i) source.put((byte) (i * 31));
        source.position(3); source.limit(40_001);
        byte[] expected = new byte[source.remaining()]; source.duplicate().get(expected);
        ByteArrayOutputStream received = new ByteArrayOutputStream();
        List<Integer> lengths = new ArrayList<>();
        byte[][] reusableChunk = {null};
        UsbBulkWriter writer = new UsbBulkWriter((data, offset, length, timeout) -> {
            require(length <= 16384 && timeout == 250, "Bulk chunk limits");
            if (reusableChunk[0] == null) reusableChunk[0] = data;
            require(data == reusableChunk[0], "Writer reuses its chunk buffer across transfers");
            lengths.add(length);
            int n = Math.min(997, length); received.write(data, offset, n); return n;
        }, () -> 0);
        require(writer.writePacket(source) == expected.length, "All short writes accounted");
        source.position(3);
        require(writer.writePacket(source) == expected.length, "Second packet uses the same writer");
        byte[] twice = new byte[expected.length * 2];
        System.arraycopy(expected, 0, twice, 0, expected.length);
        System.arraycopy(expected, 0, twice, expected.length, expected.length);
        require(Arrays.equals(received.toByteArray(), twice), "Reused scratch buffer preserves each packet");
        require(Arrays.equals(received.toByteArray(), twice), "Chunk boundaries preserve bytes");
        require(source.position() == 3 && source.limit() == 40_001, "Caller packet unchanged");
        require(lengths.size() > 3, "Short-write fixture exercised");
        expect(IllegalArgumentException.class, () -> writer.writePacket(null));
        expect(IllegalArgumentException.class, () -> writer.writePacket(ByteBuffer.allocate(4)));
        expect(IllegalArgumentException.class, () -> writer.writePacket(ByteBuffer.allocateDirect(0)));
        expect(IllegalArgumentException.class, () -> new UsbBulkWriter((UsbBulkWriter.Transfer) null, () -> 0));
        expect(IllegalArgumentException.class, () -> new UsbBulkWriter((UsbHost.Connection) null, null));
        for (int result : new int[]{-1, 0, 5}) {
            UsbBulkWriter failing = new UsbBulkWriter((b, o, n, t) -> result, () -> 0);
            expect(IOException.class, () -> failing.writePacket(ByteBuffer.allocateDirect(4)));
        }
        AtomicLong now = new AtomicLong();
        int[] calls = {0};
        UsbBulkWriter deadline = new UsbBulkWriter((b, o, n, t) -> {
            ++calls[0]; now.addAndGet(5000); return 1;
        }, now::get);
        expect(IOException.class, () -> deadline.writePacket(ByteBuffer.allocateDirect(100)));
        require(calls[0] == 2, "Deadline applies within a short-write chunk");
        Thread.currentThread().interrupt();
        try { expect(InterruptedIOException.class, () -> writer.writePacket(source)); }
        finally { Thread.interrupted(); }
        require(Arrays.equals(received.toByteArray(), twice), "Cancelled write submitted no bytes");
    }

    static void descriptors() throws Exception {
        UsbDevice device = adapter();
        require(device.getVendorId() == 0x534d && device.getInterfaceCount() == 1, "Parcelable fixture descriptor");
        require(UsbAdapterSession.isSupportedDevice(device), "Supported adapter");
        require(!UsbAdapterSession.isSupportedDevice(null), "Null adapter");
        for (int product : new int[]{0x6021, 0x0821})
            require(UsbAdapterSession.isSupportedDevice(device(0x534d, product)), "USB2 product");
        for (int product : new int[]{0x9132, 0x9133, 0x9135}) {
            require(UsbAdapterSession.isSupportedDevice(device(0x345f, product)), "USB3 product");
            require(!UsbAdapterSession.isSupportedDevice(device(0x534d, product)), "USB3 wrong vendor");
        }
        require(!UsbAdapterSession.isSupportedDevice(device(1, 0x6021)), "Wrong vendor");
        require(!UsbAdapterSession.isSupportedDevice(device(0x534d, 1)), "Wrong product");
        Host host = new Host(new Connection(edid(NativeAdapter.modeCatalog().get(0))));
        expect(IllegalArgumentException.class, () -> UsbAdapterSession.open(host, null));
        expect(SecurityException.class, () -> UsbAdapterSession.open((UsbHost) null, device));
        host.permission = false;
        expect(SecurityException.class, () -> UsbAdapterSession.open(host, device));
        host.permission = true;
        List<UsbDevice> invalid = Arrays.asList(
                device(0x534d, 0x6021),
                device(0x534d, 0x6021, iface(0, 3, 0, 0, endpoint(4, 2))),
                device(0x534d, 0x6021, iface(0, 255, 1, 0, endpoint(4, 2))),
                device(0x534d, 0x6021, iface(0, 255, 0, 1, endpoint(4, 2))),
                device(0x534d, 0x6021, iface(0, 255, 0, 0, endpoint(0x84, 2), endpoint(4, 3))),
                device(0x534d, 0x6021, iface(0, 255, 0, 0, endpoint(4, 2), endpoint(4, 2))),
                device(0x534d, 0x6021, iface(0, 255, 0, 0, endpoint(4, 2)),
                        iface(1, 255, 0, 0, endpoint(4, 2))));
        for (UsbDevice bad : invalid) expect(IllegalStateException.class, () -> UsbAdapterSession.open(host, bad));
        require(host.opens == 0, "Invalid descriptors never open hardware");
        host.openNull = true;
        expect(IllegalStateException.class, () -> UsbAdapterSession.open(host, device));
        host.openNull = false; host.connection.claimed = false;
        expect(IllegalStateException.class, () -> UsbAdapterSession.open(host, device));
        require(host.connection.closes == 1 && host.connection.releases == 0, "Claim failure closes connection");
        host.connection.claimThrows = true;
        expect(SecurityException.class, () -> UsbAdapterSession.open(host, device));
        require(host.connection.closes == 2, "Permission revoked during claim closes connection");
        compositeInterfaces();
    }

    private static void compositeInterfaces() throws Exception {
        UsbDevice device = compositeAdapter();
        Connection io = new Connection(edid(mode640()));
        try (UsbAdapterSession session = UsbAdapterSession.open(new Host(io), device)) {
            require(io.claims.equals(Arrays.asList(3, 0)), "Composite video and control claims");
            require(io.forcedClaims.equals(Arrays.asList(false, true)), "Only non-boot HID may detach its kernel driver");
            require(!session.probeCapabilities(device).modes.isEmpty(), "Composite EDID control recipient is owned");
            session.configureAndSend640x480Pattern();
        }
        require(io.released.equals(Arrays.asList(0, 3)) && io.closes == 1,
                "Release control and video once; audio/storage never claimed");
        for (boolean throwing : new boolean[]{false, true}) {
            Connection failure = new Connection(edid(mode640()));
            if (throwing) failure.claimThrowAt = 2; else failure.claimFailAt = 2;
            expect(throwing ? SecurityException.class : IllegalStateException.class,
                    () -> UsbAdapterSession.open(new Host(failure), device));
            require(failure.released.equals(Collections.singletonList(3)) && failure.closes == 1,
                    "Failed control claim releases the acquired video interface");
        }
        for (int release : new int[]{1, 2}) {
            Connection failure = new Connection(edid(mode640()));
            UsbAdapterSession session = UsbAdapterSession.open(new Host(failure), device);
            failure.releaseThrowAt = release;
            expect(IllegalStateException.class, session::close);
            session.close();
            require(failure.released.equals(Arrays.asList(0, 3)) && failure.closes == 1,
                    "Both interfaces and connection cleaned despite release failure");
        }
        Connection cleanup = new Connection(edid(mode640()));
        cleanup.claimThrowAt = 2; cleanup.releaseThrowAt = 1;
        expect(SecurityException.class, () -> UsbAdapterSession.open(new Host(cleanup), device));
        require(cleanup.closes == 1, "Claim exception survives cleanup failure");
        for (UsbDevice invalid : new UsbDevice[]{
                device(0x534d, 0x6021, iface(3, 255, 0, 0, endpoint(4, 2))),
                device(0x534d, 0x6021, iface(0, 3, 1, 1), iface(3, 255, 0, 0, endpoint(4, 2))),
                device(0x534d, 0x6021, iface(0, 1, 1, 0), iface(3, 255, 0, 0, endpoint(4, 2))),
                device(0x534d, 0x6021, iface(0, 3, 0, 0), iface(0, 3, 0, 0),
                        iface(3, 255, 0, 0, endpoint(4, 2)))}) {
            Host rejected = new Host(new Connection(edid(mode640())));
            expect(IllegalStateException.class, () -> UsbAdapterSession.open(rejected, invalid));
            require(rejected.opens == 0, "Malformed control interface never opens hardware");
        }
    }

    static void protocol() throws Exception {
        customTimings();
        require(NativeAdapter.flashReadRequest(-1) == null
                && NativeAdapter.flashReadRequest(0x1000000) == null
                && NativeAdapter.readEdidBlock(null, 0) == null, "Flash and EDID JNI boundaries");
        require(NativeAdapter.modePlan(1, 3, 480) == null
                && NativeAdapter.modePlan(256, 640, 480) == null
                && NativeAdapter.modePlan(1, 640, 0) == null, "Mode plan boundaries");
        require(NativeAdapter.controlWrite(1, new byte[5]) == null
                && NativeAdapter.controlWrite(256, new byte[6]) == null
                && NativeAdapter.registerRead(-1) == null
                && NativeAdapter.registerRead(65536) == null, "Control report boundaries");
        for (int value = -1; value < 8; ++value)
            require(NativeAdapter.supportsSdram(value) == (value >= 0 && value <= 3),
                    "Linux SDRAM interpretation");
        UsbDevice device = adapter();
        List<DisplayModeOption> modes = NativeAdapter.modeCatalog();
        require(modes.size() == 29, "All catalog modes exercised");
        for (DisplayModeOption mode : modes) {
            Connection io = new Connection(edid(mode)); Host host = new Host(io);
            try (UsbAdapterSession session = UsbAdapterSession.open(host, device)) {
                AdapterCapabilities caps = session.probeCapabilities(device);
                require(caps.monitorName.equals("Fixture LCD"), "EDID monitor name");
                require(caps.modes.stream().anyMatch(m -> m.id == mode.id), "EDID/catalog mode");
                require(caps.preferredMode != null && caps.preferredMode.id == mode.id
                        && caps.initialMode.id == mode.id, "USB probe carries EDID preference into initial timing");
                expect(UnsupportedOperationException.class, () -> caps.modes.clear());
                require(io.commands().isEmpty(), "Probe never enables or disables video");
                session.configureMode(mode);
                List<byte[]> commands = io.commands();
                byte[][] expected = {
                    {(byte)0xa6, 7, 1, 2, 0, 0, 0, 0}, {(byte)0xa6, 4, 0, 0, 0, 0, 0, 0},
                    {(byte)0xa6, 3, 3, 0, 0, 0, 0, 0},
                    {(byte)0xa6, 1, (byte)(mode.width >> 8), (byte)mode.width,
                        (byte)(mode.height >> 8), (byte)mode.height, 0x22, 0},
                    {(byte)0xa6, 2, (byte)mode.id, 1, (byte)(mode.width >> 8), (byte)mode.width,
                        (byte)(mode.height >> 8), (byte)mode.height},
                    {(byte)0xa6, 4, 1, 0, 0, 0, 0, 0}, {(byte)0xa6, 5, 1, 0, 0, 0, 0, 0}};
                require(commands.size() == expected.length, "Setup command count");
                for (int i = 0; i < expected.length; ++i)
                    require(Arrays.equals(commands.get(i), expected[i]), "Setup bytes/order for " + mode + " step " + i);
                io.bulkResult = 97;
                ByteBuffer packet = NativeTestPattern.create640x480Packet();
                require(session.sendFrame(packet) == packet.remaining(), "Shared mode-3 packet transferred");
                require(io.bulk.size() == packet.remaining(), "Full payload including markers");
            }
            require(io.releases == 1 && io.closes == 1, "Release once");
            require(io.commands().size() == 10, "Teardown disables video/power once");
        }
        Connection io = new Connection(edid(mode640()));
        UsbAdapterSession session = UsbAdapterSession.open(new Host(io), device);
        expect(IOException.class, () -> session.configureMode(null));
        session.configureAndSend640x480Pattern();
        session.close(); session.close();
        expect(IOException.class, () -> session.sendFrame(ByteBuffer.allocateDirect(4)));
        expect(InterruptedIOException.class, () -> session.probeCapabilities(device));
        require(io.closes == 1 && io.releases == 1, "Idempotent close");
    }

    private static void put16(byte[] bytes, int offset, int value) {
        bytes[offset] = (byte)value; bytes[offset + 1] = (byte)(value >> 8);
    }

    private static void customTimings() throws Exception {
        DisplayModeOption mode = mode640();
        for (int family : new int[]{0x13, 0x16}) {
            Connection io = new Connection(edid(mode)); io.chipFamily = family;
            io.flash = new byte[80];
            System.arraycopy("modify2".getBytes(java.nio.charset.StandardCharsets.US_ASCII), 0, io.flash, 0, 7);
            for (int n = 0; n < 2; ++n) {
                int at = 16 + 32 * n;
                io.flash[at] = (byte)(0x90 + n); io.flash[at + 1] = (byte)mode.polarity;
                int[] values = {mode.htotal, mode.vtotal, mode.width, mode.height,
                        mode.clockKHz / 10, mode.refreshHz * 100, mode.hoffset, mode.voffset,
                        mode.hsyncWidth, mode.vsyncWidth};
                for (int i = 0; i < values.length; ++i) put16(io.flash, at + 2 + i * 2, values[i]);
            }
            // An invalid record must not hide the other valid record or the stock catalog.
            put16(io.flash, 16 + 18, 0);
            try (UsbAdapterSession session = UsbAdapterSession.open(new Host(io), adapter())) {
                AdapterCapabilities caps = session.probeCapabilities(adapter());
                require(caps.modes.stream().anyMatch(m -> m.id == 0x91), "Valid custom flash timing");
                require(caps.modes.stream().noneMatch(m -> m.id == 0x90), "Reject malformed custom timing");
                require(caps.modes.stream().anyMatch(m -> m.id == mode.id), "Retain stock timing");
                DisplayModeOption custom = caps.modes.stream().filter(m -> m.id == 0x91).findFirst().get();
                require(custom.width == mode.width && custom.clockKHz == mode.clockKHz,
                        "Linux custom timing decoder fields");
                session.configureMode(custom);
                require(io.commands().stream().anyMatch(c -> c[1] == 2 && (c[2] & 255) == 0x91),
                        "Program custom adapter timing ID");
                int calls = io.calls;
                byte[] first = NativeAdapter.readEdidBlock(session, 0);
                require(first != null && io.calls == calls + 256, "Native EDID byte reader");
            }
        }
    }

    static void failures() throws Exception {
        UsbDevice device = adapter(); DisplayModeOption mode = NativeAdapter.modeCatalog().get(0);
        byte[] headerQuirk = edid(mode); headerQuirk[1] = 0;
        try (UsbAdapterSession session = UsbAdapterSession.open(
                new Host(new Connection(headerQuirk)), device)) {
            require(!session.probeCapabilities(device).modes.isEmpty(), "Known base-header quirk repaired through JNI");
        }
        byte[] headerMalformed = headerQuirk.clone(); headerMalformed[20] ^= 1;
        require(!NativeAdapter.repairEdidBaseHeader(headerMalformed) && headerMalformed[1] == 0,
                "Header repair refuses additional corruption without changing input");
        require(!NativeAdapter.repairEdidBaseHeader(null)
                && !NativeAdapter.repairEdidBaseHeader(new byte[127])
                && !NativeAdapter.repairEdidBaseHeader(edid(mode)), "Native header-repair boundaries");
        Connection baseline = new Connection(edid(mode));
        try (UsbAdapterSession session = UsbAdapterSession.open(new Host(baseline), device)) { session.configureMode(mode); }
        int calls = baseline.calls - 3; // The final three writes belong to teardown.
        for (int failAt = 1; failAt <= calls; ++failAt) {
            Connection io = new Connection(edid(mode)); io.failAt = failAt; io.failResult = failAt % 2 == 0 ? 7 : -1;
            try (UsbAdapterSession session = UsbAdapterSession.open(new Host(io), device)) {
                expect(IOException.class, () -> session.configureMode(mode));
            }
            require(io.closes == 1 && io.releases == 1, "Failure cleanup at control transfer " + failAt);
        }
        List<byte[]> malformed = new ArrayList<>();
        byte[] bad = edid(mode); bad[0] = 1; checksum(bad, 0); malformed.add(bad);
        bad = edid(mode); bad[20] ^= 1; malformed.add(bad);
        bad = edid(mode); bad[126] = 9; checksum(bad, 0); malformed.add(bad);
        bad = new byte[256]; System.arraycopy(edid(mode), 0, bad, 0, 128);
        bad[126] = 1; checksum(bad, 0); bad[128] = 2; malformed.add(bad);
        bad = new byte[128]; for (int i = 1; i < 7; ++i) bad[i] = (byte)255;
        checksum(bad, 0); malformed.add(bad);
        for (byte[] e : malformed) {
            Connection io = new Connection(e);
            try (UsbAdapterSession session = UsbAdapterSession.open(new Host(io), device)) {
                expect(IOException.class, () -> session.probeCapabilities(device));
                require(io.commands().isEmpty(), "Invalid EDID cannot program video");
            }
        }
        Connection io = new Connection(edid(mode)); io.sdram = 4;
        try (UsbAdapterSession session = UsbAdapterSession.open(new Host(io), device)) {
            expect(IOException.class, () -> session.configureMode(mode));
            expect(IOException.class, () -> session.probeCapabilities(device));
        }
        Connection teardown = new Connection(edid(mode));
        UsbAdapterSession stopping = UsbAdapterSession.open(new Host(teardown), device);
        stopping.configureMode(mode); teardown.failAt = teardown.calls + 1; stopping.close();
        require(teardown.releases == 1 && teardown.closes == 1, "Teardown write failure still releases");
        for (int sdram : new int[]{0, 2, 3}) {
            Connection supported = new Connection(edid(mode)); supported.sdram = sdram;
            try (UsbAdapterSession session = UsbAdapterSession.open(new Host(supported), device)) {
                require(!session.probeCapabilities(device).modes.isEmpty(), "Supported SDRAM");
            }
        }
        byte[] extended = Arrays.copyOf(edid(mode), 256); extended[126] = 1; checksum(extended, 0);
        extended[128] = 2; extended[129] = 3; extended[130] = 6; extended[132] = 0x41; extended[133] = 4;
        checksum(extended, 1);
        Connection extension = new Connection(extended);
        try (UsbAdapterSession session = UsbAdapterSession.open(new Host(extension), device)) {
            require(session.probeCapabilities(device).modes.stream().anyMatch(m -> m.id == 0x4f), "CTA block read and validated");
            expect(IOException.class, () -> session.configureMode(mode640()));
        }
        io = new Connection(edid(mode));
        try (UsbAdapterSession session = UsbAdapterSession.open(new Host(io), device)) {
            Thread.currentThread().interrupt();
            try { expect(InterruptedIOException.class, () -> session.configureMode(mode)); }
            finally { Thread.interrupted(); }
        }
    }
}
