//! `install::loopback`: the `Elevation` that runs the helper library in-process against temp roots (owner T010; plan §4.4).
//!
//! `launch` parses the argv with `cli::parse_args` and calls `krate_manager_helper::run` over a `TempIo` on the test's
//! base, a `FakeRegistry` and a runner that answers every child with a `ScriptedRunner` script whose effect
//! "installs" or "uninstalls" under the temp roots. Every root it hands the helper lies under the base (tasks Rule 1),
//! so a root left on a production value panics in `TempIo` instead of touching the machine.
//!
//! Spawn indices (`exit_for`, `never_exits`, `serve_log`) count every `ProcessRunner::spawn` the helper makes through
//! this loopback, across launches, from 0; the `pkgutil` queries made through `output` are not counted.

use super::job::{self, JobOp, WrittenJob};
use super::{ElevatedCommand, Elevation, ElevationGuard, FsInstallIo, InstallIo, LaunchOutcome};
use crate::detect::{InnoValues, PkgOutputs, RecordRead};
use crate::download::DOWNLOADS_DIR;
use crate::feed::{InstallFacts, PlatformKey, PluginEntry};
use crate::platform::{Os, VENDOR_DIR};
use crate::roots::{RootKind, Roots};
use crate::test_signer::TestSigner;
use crate::test_support::{LiveHostFacts, TestPlugin, fresh_elevated_roots, test_feed};
use krate_manager_helper::classify::{InnoVerdict, classify_inno};
use krate_manager_helper::roots::{
    AllowedRoots, HELPER_FOLDER, MacosRoots, OsRoots, StateDirRule, WindowsRoots,
};
use krate_manager_helper::seams::fake::{
    Event, FakeRegistry, Match, Script, ScriptedRunner, TempIo, World,
};
use krate_manager_helper::seams::{ChildId, ProcessRunner, Registry, Seams};
use krate_manager_helper::{HelperOs, cli, digest, error, ops};
use serde_json::Value;
use std::collections::{BTreeMap, BTreeSet};
use std::ffi::OsString;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard, PoisonError};
use std::time::Duration;

/// The helper path the loopback reports: a bare name, never executed.
const LOOPBACK_HELPER: &str = "loopback-krate-manager-helper";
/// The Windows run base under the test's base (plan §4.4).
const RUN_DIR: &str = "run";
/// The macOS `/Applications` stand-in under the base.
const APPLICATIONS_DIR: &str = "Applications";
/// The macOS run ancestors under the base: `<base>/Library`, `<base>/Library/Application Support`.
const LIBRARY_DIR: &str = "Library";
const APP_SUPPORT_DIR: &str = "Application Support";
/// The components root beside the temp VST3 global root when the roots carry no AU root (off macOS).
const AU_FALLBACK_DIR: &str = "au-global";
/// The uninstaller an Inno setup leaves in its install directory (FR-031).
const UNINS_NAME: &str = "unins000.exe";
/// The Inno argv's log argument prefix (FR-030 step 3).
const LOG_ARG: &str = "/LOG=";
/// The `pkgutil` query flags (FR-033).
const PKG_INFO: &str = "--pkg-info";
const FILES: &str = "--files";
const ONLY_FILES: &str = "--only-files";
const ONLY_DIRS: &str = "--only-dirs";
/// The usage exit of the helper binary (FR-005).
const USAGE_EXIT: i32 = 2;

/// What one spawn does instead of its default effect.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Plan {
    Exit(i32),
    NeverExits,
}

/// A Windows install the loopback made: its record key, registry subkey and install directory.
#[derive(Clone, Debug)]
struct InnoInstall {
    key: String,
    subkey: String,
    dir: PathBuf,
}

/// A macOS receipt the loopback made: the three `pkgutil` outputs.
#[derive(Clone, Debug)]
struct Receipt {
    info: String,
    files: String,
    dirs: String,
}

/// What a spawn's script does to the temp world when it runs.
enum Act {
    InnoInstall(PluginEntry),
    PkgInstall(PluginEntry),
    InnoUninstall(String),
}

#[derive(Default)]
struct State {
    /// Installer digest (lower hex) → the entry a run of it installs.
    installs: BTreeMap<String, PluginEntry>,
    plans: BTreeMap<usize, Plan>,
    logs: BTreeMap<usize, Vec<u8>>,
    launches: Vec<ElevatedCommand>,
    jobs: Vec<Vec<u8>>,
    spawns: Vec<Vec<OsString>>,
    /// Keyed by the uninstaller path as the registry holds it (unquoted).
    inno: BTreeMap<String, InnoInstall>,
    /// Keyed by pkg id.
    pkgs: BTreeMap<String, Receipt>,
}

/// The loopback `Elevation` (plan §4.4, PD-8, SI-19; tasks TA-14).
pub(super) struct Loopback<'a> {
    os: Os,
    base: PathBuf,
    roots: AllowedRoots,
    world: World,
    host: &'a LiveHostFacts,
    guard: ElevationGuard,
    classify: fn(i32, &[String]) -> InnoVerdict,
    helper: PathBuf,
    state: Mutex<State>,
}

impl<'a> Loopback<'a> {
    /// The loopback for `os` over `roots` (from `fresh_elevated_roots()`), `base` its `TempDir`; records go to `host`.
    pub(super) fn new(os: Os, roots: &Roots, base: &Path, host: &'a LiveHostFacts) -> Loopback<'a> {
        let base = fs::canonicalize(base)
            .unwrap_or_else(|e| panic!("loopback cannot canonicalize {}: {e}", base.display()));
        let world = World::new();
        let made = |dir: &Path| {
            assert!(
                dir != base && dir.starts_with(&base),
                "loopback: {} is outside the test sandbox {}",
                dir.display(),
                base.display()
            );
            fs::create_dir_all(dir)
                .unwrap_or_else(|e| panic!("loopback cannot create {}: {e}", dir.display()));
        };
        let os_roots = match os {
            Os::Windows => {
                let run_base = base.join(RUN_DIR);
                made(&run_base);
                OsRoots::Windows(WindowsRoots {
                    vst3_vendor: roots.vst3_global().join(VENDOR_DIR),
                    preset_vendor: roots.presets().factory.clone(),
                    run_base,
                })
            }
            Os::Macos => {
                let library = base.join(LIBRARY_DIR);
                let app_support = library.join(APP_SUPPORT_DIR);
                let run_base = app_support.join(HELPER_FOLDER);
                made(&app_support);
                for dir in [&library, &app_support, &run_base] {
                    world.root_owned(dir);
                }
                let components = roots
                    .plugin_roots()
                    .iter()
                    .find(|r| matches!(r.kind, RootKind::AuGlobal))
                    .map(|r| r.path.clone())
                    .unwrap_or_else(|| roots.vst3_global().with_file_name(AU_FALLBACK_DIR));
                OsRoots::Macos(MacosRoots {
                    vst3: roots.vst3_global().to_path_buf(),
                    components,
                    presets: roots.presets().factory.clone(),
                    applications: base.join(APPLICATIONS_DIR),
                    volume: base.clone(),
                    run_base,
                    run_ancestors: vec![library, app_support],
                })
            }
            Os::Linux => OsRoots::None,
        };
        Loopback {
            os,
            roots: AllowedRoots {
                os: os_roots,
                state_dir: StateDirRule::Exact(roots.state_dir().to_path_buf()),
            },
            base,
            world,
            host,
            guard: ElevationGuard::new(),
            classify: classify_inno,
            helper: PathBuf::from(LOOPBACK_HELPER),
            state: Mutex::new(State::default()),
        }
    }

    /// The Inno outcome rule the helper runs with (default `classify_inno`).
    pub(super) fn classify(self, f: fn(i32, &[String]) -> InnoVerdict) -> Self {
        Loopback {
            classify: f,
            ..self
        }
    }

    /// A run of the installer whose digest is `sha256_hex` installs `entry` at its version: Windows an Inno record in
    /// `host.inno`, the registry key with its `UninstallString` and that file, and the VST3 bundle under the vendor
    /// root; macOS a pkg record in `host.pkg`, the receipt `pkgutil` reports, and the VST3 and component bundles.
    pub(super) fn installs(&self, sha256_hex: &str, entry: &PluginEntry) {
        self.state()
            .installs
            .insert(sha256_hex.to_owned(), entry.clone());
    }

    /// Spawn `spawn_index` exits `code` and installs nothing.
    pub(super) fn exit_for(&self, spawn_index: usize, code: i32) {
        self.state().plans.insert(spawn_index, Plan::Exit(code));
    }

    /// Spawn `spawn_index` never exits (the helper's wait times out) and installs nothing.
    pub(super) fn never_exits(&self, spawn_index: usize) {
        self.state().plans.insert(spawn_index, Plan::NeverExits);
    }

    /// Spawn `spawn_index`, an Inno setup, writes `log` as its `/LOG=` file (default: an empty log).
    pub(super) fn serve_log(&self, spawn_index: usize, log: Vec<u8>) {
        self.state().logs.insert(spawn_index, log);
    }

    /// Every launch, in order.
    pub(super) fn launches(&self) -> Vec<ElevatedCommand> {
        self.state().launches.clone()
    }

    /// The job bytes of every launch, read before the helper ran.
    pub(super) fn jobs(&self) -> Vec<Vec<u8>> {
        self.state().jobs.clone()
    }

    /// The argv of every spawn, in spawn-index order.
    pub(super) fn spawns(&self) -> Vec<Vec<OsString>> {
        self.state().spawns.clone()
    }

    fn state(&self) -> MutexGuard<'_, State> {
        self.state.lock().unwrap_or_else(PoisonError::into_inner)
    }

    fn helper_os(&self) -> HelperOs {
        match self.os {
            Os::Windows => HelperOs::Windows,
            Os::Macos => HelperOs::Macos,
            Os::Linux => HelperOs::Linux,
        }
    }

    /// Panics unless `path` is strictly under the base (lexical; for paths the loopback builds itself).
    fn check(&self, path: &Path) {
        assert!(
            path != self.base && path.starts_with(&self.base),
            "loopback: {} is outside the test sandbox {}",
            path.display(),
            self.base.display()
        );
    }

    /// A path the helper named, with its parent canonicalized, checked strictly under the base.
    fn sandboxed(&self, path: &Path) -> PathBuf {
        let (Some(parent), Some(name)) = (path.parent(), path.file_name()) else {
            panic!("loopback: {} has no parent", path.display());
        };
        let parent = fs::canonicalize(parent)
            .unwrap_or_else(|e| panic!("loopback cannot canonicalize {}: {e}", parent.display()));
        let out = parent.join(name);
        self.check(&out);
        out
    }

    fn write(&self, path: &Path, bytes: &[u8]) {
        self.check(path);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)
                .unwrap_or_else(|e| panic!("loopback cannot create {}: {e}", parent.display()));
        }
        fs::write(path, bytes)
            .unwrap_or_else(|e| panic!("loopback cannot write {}: {e}", path.display()));
    }

    /// A VST3 bundle with a `moduleinfo.json` of `version`; returns the file written.
    fn write_vst3(&self, bundle: &Path, name: &str, version: &str) -> PathBuf {
        let info = bundle
            .join("Contents")
            .join("Resources")
            .join("moduleinfo.json");
        let text = serde_json::json!({ "Name": name, "Version": version });
        self.write(&info, text.to_string().as_bytes());
        info
    }

    /// An AU component with an XML `Info.plist` of `version`; returns the file written.
    fn write_component(&self, bundle: &Path, version: &str) -> PathBuf {
        let plist = bundle.join("Contents").join("Info.plist");
        let text = format!(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\"><dict>\
             <key>CFBundleShortVersionString</key><string>{version}</string></dict></plist>\n"
        );
        self.write(&plist, text.as_bytes());
        plist
    }

    /// `path` relative to the volume (the base), `/`-joined, as a `pkgutil --files` line.
    fn receipt_line(&self, path: &Path) -> String {
        let rel = path
            .strip_prefix(&self.base)
            .unwrap_or_else(|_| panic!("loopback: {} is not under the volume", path.display()));
        rel.components()
            .map(|c| c.as_os_str().to_string_lossy().into_owned())
            .collect::<Vec<_>>()
            .join("/")
    }

    /// The script of spawn `argv`, with its index recorded, its log written and its effect chosen.
    fn script_for(&self, argv: &[OsString]) -> Script<'_> {
        let (plan, log) = {
            let mut st = self.state();
            let index = st.spawns.len();
            st.spawns.push(argv.to_vec());
            (
                st.plans.get(&index).copied(),
                st.logs.remove(&index).unwrap_or_default(),
            )
        };
        // An Inno setup's argv ends with its `/LOG=` argument (FR-030 step 3).
        if let Some(log_path) = argv
            .get(ops::INNO_FLAGS.len() + 1)
            .and_then(|a| a.to_str())
            .and_then(|a| a.strip_prefix(LOG_ARG))
        {
            let log_path = self.sandboxed(Path::new(log_path));
            self.write(&log_path, &log);
        }
        match plan {
            Some(Plan::Exit(code)) => {
                return Script {
                    exit: code,
                    ..Script::default()
                };
            }
            Some(Plan::NeverExits) => {
                return Script {
                    never_exits: true,
                    ..Script::default()
                };
            }
            None => {}
        }
        if argv.first().is_some_and(|a| a == ops::PKGUTIL) {
            return self.pkgutil(argv);
        }
        let act = self.act_of(argv);
        Script {
            effect: Some(Box::new(move |_argv: &[OsString]| self.apply(&act))),
            ..Script::default()
        }
    }

    /// The default effect of an installer or uninstaller spawn; panics on any other argv.
    fn act_of(&self, argv: &[OsString]) -> Act {
        let text: Vec<&str> = argv.iter().filter_map(|a| a.to_str()).collect();
        assert_eq!(
            text.len(),
            argv.len(),
            "loopback: a non-UTF-8 argv {argv:?}"
        );
        let inno_install = text.len() == ops::INNO_FLAGS.len() + 2
            && text.get(1..=ops::INNO_FLAGS.len()) == Some(&ops::INNO_FLAGS[..]);
        let pkg_install = text.first() == Some(&ops::INSTALLER);
        let inno_uninstall = text.len() == ops::UNINSTALL_FLAGS.len() + 1
            && text.get(1..) == Some(&ops::UNINSTALL_FLAGS[..]);
        if inno_install || pkg_install {
            let copy = if inno_install {
                text[0]
            } else {
                text.get(2).copied().unwrap_or_default()
            };
            let copy = self.sandboxed(Path::new(copy));
            let bytes = fs::read(&copy)
                .unwrap_or_else(|e| panic!("loopback cannot read {}: {e}", copy.display()));
            let hex = digest::hex(&digest::sha256(&bytes));
            let entry = self.state().installs.get(&hex).cloned().unwrap_or_else(|| {
                panic!("loopback: no `installs` names the installer digest {hex}")
            });
            return if inno_install {
                Act::InnoInstall(entry)
            } else {
                Act::PkgInstall(entry)
            };
        }
        if inno_uninstall {
            return Act::InnoUninstall(text[0].to_owned());
        }
        panic!("loopback: no effect for the spawn {argv:?}")
    }

    fn apply(&self, act: &Act) {
        match act {
            Act::InnoInstall(entry) => self.inno_install(entry),
            Act::PkgInstall(entry) => self.pkg_install(entry),
            Act::InnoUninstall(unins) => self.inno_uninstall(unins),
        }
    }

    fn inno_install(&self, entry: &PluginEntry) {
        let OsRoots::Windows(w) = &self.roots.os else {
            panic!("loopback: an Inno install on a non-Windows loopback");
        };
        let facts = facts(entry);
        let key = facts
            .windows_uninstall_key
            .as_ref()
            .unwrap_or_else(|| panic!("loopback: {} has no windows_uninstall_key", entry.name))
            .as_str()
            .to_owned();
        let end = key
            .find('}')
            .unwrap_or_else(|| panic!("loopback: the key {key} has no app id"));
        let subkey = format!(
            "{}\\{}{}",
            ops::UNINSTALL_KEY,
            &key[..=end],
            ops::INNO_KEY_SUFFIX
        );
        let dir = w.vst3_vendor.join(&entry.name);
        self.write_vst3(
            &dir.join(facts.vst3_bundle.as_str()),
            &entry.name,
            &entry.version_text,
        );
        let unins = dir.join(UNINS_NAME);
        self.write(&unins, b"");
        let unins_text = path_text(&unins);
        let location = format!("{}\\", path_text(&dir));
        FakeRegistry::new(&self.world).set(
            &subkey,
            Some(&format!("\"{unins_text}\"")),
            Some(&location),
        );
        lock(&self.host.inno).insert(
            key.clone(),
            RecordRead::Found(InnoValues {
                display_version: Some(entry.version_text.clone()),
                install_location: Some(location),
            }),
        );
        self.state()
            .inno
            .insert(unins_text, InnoInstall { key, subkey, dir });
    }

    fn inno_uninstall(&self, unins: &str) {
        let Some(install) = self.state().inno.remove(unins) else {
            panic!("loopback: no install left the uninstaller {unins}");
        };
        self.check(&install.dir);
        fs::remove_dir_all(&install.dir)
            .unwrap_or_else(|e| panic!("loopback cannot remove {}: {e}", install.dir.display()));
        // The key goes when the helper's poll next reads the fake clock (D6).
        self.world
            .at(self.world.now(), Event::RemoveKey(install.subkey));
        lock(&self.host.inno).remove(&install.key);
    }

    fn pkg_install(&self, entry: &PluginEntry) {
        let OsRoots::Macos(m) = &self.roots.os else {
            panic!("loopback: a pkg install on a non-macOS loopback");
        };
        let facts = facts(entry);
        let id = facts
            .macos_pkg_id
            .as_ref()
            .unwrap_or_else(|| panic!("loopback: {} has no macos_pkg_id", entry.name))
            .as_str()
            .to_owned();
        let mut files = Vec::new();
        let mut dirs = BTreeSet::new();
        let mut bundles = vec![(m.vst3.clone(), m.vst3.join(facts.vst3_bundle.as_str()))];
        if let Some(component) = &facts.au_component {
            bundles.push((m.components.clone(), m.components.join(component.as_str())));
        }
        for (i, (root, bundle)) in bundles.iter().enumerate() {
            let file = if i == 0 {
                self.write_vst3(bundle, &entry.name, &entry.version_text)
            } else {
                self.write_component(bundle, &entry.version_text)
            };
            // pkgutil lists every directory of the payload, the structural ones above the roots included (D16).
            for dir in file.ancestors().skip(1) {
                if dir == self.base {
                    break;
                }
                if dir.starts_with(root) || root.starts_with(dir) {
                    dirs.insert(self.receipt_line(dir));
                }
            }
            files.push(self.receipt_line(&file));
        }
        let lines = |set: Vec<String>| set.iter().map(|l| format!("{l}\n")).collect::<String>();
        let receipt = Receipt {
            info: format!(
                "package-id: {id}\nversion: {}\nvolume: /\n",
                entry.version_text
            ),
            files: lines(files),
            dirs: lines(dirs.into_iter().collect()),
        };
        lock(&self.host.pkg).insert(
            id.clone(),
            RecordRead::Found(PkgOutputs {
                pkg_info: receipt.info.clone(),
                files: receipt.files.clone(),
            }),
        );
        self.state().pkgs.insert(id, receipt);
    }

    /// The `pkgutil` answers (FR-033): the receipt queries, and the one other call the helper makes, which drops the
    /// receipt and the pkg record at once.
    fn pkgutil(&self, argv: &[OsString]) -> Script<'_> {
        let text: Vec<&str> = argv.iter().skip(1).filter_map(|a| a.to_str()).collect();
        let receipt = |id: &str| self.state().pkgs.get(id).cloned();
        let answer = |out: Option<String>| match out {
            Some(stdout) => Script {
                stdout: stdout.into_bytes(),
                ..Script::default()
            },
            None => Script {
                exit: 1,
                ..Script::default()
            },
        };
        match text.as_slice() {
            [PKG_INFO, id] => answer(receipt(id).map(|r| r.info)),
            [FILES, id, ONLY_FILES] => answer(receipt(id).map(|r| r.files)),
            [FILES, id, ONLY_DIRS] => answer(receipt(id).map(|r| r.dirs)),
            [_, id] => {
                let gone = self.state().pkgs.remove(*id);
                lock(&self.host.pkg).remove(*id);
                answer(gone.map(|_| String::new()))
            }
            _ => panic!("loopback: no pkgutil answer for {argv:?}"),
        }
    }
}

impl Elevation for Loopback<'_> {
    fn helper(&self) -> Option<&Path> {
        Some(&self.helper)
    }

    fn launch(&self, command: &ElevatedCommand, _timeout: Duration) -> LaunchOutcome {
        let job = command
            .args
            .get(1)
            .map(PathBuf::from)
            .unwrap_or_else(|| panic!("loopback: no job argument in {:?}", command.args));
        let bytes = fs::read(&job)
            .unwrap_or_else(|e| panic!("loopback cannot read the job {}: {e}", job.display()));
        {
            let mut st = self.state();
            st.jobs.push(bytes);
            st.launches.push(command.clone());
        }
        let argv: Vec<OsString> = command.args.iter().map(OsString::from).collect();
        let Some(args) = cli::parse_args(&argv) else {
            return LaunchOutcome::Exited(USAGE_EXIT);
        };
        let io = TempIo::new(&self.world, &self.base);
        let runner = LoopbackRunner {
            loopback: self,
            children: Mutex::new(Vec::new()),
        };
        let registry = FakeRegistry::new(&self.world);
        let seams = Seams {
            io: &io,
            runner: &runner,
            registry: &registry,
            classify: self.classify,
        };
        let exit = krate_manager_helper::run(&args, self.helper_os(), &self.roots, &seams);
        LaunchOutcome::Exited(i32::from(exit.status))
    }

    fn guard(&self) -> &ElevationGuard {
        &self.guard
    }
}

/// The helper's `ProcessRunner` in a launch: each call is answered by a fresh `ScriptedRunner` holding the one script
/// the loopback chose for it, so the spawn index is the loopback's own.
struct LoopbackRunner<'l, 'a> {
    loopback: &'l Loopback<'a>,
    children: Mutex<Vec<(ScriptedRunner<'l>, ChildId)>>,
}

impl ProcessRunner for LoopbackRunner<'_, '_> {
    fn spawn(&self, argv: &[OsString]) -> io::Result<ChildId> {
        let inner = ScriptedRunner::new(&self.loopback.world);
        inner.on(Match::Any, self.loopback.script_for(argv));
        let child = inner.spawn(argv)?;
        let mut children = lock(&self.children);
        children.push((inner, child));
        Ok(ChildId(
            u64::try_from(children.len() - 1).unwrap_or(u64::MAX),
        ))
    }

    fn wait(&self, child: ChildId, timeout: Duration) -> io::Result<Option<i32>> {
        let children = lock(&self.children);
        let (inner, id) = usize::try_from(child.0)
            .ok()
            .and_then(|i| children.get(i))
            .ok_or_else(|| io::Error::from(io::ErrorKind::NotFound))?;
        inner.wait(*id, timeout)
    }

    fn output(&self, argv: &[OsString], timeout: Duration) -> io::Result<Option<(i32, Vec<u8>)>> {
        assert!(
            argv.first().is_some_and(|a| a == ops::PKGUTIL),
            "loopback: an output call that is not pkgutil: {argv:?}"
        );
        let inner = ScriptedRunner::new(&self.loopback.world);
        inner.on(Match::Any, self.loopback.pkgutil(argv));
        inner.output(argv, timeout)
    }
}

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    m.lock().unwrap_or_else(PoisonError::into_inner)
}

fn facts(entry: &PluginEntry) -> &InstallFacts {
    entry
        .release
        .as_ref()
        .and_then(|r| r.install.as_ref())
        .unwrap_or_else(|| panic!("loopback: {} has no install facts", entry.name))
}

fn path_text(path: &Path) -> String {
    path.to_str()
        .unwrap_or_else(|| panic!("loopback: {} is not UTF-8", path.display()))
        .to_owned()
}

// ---- the self-test (tasks TA-13) -------------------------------------------------------------------------------------

const ALPHA: &str = "Alpha";
const V12: &str = "1.2.0";
/// The test classifier's locked exit and log line (the loopback's own; the production rule is M-1's).
const TEST_LOCKED_EXIT: i32 = 99;
const TEST_LOCKED_LINE: &str = "loopback: a file is in use";

fn test_classify(code: i32, log: &[String]) -> InnoVerdict {
    if code == 0 {
        InnoVerdict::Installed
    } else if code == TEST_LOCKED_EXIT && log.iter().any(|l| l.contains(TEST_LOCKED_LINE)) {
        InnoVerdict::Locked
    } else {
        InnoVerdict::Failed
    }
}

/// `bytes` staged at `<staging>/downloads/<sha hex>/<asset>`, with its feed entry; returns (entry, canonical file, hex).
fn staged(
    roots: &Roots,
    key: PlatformKey,
    asset: &str,
    bytes: &[u8],
) -> (PluginEntry, String, String) {
    let sha = digest::sha256(bytes);
    let hex = digest::hex(&sha);
    let signature = TestSigner::new().sign_c3(bytes, ALPHA, V12, key, asset);
    let feed = test_feed(&[TestPlugin {
        name: ALPHA,
        version: V12,
        key,
        asset,
        size: bytes.len() as u64,
        sha256: sha,
        signature,
        min_os: None,
    }]);
    let entry = feed.plugins.get(ALPHA).expect("the entry").clone();
    let path = roots
        .staging_dir()
        .join(DOWNLOADS_DIR)
        .join(&hex)
        .join(asset);
    fs::create_dir_all(path.parent().expect("a parent")).expect("the downloads dir");
    fs::write(&path, bytes).expect("the staged artifact");
    let file = FsInstallIo
        .canonicalize(&path)
        .expect("canonical staged file");
    (entry, path_text(&file), hex)
}

/// `ops` written by `job::write` and launched through the loopback.
fn run(lb: &Loopback<'_>, roots: &Roots, ops: Vec<JobOp>) -> (LaunchOutcome, WrittenJob) {
    let job = job::write(&FsInstallIo, roots.state_dir(), ops).expect("job::write");
    let command = ElevatedCommand {
        program: lb.helper.clone(),
        args: vec![
            "--job".to_owned(),
            path_text(&job.path),
            "--job-sha256".to_owned(),
            job.sha256.clone(),
        ],
    };
    (lb.launch(&command, job::ceiling(&job.ops)), job)
}

/// `(outcome, code)` per op of the job's result file.
fn outcomes(job: &WrittenJob) -> Vec<(String, Option<String>)> {
    let bytes = fs::read(&job.result).expect("a result file");
    let doc: Value = serde_json::from_slice(&bytes).expect("result JSON");
    assert_eq!(doc["job_sha256"].as_str(), Some(job.sha256.as_str()));
    doc["ops"]
        .as_array()
        .expect("ops")
        .iter()
        .map(|op| {
            (
                op["outcome"].as_str().unwrap_or_default().to_owned(),
                op["code"].as_str().map(str::to_owned),
            )
        })
        .collect()
}

fn done(n: usize) -> Vec<(String, Option<String>)> {
    vec![("done".to_owned(), None); n]
}

/// Rule 1: every root the loopback hands the helper, the state dir included, lies under the base.
fn assert_roots_under_base(lb: &Loopback<'_>) {
    let mut paths: Vec<&Path> = match &lb.roots.os {
        OsRoots::Windows(w) => vec![&w.vst3_vendor, &w.preset_vendor, &w.run_base],
        OsRoots::Macos(m) => {
            let mut v: Vec<&Path> = vec![
                &m.vst3,
                &m.components,
                &m.presets,
                &m.applications,
                &m.volume,
                &m.run_base,
            ];
            v.extend(m.run_ancestors.iter().map(PathBuf::as_path));
            v
        }
        OsRoots::None => panic!("the smoke test runs Windows and macOS only"),
    };
    match &lb.roots.state_dir {
        StateDirRule::Exact(dir) => paths.push(dir),
        other => panic!("the loopback state dir rule is {other:?}, not Exact"),
    }
    for path in paths {
        assert!(
            path.starts_with(&lb.base),
            "{} is not under {}",
            path.display(),
            lb.base.display()
        );
    }
}

#[test]
fn loopback_smoke() {
    // Windows: install, uninstall, then one batch of three spawns scripted by exit_for / serve_log / never_exits.
    {
        let (td, roots) = fresh_elevated_roots();
        let host = LiveHostFacts::new(None);
        let lb = Loopback::new(Os::Windows, &roots, td.path(), &host).classify(test_classify);
        assert_roots_under_base(&lb);
        let asset = "Alpha-1.2.0-Windows-x64.exe";
        let (entry, file, hex) = staged(&roots, PlatformKey::WindowsX86_64, asset, b"an installer");
        lb.installs(&hex, &entry);
        let key = facts(&entry)
            .windows_uninstall_key
            .as_ref()
            .expect("a key")
            .as_str()
            .to_owned();
        let app_id = key[..=key.find('}').expect("an app id")].to_owned();
        let subkey = format!("{}\\{app_id}{}", ops::UNINSTALL_KEY, ops::INNO_KEY_SUFFIX);
        let OsRoots::Windows(w) = &lb.roots.os else {
            panic!("Windows roots");
        };
        let dir = w.vst3_vendor.join(ALPHA);
        let bundle = dir.join(facts(&entry).vst3_bundle.as_str());
        let install = || JobOp::RunInnoInstaller {
            file: file.clone(),
            sha256: hex.clone(),
        };

        let (outcome, job) = run(&lb, &roots, vec![install()]);
        assert_eq!(outcome, LaunchOutcome::Exited(0));
        assert_eq!(outcomes(&job), done(1));
        assert!(matches!(
            lock(&host.inno).get(&key),
            Some(RecordRead::Found(_))
        ));
        assert!(FakeRegistry::new(&lb.world).key_exists(&subkey));
        assert!(
            bundle
                .join("Contents")
                .join("Resources")
                .join("moduleinfo.json")
                .is_file()
        );
        assert!(dir.join(UNINS_NAME).is_file());

        let (outcome, job) = run(
            &lb,
            &roots,
            vec![JobOp::RunInnoUninstaller {
                app_id: app_id.clone(),
            }],
        );
        assert_eq!(outcome, LaunchOutcome::Exited(0));
        assert_eq!(outcomes(&job), done(1));
        assert!(lock(&host.inno).is_empty());
        assert!(!FakeRegistry::new(&lb.world).key_exists(&subkey));
        assert!(!dir.exists());

        lb.exit_for(2, 1);
        lb.exit_for(3, TEST_LOCKED_EXIT);
        lb.serve_log(
            3,
            format!("line one\r\n{TEST_LOCKED_LINE}\r\n").into_bytes(),
        );
        lb.never_exits(4);
        let (outcome, job) = run(&lb, &roots, vec![install(), install(), install()]);
        assert_eq!(outcome, LaunchOutcome::Exited(0));
        let code = |e: error::HelperError| Some(e.code.to_owned());
        assert_eq!(
            outcomes(&job),
            vec![
                ("failed".to_owned(), code(error::EXIT)),
                ("locked".to_owned(), code(error::LOCKED)),
                ("failed".to_owned(), code(error::TIMEOUT)),
            ]
        );
        assert!(
            lock(&host.inno).is_empty(),
            "a scripted spawn installs nothing"
        );
        assert!(!bundle.exists());

        let launches = lb.launches();
        let jobs = lb.jobs();
        assert_eq!((launches.len(), jobs.len()), (3, 3));
        assert_eq!(jobs[2], fs::read(&job.path).expect("the job"));
        let spawns = lb.spawns();
        assert_eq!(spawns.len(), 5);
        let flags: Vec<OsString> = ops::UNINSTALL_FLAGS.iter().map(OsString::from).collect();
        assert_eq!(spawns[1].get(1..), Some(&flags[..]));
    }
    // macOS: install, uninstall.
    {
        let (td, roots) = fresh_elevated_roots();
        let host = LiveHostFacts::new(None);
        let lb = Loopback::new(Os::Macos, &roots, td.path(), &host);
        assert_roots_under_base(&lb);
        let asset = "Alpha-1.2.0-macOS.pkg";
        let (entry, file, hex) = staged(&roots, PlatformKey::DarwinUniversal, asset, b"a pkg");
        lb.installs(&hex, &entry);
        let id = facts(&entry)
            .macos_pkg_id
            .as_ref()
            .expect("a pkg id")
            .as_str()
            .to_owned();
        let OsRoots::Macos(m) = &lb.roots.os else {
            panic!("macOS roots");
        };
        let vst3 = m.vst3.join(facts(&entry).vst3_bundle.as_str());
        let component = m.components.join(
            facts(&entry)
                .au_component
                .as_ref()
                .expect("a component")
                .as_str(),
        );

        let (outcome, job) = run(
            &lb,
            &roots,
            vec![JobOp::RunPkgInstaller { file, sha256: hex }],
        );
        assert_eq!(outcome, LaunchOutcome::Exited(0));
        assert_eq!(outcomes(&job), done(1));
        assert!(matches!(
            lock(&host.pkg).get(&id),
            Some(RecordRead::Found(_))
        ));
        assert!(
            vst3.join("Contents")
                .join("Resources")
                .join("moduleinfo.json")
                .is_file()
        );
        assert!(component.join("Contents").join("Info.plist").is_file());

        let (outcome, job) = run(
            &lb,
            &roots,
            vec![JobOp::UninstallPkg { pkg_id: id.clone() }],
        );
        assert_eq!(outcome, LaunchOutcome::Exited(0));
        assert_eq!(outcomes(&job), done(1));
        assert!(lock(&host.pkg).is_empty());
        assert!(!vst3.exists());
        assert!(!component.exists());
        assert!(m.vst3.is_dir() && m.components.is_dir(), "the roots stay");
        assert_eq!(lb.spawns().len(), 1);
    }
}
