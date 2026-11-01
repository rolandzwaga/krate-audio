//! Phase 10 FR-100 / SC-017: the stub installer of the `helper-elevated (windows-latest)` CI job. Run through
//! `run_inno_installer`, it writes one JSON object to the compile-time path `ARGV_RECORDER_OUT`:
//! `{"args": [...], "current_exe": "...", "sddl": "..."}` (`args` is the whole of `args_os`, argv[0] included, lossy;
//! `sddl` is the SDDL of its own directory, read with `Get-Acl`), then exits 0. Never shipped, never run outside that
//! job.

use std::path::Path;
use std::process::Command;

/// The record file, baked in at build time (a path under `$RUNNER_TEMP`).
const OUT: &str = env!("ARGV_RECORDER_OUT");

fn json_string(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 2);
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if u32::from(c) < 0x20 => out.push_str(&format!("\\u{:04x}", u32::from(c))),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

fn dir_sddl(dir: &Path) -> String {
    let script = format!(
        "(Get-Acl -LiteralPath '{}').Sddl",
        dir.to_string_lossy().replace('\'', "''")
    );
    match Command::new("pwsh")
        .args(["-NoProfile", "-Command", &script])
        .output()
    {
        Ok(o) => String::from_utf8_lossy(&o.stdout).trim().to_owned(),
        Err(e) => format!("<pwsh failed: {e}>"),
    }
}

fn main() {
    let args: Vec<String> = std::env::args_os()
        .map(|a| json_string(&a.to_string_lossy()))
        .collect();
    let exe = std::env::current_exe().ok();
    let exe_text = exe
        .as_deref()
        .map(|p| p.to_string_lossy().into_owned())
        .unwrap_or_default();
    let sddl = exe
        .as_deref()
        .and_then(Path::parent)
        .map(dir_sddl)
        .unwrap_or_default();
    let record = format!(
        "{{\"args\":[{}],\"current_exe\":{},\"sddl\":{}}}\n",
        args.join(","),
        json_string(&exe_text),
        json_string(&sddl)
    );
    std::fs::write(OUT, record).expect("the record file is writable");
}
