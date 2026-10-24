//! Android wireless ADB bridge used by the APK through JNI.

use std::{
    ffi::c_char,
    future::Future,
    ptr,
    sync::atomic::{AtomicBool, Ordering},
};

use async_trait::async_trait;
use droidmux::pairing::{
    CredentialStore, CredentialStoreError, DirectConnectionRequest, PairingRequest,
    StoredHostCredential, StoredPairedDevice, connect_tcp_device, pair_and_connect,
};
use jni::{
    JNIEnv, JavaVM,
    objects::{GlobalRef, JByteArray, JClass, JObject, JObjectArray, JString},
    sys::{jboolean, jint, jstring},
};
use secrecy::SecretString;

static BACKEND_VERSION: &[u8] = b"DroidMux 0.1.0 @ 9adeb625\0";
const CLIENT_NAME: &str = "ms912x@android";
const PAIR_AND_SHELL_TIMEOUT_SECS: u64 = 75;
static ADB_CANCELLED: AtomicBool = AtomicBool::new(false);

struct JavaCredentialStore {
    vm: JavaVM,
    store: GlobalRef,
}

impl JavaCredentialStore {
    fn new(env: &JNIEnv<'_>, store: &JObject<'_>) -> Result<Self, String> {
        if store.is_null() {
            return Err("ADB credential store is required".to_owned());
        }
        let vm = env
            .get_java_vm()
            .map_err(|_| "Could not access Android VM")?;
        let store = env
            .new_global_ref(store)
            .map_err(|_| "Could not retain Android credential store")?;
        Ok(Self { vm, store })
    }

    fn with_env<T>(
        &self,
        call: impl FnOnce(&mut JNIEnv<'_>, &JObject<'_>) -> jni::errors::Result<T>,
    ) -> Result<T, CredentialStoreError> {
        let mut env = self
            .vm
            .attach_current_thread()
            .map_err(|_| CredentialStoreError::new("Android credential store unavailable"))?;
        call(&mut env, self.store.as_obj()).map_err(|_| {
            // Translate Java failures before creating a JNI result string.
            let _ = env.exception_clear();
            CredentialStoreError::new("Android credential store operation failed")
        })
    }

    fn local_address_allowed(&self, host: &str) -> Result<bool, String> {
        let mut env = self
            .vm
            .attach_current_thread()
            .map_err(|_| "Android VM unavailable")?;
        let host = env
            .new_string(host)
            .map_err(|_| "Could not validate endpoint")?;
        env.call_method(
            self.store.as_obj(),
            "isLocalAddress",
            "(Ljava/lang/String;)Z",
            &[(&host).into()],
        )
        .and_then(|value| value.z())
        .map_err(|_| {
            let _ = env.exception_clear();
            "Could not validate endpoint".to_owned()
        })
    }
}

#[async_trait]
impl CredentialStore for JavaCredentialStore {
    async fn load_host_credential(
        &self,
    ) -> Result<Option<StoredHostCredential>, CredentialStoreError> {
        self.with_env(|env, store| {
            let value = env
                .call_method(store, "loadHostCredential", "()[B", &[])?
                .l()?;
            if value.is_null() {
                return Ok(None);
            }
            let bytes = env.convert_byte_array(JByteArray::from(value))?;
            let pem = String::from_utf8_lossy(&bytes).into_owned();
            Ok(Some(StoredHostCredential::new(
                SecretString::from(pem),
                CLIENT_NAME,
            )))
        })
    }

    async fn save_host_credential(
        &self,
        credential: &StoredHostCredential,
    ) -> Result<(), CredentialStoreError> {
        let bytes = credential.expose_private_key_pem().as_bytes();
        self.with_env(|env, store| {
            let value = env.byte_array_from_slice(bytes)?;
            env.call_method(store, "saveHostCredential", "([B)V", &[(&value).into()])?;
            Ok(())
        })
    }

    async fn save_paired_device(
        &self,
        device: &StoredPairedDevice,
    ) -> Result<(), CredentialStoreError> {
        self.with_env(|env, store| {
            let device_id = env.new_string(&device.device_id)?;
            let host = env.new_string(&device.host)?;
            let fingerprint = env.new_string(&device.certificate_fingerprint)?;
            env.call_method(
                store,
                "savePairedDevice",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                &[(&device_id).into(), (&host).into(), (&fingerprint).into()],
            )?;
            Ok(())
        })
    }

    async fn load_paired_device(
        &self,
        device_id: &str,
    ) -> Result<Option<StoredPairedDevice>, CredentialStoreError> {
        self.with_env(|env, store| {
            let device_id_arg = env.new_string(device_id)?;
            let value = env
                .call_method(
                    store,
                    "loadPairedDevice",
                    "(Ljava/lang/String;)[Ljava/lang/String;",
                    &[(&device_id_arg).into()],
                )?
                .l()?;
            if value.is_null() {
                return Ok(None);
            }
            let fields = JObjectArray::from(value);
            if env.get_array_length(&fields)? != 3 {
                return Err(jni::errors::Error::JniCall(jni::errors::JniError::Unknown));
            }
            let device_id_object = env.get_object_array_element(&fields, 0)?;
            let host_object = env.get_object_array_element(&fields, 1)?;
            let fingerprint_object = env.get_object_array_element(&fields, 2)?;
            let device_id = env
                .get_string(&JString::from(device_id_object))?
                .to_string_lossy()
                .into_owned();
            let host = env
                .get_string(&JString::from(host_object))?
                .to_string_lossy()
                .into_owned();
            let certificate_fingerprint = env
                .get_string(&JString::from(fingerprint_object))?
                .to_string_lossy()
                .into_owned();
            Ok(Some(StoredPairedDevice {
                device_id,
                host,
                certificate_fingerprint,
            }))
        })
    }
}

/// Returns the pinned backend identity for startup diagnostics.
#[unsafe(no_mangle)]
pub extern "C" fn ms912x_adb_backend_version() -> *const c_char {
    BACKEND_VERSION.as_ptr().cast()
}

#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_ms912x_desktop_NativeAdbBridge_nativeBackendVersion(
    env: JNIEnv<'_>,
    _class: JClass<'_>,
) -> jstring {
    match env.new_string(String::from_utf8_lossy(
        &BACKEND_VERSION[..BACKEND_VERSION.len() - 1],
    )) {
        Ok(value) => value.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_ms912x_desktop_NativeAdbBridge_nativePairAndVerify(
    mut env: JNIEnv<'_>,
    _class: JClass<'_>,
    store_object: JObject<'_>,
    pairing_host: JString<'_>,
    pairing_port: jint,
    connect_host: JString<'_>,
    connect_port: jint,
    pairing_code: JString<'_>,
) -> jstring {
    ADB_CANCELLED.store(false, Ordering::Release);
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        pair_and_verify(
            &mut env,
            &store_object,
            &pairing_host,
            pairing_port,
            &connect_host,
            connect_port,
            &pairing_code,
        )
    }))
    .unwrap_or_else(|_| "ADB provider encountered an internal error".to_owned());
    match env.new_string(result) {
        Ok(value) => value.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// Debug-build probe for connecting to a locally exposed adbd TCP port.
#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_ms912x_desktop_NativeAdbBridge_nativeConnectLocalTcpAndVerify(
    mut env: JNIEnv<'_>,
    _class: JClass<'_>,
    store_object: JObject<'_>,
    host: JString<'_>,
    port: jint,
) -> jstring {
    ADB_CANCELLED.store(false, Ordering::Release);
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        connect_local_tcp_and_verify(&mut env, &store_object, &host, port)
    }))
    .unwrap_or_else(|panic| {
        format!(
            "Local ADB provider panicked before async execution: {}",
            panic_payload_message(panic.as_ref(), true)
        )
    });
    match env.new_string(result) {
        Ok(value) => value.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_ms912x_desktop_NativeAdbBridge_nativeCancelCurrent(
    _env: JNIEnv<'_>,
    _class: JClass<'_>,
) {
    ADB_CANCELLED.store(true, Ordering::Release);
}

#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_ms912x_desktop_NativeAdbBridge_nativeIsLocalAddress(
    mut env: JNIEnv<'_>,
    _class: JClass<'_>,
    store_object: JObject<'_>,
    host: JString<'_>,
) -> jboolean {
    let allowed = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let Ok(store) = JavaCredentialStore::new(&env, &store_object) else {
            return false;
        };
        let Ok(host) = java_string(&mut env, &host) else {
            return false;
        };
        store.local_address_allowed(&host).unwrap_or(false)
    }))
    .unwrap_or(false);
    u8::from(allowed)
}

fn pair_and_verify(
    env: &mut JNIEnv<'_>,
    store_object: &JObject<'_>,
    pairing_host: &JString<'_>,
    pairing_port: jint,
    connect_host: &JString<'_>,
    connect_port: jint,
    pairing_code: &JString<'_>,
) -> String {
    let inputs = (|| {
        let store = JavaCredentialStore::new(env, store_object)?;
        let pairing_host = java_string(env, pairing_host)?;
        let connect_host = java_string(env, connect_host)?;
        let pairing_code = java_string(env, pairing_code)?;
        if !(1..=u16::MAX as jint).contains(&pairing_port)
            || !(1..=u16::MAX as jint).contains(&connect_port)
        {
            return Err("ADB endpoint port is invalid".to_owned());
        }
        if !store.local_address_allowed(&pairing_host)?
            || !store.local_address_allowed(&connect_host)?
        {
            return Err("ADB endpoint is not an address assigned to this phone".to_owned());
        }
        if pairing_host != connect_host {
            return Err("Pairing and TLS endpoints must use the same local address".to_owned());
        }
        if pairing_code.len() != 6 || !pairing_code.bytes().all(|byte| byte.is_ascii_digit()) {
            return Err("Pairing code must contain six digits".to_owned());
        }
        Ok((store, pairing_host, pairing_code))
    })();

    let (store, pairing_host, pairing_code) = match inputs {
        Ok(values) => values,
        Err(error) => return error,
    };

    let request = PairingRequest {
        host: pairing_host,
        port: pairing_port as u16,
        pairing_code: SecretString::from(pairing_code),
        client_name: CLIENT_NAME.to_owned(),
    };
    run_async(
        async move {
            let (paired, client) = pair_and_connect(request, connect_port as u16, &store)
                .await
                .map_err(|error| format!("ADB pairing or connection failed: {error}"))?;
            let shell = droidmux::shell::execute(&client, "id")
                .await
                .map_err(|_| "ADB shell verification failed".to_owned())?;
            let _ = client.close().await;
            if shell.exit_code != Some(0) {
                return Err("ADB shell verification returned a failure".to_owned());
            }
            let output = String::from_utf8_lossy(&shell.stdout).trim().to_owned();
            Ok(format!(
                "Paired device {}; shell: {}",
                paired.device_id, output
            ))
        },
        false,
    )
}

fn connect_local_tcp_and_verify(
    env: &mut JNIEnv<'_>,
    store_object: &JObject<'_>,
    host: &JString<'_>,
    port: jint,
) -> String {
    let (store, host) = match (|| {
        let store = JavaCredentialStore::new(env, store_object)?;
        let host = java_string(env, host)?;
        if host.parse::<std::net::IpAddr>().is_err() {
            return Err("Debug TCP probe only accepts a numeric IP address".to_owned());
        }
        if port != 5555 {
            return Err("Debug TCP probe only accepts port 5555".to_owned());
        }
        if !store.local_address_allowed(&host)? {
            return Err("Debug TCP endpoint is not assigned to this phone".to_owned());
        }
        Ok((store, host))
    })() {
        Ok(values) => values,
        Err(error) => return error,
    };

    run_async(
        async move {
            let request = DirectConnectionRequest {
                host,
                port: port as u16,
                client_name: CLIENT_NAME.to_owned(),
            };
            let client = connect_tcp_device(request, &store)
                .await
                .map_err(|error| format!("Local ADB TCP connection failed: {error}"))?;
            let shell = droidmux::shell::execute(&client, "id")
                .await
                .map_err(|_| "Local ADB shell verification failed".to_owned())?;
            let _ = client.close().await;
            if shell.exit_code != Some(0) {
                return Err("Local ADB shell verification returned a failure".to_owned());
            }
            let output = String::from_utf8_lossy(&shell.stdout).trim().to_owned();
            Ok(format!("Local adbd TCP shell verified: {output}"))
        },
        true,
    )
}

fn run_async(
    operation: impl Future<Output = Result<String, String>> + Send + 'static,
    expose_panic_message: bool,
) -> String {
    run_async_with_timeout(
        operation,
        expose_panic_message,
        std::time::Duration::from_secs(PAIR_AND_SHELL_TIMEOUT_SECS),
    )
}

fn run_async_with_timeout(
    operation: impl Future<Output = Result<String, String>> + Send + 'static,
    expose_panic_message: bool,
    timeout: std::time::Duration,
) -> String {
    let runtime = match tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
    {
        Ok(runtime) => runtime,
        Err(_) => return "Could not start ADB runtime".to_owned(),
    };
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        runtime.block_on(async move {
            tokio::select! {
                result = tokio::time::timeout(timeout, operation) => Some(result),
                () = wait_for_cancellation() => None,
            }
        })
    })) {
        Ok(Some(Ok(Ok(message)))) => message,
        Ok(Some(Ok(Err(error)))) => error,
        Ok(Some(Err(_))) => "ADB pairing or shell verification timed out".to_owned(),
        Ok(None) => "ADB operation cancelled".to_owned(),
        Err(panic) => format!(
            "ADB operation panicked inside Tokio: {}",
            panic_payload_message(panic.as_ref(), expose_panic_message)
        ),
    }
}

async fn wait_for_cancellation() {
    loop {
        if ADB_CANCELLED.load(Ordering::Acquire) {
            return;
        }
        tokio::time::sleep(std::time::Duration::from_millis(25)).await;
    }
}

fn java_string(env: &mut JNIEnv<'_>, value: &JString<'_>) -> Result<String, String> {
    env.get_string(value)
        .map(|value| value.to_string_lossy().into_owned())
        .map_err(|_| {
            let _ = env.exception_clear();
            "Invalid text passed to the ADB bridge".to_owned()
        })
}

fn panic_payload_message(payload: &(dyn std::any::Any + Send), expose: bool) -> String {
    if !expose {
        return "operation panicked (payload suppressed)".to_owned();
    }
    let message = payload
        .downcast_ref::<&str>()
        .map(|message| (*message).to_owned())
        .or_else(|| payload.downcast_ref::<String>().cloned())
        .unwrap_or_else(|| "operation panicked with a non-text payload".to_owned());
    message
        .chars()
        .filter(|character| !character.is_control())
        .take(200)
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn backend_identity_is_nul_terminated_and_names_the_pin() {
        assert!(BACKEND_VERSION.ends_with(&[0]));
        assert!(BACKEND_VERSION.starts_with(b"DroidMux 0.1.0 @ 9adeb625"));
    }

    #[test]
    fn pairing_codes_must_be_ascii_digits() {
        for invalid in ["12345", "1234567", "12a456", "１２３４５６"] {
            assert!(invalid.len() != 6 || !invalid.bytes().all(|byte| byte.is_ascii_digit()));
        }
        assert!("004219".bytes().all(|byte| byte.is_ascii_digit()));
    }
}

#[cfg(test)]
mod runtime_fixtures {
    use super::*;

    #[test]
    fn runtime_translates_success_failure_timeout_and_panics() {
        assert_eq!(
            run_async(async { Ok("shell fixture".into()) }, false),
            "shell fixture"
        );
        assert_eq!(
            run_async(async { Err("connection fixture".into()) }, false),
            "connection fixture"
        );
        assert_eq!(
            run_async_with_timeout(
                async { std::future::pending::<Result<String, String>>().await },
                false,
                std::time::Duration::from_millis(1)
            ),
            "ADB pairing or shell verification timed out"
        );
        ADB_CANCELLED.store(true, Ordering::Release);
        assert_eq!(
            run_async_with_timeout(
                std::future::pending::<Result<String, String>>(),
                false,
                std::time::Duration::from_secs(1)
            ),
            "ADB operation cancelled"
        );
        ADB_CANCELLED.store(false, Ordering::Release);
        let hidden = run_async(async { panic!("private identity") }, false);
        assert!(hidden.contains("payload suppressed") && !hidden.contains("private identity"));
        let visible = run_async(async { panic!("debug\nfixture") }, true);
        assert!(visible.contains("debugfixture"));
    }

    #[test]
    fn panic_payloads_are_bounded_sanitized_and_support_non_text() {
        assert_eq!(
            panic_payload_message(&String::from("a\nb\rc\td"), true),
            "abcd"
        );
        assert_eq!(panic_payload_message(&"x".repeat(300), true).len(), 200);
        assert_eq!(
            panic_payload_message(&42_u32, true),
            "operation panicked with a non-text payload"
        );
        assert_eq!(
            panic_payload_message(&"secret", false),
            "operation panicked (payload suppressed)"
        );
    }
}
