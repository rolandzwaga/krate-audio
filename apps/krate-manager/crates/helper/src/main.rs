#![cfg_attr(windows, windows_subsystem = "windows")]
fn main() -> std::process::ExitCode {
    let io = krate_manager_helper::os::OsHelperIo::new();
    let runner = krate_manager_helper::os::OsRunner::new();
    let seams = krate_manager_helper::seams::Seams {
        io: &io,
        runner: &runner,
        registry: &krate_manager_helper::os::OsRegistry,
        classify: krate_manager_helper::classify::classify_inno,
    };
    krate_manager_helper::cli::main_with(
        std::env::args_os().skip(1).collect(),
        &seams,
        krate_manager_helper::run,
    )
    .into()
}
