package io.github.ms912x.desktop;

import android.content.Context;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;

import java.io.IOException;
import java.io.InterruptedIOException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Opens the supported MacroSilicon display interface after UsbManager consent. */
final class UsbAdapterSession implements AutoCloseable {
    private static final int INTERFACE_CLASS_VENDOR = 0xff;
    private static final int BULK_OUT_ADDRESS = 0x04;

    private final UsbHost.Connection connection;
    private final UsbInterface displayInterface;
    private final UsbInterface controlInterface;
    private final UsbBulkWriter writer;
    private boolean closed;
    private boolean configurationStarted;
    private final byte[][] edidCache = new byte[9][];

    private UsbAdapterSession(UsbHost.Connection connection,
                              UsbInterface displayInterface, UsbInterface controlInterface,
                              UsbEndpoint endpoint) {
        this.connection = connection;
        this.displayInterface = displayInterface;
        this.controlInterface = controlInterface;
        writer = new UsbBulkWriter(connection, endpoint);
    }

    static boolean isSupportedDevice(UsbDevice device) {
        return device != null && NativeCapture.isSupportedDevice(
                device.getVendorId(), device.getProductId());
    }

    static UsbAdapterSession open(Context context, UsbDevice device) {
        return open(UsbHost.fromContext(context), device);
    }

    static UsbAdapterSession open(UsbHost host, UsbDevice device) {
        if (!isSupportedDevice(device)) {
            throw new IllegalArgumentException("Unsupported display adapter");
        }
        if (host == null || !host.hasPermission(device)) {
            throw new SecurityException("Android USB permission has not been granted");
        }

        UsbInterface selectedInterface = null;
        UsbInterface selectedControl = null;
        UsbEndpoint selectedEndpoint = null;
        for (int i = 0; i < device.getInterfaceCount(); ++i) {
            UsbInterface candidate = device.getInterface(i);
            if (candidate.getId() == 0 && candidate.getAlternateSetting() == 0) {
                if (selectedControl != null) {
                    throw new IllegalStateException("Multiple adapter control interfaces found");
                }
                selectedControl = candidate;
            }
            if (candidate.getInterfaceClass() != INTERFACE_CLASS_VENDOR
                    || candidate.getInterfaceSubclass() != 0
                    || candidate.getInterfaceProtocol() != 0) {
                continue;
            }
            UsbEndpoint endpoint = null;
            for (int j = 0; j < candidate.getEndpointCount(); ++j) {
                UsbEndpoint current = candidate.getEndpoint(j);
                if (current.getAddress() == BULK_OUT_ADDRESS
                        && current.getDirection() == UsbConstants.USB_DIR_OUT
                        && current.getType() == UsbConstants.USB_ENDPOINT_XFER_BULK) {
                    if (endpoint != null) {
                        throw new IllegalStateException(
                                "Ambiguous bulk OUT endpoints on adapter interface");
                    }
                    endpoint = current;
                }
            }
            if (endpoint != null) {
                if (selectedInterface != null) {
                    throw new IllegalStateException(
                            "Multiple compatible display interfaces found");
                }
                selectedInterface = candidate;
                selectedEndpoint = endpoint;
            }
        }
        if (selectedInterface == null || selectedEndpoint == null) {
            throw new IllegalStateException("Adapter has no supported bulk OUT interface");
        }
        if (selectedControl == null || (selectedControl != selectedInterface
                && (selectedControl.getInterfaceClass() != UsbConstants.USB_CLASS_HID
                    || selectedControl.getInterfaceSubclass() != 0
                    || selectedControl.getInterfaceProtocol() != 0))) {
            throw new IllegalStateException("Adapter has no supported control interface 0");
        }

        UsbHost.Connection connection = host.openDevice(device);
        if (connection == null) {
            throw new IllegalStateException("Android could not open the adapter");
        }
        boolean displayClaimed = false;
        boolean controlClaimed = false;
        try {
            displayClaimed = connection.claimInterface(selectedInterface, false);
            if (!displayClaimed) {
                throw new IllegalStateException("Adapter display interface is already in use");
            }
            if (selectedControl != selectedInterface) {
                // Class SET/GET_REPORT targets interface 0, separately from bulk video.
                // Detach only this adapter's non-boot HID driver when necessary.
                controlClaimed = connection.claimInterface(selectedControl, true);
                if (!controlClaimed) {
                    throw new IllegalStateException("Adapter control interface is already in use");
                }
            }
            return new UsbAdapterSession(connection, selectedInterface, selectedControl,
                    selectedEndpoint);
        } catch (RuntimeException | Error failure) {
            try {
                releaseAndClose(connection, displayClaimed ? selectedInterface : null,
                        controlClaimed ? selectedControl : null);
            } catch (RuntimeException | Error cleanup) {
                failure.addSuppressed(cleanup);
            }
            throw failure;
        }
    }

    private static void releaseAndClose(UsbHost.Connection connection,
                                        UsbInterface display, UsbInterface control) {
        try {
            if (control != null && control != display) connection.releaseInterface(control);
        } finally {
            try {
                if (display != null) connection.releaseInterface(display);
            } finally {
                connection.close();
            }
        }
    }

    void configureMode(DisplayModeOption mode) throws IOException {
        if (mode == null) {
            throw new IOException("No adapter output mode was selected");
        }
        int sdram = readRegister(0x0030);
        if (!NativeCapture.supportsSdram(sdram)) {
            throw new IOException("Adapter reports no supported external SDRAM: "
                    + sdram);
        }
        Arrays.fill(edidCache, null);
        byte[] edid = readEdid();
        if (!mode.advertisedBy(edid)) {
            throw new IOException("Sink EDID does not advertise " + mode);
        }

        configurationStarted = true;
        byte[] data = new byte[6];
        data[0] = 1;
        data[1] = 2;
        writeCommand(0x07, data);
        byte[] plan = NativeCapture.modePlan(mode.id, mode.width, mode.height);
        if (plan == null) throw new IOException("Invalid adapter mode geometry");
        for (int offset = 0; offset < plan.length; offset += 8) {
            byte[] report = Arrays.copyOfRange(plan, offset, offset + 8);
            if ((report[0] & 0xff) == 0xb5) {
                readRegister((report[1] & 0xff) << 8 | (report[2] & 0xff));
            } else {
                sendControlReport(report);
            }
        }

    }

    AdapterCapabilities probeCapabilities(UsbDevice device) throws IOException {
        int sdram = readRegister(0x0030);
        if (!NativeCapture.supportsSdram(sdram)) {
            throw new IOException("Adapter reports unsupported SDRAM: " + sdram);
        }
        byte[] edid = readEdid();
        List<DisplayModeOption> supported = new ArrayList<>();
        List<DisplayModeOption> candidates = new ArrayList<>();
        try {
            for (DisplayModeOption custom : NativeCapture.customModes(this)) {
                if ((custom.width & 1) == 0 && (custom.polarity & 1) != 0) candidates.add(custom);
            }
        } catch (IOException unavailable) {
            // Linux also retains the standard catalog when optional flash discovery fails.
        }
        candidates.addAll(NativeCapture.modeCatalog());
        for (DisplayModeOption mode : candidates) {
            if (mode.advertisedBy(edid)) supported.add(mode);
        }
        if (supported.isEmpty()) {
            throw new IOException("The monitor EDID has no modes supported by this adapter");
        }
        String name = "Connected monitor";
        for (int offset = 54; offset + 18 <= 126; offset += 18) {
            if (edid[offset] == 0 && edid[offset + 1] == 0
                    && edid[offset + 2] == 0 && (edid[offset + 3] & 0xff) == 0xfc) {
                String candidate = new String(edid, offset + 5, 13,
                        StandardCharsets.US_ASCII).replace('\n', ' ').replace('\r', ' ').trim();
                if (!candidate.isEmpty()) name = candidate;
            }
        }
        return new AdapterCapabilities(device, name, supported, edid);
    }

    int sendFrame(ByteBuffer packet) throws IOException {
        if (closed) {
            throw new IOException("Adapter session is closed");
        }
        return writer.writePacket(packet);
    }

    int configureAndSend640x480Pattern() throws IOException {
        configureMode(new DisplayModeOption(0x40, 640, 480, 60,
                25170, 800, 525, 144, 35, 96, 2, 1));
        return sendFrame(NativeTestPattern.create640x480Packet());
    }

    private void writeCommand(int command, byte[] data) throws IOException {
        byte[] report = NativeCapture.controlWrite(command, data);
        if (report == null) throw new IOException("Invalid control payload");
        sendControlReport(report);
    }

    private void sendControlReport(byte[] report) throws IOException {
        int transferred = connection.controlTransfer(0x21, 0x09, 0x0300, 0,
                report, report.length, 1000);
        if (transferred != report.length) {
            throw new IOException("Adapter control write failed for command 0x"
                    + Integer.toHexString(report[1] & 0xff) + ": " + transferred);
        }
    }

    private int readRegister(int address) throws IOException {
        if (closed || Thread.currentThread().isInterrupted()) {
            throw new InterruptedIOException("Adapter operation cancelled");
        }
        byte[] request = NativeCapture.registerRead(address);
        if (request == null) throw new IOException("Invalid register address");
        int sent = connection.controlTransfer(0x21, 0x09, 0x0300, 0,
                request, request.length, 1000);
        if (sent != request.length) {
            throw new IOException("Adapter register request failed at 0x"
                    + Integer.toHexString(address) + ": " + sent + " bytes");
        }
        byte[] response = new byte[8];
        int received = connection.controlTransfer(0xa1, 0x01, 0x0300, 0,
                response, response.length, 1000);
        if (received != response.length) {
            throw new IOException("Adapter register read failed at 0x"
                    + Integer.toHexString(address) + ": " + received + " bytes");
        }
        return response[3] & 0xff;
    }

    private byte[] readEdid() throws IOException {
        byte[] base = readEdidBlock(0);
        int extensions = base[126] & 0xff;
        if (extensions > 8) {
            throw new IOException("Sink EDID has too many extension blocks to validate");
        }
        byte[] edid = new byte[(extensions + 1) * 128];
        System.arraycopy(base, 0, edid, 0, 128);
        for (int block = 1; block <= extensions; ++block) {
            System.arraycopy(readEdidBlock(block), 0, edid, block * 128, 128);
        }
        return edid;
    }

    private byte[] readEdidBlock(int block) throws IOException {
        if (block < 0 || block >= edidCache.length) throw new IOException("Invalid EDID block");
        if (edidCache[block] == null) {
            byte[] bytes = NativeCapture.readEdidBlock(this, block);
            if (bytes == null) throw new IOException("Adapter returned invalid EDID block " + block);
            edidCache[block] = bytes;
        }
        return edidCache[block].clone();
    }

    private byte[] readFlash(int address, int length) throws IOException {
        if (address < 0 || length < 0 || address > 0x1000000 - length)
            throw new IOException("Invalid flash read range");
        byte[] result = new byte[length];
        for (int offset = 0; offset < length; offset += 8) {
            if (closed || Thread.currentThread().isInterrupted())
                throw new InterruptedIOException("Adapter operation cancelled");
            byte[] request = NativeCapture.flashReadRequest(address + offset);
            int sent = connection.controlTransfer(0x21, 0x09, 0x0300, 0, request, 8, 1000);
            if (sent != 8) throw new IOException("Adapter flash request failed");
            byte[] response = new byte[8];
            int received = connection.controlTransfer(0xa1, 0x01, 0x0300, 0, response, 8, 1000);
            if (received != 8) throw new IOException("Adapter flash read failed");
            System.arraycopy(response, 0, result, offset, Math.min(8, length - offset));
        }
        return result;
    }

    @Override
    public void close() {
        if (closed) {
            return;
        }
        closed = true;
        try {
            if (configurationStarted) {
                try {
                    writeCommand(0x04, new byte[6]);
                    writeCommand(0x05, new byte[6]);
                    writeCommand(0x07, new byte[6]);
                } catch (IOException ignored) {
                    // The USB connection can already be detached during teardown.
                }
            }
        } finally {
            releaseAndClose(connection, displayInterface, controlInterface);
        }
    }
}
