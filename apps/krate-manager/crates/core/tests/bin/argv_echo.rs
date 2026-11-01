//! Phase 10 SC-015: prints its arguments (argv[0] excluded) as one JSON array line, exit 0; windows_quoting.rs's echo
//! target, never shipped.

fn main() {
    let args: Vec<String> = std::env::args_os()
        .skip(1)
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    println!(
        "{}",
        serde_json::to_string(&args).expect("a string array serializes")
    );
}
