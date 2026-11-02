//! install::batch (T003): the Update-all cases (plan §7.1 SC-002 to SC-010, SC-027).
//!
//! Every case builds its roots with `fresh_elevated_roots()` (through `Ev`) or `fresh_temp_roots()` (through `Fx`),
//! drives `FakeElevation` (never a real prompt) and reaches the loopback `FixtureServer` only through the fixture
//! seam; every asset is signed by the rig's throwaway `TestSigner`. A scripted helper `done` applies the installer's
//! effect (`Machine::install`) inside the launch, never before stage 1 (spec "Scripted helper effects").

use super::tests::{
    ALPHA, BETA, Ev, FEED_ROUTE, Fx, MAC, Machine, NOW, Plat, Release, Seam, V12, V13, WIN,
    code_of, host, installer, result_doc,
};
use super::*;
use crate::download::{DOWNLOADS_DIR, Progress};
use crate::extract::FsExtractIo;
use crate::fixture_server::{HoldGate, Reply};
use crate::self_update::InFlightGuard;
use crate::tar_builder_tests::sha;
use crate::test_support::{
    FakeElevation, FaultyStagingIo, OneDriver, Op, RecordingInstallIo, Scripted,
    canonical_temp_dir, install_capture, snapshot, take_mine,
};
use crate::verify::hex_lower;
use log::Level;
use serde_json::{Value, json};
use std::collections::BTreeMap;
use std::fs;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::mpsc;
use std::thread;
use std::time::Instant;

/// Invented plugin names (never a shipped one), in item order.
const NAMES: [&str; 9] = [
    "Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta", "Iota",
];
/// SC-003: the whole-case bound, and the grace during which no further asset request may open.
const WATCHDOG: Duration = Duration::from_secs(30);
const GRACE: Duration = Duration::from_millis(300);

// ---- the rig -------------------------------------------------------------------------------------------------------

/// One scripted effect per job op, in op order: `"done"` or `"locked"`, and what the installer changed.
type Effect = (&'static str, Box<dyn Fn() + Send>);

/// PD-20: `Exited(0)` and a result with op *i* `effects[i].0`, after `effects[i].1()` ran (a `locked` op gets
/// `E_HELPER_LOCKED` and exit code 5, as the Phase 10 mapping reads them). The log tail names no path.
pub(super) fn ops_after(effects: Vec<Effect>) -> Scripted {
    Scripted {
        outcome: LaunchOutcome::Exited(0),
        result: Some(Box::new(move |job: &Path| {
            let bytes = fs::read(job).expect("the job");
            let doc: Value = serde_json::from_slice(&bytes).expect("the job is JSON");
            let ops = doc["ops"].as_array().cloned().unwrap_or_default();
            assert_eq!(ops.len(), effects.len(), "one scripted effect per op");
            let entries: Vec<Value> = ops
                .iter()
                .zip(&effects)
                .enumerate()
                .map(|(i, (op, (outcome, effect)))| {
                    effect();
                    let (code, exit_code) = match *outcome {
                        "locked" => (Some("E_HELPER_LOCKED"), Some(5)),
                        _ => (None, None),
                    };
                    json!({
                        "index": i, "op": op["op"], "outcome": outcome, "code": code, "exit_code": exit_code,
                        "seconds": 1.5, "log_tail": ["the installer's log"],
                    })
                })
                .collect();
            Some(serde_json::to_vec(&result_doc(&bytes, None, entries)).expect("JSON"))
        })),
    }
}

/// A `done` op whose installer placed 1.3.0 and recorded it.
fn done(machine: &Machine) -> Effect {
    let machine = machine.clone();
    ("done", Box::new(move || machine.install(V13)))
}

/// A `locked` op: the installer changed nothing.
fn locked() -> Effect {
    ("locked", Box::new(|| {}))
}

/// `names` at `version`, as the webview sends them.
fn items(names: &[&str], version: &str) -> Vec<BatchItem> {
    names
        .iter()
        .map(|name| BatchItem {
            name: (*name).to_owned(),
            expected_version: version.to_owned(),
        })
        .collect()
}

/// An update to 1.3.0 as the single command reports it.
fn updated(name: &str, notice: Notice) -> ActionSummary {
    ActionSummary {
        name: name.to_owned(),
        action: ActionKind::Update,
        version: Some(V13.to_owned()),
        kept: Vec::new(),
        notice: Some(notice),
    }
}

/// Each result as `Ok("ok")` or `Err(code)`, in request order, with its name.
fn outcomes(summary: &BatchSummary) -> Vec<(String, Result<&'static str, &'static str>)> {
    summary
        .results
        .iter()
        .map(|r| {
            let outcome = match &r.outcome {
                Ok(_) => Ok("ok"),
                Err(e) => Err(e.code()),
            };
            (r.name.clone(), outcome)
        })
        .collect()
}

/// An `Ev` for `plat` whose `names` are installed at 1.2.0 (managed copy and record) and offered at 1.3.0, so every
/// card offers Update; each plugin's `Machine`, in order.
fn updatable(plat: Plat, names: &[&str]) -> (Ev, Vec<Machine>) {
    let ev = Ev::new(plat);
    // The entries of the whole feed: a test feed's Windows uninstall key follows the plugin's place in it.
    let installed: Vec<(&str, &str)> = names.iter().map(|name| (*name, V12)).collect();
    let doc = ev.doc_for(plat, &installed);
    let machines: Vec<Machine> = names
        .iter()
        .map(|name| {
            let machine = ev.machine(&Fx::entry_in(&doc, name));
            machine.install(V12);
            machine
        })
        .collect();
    let offered: Vec<(&str, &str)> = names.iter().map(|name| (*name, V13)).collect();
    ev.offer(&offered);
    (ev, machines)
}

/// The route an elevated asset is served at (as `Ev::offer` sets it).
fn asset_route(plat: Plat, name: &str, version: &str) -> String {
    format!(
        "/{}/v{version}/{}",
        name.to_ascii_lowercase(),
        plat.asset(name, version)
    )
}

/// The batch's `CommandEnv` for `ev`'s OS over `io`, `elevation` and `drivers`, with the fixture seam.
fn env_with<'a>(
    ev: &'a Ev,
    io: &'a dyn InstallIo,
    elevation: &'a dyn Elevation,
    drivers: &'a dyn DriverResolver,
    seam: &'a Seam<'a>,
) -> CommandEnv<'a> {
    let mut env = ev.fx.rig.command_env_with(
        ev.plat.os,
        ev.plat.arch,
        io,
        &FsExtractIo,
        drivers,
        (elevation, &ev.fx.rig.host_io),
    );
    env.request_seam = Some(seam);
    env
}

/// One elevated batch over the native resolver and `FsInstallIo`, the rig's staging as the batch's seam.
fn batch(
    ev: &Ev,
    elevation: &dyn Elevation,
    policy: &BatchPolicy,
    items: &[BatchItem],
    progress: &mut dyn FnMut(BatchProgress),
) -> Result<BatchSummary, InstallError> {
    let seam = ev.fx.seam();
    let env = env_with(ev, &FsInstallIo, elevation, &NativeDrivers, &seam);
    batch_for_command(
        env,
        &ev.fx.rig.staging,
        policy,
        items,
        NOW,
        &*ev.live,
        progress,
    )
}

/// An `Fx` (Linux) whose `names` were installed at 1.2.0 by `LinuxDriver` and are offered at 1.3.0.
fn linux_updatable(names: &[&str]) -> Fx {
    let fx = Fx::new();
    for name in names {
        fx.offer(&[&Release::of(name, V12)]);
        fx.install(name, V12);
    }
    let releases: Vec<Release> = names.iter().map(|name| Release::of(name, V13)).collect();
    fx.offer(&releases.iter().collect::<Vec<_>>());
    fx
}

/// One forced-Linux batch over `drivers` and `FsInstallIo`.
fn linux_batch(
    fx: &Fx,
    drivers: &dyn DriverResolver,
    items: &[BatchItem],
    progress: &mut dyn FnMut(BatchProgress),
) -> Result<BatchSummary, InstallError> {
    let seam = fx.seam();
    let env = fx.env(Os::Linux, Arch::X86_64, &FsInstallIo, drivers, &seam);
    batch_for_command(
        env,
        &fx.rig.staging,
        &BatchPolicy::PRODUCTION,
        items,
        NOW,
        &host(),
        progress,
    )
}

/// `snapshot` with directory lengths zeroed.
fn tree(root: &Path) -> Vec<(String, char, u64)> {
    snapshot(root)
        .into_iter()
        .map(|(p, k, len)| (p, k, if k == 'd' { 0 } else { len }))
        .collect()
}

/// The verified artifact `<staging>/downloads/<hex>/<asset>` of `name` at 1.3.0.
fn staged_path(ev: &Ev, name: &str) -> PathBuf {
    ev.fx
        .ctx()
        .roots
        .staging_dir()
        .join(DOWNLOADS_DIR)
        .join(hex_lower(&sha(&installer(name, V13))))
        .join(ev.plat.asset(name, V13))
}

/// The installed version detection reads for `name` (through `status::build_with_os`).
fn installed_version(ev: &Ev, name: &str) -> Option<String> {
    ev.view()
        .plugins
        .into_iter()
        .find(|p| p.name == name)
        .unwrap_or_else(|| panic!("no card for {name}"))
        .installed_version
}

// ---- SC-002: one prompt per batch ----------------------------------------------------------------------------------

fn one_launch(plat: Plat) {
    for n in [1, 3, 9] {
        let names = &NAMES[..n];
        let (ev, machines) = updatable(plat, names);
        let elevation = FakeElevation::refusing();
        elevation.push(ops_after(machines.iter().map(done).collect()));
        let summary = batch(
            &ev,
            &elevation,
            &BatchPolicy::PRODUCTION,
            &items(names, V13),
            &mut |_| {},
        )
        .expect("the batch runs");
        assert_eq!(elevation.launches().len(), 1, "{plat:?} N={n}: one launch");
        let jobs = elevation.jobs();
        assert_eq!(jobs.len(), 1, "{plat:?} N={n}: one job");
        let job: Value = serde_json::from_slice(&jobs[0]).expect("the job is JSON");
        let ops = job["ops"].as_array().expect("ops");
        let files: Vec<String> = ops
            .iter()
            .map(|op| {
                assert_eq!(op["op"], plat.installer_op(), "{plat:?} N={n}");
                let file = op["file"].as_str().expect("a file");
                file.rsplit(['/', '\\']).next().unwrap_or(file).to_owned()
            })
            .collect();
        let want: Vec<String> = names.iter().map(|name| plat.asset(name, V13)).collect();
        assert_eq!(files, want, "{plat:?} N={n}: N ops in item order");
        let results: Vec<BatchItemResult> = names
            .iter()
            .map(|name| BatchItemResult {
                name: (*name).to_owned(),
                outcome: Ok(updated(name, plat.notice())),
            })
            .collect();
        assert_eq!(summary.results, results, "{plat:?} N={n}");
        assert_eq!(summary.notice, Some(plat.notice()), "{plat:?} N={n}");
        assert!(
            ev.jobs_left().is_empty(),
            "{plat:?} N={n}: {:?}",
            ev.jobs_left()
        );
    }
}

#[test]
fn one_launch_windows() {
    one_launch(WIN);
}

#[test]
fn one_launch_macos() {
    one_launch(MAC);
}

#[test]
fn no_launch_linux() {
    let names = &NAMES[..3];
    let fx = linux_updatable(names);
    let summary =
        linux_batch(&fx, &NativeDrivers, &items(names, V13), &mut |_| {}).expect("the batch runs");
    assert!(fx.rig.elevation.launches().is_empty(), "no launch");
    assert!(fx.rig.elevation.jobs().is_empty(), "no job");
    assert_eq!(
        outcomes(&summary),
        names
            .iter()
            .map(|name| ((*name).to_owned(), Ok("ok")))
            .collect::<Vec<_>>()
    );
    for (r, name) in summary.results.iter().zip(names) {
        let s = r.outcome.as_ref().expect("updated");
        assert_eq!(
            (s.action, s.version.as_deref(), s.notice),
            (ActionKind::Update, Some(V13), Some(Notice::RescanDaw)),
            "{name}"
        );
        let manifest = fx.manifest(&FsInstallIo, name).expect("a manifest");
        assert_eq!(manifest.version, V13, "{name}: placed at 1.3.0");
    }
    assert_eq!(summary.notice, Some(Notice::RescanDaw));
}

// ---- SC-003: bounded parallel downloads ----------------------------------------------------------------------------

/// The test side of the gate: holds every asset response until `min(bound, left)` are open, checks during `GRACE`
/// that no further one opens, then releases the oldest; the peak of open responses, or why it failed.
fn control(gate: &HoldGate, bound: usize, total: usize) -> Result<usize, String> {
    let start = Instant::now();
    let mut peak = 0;
    for released in 0..total {
        let want = bound.min(total - released);
        if !gate.wait_open(want, WATCHDOG.saturating_sub(start.elapsed())) {
            gate.release_all();
            return Err(format!("{want} open expected, {:?} open", gate.open()));
        }
        thread::sleep(GRACE);
        let open = gate.open();
        if open.len() != want {
            gate.release_all();
            return Err(format!("after the grace {want} open expected, {open:?}"));
        }
        peak = peak.max(open.len());
        gate.release_one();
    }
    Ok(peak)
}

fn bounded(bound: usize) {
    let names = &NAMES[..5];
    let (mut ev, machines) = updatable(WIN, names);
    // No retry can fire while a response is held (SC-003).
    ev.fx.rig.policy.stall = Duration::from_secs(60);
    ev.fx.rig.policy.attempt_deadline = Duration::from_secs(120);
    let gate = HoldGate::new();
    let routes: Vec<String> = names
        .iter()
        .map(|name| asset_route(WIN, name, V13))
        .collect();
    for (name, route) in names.iter().zip(&routes) {
        ev.fx.server.set_route(
            route,
            Reply::Held {
                gate: gate.clone(),
                reply: Box::new(Reply::Ranged {
                    body: installer(name, V13),
                }),
            },
        );
    }
    let elevation = FakeElevation::refusing();
    elevation.push(ops_after(machines.iter().map(done).collect()));
    let policy = BatchPolicy {
        parallel_downloads: bound,
    };
    let (summary, controlled, fired) = thread::scope(|s| {
        let (finished, finished_rx) = mpsc::channel::<()>();
        let watchdog = {
            let gate = gate.clone();
            s.spawn(move || {
                let fired = finished_rx.recv_timeout(WATCHDOG).is_err();
                if fired {
                    gate.release_all();
                }
                fired
            })
        };
        let controller = {
            let gate = gate.clone();
            s.spawn(move || control(&gate, bound, names.len()))
        };
        let summary = batch(&ev, &elevation, &policy, &items(names, V13), &mut |_| {});
        let controlled = controller.join().expect("the controller");
        let _ = finished.send(());
        (summary, controlled, watchdog.join().expect("the watchdog"))
    });
    assert!(!fired, "bound {bound}: the 30 s watchdog fired");
    assert_eq!(
        controlled,
        Ok(bound),
        "bound {bound}: the peak of open transfers"
    );
    let summary = summary.expect("the batch runs");
    assert_eq!(
        outcomes(&summary),
        names
            .iter()
            .map(|name| ((*name).to_owned(), Ok("ok")))
            .collect::<Vec<_>>(),
        "bound {bound}: every item verified and updated"
    );
    let requests = ev.fx.server.requests();
    for route in &routes {
        assert_eq!(
            requests.iter().filter(|r| r.path == *route).count(),
            1,
            "bound {bound}: {route} requested once (no retry)"
        );
    }
}

#[test]
fn download_bound() {
    bounded(3);
    bounded(1);
}

#[test]
fn production_bound_is_three() {
    assert_eq!(BatchPolicy::PRODUCTION.parallel_downloads, 3);
    assert_eq!(
        serde_json::from_value::<BatchItem>(json!({ "name": ALPHA, "expectedVersion": V13 }))
            .expect("the webview's two keys"),
        BatchItem {
            name: ALPHA.to_owned(),
            expected_version: V13.to_owned(),
        }
    );
    // No argument of the command can change the bound (D3).
    for doc in [
        json!({ "name": ALPHA, "expectedVersion": V13, "parallelDownloads": 1 }),
        json!({ "name": ALPHA, "expectedVersion": V13, "parallel_downloads": 1 }),
        json!({ "name": ALPHA, "expected_version": V13 }),
    ] {
        assert!(
            serde_json::from_value::<BatchItem>(doc.clone()).is_err(),
            "{doc}"
        );
    }
}

// ---- SC-004: sequential installs, after the downloads --------------------------------------------------------------

/// A driver that stamps one shared sequence at the entry and exit of each run, then delegates.
struct StampDriver<'a> {
    inner: &'a dyn InstallDriver,
    seq: &'a AtomicUsize,
    /// (start stamp, end stamp, actions) per run.
    runs: Mutex<Vec<(usize, usize, usize)>>,
}

impl InstallDriver for StampDriver<'_> {
    fn os(&self) -> Os {
        self.inner.os()
    }

    fn run(&self, env: InstallEnv<'_>, batch: Vec<Action>) -> Vec<ActionResult> {
        let start = self.seq.fetch_add(1, Ordering::SeqCst);
        let actions = batch.len();
        let results = self.inner.run(env, batch);
        let end = self.seq.fetch_add(1, Ordering::SeqCst);
        self.runs
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push((start, end, actions));
        results
    }
}

/// Stamps each item's `Verified` event on the shared sequence.
fn stamp_verified<'a>(
    seq: &'a AtomicUsize,
    verified: &'a Mutex<Vec<usize>>,
) -> impl FnMut(BatchProgress) + 'a {
    move |p| {
        if let BatchProgress::Item {
            event:
                InstallProgress::Download {
                    event: Progress::Verified,
                },
            ..
        } = p
        {
            verified
                .lock()
                .unwrap_or_else(PoisonError::into_inner)
                .push(seq.fetch_add(1, Ordering::SeqCst));
        }
    }
}

#[test]
fn installs_after_downloads() {
    let names = &NAMES[..3];
    for plat in [WIN, MAC] {
        let (ev, machines) = updatable(plat, names);
        let elevation = FakeElevation::refusing();
        elevation.push(ops_after(machines.iter().map(done).collect()));
        let seq = AtomicUsize::new(0);
        let verified = Mutex::new(Vec::new());
        let stamp = StampDriver {
            inner: plat.driver(),
            seq: &seq,
            runs: Mutex::new(Vec::new()),
        };
        let drivers = OneDriver(Some(&stamp));
        let seam = ev.fx.seam();
        let env = env_with(&ev, &FsInstallIo, &elevation, &drivers, &seam);
        let summary = batch_for_command(
            env,
            &ev.fx.rig.staging,
            &BatchPolicy::PRODUCTION,
            &items(names, V13),
            NOW,
            &*ev.live,
            &mut stamp_verified(&seq, &verified),
        )
        .expect("the batch runs");
        assert!(
            summary.results.iter().all(|r| r.outcome.is_ok()),
            "{plat:?}"
        );
        let verified = verified
            .into_inner()
            .unwrap_or_else(PoisonError::into_inner);
        let runs = stamp
            .runs
            .into_inner()
            .unwrap_or_else(PoisonError::into_inner);
        assert_eq!(verified.len(), 3, "{plat:?}: three downloads verified");
        assert_eq!(runs.len(), 1, "{plat:?}: one driver call");
        assert_eq!(runs[0].2, 3, "{plat:?}: with three actions");
        let last = verified.iter().copied().max().expect("a stamp");
        assert!(
            runs[0].0 > last,
            "{plat:?}: the driver started after the last download: {runs:?} {verified:?}"
        );
    }
    // Linux: one run per item, in item order, never overlapping, all after the last download.
    let fx = linux_updatable(names);
    let seq = AtomicUsize::new(0);
    let verified = Mutex::new(Vec::new());
    let stamp = StampDriver {
        inner: &LinuxDriver,
        seq: &seq,
        runs: Mutex::new(Vec::new()),
    };
    let summary = linux_batch(
        &fx,
        &OneDriver(Some(&stamp)),
        &items(names, V13),
        &mut stamp_verified(&seq, &verified),
    )
    .expect("the batch runs");
    assert!(summary.results.iter().all(|r| r.outcome.is_ok()));
    let verified = verified
        .into_inner()
        .unwrap_or_else(PoisonError::into_inner);
    let runs = stamp
        .runs
        .into_inner()
        .unwrap_or_else(PoisonError::into_inner);
    assert_eq!(verified.len(), 3);
    assert_eq!(runs.len(), 3, "one run per item: {runs:?}");
    assert!(
        runs.iter().all(|r| r.2 == 1),
        "one action per run: {runs:?}"
    );
    let last = verified.iter().copied().max().expect("a stamp");
    assert!(runs[0].0 > last, "{runs:?} {verified:?}");
    for pair in runs.windows(2) {
        assert!(pair[0].1 < pair[1].0, "overlapping runs: {runs:?}");
    }
}

// ---- SC-005, SC-006: locked and the notice -------------------------------------------------------------------------

#[test]
fn locked_item_continues() {
    let names = &NAMES[..3];
    let (ev, machines) = updatable(WIN, names);
    let elevation = FakeElevation::refusing();
    elevation.push(ops_after(vec![
        done(&machines[0]),
        locked(),
        done(&machines[2]),
    ]));
    let summary = batch(
        &ev,
        &elevation,
        &BatchPolicy::PRODUCTION,
        &items(names, V13),
        &mut |_| {},
    )
    .expect("the batch runs");
    assert_eq!(
        summary.results,
        vec![
            BatchItemResult {
                name: ALPHA.to_owned(),
                outcome: Ok(updated(ALPHA, Notice::RescanDaw)),
            },
            BatchItemResult {
                name: BETA.to_owned(),
                outcome: Err(InstallError::Locked),
            },
            BatchItemResult {
                name: NAMES[2].to_owned(),
                outcome: Ok(updated(NAMES[2], Notice::RescanDaw)),
            },
        ]
    );
    assert_eq!(summary.notice, Some(Notice::RescanDaw));
    // The webview's view: the error as its code-first text, exactly one of summary / error.
    let view = summary.view();
    assert_eq!(view.notice, Some(Notice::RescanDaw));
    assert_eq!(view.results[1].name, BETA);
    assert_eq!(view.results[1].summary, None);
    assert_eq!(
        view.results[1].error.as_deref(),
        Some("E_INSTALL_LOCKED: A plug-in file is in use. Close your DAW and retry.")
    );
    assert_eq!(
        view.results[0].summary,
        Some(updated(ALPHA, Notice::RescanDaw))
    );
    assert_eq!(view.results[0].error, None);
    // Detection afterwards: the locked item keeps its old version.
    assert_eq!(installed_version(&ev, ALPHA).as_deref(), Some(V13));
    assert_eq!(installed_version(&ev, BETA).as_deref(), Some(V12));
    assert_eq!(installed_version(&ev, NAMES[2]).as_deref(), Some(V13));
}

#[test]
fn notice_cases() {
    for plat in [WIN, MAC] {
        // One updated.
        let (ev, machines) = updatable(plat, &[ALPHA]);
        let elevation = FakeElevation::refusing();
        elevation.push(ops_after(vec![done(&machines[0])]));
        let summary = batch(
            &ev,
            &elevation,
            &BatchPolicy::PRODUCTION,
            &items(&[ALPHA], V13),
            &mut |_| {},
        )
        .expect("the batch runs");
        assert_eq!(summary.notice, Some(plat.notice()), "{plat:?}");
        assert_eq!(
            summary.results[0].outcome,
            Ok(updated(ALPHA, plat.notice())),
            "{plat:?}: the item's notice is the single command's"
        );
        // None updated (the prompt refused).
        let (ev, _) = updatable(plat, &[ALPHA, BETA]);
        let summary = batch(
            &ev,
            &FakeElevation::refusing(),
            &BatchPolicy::PRODUCTION,
            &items(&[ALPHA, BETA], V13),
            &mut |_| {},
        )
        .expect("the batch runs");
        assert_eq!(summary.notice, None, "{plat:?}: none updated");
    }
    // Forced Linux: the per-item LinuxDriver path.
    let fx = linux_updatable(&[ALPHA]);
    let summary =
        linux_batch(&fx, &NativeDrivers, &items(&[ALPHA], V13), &mut |_| {}).expect("runs");
    assert_eq!(summary.notice, Some(Notice::RescanDaw));
    let summary =
        linux_batch(&fx, &NativeDrivers, &items(&[ALPHA], V13), &mut |_| {}).expect("runs");
    assert_eq!(
        outcomes(&summary),
        vec![(ALPHA.to_owned(), Err("E_INSTALL_NOT_NEWER"))]
    );
    assert_eq!(summary.notice, None, "Linux: none updated");
}

// ---- SC-008: failures stay per item --------------------------------------------------------------------------------

#[test]
fn download_failure_excluded() {
    let names = &NAMES[..3];
    let wrong = |ev: &Ev, name: &str| {
        let mut body = installer(name, V13);
        body[0] ^= 1;
        ev.fx
            .server
            .set_route(&asset_route(ev.plat, name, V13), Reply::Ranged { body });
    };
    // One wrong hash: that item fails, the driver receives the other two.
    let (ev, machines) = updatable(WIN, names);
    wrong(&ev, BETA);
    let elevation = FakeElevation::refusing();
    elevation.push(ops_after(vec![done(&machines[0]), done(&machines[2])]));
    let summary = batch(
        &ev,
        &elevation,
        &BatchPolicy::PRODUCTION,
        &items(names, V13),
        &mut |_| {},
    )
    .expect("the batch runs");
    assert_eq!(
        outcomes(&summary),
        vec![
            (ALPHA.to_owned(), Ok("ok")),
            (BETA.to_owned(), Err("E_VERIFY_SHA256")),
            (NAMES[2].to_owned(), Ok("ok")),
        ]
    );
    let job: Value = serde_json::from_slice(&elevation.jobs()[0]).expect("job JSON");
    assert_eq!(job["ops"].as_array().map(Vec::len), Some(2), "{job}");
    // Every download fails: no driver run, no job, no launch.
    let (ev, _) = updatable(WIN, names);
    for name in names {
        wrong(&ev, name);
    }
    let elevation = FakeElevation::refusing();
    let summary = batch(
        &ev,
        &elevation,
        &BatchPolicy::PRODUCTION,
        &items(names, V13),
        &mut |_| {},
    )
    .expect("the batch runs");
    assert!(
        summary
            .results
            .iter()
            .all(|r| matches!(&r.outcome, Err(e) if e.code() == "E_VERIFY_SHA256")),
        "{:?}",
        summary.results
    );
    assert!(elevation.launches().is_empty(), "no launch");
    assert!(elevation.jobs().is_empty(), "no job");
    assert!(ev.jobs_left().is_empty());
    assert_eq!(summary.notice, None);
}

#[test]
fn staging_full_excluded() {
    let names = &NAMES[..2];
    let (mut ev, machines) = updatable(WIN, names);
    ev.fx.rig.staging = FaultyStagingIo::failing(Op::Write, 1, io::ErrorKind::StorageFull);
    let elevation = FakeElevation::refusing();
    elevation.push(ops_after(vec![done(&machines[1])]));
    let summary = batch(
        &ev,
        &elevation,
        &BatchPolicy {
            parallel_downloads: 1,
        },
        &items(names, V13),
        &mut |_| {},
    )
    .expect("the batch runs");
    assert_eq!(
        outcomes(&summary),
        vec![
            (ALPHA.to_owned(), Err("E_STAGING_FULL")),
            (BETA.to_owned(), Ok("ok")),
        ]
    );
    let part = ev
        .fx
        .ctx()
        .roots
        .staging_dir()
        .join(DOWNLOADS_DIR)
        .join(format!("{}.part", hex_lower(&sha(&installer(ALPHA, V13)))));
    assert!(
        matches!(fs::symlink_metadata(&part), Err(e) if e.kind() == io::ErrorKind::NotFound),
        "the failed item's .part is removed"
    );
    let job: Value = serde_json::from_slice(&elevation.jobs()[0]).expect("job JSON");
    assert_eq!(job["ops"].as_array().map(Vec::len), Some(1), "{job}");
}

// ---- SC-009: the re-check per item ---------------------------------------------------------------------------------

#[test]
fn state_changed_items() {
    let names = &NAMES[..4];
    let (ev, machines) = updatable(WIN, names);
    // Beta is already at 1.3.0.
    machines[1].install(V13);
    // Gamma's entry has no install block (and so, by the contract's `dependentRequired`, no asset).
    let offered: Vec<(&str, &str)> = names.iter().map(|name| (*name, V13)).collect();
    let mut doc: Value =
        serde_json::from_slice(&ev.doc_for(WIN, &offered)).expect("the feed is JSON");
    let gamma = doc["plugins"][NAMES[2]].as_object_mut().expect("an entry");
    gamma.remove("platforms");
    gamma.remove("install");
    ev.fx.server.set_route(
        FEED_ROUTE,
        Reply::Ok {
            body: serde_json::to_vec(&doc).expect("JSON"),
            content_type: "application/json",
        },
    );
    let elevation = FakeElevation::refusing();
    elevation.push(ops_after(vec![done(&machines[3])]));
    let rec = RecordingInstallIo::forwarding(&FsInstallIo);
    let readings = Mutex::new(Vec::new());
    let hook = |stage: BatchStage| {
        readings
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push((
                stage,
                ev.fx.counts(),
                ev.fx.rig.staging.calls(),
                rec.mutating_calls(),
            ));
    };
    let before = ev.fx.counts();
    let staging_before = ev.fx.rig.staging.calls();
    let seam = ev.fx.seam();
    let mut env = env_with(&ev, &rec, &elevation, &NativeDrivers, &seam);
    env.stage_seam = Some(&hook);
    let mut wanted = items(names, V13);
    wanted[0].expected_version = V12.to_owned();
    let summary = batch_for_command(
        env,
        &ev.fx.rig.staging,
        &BatchPolicy::PRODUCTION,
        &wanted,
        NOW,
        &*ev.live,
        &mut |_| {},
    )
    .expect("the batch runs");
    assert_eq!(
        outcomes(&summary),
        vec![
            (ALPHA.to_owned(), Err("E_DOWNLOAD_VERSION_CHANGED")),
            (BETA.to_owned(), Err("E_INSTALL_NOT_NEWER")),
            (NAMES[2].to_owned(), Err("E_DOWNLOAD_UNAVAILABLE")),
            (NAMES[3].to_owned(), Ok("ok")),
        ]
    );
    let readings = readings
        .into_inner()
        .unwrap_or_else(PoisonError::into_inner);
    assert_eq!(readings.len(), 2, "{readings:?}");
    assert_eq!(
        readings[0],
        (
            BatchStage::ChecksDone,
            (before.0 + 1, before.1),
            staging_before,
            0
        ),
        "the end of stage 1: one feed request, no asset request, no staging or mutating install call"
    );
    assert_eq!(readings[1].0, BatchStage::DownloadsDone);
    assert_eq!(
        ev.fx.counts().0,
        before.0 + 1,
        "exactly one feed request for the batch"
    );
}

// ---- SC-010: exclusion ---------------------------------------------------------------------------------------------

/// A driver that, inside stage 3, reads `any_busy` and tries a single update of `probe`, then delegates.
struct BusyDriver<'a> {
    ev: &'a Ev,
    probe: &'a str,
    inner: &'a dyn InstallDriver,
    seen: Mutex<Vec<(bool, &'static str)>>,
}

impl InstallDriver for BusyDriver<'_> {
    fn os(&self) -> Os {
        self.inner.os()
    }

    fn run(&self, env: InstallEnv<'_>, batch: Vec<Action>) -> Vec<ActionResult> {
        let busy = any_busy(&self.ev.fx.state());
        let seam = self.ev.fx.seam();
        let other = FakeElevation::refusing();
        let single = for_command(
            self.ev.env(&FsInstallIo, &other, &seam),
            self.probe,
            V13,
            "update",
            NOW,
            &*self.ev.live,
            &mut |_| {},
        );
        self.seen
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push((busy, code_of(&single)));
        self.inner.run(env, batch)
    }
}

#[test]
fn busy_items() {
    let names = &NAMES[..2];
    // A held busy key for one item: that item E_INSTALL_BUSY, the other updated.
    {
        let (ev, machines) = updatable(WIN, names);
        let held = hold_busy_for_test(&ev.fx.state(), BETA);
        let elevation = FakeElevation::refusing();
        elevation.push(ops_after(vec![done(&machines[0])]));
        let summary = batch(
            &ev,
            &elevation,
            &BatchPolicy::PRODUCTION,
            &items(names, V13),
            &mut |_| {},
        )
        .expect("the batch runs");
        drop(held);
        assert_eq!(
            summary.results[1].outcome,
            Err(InstallError::Busy {
                name: BETA.to_owned()
            })
        );
        assert_eq!(
            summary.results[0].outcome,
            Ok(updated(ALPHA, Notice::RescanDaw))
        );
    }
    // Stage 2: while the batch downloads, a single update of one of its items is E_INSTALL_BUSY.
    {
        let (ev, machines) = updatable(WIN, names);
        let gate = HoldGate::new();
        ev.fx.server.set_route(
            &asset_route(WIN, ALPHA, V13),
            Reply::Held {
                gate: gate.clone(),
                reply: Box::new(Reply::Ranged {
                    body: installer(ALPHA, V13),
                }),
            },
        );
        let elevation = FakeElevation::refusing();
        elevation.push(ops_after(machines.iter().map(done).collect()));
        let (summary, during) = thread::scope(|s| {
            let probe = s.spawn(|| {
                let open = gate.wait_open(1, WATCHDOG);
                let seam = ev.fx.seam();
                let other = FakeElevation::refusing();
                let single = for_command(
                    ev.env(&FsInstallIo, &other, &seam),
                    ALPHA,
                    V13,
                    "update",
                    NOW,
                    &*ev.live,
                    &mut |_| {},
                );
                gate.release_all();
                (open, code_of(&single))
            });
            let summary = batch(
                &ev,
                &elevation,
                &BatchPolicy::PRODUCTION,
                &items(names, V13),
                &mut |_| {},
            );
            (summary, probe.join().expect("the probe"))
        });
        assert_eq!(during, (true, "E_INSTALL_BUSY"), "stage 2");
        assert!(
            summary
                .expect("the batch runs")
                .results
                .iter()
                .all(|r| r.outcome.is_ok())
        );
        assert!(
            !any_busy(&ev.fx.state()),
            "every key released after the batch"
        );
    }
    // Stage 3: the keys are held while the driver runs.
    {
        let (ev, machines) = updatable(WIN, names);
        let elevation = FakeElevation::refusing();
        elevation.push(ops_after(machines.iter().map(done).collect()));
        let driver = BusyDriver {
            ev: &ev,
            probe: BETA,
            inner: &WindowsDriver,
            seen: Mutex::new(Vec::new()),
        };
        let drivers = OneDriver(Some(&driver));
        let seam = ev.fx.seam();
        let summary = batch_for_command(
            env_with(&ev, &FsInstallIo, &elevation, &drivers, &seam),
            &ev.fx.rig.staging,
            &BatchPolicy::PRODUCTION,
            &items(names, V13),
            NOW,
            &*ev.live,
            &mut |_| {},
        )
        .expect("the batch runs");
        assert!(summary.results.iter().all(|r| r.outcome.is_ok()));
        assert_eq!(
            driver
                .seen
                .into_inner()
                .unwrap_or_else(PoisonError::into_inner),
            vec![(true, "E_INSTALL_BUSY")],
            "stage 3"
        );
        assert!(
            !any_busy(&ev.fx.state()),
            "every key released after the batch"
        );
    }
    // A self-update in flight refuses the whole batch before any request.
    {
        let (ev, _) = updatable(WIN, names);
        let state = ev.fx.state();
        let flag = InFlightGuard::enter(Some(&state));
        let before = ev.fx.counts();
        let r = batch(
            &ev,
            &FakeElevation::refusing(),
            &BatchPolicy::PRODUCTION,
            &items(names, V13),
            &mut |_| {},
        );
        drop(flag);
        assert_eq!(code_of(&r), crate::self_update::SELF_UPDATING_CODE);
        assert_eq!(ev.fx.counts(), before, "no request");
        assert!(!any_busy(&state));
    }
}

// ---- SC-027: the remaining FRs -------------------------------------------------------------------------------------

#[test]
fn input_checks() {
    let (ev, _) = updatable(WIN, &[ALPHA]);
    let state = ev.fx.state();
    let many = |n: usize| -> Vec<BatchItem> {
        (0..n)
            .map(|i| BatchItem {
                name: format!("Plugin{i:02}"),
                expected_version: V13.to_owned(),
            })
            .collect()
    };
    let mut twice = items(&[ALPHA, BETA], V13);
    twice[1].name = ALPHA.to_owned();
    for (wanted, reason) in [
        (Vec::new(), "no items"),
        (many(MAX_BATCH_ITEMS + 1), "more than 32 items"),
        (twice, "a plugin is named twice"),
    ] {
        let calls = AtomicUsize::new(0);
        let hook = |_: BatchStage| {
            calls.fetch_add(1, Ordering::SeqCst);
        };
        let before = ev.fx.counts();
        let elevation = FakeElevation::refusing();
        let seam = ev.fx.seam();
        let mut env = env_with(&ev, &FsInstallIo, &elevation, &NativeDrivers, &seam);
        env.stage_seam = Some(&hook);
        let r = batch_for_command(
            env,
            &ev.fx.rig.staging,
            &BatchPolicy::PRODUCTION,
            &wanted,
            NOW,
            &*ev.live,
            &mut |_| {},
        );
        assert_eq!(r, Err(InstallError::Batch { reason }), "{reason}");
        assert_eq!(
            r.expect_err(reason).to_string(),
            format!("E_INSTALL_BATCH: {reason}")
        );
        assert_eq!(ev.fx.counts(), before, "{reason}: no request");
        assert_eq!(
            calls.load(Ordering::SeqCst),
            0,
            "{reason}: no stage reached"
        );
        assert!(!any_busy(&state), "{reason}: no busy key");
        assert!(elevation.launches().is_empty(), "{reason}");
    }
    assert_eq!(MAX_BATCH_ITEMS, 32);
    // Exactly the cap is accepted: every unknown name is its own refused result.
    let summary = batch(
        &ev,
        &FakeElevation::refusing(),
        &BatchPolicy::PRODUCTION,
        &many(MAX_BATCH_ITEMS),
        &mut |_| {},
    )
    .expect("32 items are a batch");
    assert_eq!(summary.results.len(), 32);
    assert!(summary.results.iter().all(|r| r.outcome.is_err()));
    assert!(!any_busy(&state));
}

#[test]
fn refused_prompt() {
    let names = &NAMES[..2];
    for plat in [WIN, MAC] {
        let (ev, _) = updatable(plat, names);
        let plugins = ev.fx.base.join("plugins");
        let before = tree(&plugins);
        let elevation = FakeElevation::refusing();
        for (label, assets) in [("first", 2), ("retry", 0)] {
            let counts = ev.fx.counts();
            let summary = batch(
                &ev,
                &elevation,
                &BatchPolicy::PRODUCTION,
                &items(names, V13),
                &mut |_| {},
            )
            .expect("the batch runs");
            assert_eq!(
                outcomes(&summary),
                names
                    .iter()
                    .map(|name| ((*name).to_owned(), Err("E_INSTALL_REFUSED")))
                    .collect::<Vec<_>>(),
                "{plat:?} {label}"
            );
            assert_eq!(
                ev.fx.counts().1 - counts.1,
                assets,
                "{plat:?} {label}: asset requests"
            );
            assert_eq!(tree(&plugins), before, "{plat:?} {label}: nothing changed");
            for name in names {
                assert!(
                    staged_path(&ev, name).is_file(),
                    "{plat:?} {label}: {name}'s verified artifact stays in staging"
                );
            }
            assert!(ev.jobs_left().is_empty(), "{plat:?} {label}");
        }
        assert_eq!(
            elevation.launches().len(),
            2,
            "{plat:?}: one launch per batch"
        );
    }
}

#[test]
fn elevation_busy() {
    let names = &NAMES[..2];
    for plat in [WIN, MAC] {
        let (ev, _) = updatable(plat, names);
        let elevation = FakeElevation::refusing();
        let held = elevation.guard().hold_for_test();
        let summary = batch(
            &ev,
            &elevation,
            &BatchPolicy::PRODUCTION,
            &items(names, V13),
            &mut |_| {},
        )
        .expect("the batch runs");
        drop(held);
        for r in &summary.results {
            assert_eq!(
                r.outcome,
                Err(InstallError::ElevationBusy { stale: false }),
                "{plat:?} {}",
                r.name
            );
        }
        assert!(elevation.launches().is_empty(), "{plat:?}");
    }
}

#[test]
fn registrar_kill_once() {
    let names = &NAMES[..3];
    let (ev, machines) = updatable(MAC, names);
    let elevation = FakeElevation::refusing();
    elevation.push(ops_after(machines.iter().map(done).collect()));
    let summary = batch(
        &ev,
        &elevation,
        &BatchPolicy::PRODUCTION,
        &items(names, V13),
        &mut |_| {},
    )
    .expect("the batch runs");
    assert!(summary.results.iter().all(|r| r.outcome.is_ok()));
    assert_eq!(ev.killalls(), 1, "three done: one registrar kill");
    let (ev, _) = updatable(MAC, names);
    let elevation = FakeElevation::refusing();
    elevation.push(ops_after(vec![locked(), locked(), locked()]));
    let summary = batch(
        &ev,
        &elevation,
        &BatchPolicy::PRODUCTION,
        &items(names, V13),
        &mut |_| {},
    )
    .expect("the batch runs");
    assert!(
        summary
            .results
            .iter()
            .all(|r| r.outcome == Err(InstallError::Locked))
    );
    assert_eq!(ev.killalls(), 0, "all locked: no registrar kill");
}

/// Per item name, its events in arrival order.
fn by_name(events: &[BatchProgress]) -> BTreeMap<String, Vec<InstallProgress>> {
    let mut out: BTreeMap<String, Vec<InstallProgress>> = BTreeMap::new();
    for e in events {
        if let BatchProgress::Item { name, event } = e {
            out.entry(name.clone()).or_default().push(event.clone());
        }
    }
    out
}

/// Each item's download events begin with `Started` and end with `Verified`.
fn check_downloads(per: &BTreeMap<String, Vec<InstallProgress>>, names: &[&str], label: &str) {
    let keys: Vec<&str> = per.keys().map(String::as_str).collect();
    let mut want: Vec<&str> = names.to_vec();
    want.sort_unstable();
    assert_eq!(keys, want, "{label}: every item and only those");
    for (name, events) in per {
        let downloads: Vec<&Progress> = events
            .iter()
            .filter_map(|e| match e {
                InstallProgress::Download { event } => Some(event),
                _ => None,
            })
            .collect();
        assert!(
            matches!(downloads.first(), Some(Progress::Started { .. })),
            "{label} {name}: {downloads:?}"
        );
        assert_eq!(
            downloads.last(),
            Some(&&Progress::Verified),
            "{label} {name}"
        );
    }
}

#[test]
fn progress_mapping() {
    let names = &NAMES[..3];
    for plat in [WIN, MAC] {
        let (ev, machines) = updatable(plat, names);
        let elevation = FakeElevation::refusing();
        elevation.push(ops_after(machines.iter().map(done).collect()));
        let mut events = Vec::new();
        batch(
            &ev,
            &elevation,
            &BatchPolicy::PRODUCTION,
            &items(names, V13),
            &mut |e| events.push(e),
        )
        .expect("the batch runs");
        let elevating: Vec<usize> = events
            .iter()
            .enumerate()
            .filter(|(_, e)| **e == BatchProgress::Elevating)
            .map(|(i, _)| i)
            .collect();
        assert_eq!(elevating.len(), 1, "{plat:?}: Elevating once: {events:?}");
        assert_eq!(
            elevating[0],
            events.len() - 1,
            "{plat:?}: after every download"
        );
        let per = by_name(&events);
        check_downloads(&per, names, &format!("{plat:?}"));
        assert!(
            per.values()
                .flatten()
                .all(|e| matches!(e, InstallProgress::Download { .. })),
            "{plat:?}: the elevated run sends only Elevating"
        );
    }
    // Linux: no Elevating; each item's install events under its own name, in item order.
    let fx = linux_updatable(names);
    let mut events = Vec::new();
    linux_batch(&fx, &NativeDrivers, &items(names, V13), &mut |e| {
        events.push(e)
    })
    .expect("the batch runs");
    assert!(
        !events.contains(&BatchProgress::Elevating),
        "Linux: no Elevating"
    );
    let per = by_name(&events);
    check_downloads(&per, names, "Linux");
    for (name, events) in &per {
        let installs: Vec<&InstallProgress> = events
            .iter()
            .filter(|e| !matches!(e, InstallProgress::Download { .. }))
            .collect();
        assert_eq!(
            installs.last(),
            Some(&&InstallProgress::Done),
            "{name}: {installs:?}"
        );
    }
    let mut order: Vec<&str> = Vec::new();
    for e in &events {
        if let BatchProgress::Item { name, event } = e
            && !matches!(event, InstallProgress::Download { .. })
            && order.last() != Some(&name.as_str())
        {
            order.push(name);
        }
    }
    assert_eq!(
        order,
        names.to_vec(),
        "Linux: stage 3 in item order, one item at a time"
    );
}

#[test]
fn log_lines() {
    install_capture();
    let _ = take_mine();
    let names = &NAMES[..3];
    let temp = text(&canonical_temp_dir());
    let temp = temp.strip_prefix(r"\\?\").unwrap_or(&temp).to_owned();
    let check = |lines: &[String], url: &str, label: &str, elevated: bool| {
        for name in names {
            let mine: Vec<&String> = lines
                .iter()
                .filter(|l| l.starts_with(&format!("{name} ")))
                .collect();
            assert_eq!(
                mine.len(),
                1,
                "{label} {name}: one line per item: {lines:?}"
            );
        }
        assert!(
            lines.contains(&format!(
                "{ALPHA} {V12} update: E_DOWNLOAD_VERSION_CHANGED, placed 0, removed 0, kept 0"
            )),
            "{label}: the refusal line: {lines:?}"
        );
        assert_eq!(
            lines
                .iter()
                .filter(|l| l.starts_with("batch of "))
                .collect::<Vec<_>>(),
            vec!["batch of 3: 2 accepted, 2 verified, 2 updated"],
            "{label}"
        );
        assert_eq!(
            lines
                .iter()
                .filter(|l| l.starts_with("elevated batch of 2 op(s)"))
                .count(),
            usize::from(elevated),
            "{label}: the elevated driver's line (SI-12): {lines:?}"
        );
        let hashes: Vec<String> = names
            .iter()
            .flat_map(|name| [installer(name, V12), installer(name, V13)])
            .map(|bytes| hex_lower(&sha(&bytes)))
            .collect();
        for line in lines {
            assert!(!line.contains(&temp), "{label}: a path in {line:?}");
            assert!(!line.contains(url), "{label}: a fixture URL in {line:?}");
            assert!(!line.contains("http"), "{label}: a URL in {line:?}");
            for hash in &hashes {
                assert!(!line.contains(hash.as_str()), "{label}: a hash in {line:?}");
            }
        }
    };
    let mine = || -> Vec<String> {
        take_mine()
            .into_iter()
            .filter(|(target, level, _)| target == LOG_TARGET && *level <= Level::Info)
            .map(|(_, _, m)| m)
            .collect()
    };
    let mut wanted = items(names, V13);
    wanted[0].expected_version = V12.to_owned();
    for plat in [WIN, MAC] {
        let (ev, machines) = updatable(plat, names);
        let elevation = FakeElevation::refusing();
        elevation.push(ops_after(vec![done(&machines[1]), done(&machines[2])]));
        let _ = take_mine();
        let summary = batch(
            &ev,
            &elevation,
            &BatchPolicy::PRODUCTION,
            &wanted,
            &mut |_| {},
        )
        .expect("the batch runs");
        assert_eq!(
            summary.results.iter().filter(|r| r.outcome.is_ok()).count(),
            2
        );
        check(&mine(), &ev.fx.server.url(""), &format!("{plat:?}"), true);
    }
    let fx = linux_updatable(names);
    let _ = take_mine();
    let summary = linux_batch(&fx, &NativeDrivers, &wanted, &mut |_| {}).expect("the batch runs");
    assert_eq!(
        summary.results.iter().filter(|r| r.outcome.is_ok()).count(),
        2
    );
    check(&mine(), &fx.server.url(""), "Linux", false);
}

#[test]
fn batch_level_errors() {
    install_capture();
    let _ = take_mine();
    let names = &NAMES[..2];
    let item_lines = || -> usize {
        take_mine()
            .into_iter()
            .filter(|(target, _, m)| {
                target == LOG_TARGET && names.iter().any(|n| m.starts_with(&format!("{n} ")))
            })
            .count()
    };
    // A corrupt feed and no cache: the batch's E_INSTALL_NO_FEED, before any item.
    {
        let (ev, _) = updatable(WIN, names);
        ev.fx.server.set_route(
            FEED_ROUTE,
            Reply::Ok {
                body: b"{ not json".to_vec(),
                content_type: "application/json",
            },
        );
        let calls = AtomicUsize::new(0);
        let hook = |_: BatchStage| {
            calls.fetch_add(1, Ordering::SeqCst);
        };
        let before = ev.fx.counts();
        let elevation = FakeElevation::refusing();
        let seam = ev.fx.seam();
        let mut env = env_with(&ev, &FsInstallIo, &elevation, &NativeDrivers, &seam);
        env.stage_seam = Some(&hook);
        let r = batch_for_command(
            env,
            &ev.fx.rig.staging,
            &BatchPolicy::PRODUCTION,
            &items(names, V13),
            NOW,
            &*ev.live,
            &mut |_| {},
        );
        assert_eq!(code_of(&r), "E_INSTALL_NO_FEED");
        assert_eq!(ev.fx.counts().1, before.1, "no asset request");
        assert_eq!(calls.load(Ordering::SeqCst), 0, "no stage reached");
        assert!(!any_busy(&ev.fx.state()));
        assert_eq!(item_lines(), 0, "no item line");
    }
    // No driver for the OS: the batch's E_INSTALL_NO_DRIVER, no item checked.
    {
        let (ev, _) = updatable(WIN, names);
        let calls = AtomicUsize::new(0);
        let hook = |_: BatchStage| {
            calls.fetch_add(1, Ordering::SeqCst);
        };
        let elevation = FakeElevation::refusing();
        let seam = ev.fx.seam();
        let none = OneDriver(None);
        let mut env = env_with(&ev, &FsInstallIo, &elevation, &none, &seam);
        env.stage_seam = Some(&hook);
        let before = ev.fx.counts();
        let r = batch_for_command(
            env,
            &ev.fx.rig.staging,
            &BatchPolicy::PRODUCTION,
            &items(names, V13),
            NOW,
            &*ev.live,
            &mut |_| {},
        );
        assert_eq!(r, Err(InstallError::NoDriver));
        assert_eq!(ev.fx.counts().1, before.1, "no asset request");
        assert_eq!(calls.load(Ordering::SeqCst), 0, "no stage reached");
        assert!(!any_busy(&ev.fx.state()), "no busy key");
        assert_eq!(item_lines(), 0, "no item check ran");
        assert!(elevation.launches().is_empty());
    }
}
