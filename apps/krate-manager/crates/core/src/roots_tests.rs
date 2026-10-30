use super::*;
use crate::test_support::{canonical_temp_dir, fresh_temp_roots};
use std::ffi::OsString;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::MAIN_SEPARATOR_STR;

/// The one `Roots::new` call of the workspace's tests (D6 site 6).
fn try_new(p: RootPaths) -> Result<Roots, RootsError> {
    #[expect(clippy::disallowed_methods, reason = "the Roots::new validation test")]
    let result = Roots::new(p);
    result
}

/// `<dir><sep>..<sep><name>`, spelled as text: on Windows a `\\?\` base would make
/// `join("..")` pop a component instead of keeping it.
fn parent_hop(dir: &Path, name: &str) -> PathBuf {
    let mut s = OsString::from(dir.as_os_str());
    s.push(MAIN_SEPARATOR_STR);
    s.push("..");
    s.push(MAIN_SEPARATOR_STR);
    s.push(name);
    let p = PathBuf::from(s);
    assert!(p.components().any(|c| c == Component::ParentDir));
    p
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

fn canonical_base(roots: &Roots) -> PathBuf {
    roots
        .state_dir()
        .parent()
        .expect("the state dir has a parent")
        .to_path_buf()
}

fn assert_non_plugin_roots_equal(a: &Roots, b: &Roots) {
    assert_eq!(a.presets(), b.presets());
    assert_eq!(a.state_dir(), b.state_dir());
    assert_eq!(a.staging_dir(), b.staging_dir());
}

#[test]
fn new_validates_and_touches_nothing() {
    let (_td, roots) = fresh_temp_roots();
    let base = canonical_base(&roots);
    let n = base.join("not-created");
    let state = n.join("state");
    let good = RootPaths {
        plugin_roots: vec![
            PluginRoot {
                kind: RootKind::Vst3User,
                path: n.join("vst3-user"),
            },
            PluginRoot {
                kind: RootKind::Vst3Global,
                path: n.join("vst3-global"),
            },
        ],
        presets: PresetRoots {
            factory: n.join("factory"),
            user: n.join("user"),
        },
        state_dir: state.clone(),
        staging_dir: state.join(STAGING_DIR),
    };

    let before = listing(&base);
    let made = try_new(good.clone()).expect("valid paths are accepted");
    assert_eq!(listing(&base), before);
    assert_eq!(made.plugin_roots(), good.plugin_roots.as_slice());
    assert_eq!(made.presets(), &good.presets);
    assert_eq!(made.state_dir(), good.state_dir.as_path());
    assert_eq!(made.staging_dir(), good.staging_dir.as_path());

    let mut relative = good.clone();
    relative.plugin_roots[0].path = PathBuf::from("relative").join("vst3-user");
    assert_eq!(
        try_new(relative),
        Err(RootsError::Relative {
            which: "plugin_root"
        })
    );

    let mut no_user = good.clone();
    no_user
        .plugin_roots
        .retain(|r| r.kind != RootKind::Vst3User);
    assert_eq!(
        try_new(no_user),
        Err(RootsError::MissingRoot {
            kind: RootKind::Vst3User
        })
    );

    let mut no_global = good.clone();
    no_global
        .plugin_roots
        .retain(|r| r.kind != RootKind::Vst3Global);
    assert_eq!(
        try_new(no_global),
        Err(RootsError::MissingRoot {
            kind: RootKind::Vst3Global
        })
    );

    let mut staging = good.clone();
    staging.staging_dir = state.join("elsewhere");
    assert_eq!(try_new(staging), Err(RootsError::Staging));

    let mut dotdot = good.clone();
    dotdot.plugin_roots[0].path = parent_hop(&base.join("a"), "b");
    assert_eq!(
        try_new(dotdot),
        Err(RootsError::NotNormal {
            which: "plugin_root"
        })
    );

    let mut fs_root = good.clone();
    fs_root.presets.user = base
        .ancestors()
        .last()
        .expect("a path has ancestors")
        .to_path_buf();
    assert_eq!(
        try_new(fs_root),
        Err(RootsError::NotNormal {
            which: "presets.user"
        })
    );

    assert_eq!(listing(&base), before);
}

#[test]
fn with_configured_plugin_root_cases() {
    let (_td, roots) = fresh_temp_roots();
    let base = canonical_base(&roots);
    let x = base.join("configured");
    let y = base.join("other");
    let configured = |path: &Path| PluginRoot {
        kind: RootKind::Configured,
        path: path.to_path_buf(),
    };

    let with_x = roots
        .with_configured_plugin_root(Some(&x))
        .expect("X is accepted");
    assert_eq!(with_x.plugin_roots()[0], configured(&x));
    assert_eq!(&with_x.plugin_roots()[1..], roots.plugin_roots());
    assert!(
        with_x
            .plugin_roots()
            .iter()
            .any(|r| r.kind == RootKind::Vst3User)
    );
    assert_non_plugin_roots_equal(&with_x, &roots);

    let none = roots
        .with_configured_plugin_root(None)
        .expect("None is accepted");
    assert_eq!(none, roots);

    let removed = with_x
        .with_configured_plugin_root(None)
        .expect("None is accepted");
    assert_eq!(removed, roots);

    let with_y = with_x
        .with_configured_plugin_root(Some(&y))
        .expect("Y is accepted");
    assert_eq!(with_y.plugin_roots()[0], configured(&y));
    assert_eq!(&with_y.plugin_roots()[1..], roots.plugin_roots());
    assert_eq!(
        with_y
            .plugin_roots()
            .iter()
            .filter(|r| r.kind == RootKind::Configured)
            .count(),
        1
    );
    assert_non_plugin_roots_equal(&with_y, &roots);

    let user = roots.vst3_user().to_path_buf();
    let same = roots
        .with_configured_plugin_root(Some(&user))
        .expect("the Vst3User path is accepted");
    assert_eq!(same, roots);

    assert_eq!(
        roots.with_configured_plugin_root(Some(Path::new("relative-root"))),
        Err(RootsError::Relative {
            which: "configured"
        })
    );

    let hop = parent_hop(&user, "vst3-user");
    assert_eq!(
        roots.with_configured_plugin_root(Some(&hop)),
        Err(RootsError::NotNormal {
            which: "configured"
        })
    );

    assert_non_plugin_roots_equal(&roots, &none);
}

#[test]
fn logs_dir_is_state_logs() {
    let (_td, roots) = fresh_temp_roots();
    assert_eq!(roots.logs_dir(), roots.state_dir().join("logs"));
}

#[test]
fn sandboxed_roots_refuse_an_outside_configured_root() {
    let (_td, roots) = fresh_temp_roots();
    let base = canonical_base(&roots);
    let outside = canonical_temp_dir().join("km-outside-sandbox");
    assert!(!outside.exists());

    for refused in [outside.as_path(), base.as_path()] {
        let result = catch_unwind(AssertUnwindSafe(|| {
            roots.with_configured_plugin_root(Some(refused))
        }));
        let payload = result.expect_err("a root outside the sandbox panics");
        let message = payload
            .downcast_ref::<String>()
            .cloned()
            .or_else(|| payload.downcast_ref::<&str>().map(|s| (*s).to_owned()))
            .expect("a string panic payload");
        assert!(
            message.contains("outside the test sandbox"),
            "message: {message}"
        );
    }

    let inside = base.join("configured");
    assert!(roots.with_configured_plugin_root(Some(&inside)).is_ok());
    assert!(!outside.exists());
}
