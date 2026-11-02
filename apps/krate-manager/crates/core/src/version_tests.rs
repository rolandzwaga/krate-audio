//! version tests (plan §4.4, §7.1; spec SC-006, SC-007, SC-008, SC-027).

use super::{
    Arch, Availability, CoreVersion, Installed, OsVersion, UpdateState, compare,
    platform_candidates, reduce_class_version, resolve,
};
use crate::feed::{self, Feed, PlatformKey, PluginEntry};
use crate::platform::Os;

const OSES: [Os; 3] = [Os::Windows, Os::Macos, Os::Linux];
const ARCHES: [Arch; 3] = [Arch::X86_64, Arch::Aarch64, Arch::Other];
const PLUGINS: [&str; 3] = ["Alpha", "Bravo", "Charlie"];

/// A contract fixture, read in place (FR-071) and parsed by the client.
fn fixture(name: &str) -> Feed {
    let path = format!(
        "{}/../../../../tools/feed-contract/fixtures/feeds/valid/{name}",
        env!("CARGO_MANIFEST_DIR")
    );
    let bytes = std::fs::read(&path).unwrap_or_else(|e| panic!("read {path}: {e}"));
    feed::parse(&bytes).unwrap_or_else(|e| panic!("parse {name}: {e}"))
}

fn entry<'a>(feed: &'a Feed, name: &str) -> &'a PluginEntry {
    feed.plugins
        .get(name)
        .unwrap_or_else(|| panic!("{name} missing from the fixture"))
}

fn os_v(s: &str) -> OsVersion {
    OsVersion::parse(s).unwrap_or_else(|| panic!("{s:?} should parse"))
}

/// One expected cell.
#[derive(Clone, Copy, Debug)]
enum Want {
    Avail(PlatformKey),
    NotThrough,
    NotForMachine,
    /// The asset under this key needs `min_os` (its text and components).
    Newer(PlatformKey, &'static str, &'static [u32]),
}

fn check_cell(e: &PluginEntry, os: Os, arch: Arch, running: Option<&OsVersion>, want: Want) {
    let got = resolve(e, os, arch, running);
    let cell = format!("{} on ({os:?}, {arch:?}) running {running:?}", e.name);
    match want {
        Want::Avail(want_key) => {
            let Availability::Available { key, asset } = got else {
                panic!("{cell}: want Available({want_key:?}), got {got:?}");
            };
            assert_eq!(key, want_key, "{cell}");
            let release = e
                .release
                .as_ref()
                .expect("an Available entry has a release");
            let own = release
                .platforms
                .get(&want_key)
                .expect("the chosen key is present");
            assert!(
                std::ptr::eq(asset, own),
                "{cell}: the asset is the entry's own"
            );
        }
        Want::NotThrough => assert_eq!(got, Availability::NotThroughManager, "{cell}"),
        Want::NotForMachine => assert_eq!(got, Availability::NotForThisMachine, "{cell}"),
        Want::Newer(key, text, parts) => {
            let Availability::NeedsNewerOs { min_os } = got else {
                panic!("{cell}: want NeedsNewerOs({text}), got {got:?}");
            };
            assert_eq!(min_os.to_string(), text, "{cell}");
            assert_eq!(min_os.components(), parts, "{cell}");
            let own = e
                .release
                .as_ref()
                .and_then(|r| r.platforms.get(&key))
                .and_then(|a| a.min_os)
                .expect("the gating asset has a min_os");
            assert_eq!(
                min_os.components(),
                own.components(),
                "{cell}: the asset's own min_os"
            );
        }
    }
}

/// The SC-006 row of `full-v2.json` for one `(Os, Arch)` pair: Alpha, Bravo, Charlie.
fn full_v2_row(os: Os, arch: Arch) -> [Want; 3] {
    use PlatformKey as K;
    use Want::{Avail, NotForMachine as Nfm};
    match (os, arch) {
        (Os::Windows, Arch::X86_64) => [
            Avail(K::WindowsX86_64),
            Avail(K::WindowsX86_64),
            Avail(K::WindowsX86_64),
        ],
        (Os::Macos, Arch::Aarch64) => [Avail(K::DarwinAarch64), Nfm, Avail(K::DarwinUniversal)],
        (Os::Macos, Arch::X86_64) => [Nfm, Avail(K::DarwinX86_64), Avail(K::DarwinUniversal)],
        (Os::Linux, Arch::X86_64) => [
            Avail(K::LinuxX86_64),
            Avail(K::LinuxX86_64),
            Avail(K::LinuxX86_64),
        ],
        _ => [Nfm, Nfm, Nfm],
    }
}

fn check_row(feed: &Feed, os: Os, arch: Arch, running: Option<&OsVersion>, row: [Want; 3]) {
    for (name, want) in PLUGINS.into_iter().zip(row) {
        check_cell(entry(feed, name), os, arch, running, want);
    }
}

#[test]
fn parse_core_cases() {
    for (s, want) in [
        ("0.0.0", (0, 0, 0)),
        ("1.10.2", (1, 10, 2)),
        ("999999999.0.1", (999_999_999, 0, 1)),
    ] {
        let v = CoreVersion::parse(s).unwrap_or_else(|| panic!("{s:?} should parse"));
        assert_eq!((v.major, v.minor, v.patch), want, "{s:?}");
        assert_eq!(v.to_string(), s, "Display round-trips {s:?}");
    }
    for s in [
        "1.2",
        "1.2.3.4",
        "01.2.3",
        "1.02.3",
        "1.2.3-beta.1",
        "1.2.3+b1",
        " 1.2.3",
        "1000000000.0.0",
        "",
    ] {
        assert_eq!(CoreVersion::parse(s), None, "{s:?} must not parse");
    }
    let order: Vec<CoreVersion> = ["0.12.2", "0.12.10", "1.0.0", "1.9.9", "1.10.0"]
        .into_iter()
        .map(|s| CoreVersion::parse(s).expect("ordering case parses"))
        .collect();
    for pair in order.windows(2) {
        assert!(pair[0] < pair[1], "{} < {}", pair[0], pair[1]);
    }
}

#[test]
fn reduce_class_version_cases() {
    for (s, want) in [
        ("1.0.0.1", "1.0.0"),
        ("1.0.4.0", "1.0.4"),
        ("0.8.0.3", "0.8.0"),
        ("1.2.3", "1.2.3"),
    ] {
        assert_eq!(
            reduce_class_version(s),
            CoreVersion::parse(want),
            "{s:?} reduces to {want}"
        );
        assert!(reduce_class_version(s).is_some(), "{s:?}");
    }
    for s in [
        "1.2.3.0123456789",
        "1.2.3.x",
        "1.2.3.4.5",
        "1.2",
        "1.2.3-beta",
        "1.2.3.",
        "01.2.3.4",
    ] {
        assert_eq!(reduce_class_version(s), None, "{s:?} must not reduce");
    }
}

#[test]
fn compare_cases() {
    let v = |s: &str| CoreVersion::parse(s).expect("compare case parses");
    let some = Some(v("1.0.3"));
    assert_eq!(compare(Installed::No, some), UpdateState::NotInstalled);
    assert_eq!(compare(Installed::No, None), UpdateState::NotInstalled);
    assert_eq!(
        compare(Installed::VersionUnknown, some),
        UpdateState::Unknown
    );
    assert_eq!(
        compare(Installed::Version(v("1.0.3")), None),
        UpdateState::Unknown
    );
    assert_eq!(
        compare(Installed::Version(v("1.0.3")), some),
        UpdateState::UpToDate
    );
    assert_eq!(
        compare(Installed::Version(v("1.0.2")), some),
        UpdateState::UpdateAvailable
    );
    assert_eq!(
        compare(Installed::Version(v("0.12.2")), Some(v("0.12.10"))),
        UpdateState::UpdateAvailable
    );
    assert_eq!(
        compare(Installed::Version(v("1.0.4")), some),
        UpdateState::InstalledNewer
    );
}

#[test]
fn os_version_cases() {
    let accepted: [(&str, &[u32]); 4] = [
        ("11.0", &[11, 0]),
        ("2.31", &[2, 31]),
        ("10.0.19045", &[10, 0, 19045]),
        ("10.0.19045.1", &[10, 0, 19045, 1]),
    ];
    for (s, parts) in accepted {
        let v = os_v(s);
        assert_eq!(v.to_string(), s, "Display as parsed");
        assert_eq!(v.components().len(), parts.len(), "{s:?}");
        assert_eq!(v.components(), parts, "{s:?}");
    }
    for s in [
        "11",
        "11.",
        "1.2.3.4.5",
        "a.b",
        "-1.0",
        "11.0 ",
        "4294967296.0",
    ] {
        assert!(OsVersion::parse(s).is_none(), "{s:?} must not parse");
    }
    assert_eq!(os_v("11.0"), os_v("11.0.0"));
    assert_eq!(os_v("11.0").cmp(&os_v("11.0.0")), std::cmp::Ordering::Equal);
    assert_eq!(
        os_v("11.0.0").to_string(),
        "11.0.0",
        "equal is not normalised"
    );
    assert!(os_v("2.31") < os_v("2.34"));
    assert!(os_v("10.9") < os_v("10.10"));
    assert!(os_v("12.0") > os_v("11.7.10"));
}

#[test]
fn resolve_table() {
    use PlatformKey as K;
    for os in OSES {
        for arch in ARCHES {
            let want: &[PlatformKey] = match (os, arch) {
                (Os::Windows, Arch::X86_64) => &[K::WindowsX86_64],
                (Os::Macos, Arch::Aarch64) => &[K::DarwinAarch64, K::DarwinUniversal],
                (Os::Macos, Arch::X86_64) => &[K::DarwinX86_64, K::DarwinUniversal],
                (Os::Linux, Arch::X86_64) => &[K::LinuxX86_64],
                _ => &[],
            };
            assert_eq!(platform_candidates(os, arch), want, "({os:?}, {arch:?})");
        }
    }

    let full = fixture("full-v2.json");
    let mixed = fixture("mixed.json");
    let unknown_key = fixture("unknown-platform-key.json");
    let v1 = fixture("v1-only.json");
    for os in OSES {
        for arch in ARCHES {
            check_row(&full, os, arch, None, full_v2_row(os, arch));
            check_cell(entry(&mixed, "Delta"), os, arch, None, Want::NotThrough);
            check_cell(
                entry(&unknown_key, "Delta"),
                os,
                arch,
                None,
                Want::NotThrough,
            );
            for name in PLUGINS {
                check_cell(entry(&v1, name), os, arch, None, Want::NotThrough);
            }
        }
    }
}

#[test]
fn needs_newer_os_table() {
    use PlatformKey as K;
    use Want::{Avail, Newer, NotForMachine as Nfm};
    const MAC: &[u32] = &[11, 0];
    const GLIBC: &[u32] = &[2, 31];
    const WIN: &[u32] = &[10, 0];
    let full = fixture("full-v2.json");
    let run = |s: &str| Some(os_v(s));

    // macOS arm64: Alpha native, Bravo none, Charlie universal; all macOS assets need 11.0.
    let mac_arm = |s: &str, row: [Want; 3]| {
        check_row(&full, Os::Macos, Arch::Aarch64, run(s).as_ref(), row);
    };
    mac_arm(
        "10.15",
        [
            Newer(K::DarwinAarch64, "11.0", MAC),
            Nfm,
            Newer(K::DarwinUniversal, "11.0", MAC),
        ],
    );
    for s in ["11.0", "11.0.0", "14.2"] {
        mac_arm(s, [Avail(K::DarwinAarch64), Nfm, Avail(K::DarwinUniversal)]);
    }
    // macOS x86_64: Alpha none, Bravo native, Charlie universal.
    check_row(
        &full,
        Os::Macos,
        Arch::X86_64,
        run("10.15").as_ref(),
        [
            Nfm,
            Newer(K::DarwinX86_64, "11.0", MAC),
            Newer(K::DarwinUniversal, "11.0", MAC),
        ],
    );

    // Linux: every asset needs glibc 2.31.
    check_row(
        &full,
        Os::Linux,
        Arch::X86_64,
        run("2.28").as_ref(),
        [Newer(K::LinuxX86_64, "2.31", GLIBC); 3],
    );
    for s in ["2.31", "2.39"] {
        check_row(
            &full,
            Os::Linux,
            Arch::X86_64,
            run(s).as_ref(),
            [Avail(K::LinuxX86_64); 3],
        );
    }

    // Windows: only Charlie carries a min_os (10.0); Alpha and Bravo never gate.
    check_row(
        &full,
        Os::Windows,
        Arch::X86_64,
        run("6.3").as_ref(),
        [
            Avail(K::WindowsX86_64),
            Avail(K::WindowsX86_64),
            Newer(K::WindowsX86_64, "10.0", WIN),
        ],
    );
    for s in ["0.1", "6.3", "10.0", "10.0.19045", "99.0"] {
        for name in ["Alpha", "Bravo"] {
            check_cell(
                entry(&full, name),
                Os::Windows,
                Arch::X86_64,
                run(s).as_ref(),
                Avail(K::WindowsX86_64),
            );
        }
    }
    for s in ["10.0", "10.0.19045"] {
        check_cell(
            entry(&full, "Charlie"),
            Os::Windows,
            Arch::X86_64,
            run(s).as_ref(),
            Avail(K::WindowsX86_64),
        );
    }

    // running = None never gates: the SC-006 row on every pair.
    // NotForThisMachine / NotThroughManager do not depend on running.
    let mixed = fixture("mixed.json");
    let v1 = fixture("v1-only.json");
    for os in OSES {
        for arch in ARCHES {
            check_row(&full, os, arch, None, full_v2_row(os, arch));
            for s in ["0.1", "999.0"] {
                let r = run(s);
                for (name, want) in PLUGINS.into_iter().zip(full_v2_row(os, arch)) {
                    if matches!(want, Nfm) {
                        check_cell(entry(&full, name), os, arch, r.as_ref(), Nfm);
                    }
                    check_cell(entry(&v1, name), os, arch, r.as_ref(), Want::NotThrough);
                }
                check_cell(
                    entry(&mixed, "Delta"),
                    os,
                    arch,
                    r.as_ref(),
                    Want::NotThrough,
                );
            }
        }
    }

    // A native darwin-aarch64 asset needing 13.0 on a 12.0 machine is NeedsNewerOs; the entry's
    // darwin-universal asset (11.0) does not replace it.
    let mut charlie = entry(&full, "Charlie").clone();
    {
        let platforms = &mut charlie
            .release
            .as_mut()
            .expect("Charlie has a release")
            .platforms;
        let mut native = platforms
            .get(&K::DarwinUniversal)
            .expect("Charlie has a universal asset")
            .clone();
        native.min_os = Some(os_v("13.0"));
        platforms.insert(K::DarwinAarch64, native);
    }
    check_cell(
        &charlie,
        Os::Macos,
        Arch::Aarch64,
        run("12.0").as_ref(),
        Newer(K::DarwinAarch64, "13.0", &[13, 0]),
    );
    check_cell(
        &charlie,
        Os::Macos,
        Arch::Aarch64,
        run("13.0").as_ref(),
        Avail(K::DarwinAarch64),
    );
    check_cell(
        &charlie,
        Os::Macos,
        Arch::Aarch64,
        None,
        Avail(K::DarwinAarch64),
    );
    check_cell(
        &charlie,
        Os::Macos,
        Arch::X86_64,
        run("12.0").as_ref(),
        Avail(K::DarwinUniversal),
    );

    // FR-038: UpdateState does not depend on Availability.
    let alpha = entry(&full, "Alpha");
    let older = CoreVersion::parse("0.1.0").expect("parses");
    assert!(matches!(
        resolve(alpha, Os::Macos, Arch::Aarch64, run("10.15").as_ref()),
        Availability::NeedsNewerOs { .. }
    ));
    assert_eq!(
        compare(Installed::Version(older), alpha.version),
        UpdateState::UpdateAvailable
    );
}

/// SC-007: the native arch the binary was built for, and `resolve_native` equal to that row.
#[cfg(any(target_os = "windows", target_os = "macos", target_os = "linux"))]
fn native_case(os: Os) {
    let built_for = if cfg!(target_arch = "x86_64") {
        Arch::X86_64
    } else if cfg!(target_arch = "aarch64") {
        Arch::Aarch64
    } else {
        Arch::Other
    };
    assert_eq!(Arch::native(), built_for);
    let full = fixture("full-v2.json");
    for (name, want) in PLUGINS.into_iter().zip(full_v2_row(os, built_for)) {
        let e = entry(&full, name);
        assert_eq!(
            super::resolve_native(e, None),
            resolve(e, os, built_for, None),
            "{name}"
        );
        check_cell(e, os, built_for, None, want);
    }
}

#[cfg(target_os = "windows")]
#[test]
fn native_candidates_windows() {
    native_case(Os::Windows);
}

#[cfg(target_os = "macos")]
#[test]
fn native_candidates_macos() {
    native_case(Os::Macos);
}

#[cfg(target_os = "linux")]
#[test]
fn native_candidates_linux() {
    native_case(Os::Linux);
}
