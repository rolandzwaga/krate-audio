//! The native elevation launcher (Phase 10 plan §4.5; FR-090, FR-091; PD-18, PD-23): built only by
//! `crate::resolve_elevation`. It executes exactly the core's `ElevatedCommand`; every rule that decides anything
//! (`windows_parameters`, `osascript_outcome`, `helper_beside`) is core's, tested there.

use krate_manager_core::install::{ElevatedCommand, Elevation, ElevationGuard, LaunchOutcome};
use std::path::{Path, PathBuf};
use std::time::Duration;

#[cfg(target_os = "macos")]
use macos::launch as launch_native;
#[cfg(windows)]
use windows::launch as launch_native;

/// The resolved helper (fixed for the process, D19) and the process's one elevation guard (PD-9).
pub struct NativeElevation {
    helper: Option<PathBuf>,
    guard: ElevationGuard,
}

impl NativeElevation {
    pub fn new(helper: Option<PathBuf>) -> NativeElevation {
        NativeElevation {
            helper,
            guard: ElevationGuard::new(),
        }
    }
}

impl Elevation for NativeElevation {
    fn helper(&self) -> Option<&Path> {
        self.helper.as_deref()
    }

    fn launch(&self, command: &ElevatedCommand, timeout: Duration) -> LaunchOutcome {
        launch_native(command, timeout)
    }

    fn guard(&self) -> &ElevationGuard {
        &self.guard
    }
}

/// FR-091: Linux has no elevated launch; the core never calls it there (FR-071).
#[cfg(not(any(windows, target_os = "macos")))]
fn launch_native(_command: &ElevatedCommand, _timeout: Duration) -> LaunchOutcome {
    LaunchOutcome::Failed("E_ELEVATION_NONE")
}

/// FR-090 (Windows): one `ShellExecuteExW` "runas" of the helper on its own COM thread; the timeout runs from the
/// call's return, which `SEE_MASK_NOASYNC` holds until the consent prompt is answered (PD-18).
#[cfg(windows)]
mod windows {
    use krate_manager_core::install::{self, ElevatedCommand, InstallError, LaunchOutcome};
    use std::ffi::OsStr;
    use std::os::windows::ffi::OsStrExt;
    use std::ptr::null;
    use std::time::Duration;
    use windows_sys::Win32::Foundation::{
        CloseHandle, ERROR_CANCELLED, GetLastError, HANDLE, WAIT_OBJECT_0, WAIT_TIMEOUT,
    };
    use windows_sys::Win32::System::Com::{
        COINIT_APARTMENTTHREADED, COINIT_DISABLE_OLE1DDE, CoInitializeEx, CoUninitialize,
    };
    use windows_sys::Win32::System::Threading::{GetExitCodeProcess, WaitForSingleObject};
    use windows_sys::Win32::UI::Shell::{
        SEE_MASK_FLAG_NO_UI, SEE_MASK_NOASYNC, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW,
        ShellExecuteExW,
    };

    /// The elevation verb (FR-090).
    const VERB: &str = "runas";
    /// The longest finite wait: `u32::MAX` is `INFINITE`, which a timeout must never become.
    const LONGEST_WAIT_MS: u32 = u32::MAX - 1;

    /// UTF-16 plus a NUL.
    fn wide(s: &OsStr) -> Vec<u16> {
        s.encode_wide().chain(Some(0)).collect()
    }

    /// The process handle `ShellExecuteExW` returned under `SEE_MASK_NOCLOSEPROCESS`, closed exactly once when dropped.
    struct Process(HANDLE);

    impl Drop for Process {
        fn drop(&mut self) {
            // SAFETY: `self.0` is the non-NULL process handle a successful ShellExecuteExW returned; only
            // `shell_execute` builds a `Process`, once per launch, and nothing uses the handle after this drop. Closing
            // it leaves the process itself running (a timed-out helper is left alone, FR-090).
            unsafe { CloseHandle(self.0) };
        }
    }

    pub fn launch(command: &ElevatedCommand, timeout: Duration) -> LaunchOutcome {
        let parameters = match install::windows_parameters(&command.args) {
            Ok(parameters) => parameters,
            Err(InstallError::Elevation { code }) => return LaunchOutcome::Failed(code),
            Err(_) => return LaunchOutcome::Failed("E_ELEVATION_ARGS"),
        };
        // FR-090: on its own thread, so the apartment this launch initialises is never the caller's.
        std::thread::scope(|scope| {
            scope
                .spawn(|| on_own_thread(command, &parameters, timeout))
                .join()
        })
        .unwrap_or(LaunchOutcome::Failed("E_ELEVATION_SHELLEXECUTE"))
    }

    fn on_own_thread(
        command: &ElevatedCommand,
        parameters: &str,
        timeout: Duration,
    ) -> LaunchOutcome {
        // SAFETY: the reserved pointer is NULL, as the CoInitializeEx page requires; this thread is new and has never
        // initialised COM. A success (S_OK or S_FALSE, both non-negative) is balanced by the CoUninitialize below on
        // this same thread; a failure is not balanced, and ShellExecuteExW is still tried.
        let initialised = unsafe {
            CoInitializeEx(
                null(),
                (COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) as u32,
            )
        } >= 0;
        let outcome = shell_execute(command, parameters, timeout);
        if initialised {
            // SAFETY: balances the successful CoInitializeEx above, on the thread that made it, after every COM use.
            unsafe { CoUninitialize() };
        }
        outcome
    }

    fn shell_execute(
        command: &ElevatedCommand,
        parameters: &str,
        timeout: Duration,
    ) -> LaunchOutcome {
        let verb = wide(OsStr::new(VERB));
        let file = wide(command.program.as_os_str());
        let parameters = wide(OsStr::new(parameters));
        let mut info = SHELLEXECUTEINFOW {
            cbSize: size_of::<SHELLEXECUTEINFOW>() as u32,
            fMask: SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI,
            lpVerb: verb.as_ptr(),
            lpFile: file.as_ptr(),
            lpParameters: parameters.as_ptr(),
            nShow: 0, // SW_HIDE
            ..Default::default()
        };
        // SAFETY: `info` is a valid, writable SHELLEXECUTEINFOW whose `cbSize` names its own size; `verb`, `file` and
        // `parameters` are NUL-terminated UTF-16 buffers that outlive the call; every other pointer and handle is
        // NULL, which the SHELLEXECUTEINFOW page allows. The call writes only `info`'s output fields.
        if unsafe { ShellExecuteExW(&mut info) } == 0 {
            // SAFETY: GetLastError has no preconditions; it is read on this thread straight after the failed call.
            return if unsafe { GetLastError() } == ERROR_CANCELLED {
                LaunchOutcome::Refused
            } else {
                LaunchOutcome::Failed("E_ELEVATION_SHELLEXECUTE")
            };
        }
        if info.hProcess.is_null() {
            return LaunchOutcome::Failed("E_ELEVATION_NO_PROCESS");
        }
        let process = Process(info.hProcess);
        let ms = u32::try_from(timeout.as_millis())
            .map_or(LONGEST_WAIT_MS, |ms| ms.min(LONGEST_WAIT_MS));
        // SAFETY: `process.0` is the live process handle ShellExecuteExW returned, open until `process` drops below.
        match unsafe { WaitForSingleObject(process.0, ms) } {
            WAIT_TIMEOUT => LaunchOutcome::TimedOut,
            WAIT_OBJECT_0 => {
                let mut code: u32 = 0;
                // SAFETY: `process.0` is live (as above) and its process has exited; `code` is a valid, writable u32
                // for the duration of the call.
                if unsafe { GetExitCodeProcess(process.0, &mut code) } == 0 {
                    LaunchOutcome::Failed("E_ELEVATION_SHELLEXECUTE")
                } else {
                    LaunchOutcome::Exited(code as i32)
                }
            }
            _ => LaunchOutcome::Failed("E_ELEVATION_SHELLEXECUTE"),
        }
    }
}

/// FR-090 (macOS): the fixed `OSASCRIPT_LINES` with the helper and its four arguments as `argv`, never as script text;
/// the timeout runs from the spawn, so the password dialog counts against it (PD-18).
#[cfg(target_os = "macos")]
mod macos {
    use krate_manager_core::install::{self, ElevatedCommand, LaunchOutcome};
    use std::io::Read;
    use std::process::{Command, Stdio};
    use std::time::{Duration, Instant};

    /// PD-14: the one home of the osascript path.
    const OSASCRIPT: &str = "/usr/bin/osascript";
    /// How often a running osascript is polled.
    const POLL: Duration = Duration::from_millis(100);

    pub fn launch(command: &ElevatedCommand, timeout: Duration) -> LaunchOutcome {
        let [line0, line1, line2] = install::OSASCRIPT_LINES;
        let spawned = Command::new(OSASCRIPT)
            .args(["-e", line0, "-e", line1, "-e", line2])
            .arg(&command.program)
            .args(&command.args)
            .stdout(Stdio::null())
            .stderr(Stdio::piped())
            .spawn();
        let Ok(mut child) = spawned else {
            return LaunchOutcome::Failed("E_ELEVATION_OSASCRIPT");
        };
        let started = Instant::now();
        loop {
            match child.try_wait() {
                Ok(Some(status)) => {
                    let mut stderr = Vec::new();
                    if let Some(mut pipe) = child.stderr.take() {
                        // A short read leaves less text to classify; osascript_outcome decides on what was read.
                        let _ = pipe.read_to_end(&mut stderr);
                    }
                    return install::osascript_outcome(
                        status.code(),
                        &String::from_utf8_lossy(&stderr),
                    );
                }
                // FR-090: the child is left running; dropping `Child` never kills it.
                Ok(None) if started.elapsed() >= timeout => return LaunchOutcome::TimedOut,
                Ok(None) => std::thread::sleep(POLL),
                Err(_) => return LaunchOutcome::Failed("E_ELEVATION_OSASCRIPT"),
            }
        }
    }
}
