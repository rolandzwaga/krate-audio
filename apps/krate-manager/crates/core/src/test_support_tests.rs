use super::*;
use serde_json::json;
use std::panic::{AssertUnwindSafe, catch_unwind};
use tempfile::TempDir;

/// `td/fake_tmp/sub/` and a sibling `td/outside/` inside a fresh `TempDir` (SC-007).
struct Fixture {
    td: TempDir,
    fake_tmp: PathBuf,
    sub: PathBuf,
    outside: PathBuf,
}

fn fixture() -> Fixture {
    let td = TempDir::new().expect("a fresh TempDir");
    let fake_tmp = td.path().join("fake_tmp");
    let sub = fake_tmp.join("sub");
    let outside = td.path().join("outside");
    std::fs::create_dir_all(&sub).expect("create fake_tmp/sub");
    std::fs::create_dir_all(&outside).expect("create outside");
    Fixture {
        td,
        fake_tmp,
        sub,
        outside,
    }
}

/// Recursive listing of `dir`: (relative path, is_dir, size in bytes), sorted.
fn listing(dir: &Path) -> Vec<(PathBuf, bool, u64)> {
    fn walk(root: &Path, dir: &Path, out: &mut Vec<(PathBuf, bool, u64)>) {
        for entry in std::fs::read_dir(dir).expect("read_dir") {
            let entry = entry.expect("dir entry");
            let path = entry.path();
            let meta = std::fs::symlink_metadata(&path).expect("symlink_metadata");
            let rel = path.strip_prefix(root).expect("strip_prefix").to_path_buf();
            out.push((rel, meta.is_dir(), meta.len()));
            if meta.is_dir() {
                walk(root, &path, out);
            }
        }
    }
    let mut out = Vec::new();
    walk(dir, dir, &mut out);
    out.sort();
    out
}

#[cfg(unix)]
fn make_dir_link(target: &Path, link: &Path) -> std::io::Result<()> {
    std::os::unix::fs::symlink(target, link)
}

#[cfg(windows)]
fn make_dir_link(target: &Path, link: &Path) -> std::io::Result<()> {
    std::os::windows::fs::symlink_dir(target, link)
}

/// `Some(link)` when `fake_tmp/link -> outside` was made; `None` only for a local
/// `ERROR_PRIVILEGE_NOT_HELD` (1314) with `CI` unset. Any other failure panics.
fn try_link_to_outside(f: &Fixture) -> Option<PathBuf> {
    let link = f.fake_tmp.join("link");
    match make_dir_link(&f.outside, &link) {
        Ok(()) => Some(link),
        Err(e) => {
            #[expect(
                clippy::disallowed_methods,
                reason = "symlink privilege failures fail only on CI"
            )]
            let on_ci = std::env::var_os("CI").is_some();
            if e.raw_os_error() == Some(1314) && !on_ci {
                eprintln!(
                    "symlink case skipped: creating {} needs the symlink privilege (os error 1314)",
                    link.display()
                );
                None
            } else {
                panic!("could not create the symlink {}: {e}", link.display());
            }
        }
    }
}

fn panic_text(payload: &(dyn std::any::Any + Send)) -> String {
    payload
        .downcast_ref::<String>()
        .cloned()
        .or_else(|| payload.downcast_ref::<&str>().map(|s| (*s).to_owned()))
        .expect("a string panic payload")
}

#[test]
fn check_under_temp_cases() {
    let f = fixture();
    let canonical_sub = std::fs::canonicalize(&f.sub).expect("canonical sub");
    assert_eq!(
        check_under_temp(&f.sub, &f.fake_tmp).expect("sub is under fake_tmp"),
        canonical_sub
    );
    assert!(matches!(
        check_under_temp(&f.outside, &f.fake_tmp),
        Err(GuardError::Outside { .. })
    ));
    assert!(matches!(
        check_under_temp(&f.fake_tmp, &f.fake_tmp),
        Err(GuardError::Outside { .. })
    ));
    let up_twice = f.sub.join("..").join("..");
    assert!(matches!(
        check_under_temp(&up_twice, &f.fake_tmp),
        Err(GuardError::Outside { .. })
    ));
    assert!(matches!(
        check_under_temp(&f.fake_tmp.join("none"), &f.fake_tmp),
        Err(GuardError::Canonicalize { which: "base", .. })
    ));
}

#[test]
fn check_under_temp_symlink_escape() {
    let f = fixture();
    let Some(link) = try_link_to_outside(&f) else {
        return;
    };
    assert!(matches!(
        check_under_temp(&link, &f.fake_tmp),
        Err(GuardError::Outside { .. })
    ));
}

#[test]
#[should_panic(expected = "temp_roots refused")]
fn temp_roots_in_outside_panics() {
    let f = fixture();
    let _roots = temp_roots_in(&f.outside, &f.fake_tmp);
}

#[test]
fn temp_roots_in_refusal_creates_nothing() {
    let f = fixture();
    assert!(listing(&f.outside).is_empty());

    let mut bases = vec![f.outside.clone()];
    bases.extend(try_link_to_outside(&f));
    for base in &bases {
        let result = catch_unwind(AssertUnwindSafe(|| temp_roots_in(base, &f.fake_tmp)));
        let payload = result.expect_err("temp_roots_in refuses a base outside the temp root");
        let message = panic_text(payload.as_ref());
        assert!(message.contains("temp_roots refused"), "message: {message}");
        assert!(listing(&f.outside).is_empty());
    }
    assert!(f.td.path().join("outside").is_dir());
}

#[test]
#[should_panic(expected = "temp_roots refused")]
fn temp_roots_on_temp_dir_itself_panics() {
    let _roots = temp_roots(&canonical_temp_dir());
}

#[test]
fn temp_roots_inside_temp_ok() {
    let (td, roots) = fresh_temp_roots();
    let temp = canonical_temp_dir();
    let td_canonical = std::fs::canonicalize(td.path()).expect("canonical TempDir");

    let mut all: Vec<&Path> = roots
        .plugin_roots()
        .iter()
        .map(|r| r.path.as_path())
        .collect();
    all.extend([
        roots.presets().factory.as_path(),
        roots.presets().user.as_path(),
        roots.state_dir(),
        roots.staging_dir(),
    ]);
    for root in all {
        assert!(
            root.starts_with(&temp),
            "{} not under {}",
            root.display(),
            temp.display()
        );
        assert!(
            root.starts_with(&td_canonical),
            "{} not under {}",
            root.display(),
            td_canonical.display()
        );
        assert!(root.is_dir(), "{} was not created", root.display());
    }
}

/// TA-6: both fakes record every call in the plan §4.8 format and answer an unlisted input with its default.
#[test]
fn fakes_record_calls_and_answer_defaults() {
    const INNO_KEY: &str = "{0A1F0000-0000-4000-8000-000000000001}_is1";
    const PKG_ID: &str = "com.krateaudio.alpha";
    let key = PlainName::checked("windows_uninstall_key", &json!(INNO_KEY))
        .expect("an uninstall key of the contract pattern");
    let pkg_id = PlainName::checked("macos_pkg_id", &json!(PKG_ID))
        .expect("a pkg id of the contract pattern");

    let empty = FakeHostFacts::default();
    assert_eq!(empty.os_version(), None);
    assert_eq!(empty.inno_values(&key), RecordRead::Absent);
    assert_eq!(empty.pkg_outputs(&pkg_id), RecordRead::Absent);

    let inno = InnoValues {
        display_version: Some("1.2.0".to_owned()),
        install_location: None,
    };
    let facts = FakeHostFacts {
        os_version: OsVersion::parse("15.7.9"),
        inno: BTreeMap::from([(INNO_KEY.to_owned(), RecordRead::Found(inno.clone()))]),
        pkg: BTreeMap::from([(PKG_ID.to_owned(), RecordRead::Failed("E_RECORD_TIMEOUT"))]),
        ..FakeHostFacts::default()
    };
    assert_eq!(facts.os_version(), OsVersion::parse("15.7.9"));
    assert_eq!(facts.inno_values(&key), RecordRead::Found(inno));
    assert_eq!(
        facts.pkg_outputs(&pkg_id),
        RecordRead::Failed("E_RECORD_TIMEOUT")
    );
    assert_eq!(
        *facts.calls.borrow(),
        [
            "os_version".to_owned(),
            format!("inno_values {INNO_KEY}"),
            format!("pkg_outputs {PKG_ID}"),
        ]
    );

    let io = FakeHostIo {
        runs: BTreeMap::from([(
            vec!["a".to_owned(), "b".to_owned()],
            RunOutcome::Output("x".to_owned()),
        )]),
        registry: BTreeMap::from([(
            "S".to_owned(),
            RegistryRead::Values([Some("v".to_owned()), None]),
        )]),
        rtl: Some((10, 0, 26200)),
        ..FakeHostIo::default()
    };
    let bound = Duration::from_secs(1);
    assert_eq!(
        io.run(&["a", "b"], bound),
        RunOutcome::Output("x".to_owned())
    );
    assert_eq!(io.run(&["c"], bound), RunOutcome::NoOutput);
    assert_eq!(
        io.registry_values("S", ["N1", "N2"]),
        RegistryRead::Values([Some("v".to_owned()), None])
    );
    assert_eq!(io.registry_values("T", ["N1", "N2"]), RegistryRead::NoKey);
    assert_eq!(io.rtl_version(), Some((10, 0, 26200)));
    assert_eq!(
        *io.calls.borrow(),
        [
            "run a b",
            "run c",
            "registry S N1 N2",
            "registry T N1 N2",
            "rtl_version"
        ]
    );
}

/// TA-3: the Phase 7 helpers, on the paths that do not delegate to `FsStagingIo` (T005 writes its bodies).
#[test]
fn phase7_helpers_cases() {
    use crate::feed::AssetFormat;
    use crate::http::HttpConfig;
    use crate::settings::Settings;
    use io::ErrorKind::{NotFound, PermissionDenied};

    assert_eq!(kat_signing_key().key_id_hex(), "440FA75168B852F7");
    let candidates = version::platform_candidates(NativePlatform.os(), Arch::native());
    assert_eq!(Some(&native_key()), candidates.first());

    // test_feed: two plugins on the native key, the KAT signature text read in place.
    let sig_path = Path::new(env!("CARGO_MANIFEST_DIR")).join(
        "../../../../tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz.minisig",
    );
    let signature = std::fs::read_to_string(&sig_path)
        .unwrap_or_else(|e| panic!("cannot read {}: {e}", sig_path.display()));
    let key = native_key();
    let plugins = || {
        [
            TestPlugin {
                name: "Alpha",
                version: "1.2.3",
                key,
                asset: "Alpha-1.2.3-test.bin",
                size: 452,
                sha256: [0x11; 32],
                signature: signature.clone(),
                min_os: None,
            },
            TestPlugin {
                name: "Bravo",
                version: "1.2.3",
                key,
                asset: "Bravo-1.2.3-test.bin",
                size: 1000,
                sha256: [0x22; 32],
                signature: signature.clone(),
                min_os: None,
            },
        ]
    };
    let feed = test_feed(&plugins());
    assert_eq!(
        feed.plugins.keys().map(String::as_str).collect::<Vec<_>>(),
        ["Alpha", "Bravo"]
    );
    let format = match key.as_str().split('-').next() {
        Some("darwin") => AssetFormat::Pkg,
        Some("linux") => AssetFormat::TarGz,
        _ => AssetFormat::InnoExe,
    };
    for p in plugins() {
        let entry = &feed.plugins[p.name];
        let release = entry.release.as_ref().expect("a stable release");
        assert_eq!(
            release.platforms.keys().copied().collect::<Vec<_>>(),
            [key],
            "{}",
            p.name
        );
        let asset = &release.platforms[&key];
        let lower = p.name.to_ascii_lowercase();
        assert_eq!(
            asset.url.as_str(),
            format!("https://assets.example.invalid/{lower}/v1.2.3/{}", p.asset)
        );
        assert_eq!(asset.asset_name.as_str(), p.asset);
        assert_eq!(asset.size, p.size);
        assert_eq!(asset.sha256, p.sha256);
        assert_eq!(asset.signature, p.signature);
        assert_eq!(asset.format, format);
    }
    assert_eq!(
        feed::parse(&test_feed_document(&plugins())).expect("the document parses"),
        feed,
        "TA-14: the document is the feed's"
    );

    // FaultyStagingIo: an injected failure touches nothing and is counted.
    let (_td, roots) = fresh_temp_roots();
    let target = roots.staging_dir().join("downloads");
    let io = FaultyStagingIo::failing(Op::CreateDir, 1, PermissionDenied);
    let error = io.create_dir_all(&target).expect_err("the planned failure");
    assert_eq!(error.kind(), PermissionDenied);
    assert!(!target.exists(), "no directory was created");
    assert_eq!(io.calls(), 1);
    assert_eq!(io.calls_of(Op::CreateDir), 1);
    assert_eq!(io.calls_of(Op::KindOf), 0);
    assert_eq!(io.written(), 0);

    let io = FaultyStagingIo::failing(Op::KindOf, 1, PermissionDenied);
    let error = io.kind_of(&target).expect_err("the planned failure");
    assert_eq!(error.kind(), PermissionDenied);
    assert_eq!(io.calls_of(Op::KindOf), 1);

    // panic_after: the panic comes after the call returned (the call is counted).
    assert_eq!(
        FaultyStagingIo::panicking_after(1).faults.panic_after,
        Some(1)
    );
    assert_eq!(FaultyStagingIo::healthy().faults.fail, None);
    let mut io = FaultyStagingIo::failing(Op::LenOf, 1, NotFound);
    io.faults.panic_after = Some(1);
    let result = catch_unwind(AssertUnwindSafe(|| io.len_of(&target)));
    let payload = result.expect_err("the planned panic");
    let message = payload
        .downcast_ref::<String>()
        .map(String::as_str)
        .unwrap_or_default();
    assert!(
        message.contains("planned panic after call 1"),
        "message: {message}"
    );
    assert_eq!(io.calls(), 1);
    assert_eq!(io.calls_of(Op::LenOf), 1);

    assert_eq!(
        test_policy(),
        DownloadPolicy {
            attempts: 4,
            backoff: vec![Duration::ZERO],
            stall: Duration::from_secs(2),
            attempt_deadline: Duration::from_secs(10),
        }
    );

    // DownloadRig: env() borrows the rig's own parts.
    let ctx = AppContext {
        roots: roots.clone(),
        settings: Settings::default(),
        feed_url: String::new(),
        http: HttpConfig::loopback(Duration::from_secs(1), Duration::from_secs(1)),
        signing_key: kat_signing_key(),
    };
    let rig = DownloadRig::new(ctx);
    let env = rig.env();
    assert!(std::ptr::eq(env.ctx, &rig.ctx));
    assert!(std::ptr::eq(env.policy, &rig.policy));
    assert!(std::ptr::eq(env.cancel, &rig.cancel));
    assert_eq!(
        std::ptr::from_ref::<dyn StagingIo>(env.io).cast::<()>(),
        std::ptr::from_ref(&rig.io).cast::<()>()
    );
    assert_eq!(rig.policy, test_policy());
    assert!(!rig.cancel.is_cancelled());
    assert_eq!(rig.io.calls(), 0);
    fn sync<T: Sync>() {}
    sync::<DownloadRig>();
}

/// The error kind of a result, `None` for `Ok` (works for handles that are not `Debug`).
fn err_kind<T>(result: io::Result<T>) -> Option<io::ErrorKind> {
    result.err().map(|e| e.kind())
}

/// The sorted entry names of `dir` on the real filesystem.
fn disk_names(dir: &Path) -> Vec<OsString> {
    let mut names: Vec<OsString> = std::fs::read_dir(dir)
        .expect("read_dir")
        .map(|entry| entry.expect("dir entry").file_name())
        .collect();
    names.sort();
    names
}

/// TA-6: the Phase 8 doubles on the paths that do not delegate to `FsExtractIo` (T004).
#[test]
fn phase8_doubles_cases() {
    use crate::extract::EntryType;
    use io::ErrorKind::{
        AlreadyExists, InvalidInput, NotFound, Other, PermissionDenied, StorageFull, TimedOut,
    };

    let (td, roots) = fresh_temp_roots();
    let staging = roots.staging_dir().to_path_buf();
    let disk_before = disk_names(&staging);
    let archive = td.path().join("ten.bin");
    std::fs::write(&archive, b"0123456789").expect("write the archive fixture");

    // MemExtractIo: the in-memory tree, its semantics and its record.
    let mem = MemExtractIo::new(&staging);
    let parent = staging.parent().expect("staging has a parent");
    assert!(mem.supports_symlinks());
    assert_eq!(mem.kind_of(&staging).expect("kind_of"), Some(NodeKind::Dir));
    assert_eq!(mem.kind_of(parent).expect("kind_of"), Some(NodeKind::Dir));
    assert_eq!(mem.kind_of(&staging.join("x")).expect("kind_of"), None);
    assert_eq!(mem.canonicalize(&staging).expect("canonicalize"), staging);
    assert_eq!(mem.canonicalize(parent).expect("canonicalize"), parent);
    let extract = staging.join("extract");
    let w = mem.create_work_dir(&extract, "ab.").expect("work dir");
    assert_eq!(w, extract.join("ab.000001"));
    assert_eq!(
        mem.create_work_dir(&extract, "ab.").expect("work dir"),
        extract.join("ab.000002")
    );
    assert!(matches!(mem.node(&extract), Some(MemNode::Dir { .. })));
    mem.plant_link(&staging.join("l"), "t");
    mem.plant_dir(&staging.join("t"), 0o755);
    assert_eq!(
        err_kind(mem.create_work_dir(&staging.join("l"), "p")),
        Some(InvalidInput),
        "PD-6: a linked extract dir is refused"
    );
    mem.create_dir(&w.join("d")).expect("create_dir");
    assert_eq!(err_kind(mem.create_dir(&w.join("d"))), Some(AlreadyExists));
    assert_eq!(
        err_kind(mem.create_dir(&staging.join("missing").join("d"))),
        Some(NotFound)
    );
    let f = w.join("f");
    let mut file = mem.create_file(&f).expect("create_file");
    file.write_all(b"he").expect("write");
    file.write_all(b"llo").expect("write");
    assert_eq!(
        mem.node(&f),
        Some(MemNode::File {
            bytes: b"hello".to_vec(),
            mode: 0o644
        })
    );
    mem.create_file(&w.join("d").join("g"))
        .expect("create_file in a subdirectory");
    let dangling = staging.join("dl");
    mem.plant_link(&dangling, "nowhere");
    assert_eq!(
        err_kind(mem.create_file(&dangling)),
        Some(AlreadyExists),
        "create_new over a dangling link"
    );
    mem.set_mode(&f, 0o755).expect("set_mode");
    assert_eq!(
        mem.node(&f),
        Some(MemNode::File {
            bytes: b"hello".to_vec(),
            mode: 0o755
        })
    );
    mem.symlink("f", &w.join("l")).expect("symlink");
    assert_eq!(
        mem.node(&w.join("l")),
        Some(MemNode::Link {
            target: "f".to_owned()
        })
    );
    assert_eq!(mem.canonicalize(&w.join("l")).expect("canonicalize"), f);
    assert_eq!(err_kind(mem.canonicalize(&dangling)), Some(NotFound));
    assert_eq!(
        mem.read_tree(&w).expect("read_tree"),
        [
            ListingEntry {
                kind: EntryType::Dir,
                mode: Some(0o755),
                path: b"d".to_vec(),
                target: None,
            },
            ListingEntry {
                kind: EntryType::File,
                mode: Some(0o644),
                path: b"d/g".to_vec(),
                target: None,
            },
            ListingEntry {
                kind: EntryType::File,
                mode: Some(0o755),
                path: b"f".to_vec(),
                target: None,
            },
            ListingEntry {
                kind: EntryType::Symlink,
                mode: Some(0o777),
                path: b"l".to_vec(),
                target: Some(b"f".to_vec()),
            },
        ]
    );
    assert_eq!(mem.read_file(&f, 2).expect("read_file"), b"hel");
    assert_eq!(
        mem.list_dir(&w).expect("list_dir"),
        Some(vec![
            (OsString::from("d"), NodeKind::Dir),
            (OsString::from("f"), NodeKind::File),
            (OsString::from("l"), NodeKind::Symlink),
        ])
    );
    assert_eq!(
        mem.list_dir(&staging.join("absent")).expect("list_dir"),
        None
    );
    mem.remove_entry(&w.join("l"), NodeKind::Symlink)
        .expect("remove_entry");
    assert_eq!(mem.node(&w.join("l")), None);
    assert!(mem.node(&f).is_some(), "the link target stays");
    mem.remove_tree(&w).expect("remove_tree");
    assert_eq!(mem.node(&w), None);
    assert!(mem.paths_under(&w).is_empty());
    assert!(mem.node(&extract.join("ab.000002")).is_some());
    let mut handle = mem.open_archive(&archive).expect("open_archive");
    let mut bytes = Vec::new();
    handle.read_to_end(&mut bytes).expect("read");
    assert_eq!(bytes, b"0123456789");
    handle.rewind().expect("rewind");
    bytes.clear();
    handle.read_to_end(&mut bytes).expect("read after rewind");
    assert_eq!(bytes, b"0123456789");
    use XKind as K;
    assert_eq!(
        mem.calls().iter().map(XOp::kind).collect::<Vec<_>>(),
        [
            K::KindOf,
            K::KindOf,
            K::KindOf,
            K::Canonicalize,
            K::Canonicalize,
            K::CreateWorkDir,
            K::CreateWorkDir,
            K::CreateWorkDir,
            K::CreateDir,
            K::CreateDir,
            K::CreateDir,
            K::CreateFile,
            K::Write,
            K::Write,
            K::CreateFile,
            K::CreateFile,
            K::SetMode,
            K::Symlink,
            K::Canonicalize,
            K::Canonicalize,
            K::ReadTree,
            K::ReadFile,
            K::ListDir,
            K::ListDir,
            K::RemoveEntry,
            K::RemoveTree,
            K::OpenArchive,
            K::Rewind,
        ]
    );
    assert_eq!(
        mem.calls()
            .into_iter()
            .filter(|op| op.kind() == K::Write)
            .collect::<Vec<_>>(),
        [
            XOp::Write {
                path: f.clone(),
                bytes: b"he".to_vec()
            },
            XOp::Write {
                path: f.clone(),
                bytes: b"llo".to_vec()
            },
        ]
    );
    assert_eq!(
        mem.calls()[5],
        XOp::CreateWorkDir {
            extract: extract.clone(),
            prefix: "ab.".to_owned()
        }
    );
    assert_eq!(mem.mutating_calls(), 15);
    assert_eq!(
        disk_names(&staging),
        disk_before,
        "Mem wrote nothing to disk"
    );

    // RecordingExtractIo::forwarding: delegation, records and the four overrides.
    let mem = MemExtractIo::new(&staging);
    let rec = RecordingExtractIo::forwarding(&mem)
        .with_read_tree(Box::new(|_: &Path, entries: &mut Vec<ListingEntry>| {
            entries.push(ListingEntry {
                kind: EntryType::Other(b'x'),
                mode: None,
                path: b"extra".to_vec(),
                target: None,
            });
        }))
        .with_canonicalize(Box::new(|_: &Path, inner: io::Result<PathBuf>| {
            inner.map(|p| p.join("mapped"))
        }))
        .with_kind_of(Box::new(|_: &Path, inner: io::Result<Option<NodeKind>>| {
            inner.map(|kind| kind.map(|_| NodeKind::Other))
        }))
        .with_open_archive(Box::new(|_: &Path| {
            Ok(
                Box::new(TwoPhaseArchive::new(b"one".to_vec(), b"two".to_vec()))
                    as Box<dyn ArchiveRead>,
            )
        }));
    let w = rec
        .create_work_dir(&staging.join("extract"), "r.")
        .expect("work dir");
    let f = w.join("f");
    let mut file = rec.create_file(&f).expect("create_file");
    file.write_all(b"xyz").expect("write");
    assert_eq!(
        mem.node(&f),
        Some(MemNode::File {
            bytes: b"xyz".to_vec(),
            mode: 0o644
        }),
        "delegated to Mem"
    );
    let entries = rec.read_tree(&w).expect("read_tree");
    assert_eq!(entries.len(), 2);
    assert_eq!(entries[0].path, b"f");
    assert_eq!(entries[1].path, b"extra");
    assert_eq!(
        rec.canonicalize(&f).expect("canonicalize"),
        f.join("mapped")
    );
    assert_eq!(
        err_kind(rec.canonicalize(&w.join("absent"))),
        Some(NotFound)
    );
    assert_eq!(rec.kind_of(&w).expect("kind_of"), Some(NodeKind::Other));
    assert_eq!(rec.kind_of(&w.join("absent")).expect("kind_of"), None);
    let fake = td.path().join("nothere.tar.gz");
    let mut handle = rec.open_archive(&fake).expect("open_archive override");
    let mut bytes = Vec::new();
    handle.read_to_end(&mut bytes).expect("read");
    assert_eq!(bytes, b"one");
    handle.rewind().expect("rewind");
    bytes.clear();
    handle.read_to_end(&mut bytes).expect("read");
    assert_eq!(bytes, b"two");
    let calls = rec.calls();
    assert!(calls.contains(&XOp::OpenArchive(fake.clone())));
    assert_eq!(calls.last(), Some(&XOp::Rewind));
    assert!(calls.contains(&XOp::Write {
        path: f.clone(),
        bytes: b"xyz".to_vec()
    }));
    assert_eq!(rec.canonicalized(), [f.clone(), w.join("absent")]);
    assert_eq!(rec.mutating_calls(), 3);
    assert!(rec.supports_symlinks());

    // RecordingExtractIo::fail_closed: every mutating call refused, nothing touched.
    let fc = RecordingExtractIo::fail_closed();
    let base = td.path();
    let snap_before = snapshot(base);
    let errors = [
        fc.create_work_dir(&base.join("e"), "p").err(),
        fc.create_dir(&base.join("d")).err(),
        fc.create_file(&base.join("f")).err(),
        fc.set_mode(&archive, 0o600).err(),
        fc.symlink("t", &base.join("l")).err(),
        fc.remove_tree(&base.join("plugins")).err(),
        fc.remove_entry(&archive, NodeKind::File).err(),
    ];
    for error in errors {
        let error = error.expect("fail-closed refuses every mutating call");
        assert_eq!(error.kind(), Other);
        assert_eq!(error.to_string(), "fail-closed");
    }
    assert_eq!(fc.calls().len(), 7);
    assert_eq!(fc.mutating_calls(), 7);
    assert_eq!(fc.calls()[5], XOp::RemoveTree(base.join("plugins")));
    assert_eq!(snapshot(base), snap_before, "fail-closed touched nothing");

    // FaultyExtractIo over Mem.
    let mem = MemExtractIo::new(&staging);
    let w = mem
        .create_work_dir(&staging.join("extract"), "f.")
        .expect("work dir");
    let faulty = FaultyExtractIo::new(&mem)
        .with_failure(XKind::CreateDir, 2, PermissionDenied)
        .with_failure(XKind::Write, 1, StorageFull)
        .with_remove_tree(RemoveTreeMode::Noop);
    faulty.create_dir(&w.join("a")).expect("first create_dir");
    assert_eq!(
        err_kind(faulty.create_dir(&w.join("b"))),
        Some(PermissionDenied)
    );
    assert_eq!(
        mem.node(&w.join("b")),
        None,
        "the failed call did not delegate"
    );
    let mut first = faulty.create_file(&w.join("f1")).expect("create_file");
    let mut second = faulty.create_file(&w.join("f2")).expect("create_file");
    assert_eq!(err_kind(first.write_all(b"1")), Some(StorageFull));
    second.write_all(b"2").expect("the second write passes");
    assert_eq!(
        mem.node(&w.join("f1")),
        Some(MemNode::File {
            bytes: Vec::new(),
            mode: 0o644
        })
    );
    faulty.remove_tree(&w).expect("Noop");
    assert!(mem.node(&w).is_some(), "Noop removed nothing");
    assert_eq!(
        faulty.calls().iter().map(XOp::kind).collect::<Vec<_>>(),
        [
            K::CreateDir,
            K::CreateDir,
            K::CreateFile,
            K::CreateFile,
            K::Write,
            K::Write,
            K::RemoveTree,
        ]
    );
    assert_eq!(faulty.mutating_calls(), 7);
    let failing = FaultyExtractIo::new(&mem).with_remove_tree(RemoveTreeMode::Fail(TimedOut));
    assert_eq!(err_kind(failing.remove_tree(&w)), Some(TimedOut));
    assert!(mem.node(&w).is_some());
    let forward = FaultyExtractIo::new(&mem).with_remove_tree(RemoveTreeMode::Forward);
    forward.remove_tree(&w).expect("Forward");
    assert_eq!(mem.node(&w), None);

    let faulty = FaultyExtractIo::new(&mem).with_read_fault(ReadFault {
        pass: 1,
        after: 4,
        kind: TimedOut,
    });
    let mut handle = faulty.open_archive(&archive).expect("open_archive");
    let mut bytes = Vec::new();
    assert_eq!(err_kind(handle.read_to_end(&mut bytes)), Some(TimedOut));
    assert_eq!(bytes, b"0123");
    let faulty = FaultyExtractIo::new(&mem).with_read_fault(ReadFault {
        pass: 2,
        after: 4,
        kind: TimedOut,
    });
    let mut handle = faulty.open_archive(&archive).expect("open_archive");
    let mut bytes = Vec::new();
    handle
        .read_to_end(&mut bytes)
        .expect("pass 1 reads everything");
    assert_eq!(bytes, b"0123456789");
    handle.rewind().expect("rewind");
    bytes.clear();
    assert_eq!(err_kind(handle.read_to_end(&mut bytes)), Some(TimedOut));
    assert_eq!(bytes, b"0123");
    assert_eq!(
        faulty.calls(),
        [XOp::OpenArchive(archive.clone()), XOp::Rewind]
    );

    // TwoPhaseArchive: `first`, then `after_rewind` after every rewind.
    let mut two = TwoPhaseArchive::new(b"ab".to_vec(), b"cd".to_vec());
    let mut bytes = Vec::new();
    two.read_to_end(&mut bytes).expect("read");
    assert_eq!(bytes, b"ab");
    for _ in 0..2 {
        two.rewind().expect("rewind");
        bytes.clear();
        two.read_to_end(&mut bytes).expect("read");
        assert_eq!(bytes, b"cd");
    }

    // snapshot: relative `/` paths, sorted, never following a link.
    let snap_td = TempDir::new().expect("a fresh TempDir");
    std::fs::create_dir(snap_td.path().join("a")).expect("create a");
    std::fs::write(snap_td.path().join("a").join("f"), b"abc").expect("write a/f");
    let snap = snapshot(snap_td.path());
    assert_eq!(snap.len(), 2, "{snap:?}");
    assert_eq!((snap[0].0.as_str(), snap[0].1), ("a", 'd'));
    assert_eq!(snap[1], ("a/f".to_owned(), 'f', 3));
    #[cfg(unix)]
    {
        std::os::unix::fs::symlink("a", snap_td.path().join("ln")).expect("symlink");
        let snap = snapshot(snap_td.path());
        assert_eq!(snap.len(), 3, "{snap:?}");
        assert_eq!((snap[2].0.as_str(), snap[2].1), ("ln", 'l'));
    }

    // TA-4: every double is `Sync`.
    fn sync<T: Sync>() {}
    sync::<MemExtractIo>();
    sync::<RecordingExtractIo<'static>>();
    sync::<FaultyExtractIo<'static>>();

    // FR-089 (unix only; `temp_roots_raw` is exercised by `raw_temp_dir_staging_root_macos`).
    #[cfg(unix)]
    {
        let link_td = TempDir::new().expect("a fresh TempDir");
        let canonical = std::fs::canonicalize(link_td.path()).expect("canonical TempDir");
        let roots = temp_roots_via_link(link_td.path());
        let via = canonical.join("via");
        let mut all: Vec<&Path> = roots
            .plugin_roots()
            .iter()
            .map(|r| r.path.as_path())
            .collect();
        all.extend([
            roots.presets().factory.as_path(),
            roots.presets().user.as_path(),
            roots.state_dir(),
            roots.staging_dir(),
        ]);
        for root in all {
            assert!(root.starts_with(&via), "{} not under via", root.display());
            assert!(root.is_dir(), "{} was not created", root.display());
        }
        assert_ne!(
            std::fs::canonicalize(roots.staging_dir()).expect("canonical staging"),
            roots.staging_dir()
        );
        assert!(
            canonical
                .join("real")
                .join("state")
                .join(STAGING_DIR)
                .is_dir()
        );
        roots.check_sandboxed(&roots.staging_dir().join("x"));
    }
}

/// TA-6: the Phase 9 doubles (T004). Block 3's `FsInstallIo` read row runs last: it needs T003's bodies, and the
/// other blocks do not.
#[test]
fn phase9_doubles_cases() {
    use crate::http::HttpConfig;
    use crate::install::{InstallProgress, Outcome, RootTag};
    use crate::settings::Settings;
    use IKind as I;
    use io::ErrorKind::{
        AlreadyExists, DirectoryNotEmpty, InvalidInput, NotFound, Other, PermissionDenied,
        StorageFull,
    };

    let (td, roots) = fresh_temp_roots();
    let base = roots
        .state_dir()
        .parent()
        .expect("the state dir has a parent")
        .to_path_buf();
    let staging = roots.staging_dir().to_path_buf();
    let user = roots.vst3_user().to_path_buf();
    let disk_before = snapshot(&base);

    // The kind split: the twelve from `CreateDirAll` on are mutating.
    let kinds = [
        I::KindOf,
        I::ReadLink,
        I::HashFile,
        I::ReadDir,
        I::ReadFile,
        I::Canonicalize,
        I::CreateDirAll,
        I::CreateDir,
        I::CopyNew,
        I::WriteNew,
        I::Symlink,
        I::SetMode,
        I::SyncFile,
        I::SyncDir,
        I::Rename,
        I::RemoveFile,
        I::RemoveDir,
        I::RemoveTree,
    ];
    assert_eq!(kinds.iter().filter(|k| k.is_mutating()).count(), 12);
    assert_eq!(kinds.iter().position(|k| k.is_mutating()), Some(6));

    // 1. MemInstallIo over a MemExtractIo-staged tree.
    let staged = MemExtractIo::new(&staging);
    let src = staging.join("extract").join("w").join("f.bin");
    staged.plant_file(&src, b"payload", 0o755);
    let mem = MemInstallIo::new(&base, &roots, &staged);
    assert!(mem.supports_symlinks());
    let mut planted: Vec<&Path> = roots
        .plugin_roots()
        .iter()
        .map(|r| r.path.as_path())
        .collect();
    planted.extend([
        base.as_path(),
        roots.presets().factory.as_path(),
        roots.presets().user.as_path(),
        roots.state_dir(),
        roots.staging_dir(),
    ]);
    let n_roots = planted.len();
    for root in &planted {
        assert_eq!(
            mem.kind_of(root).expect("kind_of"),
            Some(NodeKind::Dir),
            "{}",
            root.display()
        );
    }
    assert_eq!(
        mem.kind_of(base.parent().expect("base has a parent"))
            .expect("kind_of"),
        Some(NodeKind::Dir),
        "TA-5: a proper ancestor of base exists"
    );
    assert_eq!(mem.canonicalize(&base).expect("canonicalize"), base);
    assert_eq!(
        mem.kind_of(&src).expect("kind_of"),
        Some(NodeKind::File),
        "the staged tree is read through"
    );
    assert_eq!(mem.node(&src), staged.node(&src));
    let dest = user.join("f.bin");
    let mut fed = Vec::new();
    let copied = mem
        .copy_new(&src, &dest, &mut |chunk: &[u8]| {
            fed.extend_from_slice(chunk)
        })
        .expect("copy_new");
    assert_eq!(copied, 7);
    assert_eq!(fed, b"payload");
    assert_eq!(
        mem.node(&dest),
        Some(MemNode::File {
            bytes: b"payload".to_vec(),
            mode: 0o644
        })
    );
    assert_eq!(
        err_kind(mem.copy_new(&src, &dest, &mut |_: &[u8]| {})),
        Some(AlreadyExists),
        "create_new"
    );
    assert_eq!(
        mem.hash_file(&dest).expect("hash_file"),
        crate::verify::sha256_of(b"payload")
    );
    assert_eq!(mem.read_file(&dest, 2).expect("read_file"), b"pay");
    mem.set_mode(&dest, 0o755).expect("set_mode");
    assert_eq!(
        mem.node(&dest),
        Some(MemNode::File {
            bytes: b"payload".to_vec(),
            mode: 0o755
        })
    );
    let d = user.join("d");
    mem.create_dir(&d).expect("create_dir");
    mem.write_new(&d.join("x"), b"x").expect("write_new");
    let moved = user.join("moved");
    mem.rename(&d, &moved).expect("rename a directory");
    assert_eq!(mem.node(&d), None);
    assert_eq!(mem.node(&d.join("x")), None);
    assert_eq!(
        mem.node(&moved.join("x")),
        Some(MemNode::File {
            bytes: b"x".to_vec(),
            mode: 0o644
        }),
        "the subtree moved"
    );
    let full = user.join("full");
    mem.plant_dir(&full, 0o755);
    mem.plant_file(&full.join("y"), b"y", 0o644);
    assert_eq!(err_kind(mem.rename(&moved, &full)), Some(DirectoryNotEmpty));
    assert!(
        mem.node(&moved.join("x")).is_some(),
        "the refused rename moved nothing"
    );
    assert_eq!(err_kind(mem.remove_dir(&full)), Some(DirectoryNotEmpty));
    assert!(mem.node(&full.join("y")).is_some());
    mem.remove_file(&moved.join("x")).expect("remove_file");
    mem.remove_dir(&moved)
        .expect("remove_dir of an empty directory");
    assert_eq!(mem.node(&moved), None);
    let real = base.join("real");
    mem.plant_dir(&real, 0o755);
    mem.plant_file(&real.join("x"), b"xx", 0o600);
    let a = base.join("a");
    mem.plant_link(&a, "real");
    assert_eq!(mem.kind_of(&a).expect("kind_of"), Some(NodeKind::Symlink));
    assert_eq!(mem.read_link(&a).expect("read_link"), PathBuf::from("real"));
    assert_eq!(
        mem.kind_of(&a.join("x")).expect("kind_of"),
        Some(NodeKind::File),
        "a non-final link is followed"
    );
    assert_eq!(
        mem.canonicalize(&a.join("x")).expect("canonicalize"),
        real.join("x")
    );
    assert_eq!(
        err_kind(mem.hash_file(&a)),
        Some(InvalidInput),
        "never through a final link"
    );
    mem.remove_file(&a).expect("remove_file of a link");
    assert_eq!(mem.node(&a), None);
    assert!(mem.node(&real.join("x")).is_some(), "the link target stays");
    assert_eq!(
        mem.read_dir(&real).expect("read_dir"),
        [(OsString::from("x"), NodeKind::File)]
    );
    mem.remove_tree(&real).expect("remove_tree");
    assert_eq!(mem.node(&real), None);
    assert!(mem.paths_under(&real).is_empty());
    mem.remove_tree(&real)
        .expect("remove_tree of an absent path");
    mem.create_dir_all(&user.join("p").join("q"))
        .expect("create_dir_all");
    assert_eq!(
        mem.node(&user.join("p")),
        Some(MemNode::Dir { mode: 0o755 })
    );
    mem.symlink("f.bin", &user.join("ln")).expect("symlink");
    assert_eq!(
        mem.node(&user.join("ln")),
        Some(MemNode::Link {
            target: "f.bin".to_owned()
        })
    );
    mem.sync_file(&dest).expect("sync_file");
    mem.sync_dir(&user).expect("sync_dir");
    assert_eq!(
        [
            err_kind(mem.write_new(&src.with_file_name("new"), b"n")),
            err_kind(mem.remove_file(&src)),
            err_kind(mem.rename(&src, &user.join("stolen"))),
        ],
        [Some(PermissionDenied); 3],
        "the staged tree is read-only"
    );
    assert_eq!(
        staged.node(&src),
        Some(MemNode::File {
            bytes: b"payload".to_vec(),
            mode: 0o755
        })
    );
    assert_eq!(staged.node(&src.with_file_name("new")), None);
    assert_eq!(mem.node(&user.join("stolen")), None);
    assert!(
        staged.calls().is_empty(),
        "Mem reads the staged tree without calling it"
    );
    assert_eq!(
        mem.paths_under(&user),
        [
            dest.clone(),
            full.clone(),
            full.join("y"),
            user.join("ln"),
            user.join("p"),
            user.join("p").join("q"),
        ]
    );
    let mut expected = vec![I::KindOf; n_roots];
    expected.extend([
        I::KindOf,
        I::Canonicalize,
        I::KindOf,
        I::CopyNew,
        I::CopyNew,
        I::HashFile,
        I::ReadFile,
        I::SetMode,
        I::CreateDir,
        I::WriteNew,
        I::Rename,
        I::Rename,
        I::RemoveDir,
        I::RemoveFile,
        I::RemoveDir,
        I::KindOf,
        I::ReadLink,
        I::KindOf,
        I::Canonicalize,
        I::HashFile,
        I::RemoveFile,
        I::ReadDir,
        I::RemoveTree,
        I::RemoveTree,
        I::CreateDirAll,
        I::Symlink,
        I::SyncFile,
        I::SyncDir,
        I::WriteNew,
        I::RemoveFile,
        I::Rename,
    ]);
    assert_eq!(
        mem.calls().iter().map(IOp::kind).collect::<Vec<_>>(),
        expected
    );
    assert_eq!(
        mem.calls()[n_roots + 3],
        IOp::CopyNew {
            src: src.clone(),
            dest: dest.clone()
        }
    );
    assert_eq!(mem.calls()[0], IOp::KindOf(planted[0].to_path_buf()));
    assert_eq!(mem.mutating_calls(), 20);

    // 2. FaultyInstallIo over a fresh Mem.
    let mem = MemInstallIo::new(&base, &roots, &staged);
    for name in ["r1", "r2", "r3"] {
        mem.plant_file(&user.join(name), name.as_bytes(), 0o644);
    }
    let faulty = FaultyInstallIo::new(&mem).fail(I::Rename, 2, StorageFull);
    faulty
        .rename(&user.join("r1"), &user.join("s1"))
        .expect("the first rename");
    assert_eq!(
        err_kind(faulty.rename(&user.join("r2"), &user.join("s2"))),
        Some(StorageFull)
    );
    assert!(
        mem.node(&user.join("r2")).is_some(),
        "the failed call was not forwarded"
    );
    faulty
        .rename(&user.join("r3"), &user.join("s3"))
        .expect("one-shot: the third rename passes");
    assert_eq!(
        mem.node(&user.join("s3")),
        Some(MemNode::File {
            bytes: b"r3".to_vec(),
            mode: 0o644
        })
    );
    assert_eq!(faulty.calls().len(), 3);
    assert_eq!(faulty.mutating_calls(), 3);

    let faulty = FaultyInstallIo::new(&mem).fail_from(I::WriteNew, 1, PermissionDenied);
    for name in ["w1", "w2", "w3"] {
        assert_eq!(
            err_kind(faulty.write_new(&user.join(name), b"w")),
            Some(PermissionDenied)
        );
        assert_eq!(mem.node(&user.join(name)), None);
    }
    faulty
        .create_dir(&user.join("other"))
        .expect("another kind is forwarded");

    let faulty = FaultyInstallIo::new(&mem).crash_after(3);
    for name in ["c1", "c2", "c3"] {
        faulty.create_dir(&user.join(name)).expect("before the cut");
        assert_eq!(
            faulty
                .kind_of(&user.join(name))
                .expect("a read is not counted"),
            Some(NodeKind::Dir)
        );
    }
    let crashed = [
        faulty
            .create_dir(&user.join("c4"))
            .expect_err("after the cut"),
        faulty
            .write_new(&user.join("c5"), b"c")
            .expect_err("after the cut"),
        faulty
            .remove_dir(&user.join("c1"))
            .expect_err("after the cut"),
    ];
    for error in crashed {
        assert_eq!(error.kind(), Other);
        assert_eq!(error.to_string(), "crash");
    }
    assert_eq!(mem.node(&user.join("c4")), None);
    assert_eq!(mem.node(&user.join("c5")), None);
    assert!(mem.node(&user.join("c1")).is_some(), "not forwarded");
    assert_eq!(
        faulty
            .kind_of(&user.join("c1"))
            .expect("reads still forward"),
        Some(NodeKind::Dir)
    );
    assert_eq!(faulty.calls().len(), 10);
    assert_eq!(faulty.mutating_calls(), 6);

    let hooked = AtomicUsize::new(0);
    let user_file = user.join("h1");
    let faulty = FaultyInstallIo::new(&mem).on_call(I::CopyNew, 1, |inner: &dyn InstallIo| {
        hooked.fetch_add(1, Ordering::SeqCst);
        inner
            .write_new(&user_file, b"user")
            .expect("the hook writes through the inner I/O");
    });
    assert_eq!(
        err_kind(faulty.copy_new(&src, &user_file, &mut |_: &[u8]| {})),
        Some(AlreadyExists),
        "the hook ran just before the call was forwarded"
    );
    assert_eq!(hooked.load(Ordering::SeqCst), 1);
    assert_eq!(
        mem.node(&user_file),
        Some(MemNode::File {
            bytes: b"user".to_vec(),
            mode: 0o644
        })
    );
    faulty
        .copy_new(&src, &user.join("h2"), &mut |_: &[u8]| {})
        .expect("the second copy_new");
    assert_eq!(hooked.load(Ordering::SeqCst), 1, "only before the nth call");
    assert_eq!(
        faulty.calls().iter().map(IOp::kind).collect::<Vec<_>>(),
        [I::CopyNew, I::CopyNew],
        "the hook's own call is not recorded"
    );

    assert!(FaultyInstallIo::new(&mem).supports_symlinks());
    let plain = FaultyInstallIo::new(&mem).without_symlinks();
    assert!(!plain.supports_symlinks());
    plain
        .symlink("h2", &user.join("ln"))
        .expect("everything else is forwarded");
    assert_eq!(
        mem.node(&user.join("ln")),
        Some(MemNode::Link {
            target: "h2".to_owned()
        })
    );
    assert_eq!(
        plain.read_link(&user.join("ln")).expect("read_link"),
        PathBuf::from("h2")
    );
    assert_eq!(
        snapshot(&base),
        disk_before,
        "the Mem doubles wrote nothing to disk"
    );

    // 3. RecordingInstallIo: fail-closed refuses every mutating call; forwarding forwards both.
    let fc = RecordingInstallIo::fail_closed();
    let p = base.join("fc");
    let refused = [
        fc.create_dir_all(&p).err(),
        fc.create_dir(&p).err(),
        fc.copy_new(&p, &p.join("b"), &mut |_: &[u8]| {}).err(),
        fc.write_new(&p, b"x").err(),
        fc.symlink("t", &p).err(),
        fc.set_mode(&p, 0o600).err(),
        fc.sync_file(&p).err(),
        fc.sync_dir(&p).err(),
        fc.rename(&p, &p.join("b")).err(),
        fc.remove_file(&p).err(),
        fc.remove_dir(&p).err(),
        fc.remove_tree(&base).err(),
    ];
    for error in refused {
        let error = error.expect("fail-closed refuses every mutating call");
        assert_eq!(error.kind(), Other);
        assert_eq!(error.to_string(), "fail-closed");
    }
    assert_eq!(fc.calls().len(), 12);
    assert_eq!(fc.mutating_calls(), 12);
    assert_eq!(fc.calls()[11], IOp::RemoveTree(base.clone()));
    assert_eq!(err_kind(std::fs::symlink_metadata(&p)), Some(NotFound));
    assert_eq!(snapshot(&base), disk_before, "fail-closed touched nothing");

    let mem = MemInstallIo::new(&base, &roots, &staged);
    let rec = RecordingInstallIo::forwarding(&mem);
    rec.write_new(&user.join("fw"), b"z").expect("forwarded");
    assert_eq!(
        rec.kind_of(&user.join("fw")).expect("forwarded"),
        Some(NodeKind::File)
    );
    assert!(rec.supports_symlinks());
    assert_eq!(
        rec.calls(),
        [IOp::WriteNew(user.join("fw")), IOp::KindOf(user.join("fw"))]
    );
    assert_eq!(rec.mutating_calls(), 1);
    assert_eq!(mem.calls().len(), 2, "each call forwarded once");

    // 4. CountingDriver, RecordingDriver, OneDriver.
    let sig_path = Path::new(env!("CARGO_MANIFEST_DIR")).join(
        "../../../../tools/feed-contract/fixtures/kat/KatPlugin-1.2.3-Linux-x64.tar.gz.minisig",
    );
    let signature = std::fs::read_to_string(&sig_path)
        .unwrap_or_else(|e| panic!("cannot read {}: {e}", sig_path.display()));
    let key = native_key();
    let feed = test_feed(&[
        TestPlugin {
            name: "Alpha",
            version: "1.2.3",
            key,
            asset: "Alpha-1.2.3-test.bin",
            size: 452,
            sha256: [0x11; 32],
            signature: signature.clone(),
            min_os: None,
        },
        TestPlugin {
            name: "Beta",
            version: "1.2.3",
            key,
            asset: "Beta-1.2.3-test.bin",
            size: 452,
            sha256: [0x22; 32],
            signature,
            min_os: None,
        },
    ]);
    let uninstall = |name: &str| Action::Uninstall {
        entry: feed.plugins[name].clone(),
    };
    let rig = InstallRig::new(AppContext {
        roots: roots.clone(),
        settings: Settings::default(),
        feed_url: String::new(),
        http: HttpConfig::loopback(Duration::from_secs(1), Duration::from_secs(1)),
        signing_key: kat_signing_key(),
    });
    let kept = vec![KeptPath {
        root: RootTag::Presets,
        path: "a.preset".to_owned(),
    }];
    let recording = RecordingDriver::new(Os::Linux, kept.clone());
    assert_eq!(recording.os(), Os::Linux);
    assert_eq!(RecordingDriver::new(Os::Macos, Vec::new()).os(), Os::Macos);
    let counting = CountingDriver::new(&recording);
    assert_eq!(counting.runs(), 0);
    assert_eq!(counting.os(), Os::Linux);
    let mut progress = |_: InstallProgress| {};
    let results = counting.run(
        InstallEnv {
            ctx: &rig.ctx,
            io: &mem,
            extract: rig.extract_env(&staged),
            progress: &mut progress,
        },
        vec![uninstall("Alpha"), uninstall("Beta")],
    );
    let ok = || {
        Ok(Outcome {
            version: None,
            placed: 0,
            removed: 0,
            kept: kept.clone(),
        })
    };
    assert_eq!(
        results
            .into_iter()
            .map(|r| (r.name, r.action, r.outcome))
            .collect::<Vec<_>>(),
        [
            ("Alpha".to_owned(), ActionKind::Remove, ok()),
            ("Beta".to_owned(), ActionKind::Remove, ok()),
        ]
    );
    assert_eq!(counting.runs(), 1);
    let results = counting.run(
        InstallEnv {
            ctx: &rig.ctx,
            io: &mem,
            extract: rig.extract_env(&staged),
            progress: &mut progress,
        },
        vec![uninstall("Beta")],
    );
    assert_eq!(results.len(), 1);
    assert_eq!(counting.runs(), 2);
    assert_eq!(
        recording.batches(),
        [
            vec![
                (ActionKind::Remove, "Alpha".to_owned()),
                (ActionKind::Remove, "Beta".to_owned()),
            ],
            vec![(ActionKind::Remove, "Beta".to_owned())],
        ]
    );
    let one = OneDriver(Some(&recording));
    let found = one.driver_for(Os::Linux).expect("the driver's own OS");
    assert_eq!(
        std::ptr::from_ref(found).cast::<()>(),
        std::ptr::from_ref(&recording).cast::<()>()
    );
    assert!(one.driver_for(Os::Windows).is_none());
    assert!(one.driver_for(Os::Macos).is_none());
    assert!(OneDriver(None).driver_for(Os::Linux).is_none());

    // 5. InstallRig::command_env: the inputs, through the public fields.
    let env = rig.command_env(Os::Linux, Arch::X86_64, &mem, &staged, &one);
    assert_eq!(env.os, Os::Linux);
    assert_eq!(env.arch, Arch::X86_64);
    assert!(std::ptr::eq(env.ctx, &rig.ctx));
    assert_eq!(
        std::ptr::from_ref::<dyn InstallIo>(env.io).cast::<()>(),
        std::ptr::from_ref(&mem).cast::<()>()
    );
    assert_eq!(
        std::ptr::from_ref::<dyn ExtractIo>(env.extract.io).cast::<()>(),
        std::ptr::from_ref(&staged).cast::<()>()
    );
    assert!(std::ptr::eq(env.extract.ctx, &rig.ctx));
    assert!(std::ptr::eq(env.extract.limits, &rig.extract_limits));
    assert!(std::ptr::eq(env.download.ctx, &rig.ctx));
    assert!(std::ptr::eq(env.download.policy, &rig.policy));
    assert!(std::ptr::eq(env.download.cancel, &rig.cancel));
    assert_eq!(
        std::ptr::from_ref::<dyn StagingIo>(env.download.io).cast::<()>(),
        std::ptr::from_ref(&rig.staging).cast::<()>()
    );
    assert!(env.drivers.driver_for(Os::Linux).is_some());
    assert!(env.drivers.driver_for(Os::Windows).is_none());
    let env = rig.command_env(Os::Windows, Arch::Aarch64, &mem, &staged, &one);
    assert_eq!((env.os, env.arch), (Os::Windows, Arch::Aarch64));
    assert_eq!(rig.extract_limits, ExtractLimits::PRODUCTION);
    assert_eq!(rig.policy, test_policy());
    assert!(!rig.cancel.is_cancelled());

    fn sync<T: Sync>() {}
    sync::<MemInstallIo<'static>>();
    sync::<FaultyInstallIo<'static>>();
    sync::<RecordingInstallIo<'static>>();
    sync::<CountingDriver<'static>>();
    sync::<RecordingDriver>();
    sync::<InstallRig>();

    // 6. SC-023's roots: a non-UTF-8 user preset leaf, spelled and sandboxed, not created.
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStrExt;
        let leaf_td = TempDir::new().expect("a fresh TempDir");
        let canonical = std::fs::canonicalize(leaf_td.path()).expect("canonical TempDir");
        let leaf = OsStr::from_bytes(b"u\xff");
        let roots = temp_roots_with_user_presets(leaf_td.path(), leaf);
        let user_presets = roots.presets().user.clone();
        assert_eq!(user_presets, canonical.join("presets").join(leaf));
        assert_eq!(
            err_kind(std::fs::symlink_metadata(&user_presets)),
            Some(NotFound),
            "the user preset root is not created"
        );
        assert!(roots.presets().factory.is_dir());
        assert!(roots.staging_dir().is_dir());
        roots.check_sandboxed(&user_presets.join("x"));
        let outside = canonical
            .parent()
            .expect("the TempDir has a parent")
            .join("elsewhere");
        assert!(
            catch_unwind(AssertUnwindSafe(|| roots.check_sandboxed(&outside))).is_err(),
            "sandboxed to td"
        );
    }

    // Block 3's last row: fail-closed reads forward to `FsInstallIo` (red on T003 until its bodies land).
    let real_file = td.path().join("real.txt");
    std::fs::write(&real_file, b"r").expect("write a real temp file");
    assert_eq!(
        fc.kind_of(&real_file).expect("forwarded to FsInstallIo"),
        Some(NodeKind::File)
    );
    assert_eq!(fc.calls().last(), Some(&IOp::KindOf(real_file.clone())));
    assert_eq!(fc.mutating_calls(), 12, "a read is not mutating");
}
