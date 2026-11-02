//! The scenario bodies (FR-080's table, SI-23, SI-30, SI-34, SI-35; plan §4.7). Each runs in its own child process
//! against its own loopback fixture server, with a trap proxy for every non-loopback host, through
//! `krate_manager_lib::self_update` under `UrlPolicy::LoopbackOnly`.

use crate::keys::{self, ScenarioKey};
use crate::socks::SocksRelay;
use crate::{
    ANY, HarnessError, LOOPBACK, Plant, bind_loopback, bind_loopback_listener, confinement,
};
use base64::Engine;
use base64::engine::general_purpose::STANDARD;
use krate_manager_core::fixture_server::{FixtureServer, Reply};
use krate_manager_core::http::{
    CONNECT_TIMEOUT, HttpConfig, ProxyConfig, TOTAL_TIMEOUT, UrlPolicy,
};
use krate_manager_core::install::Elevation;
use krate_manager_core::roots::Roots;
use krate_manager_core::self_update::{
    self as core_su, DOWNLOAD_TIMEOUT, MANAGER_REPO, ManagerUpdateView, Packaging, SelfUpdateError,
    SelfUpdateProgress, SelfUpdating, UpdateAction, UpdateTimeouts, UrlRefusal,
};
use krate_manager_core::status;
use krate_manager_core::test_support::{FakeElevation, temp_roots};
use krate_manager_lib::self_update::{self as su, KeptUpdate, UpdateInputs, UpdateInstaller};
use serde_json::json;
use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4};
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Mutex, MutexGuard, PoisonError};
use std::thread;
use std::time::{Duration, Instant};
use tauri::async_runtime::block_on;
use tauri::test::{MockRuntime, mock_builder, mock_context, noop_assets};
use tauri::{App, AppHandle, Url};
use tauri_plugin_updater::Update;

/// The rsign-format fixture (PD-35 (b), TA-28), made once by `make-rsign-fixture.js`.
const RSIGN_PACKAGE: &[u8] = include_bytes!("../fixtures/rsign/package.bin");
const RSIGN_MINISIG: &[u8] = include_bytes!("../fixtures/rsign/package.bin.minisig");
const RSIGN_PUB: &[u8] = include_bytes!("../fixtures/rsign/rsign.pub");

const CURRENT: &str = "0.1.0";
const NEXT: &str = "0.2.0";
const OLD: &str = "0.0.9";
const LATER: &str = "0.3.0";
const LATEST: &str = "/latest.json";
const ASSET: &str = "krate-manager-harness.bin";
const API_ASSET_URL: &str = "https://api.github.com/repos/o/r/releases/assets/1";
/// `stall`'s per-read bound (FR-027, FR-080).
const STALL_FOR_TEST: Duration = Duration::from_secs(1);
/// `stall`: the error must come within this of the package request.
const STALL_LIMIT: Duration = Duration::from_secs(10);
/// How long a watcher waits for the package request.
const WATCH_LIMIT: Duration = Duration::from_secs(30);
const WATCH_POLL: Duration = Duration::from_millis(2);

/// Why a scenario failed.
pub enum Fail {
    Not(String),
    Harness(HarnessError),
}

impl From<HarnessError> for Fail {
    fn from(e: HarnessError) -> Fail {
        Fail::Harness(e)
    }
}

type Outcome = Result<(), Fail>;

fn not(msg: impl Into<String>) -> Fail {
    Fail::Not(msg.into())
}

fn ensure(cond: bool, msg: impl FnOnce() -> String) -> Outcome {
    if cond { Ok(()) } else { Err(not(msg())) }
}

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    m.lock().unwrap_or_else(PoisonError::into_inner)
}

pub fn run(name: &str, plant: Option<Plant>) -> Outcome {
    match name {
        "api-host" => api_host(plant),
        "busy" => busy(plant),
        "busy-after-download" => busy_after_download(plant),
        "corrupt-manifest" => corrupt_manifest(plant),
        "deb-install" => deb_install(plant),
        "downgrade" => downgrade(plant),
        "endpoint-latest-path" => endpoint_latest_path(plant),
        "failed-install-clears" => failed_install_clears(plant),
        "good" => good(plant),
        "latest-path" => latest_path(plant),
        "map-errors" => crate::map_errors::run().map_err(Fail::Not),
        "no-platform" => no_platform(plant),
        "offline" => offline(plant),
        "other-key" => other_key(plant),
        "production-inputs" => production_inputs(),
        "recheck-replaces" => recheck_replaces(plant),
        "redirect-loop" => redirect_loop(plant),
        "redirect-to-api-host" => redirect_to(plant, API_ASSET_URL, UrlRefusal::ApiHost),
        "redirect-to-http" => redirect_to(plant, "http://example.invalid/pkg", UrlRefusal::Policy),
        "replayed-old" => replayed_old(plant),
        "rsign-format" => rsign_format(),
        "same-version" => no_update(plant, CURRENT),
        "socks-proxy" => socks_proxy(plant),
        "stall" => stall(plant),
        "tampered" => tampered(plant),
        "unversioned" => unversioned(plant),
        _ => Err(not("unknown scenario")),
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------------------------------------------

/// TA-24: the per-tag path of this project's release of `version`.
fn package_path(version: &str) -> String {
    format!("/{MANAGER_REPO}/releases/download/manager/v{version}/{ASSET}")
}

/// A scenario package: fixed bytes naming its version.
fn package(version: &str) -> Vec<u8> {
    format!("krate-manager harness package {version}\n").into_bytes()
}

/// TA-20: the target key the updater looks up (`tauri_plugin_updater::target()`, `updater.rs:1748-1754`).
fn target() -> Result<String, Fail> {
    tauri_plugin_updater::target().ok_or_else(|| not("no updater target for this OS"))
}

/// Tauri's static `latest.json`; `signature` `None` drops the field from the platform entry.
fn latest_json(version: &str, url: &str, signature: Option<&str>, platform: &str) -> Vec<u8> {
    let mut entry = json!({ "url": url });
    if let Some(s) = signature {
        entry["signature"] = json!(s);
    }
    let mut platforms = serde_json::Map::new();
    platforms.insert(platform.to_owned(), entry);
    json!({
        "version": version,
        "notes": "harness",
        "pub_date": "2026-11-01T00:00:00Z",
        "platforms": platforms,
    })
    .to_string()
    .into_bytes()
}

fn json_reply(body: Vec<u8>) -> Reply {
    Reply::Ok {
        body,
        content_type: "application/json",
    }
}

fn bytes_reply(body: Vec<u8>) -> Reply {
    Reply::Ok {
        body,
        content_type: "application/octet-stream",
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Installers (neither reaches the OS install, FR-094)
// ---------------------------------------------------------------------------------------------------------------

/// The default installer: reaching it fails the scenario.
struct PanickingInstaller;

impl UpdateInstaller<MockRuntime> for PanickingInstaller {
    fn install(
        &self,
        _app: &AppHandle<MockRuntime>,
        _update: &Update,
        _bytes: Vec<u8>,
    ) -> Result<(), SelfUpdateError> {
        panic!("installer reached")
    }
}

/// Records the version it was handed and refuses (`recheck-replaces`, `failed-install-clears`, `busy`).
#[derive(Default)]
struct RefusingStub {
    calls: Mutex<Vec<String>>,
}

impl UpdateInstaller<MockRuntime> for RefusingStub {
    fn install(
        &self,
        _app: &AppHandle<MockRuntime>,
        update: &Update,
        _bytes: Vec<u8>,
    ) -> Result<(), SelfUpdateError> {
        lock(&self.calls).push(update.version.clone());
        Err(SelfUpdateError::Install {
            detail: "harness".to_owned(),
        })
    }
}

impl RefusingStub {
    fn calls(&self) -> Vec<String> {
        lock(&self.calls).clone()
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The rig: fixture server, trap proxy, temp roots, mock app, inputs
// ---------------------------------------------------------------------------------------------------------------

/// The scenario's loopback inputs (plan §4.7): `LoopbackOnly`, the trap proxy with `127.0.0.1` bypassed.
fn loopback_inputs(proxy: ProxyConfig, stall: Duration, packaging: Packaging) -> UpdateInputs {
    UpdateInputs {
        http: HttpConfig::loopback(CONNECT_TIMEOUT, TOTAL_TIMEOUT).with_proxy(Some(proxy)),
        policy: UrlPolicy::LoopbackOnly,
        timeouts: UpdateTimeouts {
            stall,
            ..UpdateTimeouts::production()
        },
        packaging,
    }
}

/// The mock app with the throwaway `plugins.updater` object and the scenario's current version.
fn mock_app(pubkey_b64: &str, endpoint: &str, current: &str) -> Result<App<MockRuntime>, Fail> {
    let mut ctx = mock_context(noop_assets());
    ctx.config_mut().plugins.0.insert(
        "updater".to_owned(),
        json!({
            "pubkey": pubkey_b64,
            "endpoints": [endpoint],
            "requireSignedVersion": true,
            "allowDowngrades": false,
            "dangerousInsecureTransportProtocol": true,
        }),
    );
    ctx.package_info_mut().version =
        semver::Version::parse(current).map_err(|e| not(format!("current version: {e}")))?;
    mock_builder()
        .plugin(tauri_plugin_updater::Builder::new().build())
        .manage(KeptUpdate::default())
        .build(ctx)
        .map_err(|e| not(format!("mock app: {e}")))
}

struct Spec {
    plant: Option<Plant>,
    pubkey_b64: String,
    current: &'static str,
    packaging: Packaging,
    stall: Duration,
}

impl Spec {
    fn new(plant: Option<Plant>, pubkey_b64: String) -> Spec {
        Spec {
            plant,
            pubkey_b64,
            current: CURRENT,
            packaging: Packaging::SelfContained,
            stall: UpdateTimeouts::production().stall,
        }
    }
}

struct Rig {
    server: FixtureServer,
    trap: FixtureServer,
    roots: Roots,
    inputs: UpdateInputs,
    app: App<MockRuntime>,
    _td: tempfile::TempDir,
}

impl Rig {
    /// Binds the fixture server (under `--plant bind-any` its bind is `0.0.0.0:0`, refused) and the trap proxy, makes
    /// the temp roots, and builds the mock app whose endpoint `endpoint` names.
    fn new(
        spec: Spec,
        endpoint: impl FnOnce(&FixtureServer) -> Result<String, Fail>,
    ) -> Result<Rig, Fail> {
        let first = if spec.plant == Some(Plant::BindAny) {
            ANY
        } else {
            LOOPBACK
        };
        let server = bind_loopback(first, vec![])?;
        let trap = bind_loopback(LOOPBACK, vec![])?;
        let td = tempfile::TempDir::new().map_err(|e| not(format!("temp dir: {e}")))?;
        confinement::record(td.path());
        let roots = temp_roots(td.path());
        for root in roots.plugin_roots() {
            confinement::record(&root.path);
        }
        confinement::record(&roots.presets().factory);
        confinement::record(&roots.presets().user);
        confinement::record(roots.state_dir());
        confinement::record(roots.staging_dir());
        let endpoint = endpoint(&server)?;
        let app = mock_app(&spec.pubkey_b64, &endpoint, spec.current)?;
        let inputs = loopback_inputs(
            ProxyConfig {
                url: trap.url(""),
                no_proxy: vec!["127.0.0.1".to_owned()],
            },
            spec.stall,
            spec.packaging,
        );
        Ok(Rig {
            server,
            trap,
            roots,
            inputs,
            app,
            _td: td,
        })
    }

    /// The common rig: endpoint `<server>/latest.json`.
    fn standard(spec: Spec) -> Result<Rig, Fail> {
        Rig::new(spec, |s| Ok(s.url(LATEST)))
    }

    fn handle(&self) -> &AppHandle<MockRuntime> {
        self.app.handle()
    }

    fn state_dir(&self) -> &Path {
        self.roots.state_dir()
    }

    fn check(&self) -> ManagerUpdateView {
        block_on(su::check(self.handle(), &self.inputs))
    }

    fn install_update(
        &self,
        installer: &dyn UpdateInstaller<MockRuntime>,
        busy: &(dyn Fn() -> bool + Sync),
        progress: &(dyn Fn(SelfUpdateProgress) + Sync),
    ) -> Result<(), SelfUpdateError> {
        confinement::record(self.state_dir());
        block_on(su::install(
            self.handle(),
            &self.inputs,
            installer,
            busy,
            Some(self.state_dir()),
            progress,
        ))
    }

    /// An install with no busy state and the progress events dropped.
    fn install_plain(
        &self,
        installer: &dyn UpdateInstaller<MockRuntime>,
    ) -> Result<(), SelfUpdateError> {
        self.install_update(installer, &|| false, &|_| {})
    }

    /// Announces `version` at its per-tag URL with `signature`, and serves `served` there.
    fn offer(&self, version: &str, signature: &str, served: Reply) -> Result<(), Fail> {
        let path = package_path(version);
        let doc = latest_json(
            version,
            &self.server.url(&path),
            Some(signature),
            &target()?,
        );
        self.server.set_route(LATEST, json_reply(doc));
        self.server.set_route(&path, served);
        Ok(())
    }

    fn count(&self, path: &str) -> usize {
        self.server
            .requests()
            .iter()
            .filter(|r| r.path == path)
            .count()
    }

    fn trap_count(&self) -> usize {
        self.trap.requests().len()
    }

    fn expect_trap_zero(&self) -> Outcome {
        let n = self.trap_count();
        ensure(n == 0, || format!("trap proxy saw {n} requests"))
    }

    fn expect_count(&self, path: &str, want: usize) -> Outcome {
        let n = self.count(path);
        ensure(n == want, || {
            format!("{n} requests for {path}, want {want}")
        })
    }
}

/// The `Update` the rig's updater finds (the builder `check` uses; `KeptUpdate`'s slot is private to the shell).
fn fetch(app: &AppHandle<MockRuntime>, inputs: &UpdateInputs) -> Result<Update, Fail> {
    let updater =
        su::build_updater(app, inputs).map_err(|e| not(format!("build_updater: {}", show(&e))))?;
    match block_on(updater.check()) {
        Ok(Some(u)) => Ok(u),
        Ok(None) => Err(not("updater check found no update")),
        Err(e) => Err(not(format!("updater check: {}", show(&su::map_error(&e))))),
    }
}

/// `download` (which verifies) then `verify`.
fn download_and_verify(u: &Update, want: &[u8]) -> Outcome {
    let bytes = block_on(u.download(|_, _| {}, || {}))
        .map_err(|e| not(format!("download: {}", show(&su::map_error(&e)))))?;
    ensure(bytes == want, || "downloaded bytes differ".to_owned())?;
    u.verify(&bytes)
        .map_err(|e| not(format!("verify: {}", show(&su::map_error(&e)))))
}

// ---------------------------------------------------------------------------------------------------------------
// Assertions
// ---------------------------------------------------------------------------------------------------------------

fn show(e: &SelfUpdateError) -> String {
    format!("{} {e:?}", e.code())
}

fn describe(view: &ManagerUpdateView) -> String {
    match (&view.available, &view.error) {
        (_, Some(e)) => format!(
            "check error {} {:?} {}",
            e.code,
            e.reason,
            e.detail.as_deref().unwrap_or("")
        ),
        (Some(a), None) => format!("check offered {} {:?}", a.version, a.action),
        (None, None) => "check offered nothing".to_owned(),
    }
}

fn expect_offer(view: &ManagerUpdateView, version: &str, action: &UpdateAction) -> Outcome {
    match (&view.available, &view.error) {
        (Some(a), None) if a.version == version && a.action == *action => Ok(()),
        _ => Err(not(describe(view))),
    }
}

fn expect_install_offer(view: &ManagerUpdateView, version: &str) -> Outcome {
    expect_offer(view, version, &UpdateAction::InstallAndRestart)
}

fn expect_nothing(view: &ManagerUpdateView) -> Outcome {
    match (&view.available, &view.error) {
        (None, None) => Ok(()),
        _ => Err(not(describe(view))),
    }
}

fn expect_view_error(view: &ManagerUpdateView, code: &str, reason: Option<UrlRefusal>) -> Outcome {
    match (&view.available, &view.error) {
        (None, Some(e)) if e.code == code && e.reason == reason => Ok(()),
        _ => Err(not(describe(view))),
    }
}

fn url_reason(e: &SelfUpdateError) -> Option<UrlRefusal> {
    match e {
        SelfUpdateError::Url { reason } => Some(*reason),
        _ => None,
    }
}

fn expect_err(
    what: &str,
    r: Result<(), SelfUpdateError>,
    code: &str,
    reason: Option<UrlRefusal>,
) -> Outcome {
    match r {
        Ok(()) => Err(not(format!("{what} returned Ok"))),
        Err(e) if e.code() == code && url_reason(&e) == reason => Ok(()),
        Err(e) => Err(not(format!("{what}: {}", show(&e)))),
    }
}

/// A fresh key, refused when it is the committed one (FR-081 (d)), before any server starts.
fn checked_key(plant: Option<Plant>) -> Result<ScenarioKey, Fail> {
    let key = keys::scenario_key(plant);
    keys::refuse_committed(&key)?;
    Ok(key)
}

fn pubkey_of(key: &ScenarioKey) -> String {
    keys::tauri_pubkey(&key.public_box_text)
}

/// A rig offering `NEXT`, validly signed, served as `served`.
fn offering(
    plant: Option<Plant>,
    spec: impl FnOnce(Spec) -> Spec,
    served: impl FnOnce(&[u8]) -> Reply,
) -> Result<(Rig, Vec<u8>), Fail> {
    let key = checked_key(plant)?;
    let rig = Rig::standard(spec(Spec::new(plant, pubkey_of(&key))))?;
    let pkg = package(NEXT);
    rig.offer(NEXT, &keys::sign(&key, &pkg, Some(NEXT)), served(&pkg))?;
    Ok((rig, pkg))
}

// ---------------------------------------------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------------------------------------------

/// `check` → offer (PD-31); the update the updater finds: `prepare_download` (timeout, direct route), `download`,
/// `verify`.
fn good(plant: Option<Plant>) -> Outcome {
    let (rig, pkg) = offering(plant, |s| s, |p| bytes_reply(p.to_vec()))?;
    expect_install_offer(&rig.check(), NEXT)?;
    let mut u = fetch(rig.handle(), &rig.inputs)?;
    su::prepare_download(&mut u, &rig.inputs)
        .map_err(|e| not(format!("prepare_download: {}", show(&e))))?;
    ensure(u.timeout == Some(DOWNLOAD_TIMEOUT), || {
        format!("Update.timeout {:?}", u.timeout)
    })?;
    ensure(u.no_proxy, || {
        "Update.no_proxy is false for loopback".to_owned()
    })?;
    download_and_verify(&u, &pkg)?;
    rig.expect_count(&package_path(NEXT), 1)?;
    rig.expect_trap_zero()
}

/// The served package differs by one byte from the signed one.
fn tampered(plant: Option<Plant>) -> Outcome {
    let (rig, _) = offering(
        plant,
        |s| s,
        |p| {
            let mut bad = p.to_vec();
            bad[0] ^= 1;
            bytes_reply(bad)
        },
    )?;
    expect_install_offer(&rig.check(), NEXT)?;
    expect_err(
        "install",
        rig.install_plain(&PanickingInstaller),
        "E_SELF_UPDATE_SIGNATURE",
        None,
    )?;
    rig.expect_count(&package_path(NEXT), 1)?;
    rig.expect_trap_zero()
}

/// Signed by a second throwaway key.
fn other_key(plant: Option<Plant>) -> Outcome {
    let key = checked_key(plant)?;
    let other = checked_key(plant)?;
    let rig = Rig::standard(Spec::new(plant, pubkey_of(&key)))?;
    let pkg = package(NEXT);
    rig.offer(
        NEXT,
        &keys::sign(&other, &pkg, Some(NEXT)),
        bytes_reply(pkg.clone()),
    )?;
    expect_install_offer(&rig.check(), NEXT)?;
    expect_err(
        "install",
        rig.install_plain(&PanickingInstaller),
        "E_SELF_UPDATE_SIGNATURE",
        None,
    )?;
    rig.expect_trap_zero()
}

/// Announced `0.2.0`; the bytes and signature of a valid `0.0.9` package signed `version:0.0.9`.
fn replayed_old(plant: Option<Plant>) -> Outcome {
    let key = checked_key(plant)?;
    let rig = Rig::standard(Spec::new(plant, pubkey_of(&key)))?;
    let old = package(OLD);
    rig.offer(
        NEXT,
        &keys::sign(&key, &old, Some(OLD)),
        bytes_reply(old.clone()),
    )?;
    expect_install_offer(&rig.check(), NEXT)?;
    expect_err(
        "install",
        rig.install_plain(&PanickingInstaller),
        "E_SELF_UPDATE_SIGNATURE",
        None,
    )?;
    rig.expect_trap_zero()
}

/// A valid signature without a `version:` field.
fn unversioned(plant: Option<Plant>) -> Outcome {
    let key = checked_key(plant)?;
    let rig = Rig::standard(Spec::new(plant, pubkey_of(&key)))?;
    let pkg = package(NEXT);
    rig.offer(
        NEXT,
        &keys::sign(&key, &pkg, None),
        bytes_reply(pkg.clone()),
    )?;
    expect_install_offer(&rig.check(), NEXT)?;
    expect_err(
        "install",
        rig.install_plain(&PanickingInstaller),
        "E_SELF_UPDATE_SIGNATURE",
        None,
    )?;
    rig.expect_trap_zero()
}

/// Announced `0.0.9`, validly signed, current `0.1.0`: nothing offered.
fn downgrade(plant: Option<Plant>) -> Outcome {
    no_update(plant, OLD)
}

/// `same-version` and `downgrade`: `check` → nothing, no package request.
fn no_update(plant: Option<Plant>, announced: &str) -> Outcome {
    let key = checked_key(plant)?;
    let rig = Rig::standard(Spec::new(plant, pubkey_of(&key)))?;
    let pkg = package(announced);
    rig.offer(
        announced,
        &keys::sign(&key, &pkg, Some(announced)),
        bytes_reply(pkg.clone()),
    )?;
    expect_nothing(&rig.check())?;
    rig.expect_count(&package_path(announced), 0)?;
    rig.expect_trap_zero()
}

/// `latest.json` names an API asset URL: refused by `check` before any download.
fn api_host(plant: Option<Plant>) -> Outcome {
    let key = checked_key(plant)?;
    let rig = Rig::standard(Spec::new(plant, pubkey_of(&key)))?;
    let pkg = package(NEXT);
    let doc = latest_json(
        NEXT,
        API_ASSET_URL,
        Some(&keys::sign(&key, &pkg, Some(NEXT))),
        &target()?,
    );
    rig.server.set_route(LATEST, json_reply(doc));
    expect_view_error(&rig.check(), "E_SELF_UPDATE_URL", Some(UrlRefusal::ApiHost))?;
    let other: Vec<String> = rig
        .server
        .requests()
        .into_iter()
        .map(|r| r.path)
        .filter(|p| p != LATEST)
        .collect();
    ensure(other.is_empty(), || format!("package requests {other:?}"))?;
    rig.expect_trap_zero()
}

/// A loopback download URL under `/releases/latest/`: only FR-011's own rule refuses it.
fn latest_path(plant: Option<Plant>) -> Outcome {
    let key = checked_key(plant)?;
    let rig = Rig::standard(Spec::new(plant, pubkey_of(&key)))?;
    let pkg = package(NEXT);
    let path = "/o/r/releases/latest/download/x";
    rig.server.set_route(path, bytes_reply(pkg.clone()));
    let doc = latest_json(
        NEXT,
        &rig.server.url(path),
        Some(&keys::sign(&key, &pkg, Some(NEXT))),
        &target()?,
    );
    rig.server.set_route(LATEST, json_reply(doc));
    expect_view_error(
        &rig.check(),
        "E_SELF_UPDATE_URL",
        Some(UrlRefusal::LatestPath),
    )?;
    rig.expect_count(path, 0)?;
    rig.expect_trap_zero()
}

/// The endpoint itself is under `/releases/latest/`: a view error, no request at all.
fn endpoint_latest_path(plant: Option<Plant>) -> Outcome {
    let key = checked_key(plant)?;
    let rig = Rig::new(Spec::new(plant, pubkey_of(&key)), |s| {
        Ok(s.url("/o/r/releases/latest/download/latest.json"))
    })?;
    expect_view_error(
        &rig.check(),
        "E_SELF_UPDATE_URL",
        Some(UrlRefusal::LatestPath),
    )?;
    let n = rig.server.requests().len();
    ensure(n == 0, || format!("fixture server saw {n} requests"))?;
    rig.expect_trap_zero()
}

/// The package route redirects to `location`: `download` refuses the hop (FR-011a).
fn redirect_to(plant: Option<Plant>, location: &str, reason: UrlRefusal) -> Outcome {
    let (rig, _) = offering(
        plant,
        |s| s,
        |_| Reply::Redirect {
            status: 302,
            location: location.to_owned(),
        },
    )?;
    expect_install_offer(&rig.check(), NEXT)?;
    expect_err(
        "install",
        rig.install_plain(&PanickingInstaller),
        "E_SELF_UPDATE_URL",
        Some(reason),
    )?;
    rig.expect_count(&package_path(NEXT), 1)?;
    rig.expect_trap_zero()
}

/// The package route redirects to itself: refused after `MAX_REDIRECTS + 1` requests.
fn redirect_loop(plant: Option<Plant>) -> Outcome {
    let (rig, _) = offering(
        plant,
        |s| s,
        |_| Reply::Redirect {
            status: 302,
            location: package_path(NEXT),
        },
    )?;
    expect_install_offer(&rig.check(), NEXT)?;
    expect_err(
        "install",
        rig.install_plain(&PanickingInstaller),
        "E_SELF_UPDATE_URL",
        Some(UrlRefusal::Policy),
    )?;
    let want = usize::try_from(krate_manager_core::http::MAX_REDIRECTS).unwrap_or(usize::MAX) + 1;
    rig.expect_count(&package_path(NEXT), want)?;
    rig.expect_trap_zero()
}

/// A second check replaces the kept update; install fetches only the newer package.
fn recheck_replaces(plant: Option<Plant>) -> Outcome {
    let key = checked_key(plant)?;
    let rig = Rig::standard(Spec::new(plant, pubkey_of(&key)))?;
    let next = package(NEXT);
    rig.offer(
        NEXT,
        &keys::sign(&key, &next, Some(NEXT)),
        bytes_reply(next.clone()),
    )?;
    expect_install_offer(&rig.check(), NEXT)?;
    let later = package(LATER);
    rig.offer(
        LATER,
        &keys::sign(&key, &later, Some(LATER)),
        bytes_reply(later.clone()),
    )?;
    expect_install_offer(&rig.check(), LATER)?;
    let stub = RefusingStub::default();
    expect_err(
        "install",
        rig.install_plain(&stub),
        "E_SELF_UPDATE_INSTALL",
        None,
    )?;
    let calls = stub.calls();
    ensure(calls == [LATER], || format!("installer calls {calls:?}"))?;
    rig.expect_count(&package_path(NEXT), 0)?;
    rig.expect_count(&package_path(LATER), 1)?;
    rig.expect_trap_zero()
}

/// A failed install clears the kept update.
fn failed_install_clears(plant: Option<Plant>) -> Outcome {
    let (rig, _) = offering(plant, |s| s, |p| bytes_reply(p.to_vec()))?;
    expect_install_offer(&rig.check(), NEXT)?;
    let stub = RefusingStub::default();
    let events = Mutex::new(Vec::new());
    let r = rig.install_update(&stub, &|| false, &|p| lock(&events).push(p));
    expect_err("first install", r, "E_SELF_UPDATE_INSTALL", None)?;
    let seen = lock(&events).clone();
    ensure(
        seen == [
            SelfUpdateProgress::Downloading,
            SelfUpdateProgress::Installing,
        ],
        || format!("progress {seen:?}"),
    )?;
    expect_err(
        "second install",
        rig.install_plain(&stub),
        "E_SELF_UPDATE_NONE",
        None,
    )?;
    rig.expect_count(&package_path(NEXT), 1)?;
    rig.expect_trap_zero()
}

/// Truncated JSON, then a platform entry without `signature`: both `E_SELF_UPDATE_MANIFEST`.
fn corrupt_manifest(plant: Option<Plant>) -> Outcome {
    let key = checked_key(plant)?;
    let rig = Rig::standard(Spec::new(plant, pubkey_of(&key)))?;
    let pkg = package(NEXT);
    let url = rig.server.url(&package_path(NEXT));
    let mut truncated = latest_json(
        NEXT,
        &url,
        Some(&keys::sign(&key, &pkg, Some(NEXT))),
        &target()?,
    );
    truncated.truncate(truncated.len() / 2);
    rig.server.set_route(LATEST, json_reply(truncated));
    expect_view_error(&rig.check(), "E_SELF_UPDATE_MANIFEST", None)
        .map_err(|f| prefix("truncated JSON", f))?;
    rig.server.set_route(
        LATEST,
        json_reply(latest_json(NEXT, &url, None, &target()?)),
    );
    expect_view_error(&rig.check(), "E_SELF_UPDATE_MANIFEST", None)
        .map_err(|f| prefix("no signature", f))?;
    rig.expect_count(&package_path(NEXT), 0)?;
    rig.expect_trap_zero()
}

fn prefix(what: &str, f: Fail) -> Fail {
    match f {
        Fail::Not(m) => Fail::Not(format!("{what}: {m}")),
        other => other,
    }
}

/// No platform entry for this OS.
fn no_platform(plant: Option<Plant>) -> Outcome {
    let key = checked_key(plant)?;
    let rig = Rig::standard(Spec::new(plant, pubkey_of(&key)))?;
    let pkg = package(NEXT);
    let doc = latest_json(
        NEXT,
        &rig.server.url(&package_path(NEXT)),
        Some(&keys::sign(&key, &pkg, Some(NEXT))),
        "plan9-mips",
    );
    rig.server.set_route(LATEST, json_reply(doc));
    expect_view_error(&rig.check(), "E_SELF_UPDATE_MANIFEST", None)?;
    rig.expect_trap_zero()
}

/// The endpoint's port is closed.
fn offline(plant: Option<Plant>) -> Outcome {
    let key = checked_key(plant)?;
    let rig = Rig::new(Spec::new(plant, pubkey_of(&key)), |_| {
        let closed = bind_loopback(LOOPBACK, vec![])?;
        let url = closed.url(LATEST);
        drop(closed);
        Ok(url)
    })?;
    expect_view_error(&rig.check(), "E_SELF_UPDATE_NETWORK", None)?;
    rig.expect_trap_zero()
}

/// The harness holds its own fake elevation guard: install refused before any request; the update is kept (PD-28),
/// so after the guard is released a second install with no new check reaches the refusing stub.
fn busy(plant: Option<Plant>) -> Outcome {
    let (rig, _) = offering(plant, |s| s, |p| bytes_reply(p.to_vec()))?;
    let elev = FakeElevation::refusing();
    expect_install_offer(&rig.check(), NEXT)?;
    let stub = RefusingStub::default();
    let is_busy = || core_su::self_update_busy(&elev, None);
    {
        let _held = elev.guard().hold_for_test();
        expect_err(
            "install while held",
            rig.install_update(&stub, &is_busy, &|_| {}),
            "E_SELF_UPDATE_BUSY",
            None,
        )?;
        rig.expect_count(&package_path(NEXT), 0)?;
    }
    expect_err(
        "install after release",
        rig.install_update(&stub, &is_busy, &|_| {}),
        "E_SELF_UPDATE_INSTALL",
        None,
    )?;
    let calls = stub.calls();
    ensure(calls == [NEXT], || format!("installer calls {calls:?}"))?;
    rig.expect_count(&package_path(NEXT), 1)?;
    rig.expect_trap_zero()
}

/// What a watcher saw while the package request was in flight.
#[derive(Default)]
struct Watch {
    failure: Mutex<Option<String>>,
    seen_at: Mutex<Option<Instant>>,
    done: AtomicBool,
}

impl Watch {
    fn fail(&self, msg: impl Into<String>) {
        lock(&self.failure).get_or_insert(msg.into());
    }

    /// Waits for the package request; then asserts the in-flight flag and the action gate for `state_dir`, and runs
    /// `hold` until the install has returned.
    fn watch(
        &self,
        server: &FixtureServer,
        path: &str,
        state_dir: &Path,
        hold: impl FnOnce(&Watch),
    ) {
        let start = Instant::now();
        while !server.requests().iter().any(|r| r.path == path) {
            if self.done.load(Ordering::SeqCst) || start.elapsed() > WATCH_LIMIT {
                self.fail("package request not seen");
                return;
            }
            thread::sleep(WATCH_POLL);
        }
        *lock(&self.seen_at) = Some(Instant::now());
        if !core_su::in_flight(state_dir) || core_su::action_gate(state_dir) != Err(SelfUpdating) {
            self.fail("in-flight not set");
        }
        hold(self);
    }

    fn wait_done(&self) {
        while !self.done.load(Ordering::SeqCst) {
            thread::sleep(WATCH_POLL);
        }
    }

    fn failure(&self) -> Option<String> {
        lock(&self.failure).clone()
    }
}

/// FR-019: the guard becomes held while the package downloads: refused after one request, the installer never reached,
/// the kept update cleared, the in-flight flag false afterwards.
fn busy_after_download(plant: Option<Plant>) -> Outcome {
    let (rig, _) = offering(
        plant,
        |s| s,
        |p| Reply::Delayed {
            after: Duration::from_millis(200),
            reply: Box::new(bytes_reply(p.to_vec())),
        },
    )?;
    let elev = FakeElevation::refusing();
    expect_install_offer(&rig.check(), NEXT)?;
    let path = package_path(NEXT);
    let state_dir = rig.state_dir();
    let watch = Watch::default();
    let events = Mutex::new(Vec::new());
    let r = thread::scope(|s| {
        s.spawn(|| {
            watch.watch(&rig.server, &path, state_dir, |w| {
                let _held = elev.guard().hold_for_test();
                w.wait_done();
            });
        });
        let r = rig.install_update(
            &PanickingInstaller,
            &|| core_su::self_update_busy(&elev, None),
            &|p| lock(&events).push(p),
        );
        watch.done.store(true, Ordering::SeqCst);
        r
    });
    if let Some(f) = watch.failure() {
        return Err(not(f));
    }
    expect_err("install", r, "E_SELF_UPDATE_BUSY", None)?;
    rig.expect_count(&path, 1)?;
    let seen = lock(&events).clone();
    ensure(seen == [SelfUpdateProgress::Downloading], || {
        format!("progress {seen:?}")
    })?;
    expect_err(
        "second install",
        rig.install_plain(&PanickingInstaller),
        "E_SELF_UPDATE_NONE",
        None,
    )?;
    ensure(!core_su::in_flight(state_dir), || {
        "in-flight still set".to_owned()
    })?;
    rig.expect_count(&path, 1)?;
    rig.expect_trap_zero()
}

/// FR-027: headers and part of the body, then a stall; the 1 s per-read bound ends it with no retry.
fn stall(plant: Option<Plant>) -> Outcome {
    let (rig, _) = offering(
        plant,
        |s| Spec {
            stall: STALL_FOR_TEST,
            ..s
        },
        |p| Reply::HeadersThenStall {
            declared_len: p.len(),
            sent: p[..p.len() / 2].to_vec(),
            stall: Duration::from_secs(30),
        },
    )?;
    expect_install_offer(&rig.check(), NEXT)?;
    let path = package_path(NEXT);
    let state_dir = rig.state_dir();
    let watch = Watch::default();
    let (r, ended) = thread::scope(|s| {
        s.spawn(|| watch.watch(&rig.server, &path, state_dir, Watch::wait_done));
        let r = rig.install_plain(&PanickingInstaller);
        let ended = Instant::now();
        watch.done.store(true, Ordering::SeqCst);
        (r, ended)
    });
    if let Some(f) = watch.failure() {
        return Err(not(f));
    }
    expect_err("install", r, "E_SELF_UPDATE_NETWORK", None)?;
    let took = lock(&watch.seen_at).map(|t| ended.duration_since(t));
    ensure(took.is_some_and(|t| t < STALL_LIMIT), || {
        format!("error {took:?} after the request")
    })?;
    rig.expect_count(&path, 1)?;
    ensure(!core_su::in_flight(state_dir), || {
        "in-flight still set".to_owned()
    })?;
    expect_err(
        "second install",
        rig.install_plain(&PanickingInstaller),
        "E_SELF_UPDATE_NONE",
        None,
    )?;
    rig.expect_count(&path, 1)?;
    rig.expect_trap_zero()
}

/// PD-24, PD-31: a system-package install is offered the release page and never installs.
fn deb_install(plant: Option<Plant>) -> Outcome {
    let (rig, _) = offering(
        plant,
        |s| Spec {
            packaging: Packaging::SystemPackage,
            ..s
        },
        |p| bytes_reply(p.to_vec()),
    )?;
    let page = format!(
        "https://{}/{}/releases/tag/manager/v{NEXT}",
        status::RELEASE_PAGE_HOST,
        MANAGER_REPO
    );
    ensure(
        status::release_page_url(&page) == Some(page.clone()),
        || format!("release_page_url refuses {page}"),
    )?;
    expect_offer(
        &rig.check(),
        NEXT,
        &UpdateAction::OpenReleasePage { url: page },
    )?;
    expect_err(
        "install",
        rig.install_plain(&PanickingInstaller),
        "E_SELF_UPDATE_UNSUPPORTED",
        None,
    )?;
    rig.expect_count(&package_path(NEXT), 0)?;
    rig.expect_trap_zero()
}

/// PD-30, SI-30: the same good path through a SOCKS5, then a SOCKS4 proxy (no bypass), the relay's CONNECTs recorded.
fn socks_proxy(plant: Option<Plant>) -> Outcome {
    let (rig, pkg) = offering(plant, |s| s, |p| bytes_reply(p.to_vec()))?;
    let relay = SocksRelay::start(bind_loopback_listener(LOOPBACK)?);
    let server_addr = SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::LOCALHOST, rig.server.port()));
    let path = package_path(NEXT);
    for (scheme, version) in [("socks5", 5u8), ("socks4", 4u8)] {
        let relay_url = relay.url(scheme);
        let inputs = loopback_inputs(
            ProxyConfig {
                url: relay_url.clone(),
                no_proxy: vec![],
            },
            UpdateTimeouts::production().stall,
            Packaging::SelfContained,
        );
        let connects_before = relay.connects().len();
        let latest_before = rig.count(LATEST);
        let pkg_before = rig.count(&path);
        let run = || -> Outcome {
            expect_install_offer(&block_on(su::check(rig.handle(), &inputs)), NEXT)?;
            let mut u = fetch(rig.handle(), &inputs)?;
            su::prepare_download(&mut u, &inputs)
                .map_err(|e| not(format!("prepare_download: {}", show(&e))))?;
            let want = Url::parse(&relay_url).map_err(|e| not(format!("relay url: {e}")))?;
            ensure(u.proxy.as_ref() == Some(&want) && !u.no_proxy, || {
                format!("Update.proxy {:?} no_proxy {}", u.proxy, u.no_proxy)
            })?;
            download_and_verify(&u, &pkg)
        };
        run().map_err(|f| prefix(scheme, f))?;
        let connects: Vec<_> = relay.connects().into_iter().skip(connects_before).collect();
        ensure(connects.len() >= 3, || {
            format!("{scheme}: {} CONNECTs for 3 fetches", connects.len())
        })?;
        ensure(
            connects
                .iter()
                .all(|c| c.version == version && c.target == server_addr),
            || format!("{scheme}: CONNECTs {connects:?}, want version {version} to {server_addr}"),
        )?;
        let latest = rig.count(LATEST) - latest_before;
        ensure(latest >= 1, || format!("{scheme}: no GET {LATEST}"))?;
        let pkgs = rig.count(&path) - pkg_before;
        ensure(pkgs == 1, || format!("{scheme}: {pkgs} package requests"))?;
    }
    let not_socks = relay.not_socks();
    ensure(not_socks.is_empty(), || format!("not socks {not_socks:?}"))?;
    let refused = relay.refused();
    ensure(refused.is_empty(), || format!("relay refused {refused:?}"))?;
    rig.expect_trap_zero()
}

/// PD-35 (b), SI-34: the good path with the committed rsign2-made signature and public key.
fn rsign_format() -> Outcome {
    let pub_text = std::str::from_utf8(RSIGN_PUB).map_err(|e| not(format!("rsign.pub: {e}")))?;
    keys::refuse_committed_text(pub_text)?;
    let rig = Rig::standard(Spec::new(None, STANDARD.encode(RSIGN_PUB)))?;
    rig.offer(
        NEXT,
        &STANDARD.encode(RSIGN_MINISIG),
        bytes_reply(RSIGN_PACKAGE.to_vec()),
    )?;
    expect_install_offer(&rig.check(), NEXT)?;
    let mut u = fetch(rig.handle(), &rig.inputs)?;
    su::prepare_download(&mut u, &rig.inputs)
        .map_err(|e| not(format!("prepare_download: {}", show(&e))))?;
    download_and_verify(&u, RSIGN_PACKAGE)?;
    rig.expect_count(&package_path(NEXT), 1)?;
    rig.expect_trap_zero()
}

/// PD-36, SI-35: the production inputs, constructed only (no request).
fn production_inputs() -> Outcome {
    let i = UpdateInputs::production(HttpConfig::production());
    ensure(i.policy == UrlPolicy::HttpsOnly, || "policy".to_owned())?;
    ensure(i.timeouts == UpdateTimeouts::production(), || {
        "timeouts".to_owned()
    })?;
    ensure(i.packaging == su::packaging(), || "packaging".to_owned())?;
    ensure(i.http == HttpConfig::production(), || "http".to_owned())
}
