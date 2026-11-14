//! Phase 11 refusal harness (D9, FR-080 to FR-084). The parent re-invokes this binary once per scenario
//! (`--scenario <name>`), all children at once, each with every per-user directory variable redirected into its own
//! temp dir (PD-11); it prints the children's lines sorted, the confinement line and the summary. `--plant
//! committed-key` / `--plant bind-any` spawn only the `good` child with that plant (PD-38).

mod confinement;
mod keys;
mod map_errors;
mod scenarios;
mod socks;

use krate_manager_core::fixture_server::{FixtureServer, Reply};
use std::fmt;
use std::fs::{self, File};
use std::io;
use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4, TcpListener};
use std::panic::{self, AssertUnwindSafe};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, ExitCode, Output, Stdio};
use std::time::Instant;

/// Every scenario child, sorted (FR-080's table, SI-23, SI-30, SI-34, SI-35).
pub const SCENARIOS: [&str; 26] = [
    "api-host",
    "busy",
    "busy-after-download",
    "corrupt-manifest",
    "deb-install",
    "downgrade",
    "endpoint-latest-path",
    "failed-install-clears",
    "good",
    "latest-path",
    "map-errors",
    "no-platform",
    "offline",
    "other-key",
    "production-inputs",
    "recheck-replaces",
    "redirect-loop",
    "redirect-to-api-host",
    "redirect-to-http",
    "replayed-old",
    "rsign-format",
    "same-version",
    "socks-proxy",
    "stall",
    "tampered",
    "unversioned",
];
/// The scenario lines plus `confinement` (FR-082).
const LINE_COUNT: usize = SCENARIOS.len() + 1;
/// Proxy variables a child never inherits, so no request can leave through a proxy the box sets.
const PROXY_VARS: [&str; 8] = [
    "HTTP_PROXY",
    "http_proxy",
    "HTTPS_PROXY",
    "https_proxy",
    "ALL_PROXY",
    "all_proxy",
    "NO_PROXY",
    "no_proxy",
];

/// FR-081 (f): the two plants.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Plant {
    CommittedKey,
    BindAny,
}

impl Plant {
    fn parse(text: &str) -> Option<Plant> {
        match text {
            "committed-key" => Some(Plant::CommittedKey),
            "bind-any" => Some(Plant::BindAny),
            _ => None,
        }
    }

    fn name(self) -> &'static str {
        match self {
            Plant::CommittedKey => "committed-key",
            Plant::BindAny => "bind-any",
        }
    }

    fn code(self) -> &'static str {
        match self {
            Plant::CommittedKey => "E_HARNESS_KEY",
            Plant::BindAny => "E_HARNESS_BIND",
        }
    }
}

/// FR-081 (c), (d): the harness's own refusals.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HarnessError {
    Key,
    Bind(SocketAddr),
}

impl fmt::Display for HarnessError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            HarnessError::Key => {
                f.write_str("E_HARNESS_KEY: the harness key is the committed release key")
            }
            HarnessError::Bind(addr) => {
                write!(
                    f,
                    "E_HARNESS_BIND: the harness binds 127.0.0.1 only, not {addr}"
                )
            }
        }
    }
}

/// The address every fixture server and the relay bind.
pub const LOOPBACK: SocketAddr = SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::LOCALHOST, 0));
/// The address `--plant bind-any` asks for.
pub const ANY: SocketAddr = SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, 0));

/// The one address check of both bind paths (FR-081 (c)).
fn check_bind(addr: SocketAddr) -> Result<(), HarnessError> {
    if addr == LOOPBACK {
        Ok(())
    } else {
        Err(HarnessError::Bind(addr))
    }
}

/// The only way a fixture server starts (`FixtureServer::start` binds `127.0.0.1:0`, `fixture_server.rs:142-147`).
pub fn bind_loopback(
    addr: SocketAddr,
    routes: Vec<(String, Reply)>,
) -> Result<FixtureServer, HarnessError> {
    check_bind(addr)?;
    Ok(FixtureServer::start(routes))
}

/// The only way the SOCKS relay's listener is bound (PD-30).
pub fn bind_loopback_listener(addr: SocketAddr) -> Result<TcpListener, HarnessError> {
    check_bind(addr)?;
    Ok(TcpListener::bind(addr).expect("relay: bind 127.0.0.1:0"))
}

/// A loopback port that refuses every connection and stays reserved while the returned socket lives: bound, never
/// listening (a connect gets RST at once on every OS, and no other bind is handed the port). `offline` used to bind a
/// fixture server, take its URL and drop it, but the scenarios run as parallel child processes, so another scenario's
/// server could be handed the freed port first: on CI (ubuntu-24.04, 2026-11-14) `offline` read a fixture's reply
/// (E_SELF_UPDATE_MANIFEST) and `same-version`'s trap proxy saw the stray request, in the same run.
pub fn bind_refusing_loopback(
    addr: SocketAddr,
) -> Result<(socket2::Socket, SocketAddr), HarnessError> {
    check_bind(addr)?;
    let socket = socket2::Socket::new(socket2::Domain::IPV4, socket2::Type::STREAM, None)
        .expect("refusing: socket");
    socket
        .bind(&addr.into())
        .expect("refusing: bind 127.0.0.1:0");
    let bound = socket
        .local_addr()
        .expect("refusing: local_addr")
        .as_socket()
        .expect("refusing: an IP address");
    Ok((socket, bound))
}

enum Mode {
    Parent(Option<Plant>),
    Child(String, Option<Plant>),
}

fn parse(args: &[String]) -> Option<Mode> {
    match args {
        [] => Some(Mode::Parent(None)),
        [flag, plant] if flag == "--plant" => Plant::parse(plant).map(|p| Mode::Parent(Some(p))),
        [flag, name] if flag == "--scenario" => Some(Mode::Child(name.clone(), None)),
        [flag, name, pflag, plant] if flag == "--scenario" && pflag == "--plant" => {
            Plant::parse(plant).map(|p| Mode::Child(name.clone(), Some(p)))
        }
        _ => None,
    }
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match parse(&args) {
        Some(Mode::Parent(plant)) => parent(plant),
        Some(Mode::Child(name, plant)) => child(&name, plant),
        None => {
            eprintln!(
                "usage: updater-refusals [--plant committed-key | --plant bind-any] | \
                 --scenario <name> [--plant committed-key | --plant bind-any]"
            );
            ExitCode::from(2)
        }
    }
}

/// One child: its scenario's line on stdout (`ok <name>`, `not ok <name> <reason>`, or a harness code).
fn child(name: &str, plant: Option<Plant>) -> ExitCode {
    confinement::record(&krate_manager_core::test_support::canonical_temp_dir());
    let outcome = panic::catch_unwind(AssertUnwindSafe(|| scenarios::run(name, plant)));
    match outcome {
        Ok(Ok(())) => {
            println!("ok {name}");
            ExitCode::SUCCESS
        }
        Ok(Err(scenarios::Fail::Not(reason))) => {
            println!("not ok {name} {reason}");
            ExitCode::from(1)
        }
        Ok(Err(scenarios::Fail::Harness(e))) => {
            println!("{e}");
            ExitCode::from(1)
        }
        Err(payload) => {
            let msg = payload
                .downcast_ref::<&str>()
                .map(|s| (*s).to_owned())
                .or_else(|| payload.downcast_ref::<String>().cloned())
                .unwrap_or_else(|| "panicked".to_owned());
            println!("not ok {name} {msg}");
            ExitCode::from(1)
        }
    }
}

fn spawn(exe: &Path, dir: &Path, name: &str, plant: Option<Plant>) -> io::Result<Child> {
    let envs = confinement::prepare(dir)?;
    let stderr = File::create(dir.join("stderr.txt"))?;
    let mut cmd = Command::new(exe);
    cmd.arg("--scenario").arg(name);
    if let Some(p) = plant {
        cmd.arg("--plant").arg(p.name());
    }
    for var in PROXY_VARS {
        cmd.env_remove(var);
    }
    cmd.envs(envs)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::from(stderr))
        .spawn()
}

fn exit_text(out: &Output) -> String {
    match out.status.code() {
        Some(c) => format!("exit={c} (0x{:08x})", c.cast_unsigned()),
        None => "exit=signal".to_owned(),
    }
}

fn first_line(out: &Output) -> Option<String> {
    String::from_utf8_lossy(&out.stdout)
        .lines()
        .map(str::trim_end)
        .find(|l| !l.is_empty())
        .map(str::to_owned)
}

/// The last non-empty line of a child's stderr file, for a child that printed no line.
fn stderr_tail(dir: &Path) -> String {
    fs::read_to_string(dir.join("stderr.txt"))
        .ok()
        .and_then(|t| {
            t.lines()
                .map(str::trim)
                .rfind(|l| !l.is_empty())
                .map(str::to_owned)
        })
        .unwrap_or_else(|| "no output".to_owned())
}

/// `(ok, line)` for one finished child (plan §4.7).
fn result_line(name: &str, dir: &Path, out: io::Result<Output>) -> (bool, String) {
    let out = match out {
        Ok(o) => o,
        Err(e) => return (false, format!("not ok {name} spawn: {e}")),
    };
    let ok = format!("ok {name}");
    let not_ok = format!("not ok {name}");
    match first_line(&out) {
        Some(l) if l == ok && out.status.success() => (true, l),
        Some(l) if l == ok => (false, format!("not ok {name} {}", exit_text(&out))),
        Some(l) if l.starts_with(&not_ok) => (false, l),
        Some(l) if l.starts_with("E_HARNESS_") => {
            let code = l.split(':').next().unwrap_or(&l).to_owned();
            (false, format!("not ok {name} {code}"))
        }
        Some(l) => (false, format!("not ok {name} {}: {l}", exit_text(&out))),
        None => (
            false,
            format!("not ok {name} {} {}", exit_text(&out), stderr_tail(dir)),
        ),
    }
}

fn parent(plant: Option<Plant>) -> ExitCode {
    let start = Instant::now();
    let before = confinement::snapshot();
    let td = tempfile::TempDir::new().expect("harness: create the temp dir");
    let canon = fs::canonicalize(td.path()).expect("harness: canonicalize the temp dir");
    let exe = std::env::current_exe().expect("harness: current_exe");
    if let Some(p) = plant {
        return plant_run(&exe, &canon, p);
    }
    let dirs: Vec<PathBuf> = SCENARIOS.iter().map(|n| canon.join(n)).collect();
    let children: Vec<io::Result<Child>> = SCENARIOS
        .iter()
        .zip(&dirs)
        .map(|(name, dir)| spawn(&exe, dir, name, None))
        .collect();
    let mut lines: Vec<(&str, bool, String)> = SCENARIOS
        .iter()
        .zip(&dirs)
        .zip(children)
        .map(|((name, dir), c)| {
            let (ok, line) = result_line(name, dir, c.and_then(Child::wait_with_output));
            (*name, ok, line)
        })
        .collect();
    lines.sort_by(|a, b| a.0.cmp(b.0));
    let mut n_ok = 0usize;
    let mut n_not = 0usize;
    for (_, ok, line) in &lines {
        println!("{line}");
        if *ok {
            n_ok += 1;
        } else {
            n_not += 1;
        }
    }
    match confinement::check(&canon, &dirs, &before) {
        Ok(()) => {
            println!("ok confinement");
            n_ok += 1;
        }
        Err(path) => {
            println!("not ok confinement {path}");
            n_not += 1;
        }
    }
    println!("harness: {n_ok} ok, {n_not} not ok");
    println!("harness: wall {} ms", start.elapsed().as_millis());
    if n_not == 0 && n_ok == LINE_COUNT {
        ExitCode::SUCCESS
    } else {
        ExitCode::from(1)
    }
}

/// PD-38: exactly one child, `good` with the plant; its first line, exit 1.
fn plant_run(exe: &Path, canon: &Path, plant: Plant) -> ExitCode {
    let dir = canon.join("good");
    let out = spawn(exe, &dir, "good", Some(plant)).and_then(Child::wait_with_output);
    match out {
        Ok(o) => {
            let line = first_line(&o).unwrap_or_default();
            if !o.status.success() && line.starts_with(plant.code()) {
                println!("{line}");
            } else {
                println!("not ok plant {}: {line}", plant.name());
            }
        }
        Err(e) => println!("not ok plant {}: spawn: {e}", plant.name()),
    }
    ExitCode::from(1)
}
