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
