//! Retained helper streams. JNI never owns a Tokio task or borrows Java buffers.
use crate::{JavaCredentialStore, java_string};
use droidmux::{
    client::AdbClient,
    pairing::connect_paired_device,
    shell::{ShellOptions, ShellSession},
};
use jni::{
    JNIEnv,
    objects::{JByteArray, JClass, JObject, JString},
    sys::{jboolean, jint, jlong, jstring},
};
use sha2::{Digest, Sha256};
use std::{
    collections::{HashMap, VecDeque},
    sync::{
        Arc, Mutex, OnceLock,
        atomic::{AtomicBool, AtomicU64, Ordering},
    },
    time::Duration,
};
use tokio::{runtime::Runtime, sync::watch};
use zeroize::Zeroizing;

const MAX_HELPER: usize = 2 * 1024 * 1024;
const MAX_ENVELOPE: usize = 2048;
const MAX_OUTPUT: usize = 64 * 1024;
const MAX_EVENTS: usize = 16;
static RUNTIME: OnceLock<Runtime> = OnceLock::new();
static SESSIONS: Mutex<Option<HashMap<u64, Arc<Session>>>> = Mutex::new(None);
static NEXT: AtomicU64 = AtomicU64::new(1);

struct Session {
    cancel: watch::Sender<bool>,
    events: Mutex<VecDeque<&'static str>>,
    done: AtomicBool,
}
impl Session {
    fn event(&self, value: &'static str) {
        if let Ok(mut events) = self.events.lock() {
            if events.len() == MAX_EVENTS {
                events.pop_front();
            }
            events.push_back(value);
        }
    }
}
fn session(id: jlong) -> Option<Arc<Session>> {
    SESSIONS.lock().ok()?.as_ref()?.get(&(id as u64)).cloned()
}
fn runtime() -> Result<&'static Runtime, &'static str> {
    if let Some(runtime) = RUNTIME.get() {
        return Ok(runtime);
    }
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .worker_threads(2)
        .enable_all()
        .build()
        .map_err(|_| "Could not create helper ADB runtime")?;
    let _ = RUNTIME.set(runtime);
    RUNTIME.get().ok_or("Helper ADB runtime unavailable")
}
fn quote(value: &str) -> String {
    format!("'{}'", value.replace('\'', "'\\''"))
}
fn path(digest: &str) -> Result<String, &'static str> {
    if digest.len() != 64
        || !digest
            .bytes()
            .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
    {
        return Err("Invalid helper digest");
    }
    Ok(format!("/data/local/tmp/ms912x-helper-{digest}.jar"))
}
async fn cancelled(cancel: &mut watch::Receiver<bool>) {
    if *cancel.borrow() {
        return;
    }
    let _ = cancel.changed().await;
}
async fn execute(client: &AdbClient, command: &str) -> Result<Vec<u8>, &'static str> {
    let options = ShellOptions {
        max_output_bytes: Some(1024),
        ..ShellOptions::default()
    };
    let output = droidmux::shell::execute_with_options(client, command, options)
        .await
        .map_err(|_| "ADB verification failed")?;
    if output.exit_code != Some(0) {
        return Err("ADB verification returned failure");
    }
    Ok(output.stdout.to_vec())
}
async fn prepare(
    client: &AdbClient,
    helper: &[u8],
    digest: &str,
    envelope: &mut [u8],
    state: &Session,
) -> Result<ShellSession, &'static str> {
    if execute(client, "id -u").await?.as_slice() != b"2000\n" {
        return Err("Helper requires shell UID 2000");
    }
    state.event("Shell UID 2000 verified");
    let remote = path(digest)?;
    let options = droidmux::sync::TransferOptions {
        file_mode: 0o600,
        ..Default::default()
    };
    droidmux::sync::push_bytes_with_options(client, helper, &remote, &options, |_| {})
        .await
        .map_err(|_| "Helper transfer failed")?;
    let result = execute(client, &format!("sha256sum {}", quote(&remote))).await?;
    if result.split(|b| b.is_ascii_whitespace()).next() != Some(digest.as_bytes()) {
        return Err("Transferred helper digest mismatch");
    }
    state.event("Helper transferred and digest verified");
    let command = format!(
        "CLASSPATH={} app_process / --nice-name=ms912x-helper io.github.ms912x.helper.HelperMain --bootstrap-stdin",
        quote(&remote)
    );
    let shell = droidmux::shell::open_shell(client, &command, ShellOptions::default())
        .await
        .map_err(|_| "Helper launch failed")?;
    if shell.write_stdin(envelope.to_vec()).await.is_err() {
        let _ = tokio::time::timeout(Duration::from_secs(2), shell.cancel()).await;
        return Err("Helper bootstrap write failed");
    }
    envelope.fill(0);
    state.event("Helper launched; awaiting authenticated Binder enrollment");
    Ok(shell)
}
async fn drain(
    shell: &ShellSession,
    cancel: &mut watch::Receiver<bool>,
) -> Result<(), &'static str> {
    let (mut stdout, mut stderr, mut bytes) = (true, true, 0usize);
    loop {
        tokio::select! {
            _=cancelled(cancel)=>{
                let _=tokio::time::timeout(Duration::from_secs(2),shell.close_stdin()).await;
                // Continue consuming output during graceful EOF termination.
                let _=tokio::time::timeout(Duration::from_secs(4),async {
                    loop { tokio::select! {
                        _=shell.wait()=>break,
                        value=shell.read_stdout(),if stdout=>{stdout=value.is_some();},
                        value=shell.read_stderr(),if stderr=>{stderr=value.is_some();},
                    }}
                }).await;
                let _=tokio::time::timeout(Duration::from_secs(2),shell.cancel()).await;
                return Ok(());
            }
            result=shell.wait()=>return if result.is_ok(){Ok(())}else{Err("ADB helper stream disconnected")},
            value=shell.read_stdout(),if stdout=>{
                if let Some(value)=value { bytes=bytes.saturating_add(value.len()); }else{stdout=false;}
            }
            value=shell.read_stderr(),if stderr=>{
                if let Some(value)=value { bytes=bytes.saturating_add(value.len()); }else{stderr=false;}
            }
        }
        if bytes > MAX_OUTPUT {
            let _ = tokio::time::timeout(Duration::from_secs(2), shell.cancel()).await;
            return Err("Helper output exceeded its limit");
        }
        // Raw helper output never crosses JNI or enters logcat; it may contain sensitive text.
    }
}
struct Launch {
    store: JavaCredentialStore,
    device: String,
    host: String,
    port: u16,
    helper: Vec<u8>,
    digest: String,
    envelope: Zeroizing<Vec<u8>>,
}
async fn run(request: Launch, state: Arc<Session>, mut cancel: watch::Receiver<bool>) {
    let Launch {
        store,
        device,
        host,
        port,
        helper,
        digest,
        mut envelope,
    } = request;
    state.event("Connecting to this phone's paired Wireless ADB endpoint");
    let connection = tokio::select! {
        _=cancelled(&mut cancel)=>Err("ADB helper startup cancelled"),
        result=tokio::time::timeout(Duration::from_secs(25),connect_paired_device(&device,host,port,&store))=>
            match result {Ok(Ok(client))=>Ok(client),_=>Err("Wireless ADB connection failed; check pairing and endpoint")},
    };
    match connection {
        Ok(client) => {
            let prepared = tokio::select! {
                _=cancelled(&mut cancel)=>Err("ADB helper startup cancelled"),
                result=tokio::time::timeout(Duration::from_secs(25),prepare(&client,&helper,&digest,&mut envelope,&state))=>
                    match result {Ok(result)=>result,Err(_)=>Err("ADB helper startup timed out")},
            };
            match prepared {
                Ok(shell) => {
                    if let Err(error) = drain(&shell, &mut cancel).await {
                        state.event(error);
                    }
                }
                Err(error) => state.event(error),
            }
            let _ = tokio::time::timeout(Duration::from_secs(2), client.close()).await;
        }
        Err(error) => state.event(error),
    }
    state.done.store(true, Ordering::Release);
}

#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_ms912x_desktop_NativeAdbBridge_nativeStartHelper(
    mut env: JNIEnv<'_>,
    _class: JClass<'_>,
    store: JObject<'_>,
    device: JString<'_>,
    host: JString<'_>,
    port: jint,
    helper: JByteArray<'_>,
    digest: JString<'_>,
    envelope: JByteArray<'_>,
) -> jlong {
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let store = JavaCredentialStore::new(&env, &store)?;
        let host = java_string(&mut env, &host)?;
        let device = java_string(&mut env, &device)?;
        let digest = java_string(&mut env, &digest)?;
        if host.parse::<std::net::IpAddr>().is_err()
            || !store.local_address_allowed(&host)?
            || !(1..=65535).contains(&port)
        {
            return Err("Helper endpoint must be a local numeric Wireless ADB address".to_owned());
        }
        if device.is_empty() || device.len() > 256 || device.chars().any(char::is_control) {
            return Err("Invalid paired device identity".into());
        }
        path(&digest).map_err(str::to_owned)?;
        let size = env
            .get_array_length(&helper)
            .map_err(|_| "Invalid helper bytes")? as usize;
        let envelope_size = env
            .get_array_length(&envelope)
            .map_err(|_| "Invalid bootstrap envelope")? as usize;
        if size == 0 || size > MAX_HELPER || envelope_size == 0 || envelope_size > MAX_ENVELOPE {
            return Err("Helper/bootstrap size is invalid".into());
        }
        let helper = env
            .convert_byte_array(helper)
            .map_err(|_| "Cannot copy helper bytes")?;
        let envelope = Zeroizing::new(
            env.convert_byte_array(envelope)
                .map_err(|_| "Cannot copy bootstrap envelope")?,
        );
        if format!("{:x}", Sha256::digest(&helper)) != digest {
            return Err("Bundled helper digest mismatch".into());
        }
        let runtime = runtime().map_err(str::to_owned)?;
        let mut sessions = SESSIONS.lock().map_err(|_| "Helper registry unavailable")?;
        let sessions = sessions.get_or_insert_with(HashMap::new);
        if !sessions.is_empty() {
            return Err("The previous ADB helper session is still closing".into());
        }
        let id = NEXT.fetch_add(1, Ordering::Relaxed);
        let (cancel, receiver) = watch::channel(false);
        let state = Arc::new(Session {
            cancel,
            events: Mutex::new(VecDeque::new()),
            done: AtomicBool::new(false),
        });
        sessions.insert(id, state.clone());
        let completion = state.clone();
        let task = runtime.spawn(run(
            Launch {
                store,
                device,
                host,
                port: port as u16,
                helper,
                digest,
                envelope,
            },
            state,
            receiver,
        ));
        runtime.spawn(async move {
            if task.await.is_err() {
                completion.event("ADB helper worker failed");
                completion.done.store(true, Ordering::Release);
            }
        });
        Ok(id as jlong)
    }))
    .unwrap_or_else(|_| Err("ADB helper bridge failed".into()));
    match result {
        Ok(id) => id,
        Err(message) => {
            let _ = env.exception_clear();
            let _ = env.throw_new("java/lang/IllegalStateException", message);
            0
        }
    }
}
#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_ms912x_desktop_NativeAdbBridge_nativePollHelper(
    env: JNIEnv<'_>,
    _class: JClass<'_>,
    id: jlong,
) -> jstring {
    let Some(state) = session(id) else {
        return std::ptr::null_mut();
    };
    let mut events = match state.events.lock() {
        Ok(events) => events,
        Err(_) => return std::ptr::null_mut(),
    };
    let mut message = events.drain(..).collect::<Vec<_>>().join("\n");
    if state.done.load(Ordering::Acquire) {
        message.push_str("\nSESSION_CLOSED");
    }
    env.new_string(message)
        .map_or(std::ptr::null_mut(), |value| value.into_raw())
}
#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_ms912x_desktop_NativeAdbBridge_nativeCloseHelper(
    _env: JNIEnv<'_>,
    _class: JClass<'_>,
    id: jlong,
) {
    if let Some(state) = session(id) {
        state.cancel.send_replace(true);
    }
}
#[unsafe(no_mangle)]
pub extern "system" fn Java_io_github_ms912x_desktop_NativeAdbBridge_nativeReleaseHelper(
    _env: JNIEnv<'_>,
    _class: JClass<'_>,
    id: jlong,
) -> jboolean {
    if let Ok(mut registry) = SESSIONS.lock() {
        if let Some(sessions) = registry.as_mut() {
            if sessions
                .get(&(id as u64))
                .is_some_and(|s| s.done.load(Ordering::Acquire))
            {
                sessions.remove(&(id as u64));
                return 1;
            }
        }
    }
    0
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn commands_and_events_are_bounded() {
        assert!(path("../../invalid").is_err());
        assert_eq!(quote("a'b"), "'a'\\''b'");
        let (cancel, _) = watch::channel(false);
        let s = Session {
            cancel,
            events: Mutex::new(VecDeque::new()),
            done: AtomicBool::new(false),
        };
        for _ in 0..100 {
            s.event("fixed event");
        }
        assert_eq!(s.events.lock().expect("events").len(), MAX_EVENTS);
    }
}
