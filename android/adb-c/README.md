# Native Wireless ADB bridge

The desktop APK builds this C11 module with the NDK/CMake toolchain already
used for capture. No Rust compiler, Cargo, JNI crate or async runtime is needed.
The Java `NativeAdbBridge` API and Android Keystore storage remain stable.
Existing PKCS#8 RSA identities are imported without replacing the key. New
identities use RSA-2048 PEM; the Java store treats PEM as opaque encrypted bytes.

| File | Responsibility |
| --- | --- |
| `adb_jni.c` | Pair/probe entry points, input validation and operation cancellation |
| `adb_jni_store.c` | Credential callbacks, retained store reference and local-address ownership policy |
| `adb_jni_util.c` | VM attachment, UTF-8/UTF-16 conversion, JNI exceptions and secret-array cleanup |
| `adb_jni_helper.c` | Monotonic handles, one retained joinable worker, bounded events, cancel/join/release |
| `adb_crypto.c` | RSA identity persistence, client certificate, Android public-key encoding and AUTH signing |
| `adb_spake2.c` | Android-compatible SPAKE2, transcript and pairing AES-GCM |
| `adb_operation.c` | Monotonic deadlines, cancellation and operation errors |
| `adb_validation.c` | Numeric addresses, pairing codes, device GUIDs, UTF-8 and digests |
| `adb_io.c` | Nonblocking socket/TLS I/O and transport failure state |
| `adb_pairing.c` | Pairing packets, TLS exporter binding and authenticated device GUID |
| `adb_protocol.c` | ADB headers/checksums, gathered packet serialization and connection-owned receive buffers |
| `adb_handshake.c` | CNXN/AUTH/STLS negotiation and exact feature-token parsing |
| `adb_stream.c` | Monotonic stream IDs, gathered writes, receive buffering and bidirectional flow control |
| `adb_shell.c` | Shell v2 stdin/stdout/stderr/exit framing and bounded verification commands |
| `adb_sync.c` | Mode-0600 helper upload using Sync SEND/DATA/DONE |
| `adb_helper.c` | UID check, upload/digest verification, fixed helper launch, bootstrap and output drain |

Only one sequential service stream is needed on each connection. Pairing/probe
operations run on the existing Java executor. A helper owns one joinable pthread;
its Java global reference and copied inputs outlive the JNI start call. Handles
are integer IDs, never pointers. Release succeeds only after worker completion;
it joins before freeing state. Pair/probe cancellation is independent of helper
cancellation. Concurrent pair/probe starts are rejected without resetting an
existing operation's cancellation flag. Closing a controller only cancels its
own operation, including a worker interrupted just before entering JNI.

Limits match the replaced bridge: 2 MiB helper, 2 KiB bootstrap, 64 KiB discarded
helper output, 16 fixed event messages, 75 seconds for pairing/verification and
25 seconds each for helper connection and preparation. Socket waits check
cancellation at most every 25 ms. Cancellation at a framing boundary attempts
bounded stdin EOF and drain. If an exact read loses a partial packet, stream
read or shell frame, the worker closes the connection without issuing another
protocol operation. Interrupted TLS writes also invalidate the connection,
since the TLS engine may retain a pending record. These deadlines and
cancellation are cooperative: they cannot interrupt a synchronous Java credential
callback or cryptographic operation while it is executing. Cancellation is
checked at subsequent checkpoints. Secret native
buffers and temporary Java credential arrays are explicitly wiped. Raw helper
output never enters the event queue.

TLS 1.3 uses the previous provider's trust policy: SPAKE2 and the TLS exporter
authenticate pairing; adbd's client-key allowlist authorizes reconnects. Device
certificate fingerprints are recorded as metadata, not enforced as pins. No
system trust-store or hostname check is substituted for this ADB-specific policy.
Only numeric endpoints accepted by the phone's existing local-address callback
are reachable through JNI. The classic AUTH probe remains restricted to port 5555.

## Dependencies

- [Mbed TLS 3.6.7](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7)
  supplies TLS 1.3/exporters, X.509, RSA, SHA-2, HKDF and AES-GCM. CMake downloads
  its release archive and verifies SHA-256
  `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6`.
  This project selects its GPL-2.0-or-later option under GPL version 2.
  PSA's pthread locking is enabled for concurrent connections.
- [Monocypher 4.0.2](https://github.com/LoupVaillant/Monocypher/tree/4.0.2/src)
  supplies Edwards arithmetic under BSD-2-Clause. Its unmodified source/header
  are vendored in `third_party/monocypher`. SHA-256:
  `monocypher.c`: `02174117935699d418443c75a558a287deb06ef8cf7c1adced61d9047d2f323d`;
  `monocypher.h`: `fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3`.
  The SPAKE2 adapter deliberately includes the pinned implementation once to use
  its internal Edwards primitives, with all protocol-specific code in the adapter.
  Revalidate the adapter and independent vectors when upgrading this pin.

The adapter preserves Android's BoringSSL-specific masks, NUL-terminated role
names, password-scalar cofactor adjustment and 256-bit raw multiplication.
Scalar-dependent loops have fixed iteration counts and use conditional field
copies; arbitrary-precision variable-time secret arithmetic is avoided. The
published compatibility vector from the previous pinned provider is retained
as an independent oracle. Each pairing cipher direction sends one PeerInfo, so
the adapter preserves Android's prescribed initial nonce in each direction.

## Validation

Run `bash tools/android/test_adb.sh` from the repository root. A C compiler,
CMake and JDK are needed; the Android SDK is unnecessary for host tests. First
configure downloads the pinned archive; subsequent builds can reuse the cache.
For offline builds supply `MBEDTLS_SOURCE_DIR` to the script, or CMake's
`FETCHCONTENT_SOURCE_DIR_MBEDTLS` pointing to an extracted, verified release.

The host tests exercise production code against independent wire fixtures:
SPAKE2/AES known answers, wrong passwords and tampering, persistent RSA keys and
AUTH signatures, TLS 1.3 client certificates/exporters, pairing and STLS, fragmented
Shell v2 frames, multi-packet helper upload/digest verification, bootstrap wiping,
cancellation/EOF, late CLSE replies for retired streams, malformed packets,
timeouts, JNI validation/exception translation
and repeated helper cancel/join/release. JNI tests run with `-Xcheck:jni`.
The edge fixture also covers unterminated CNXN banners, misleading feature
substrings, embedded NUL verification output, zero-capacity output, exhausted
stream IDs, gathered writes across packet boundaries, partial transport I/O
and cancellation halfway through shell headers/payloads. Credential fixtures
load PEM without a trailing NUL and return allocated bytes on callback failure;
both paths must wipe and free the storage adapter's bytes.
`SANITIZE=1` instruments the C core and crypto libraries with ASan/UBSan; JNI runs
separately on an ordinary JVM. These peer fixtures do not establish real-phone
pairing, OEM interoperability or physical HDMI presentation.

`SANITIZE=undefined` runs UBSan alone. Sanitizer modes use separate build
directories, avoiding contamination of the normal JNI test library. The
[validation log](../../docs/android-validation.md) records real-phone saved-key
reconnect/helper acceptance for the C port and the host ASan limitation.

On a locked phone, the optional embedded-ADB instrumentation can use
`-e reconnectOnly true -e directReconnect true`, along with its usual
`connectHost`/`connectPort` arguments. This verifies two native helper
connections and their cleanup without interacting with the endpoint picker.
The default test still exercises discovery and the picker UI.

## Performance checks

Packet serialization gathers framing headers and payloads directly into the
connection's reused transmit buffer. Shell stdin no longer allocates a temporary
frame. Sync sizes DATA records to fit the negotiated WRTE payload, avoiding
separate header acknowledgements. The 150,000-byte fixture at a 4,096-byte
packet limit uses 39 WRTEs for SEND/DATA/DONE; the previous separate-header path
required 43. Checks assert both packet count and reconstructed file bytes.
Receive-buffer cleanup wipes the used bytes instead of clearing 64 KiB for every
empty acknowledgement. Verification commands allocate output scratch space to
their actual configured limit.

SPAKE2 caches the fixed input point once per scalar multiplication, retaining
fixed iteration counts and conditional field copies. A local host microbenchmark
of full two-peer exchanges (15 samples of 200 exchanges per revision, process CPU
time, alternating revisions) measured medians of 1,068.75 microseconds before
and 965.05 after: 9.7% less CPU time. This measures cryptographic computation;
it does not measure network or display throughput. Raw samples and the harness
are retained in `android/build/reports/adb-c-review/`.
