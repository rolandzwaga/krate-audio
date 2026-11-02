//! The raw host primitives behind `HostIo` (FR-032). No decision lives here: every argv, subkey and mapping is core's.

use krate_manager_core::platform::{HostIo, RegistryRead, RunOutcome};
use krate_manager_core::sysproxy;
use std::process::Command;
use std::time::Duration;

pub struct OsHostIo;

impl HostIo for OsHostIo {
    fn run(&self, argv: &[&str], bound: Duration) -> RunOutcome {
        let Some((program, args)) = argv.split_first() else {
            return RunOutcome::NoOutput;
        };
        let mut command = Command::new(program);
        command.args(args);
        let run = sysproxy::run_bounded(vec![command], bound);
        match (run.outputs, run.abandoned.is_empty()) {
            (Some(mut outputs), _) if outputs.len() == 1 => RunOutcome::Output(outputs.remove(0)),
            (_, false) => RunOutcome::Abandoned,
            _ => RunOutcome::NoOutput,
        }
    }

    #[cfg(windows)]
    fn registry_values(&self, subkey: &str, names: [&str; 2]) -> RegistryRead {
        windows::registry_values(subkey, names)
    }

    #[cfg(not(windows))]
    fn registry_values(&self, _subkey: &str, _names: [&str; 2]) -> RegistryRead {
        RegistryRead::Failed
    }

    #[cfg(windows)]
    fn rtl_version(&self) -> Option<(u32, u32, u32)> {
        windows::rtl_version()
    }

    #[cfg(not(windows))]
    fn rtl_version(&self) -> Option<(u32, u32, u32)> {
        None
    }
}

/// The Windows registry and version reads (plan §4.9); read-only, `HKEY_LOCAL_MACHINE` in the 64-bit view.
#[cfg(windows)]
mod windows {
    use krate_manager_core::platform::RegistryRead;
    use std::ptr::{null, null_mut};
    use windows_sys::Wdk::System::SystemServices::RtlGetVersion;
    use windows_sys::Win32::Foundation::{ERROR_FILE_NOT_FOUND, ERROR_SUCCESS, STATUS_SUCCESS};
    use windows_sys::Win32::System::Registry::{
        HKEY, HKEY_LOCAL_MACHINE, KEY_READ, KEY_WOW64_64KEY, RRF_RT_REG_SZ, RegCloseKey,
        RegGetValueW, RegOpenKeyExW,
    };
    use windows_sys::Win32::System::SystemInformation::OSVERSIONINFOW;

    /// UTF-16 plus a NUL.
    fn wide(s: &str) -> Vec<u16> {
        s.encode_utf16().chain(Some(0)).collect()
    }

    /// A key opened by `RegOpenKeyExW`, closed exactly once when dropped.
    struct OpenKey(HKEY);

    impl Drop for OpenKey {
        fn drop(&mut self) {
            // SAFETY: `self.0` is the handle a successful RegOpenKeyExW returned; only `registry_values` builds an
            // `OpenKey`, once per open, and nothing uses the handle after this drop. The code carries nothing to act on.
            unsafe { RegCloseKey(self.0) };
        }
    }

    /// One value's read failed for a reason other than its absence.
    struct ReadFailed;

    pub fn registry_values(subkey: &str, names: [&str; 2]) -> RegistryRead {
        let subkey = wide(subkey);
        let mut hkey: HKEY = null_mut();
        // SAFETY: `subkey` is NUL-terminated and outlives the call; `hkey` is a valid out pointer that receives the
        // opened handle, which `OpenKey` closes on every path below (the RegOpenKeyExW page).
        let rc = unsafe {
            RegOpenKeyExW(
                HKEY_LOCAL_MACHINE,
                subkey.as_ptr(),
                0,
                KEY_READ | KEY_WOW64_64KEY,
                &mut hkey,
            )
        };
        if rc == ERROR_FILE_NOT_FOUND {
            return RegistryRead::NoKey;
        }
        if rc != ERROR_SUCCESS {
            return RegistryRead::Failed;
        }
        let key = OpenKey(hkey);
        let [first, second] = names;
        let Ok(first) = read_value(&key, first) else {
            return RegistryRead::Failed;
        };
        let Ok(second) = read_value(&key, second) else {
            return RegistryRead::Failed;
        };
        RegistryRead::Values([first, second])
    }

    /// `Ok(None)` when the value does not exist; any other error, a value that grew between the two calls
    /// (`ERROR_MORE_DATA`) included, is `Err`.
    fn read_value(key: &OpenKey, name: &str) -> Result<Option<String>, ReadFailed> {
        let name = wide(name);
        let mut size: u32 = 0;
        // SAFETY: `key.0` is open; `name` is NUL-terminated and outlives the call; a NULL subkey reads the key itself;
        // with a NULL data pointer the call only stores the value's size in bytes in `size` (the RegGetValueW page).
        let rc = unsafe {
            RegGetValueW(
                key.0,
                null(),
                name.as_ptr(),
                RRF_RT_REG_SZ,
                null_mut(),
                null_mut(),
                &mut size,
            )
        };
        if rc == ERROR_FILE_NOT_FOUND {
            return Ok(None);
        }
        if rc != ERROR_SUCCESS {
            return Err(ReadFailed);
        }
        let mut data = vec![0u16; (size as usize).div_ceil(2)];
        // SAFETY: `data` holds at least `size` bytes and is writable for the call; `size` says so, and the call writes
        // at most that many bytes (it fails with ERROR_MORE_DATA instead of writing past it).
        let rc = unsafe {
            RegGetValueW(
                key.0,
                null(),
                name.as_ptr(),
                RRF_RT_REG_SZ,
                null_mut(),
                data.as_mut_ptr().cast(),
                &mut size,
            )
        };
        if rc != ERROR_SUCCESS {
            return Err(ReadFailed);
        }
        let len = data
            .iter()
            .position(|&unit| unit == 0)
            .unwrap_or(data.len());
        Ok(Some(String::from_utf16_lossy(&data[..len])))
    }

    pub fn rtl_version() -> Option<(u32, u32, u32)> {
        let mut info = OSVERSIONINFOW {
            dwOSVersionInfoSize: size_of::<OSVERSIONINFOW>() as u32,
            ..Default::default()
        };
        // SAFETY: `info` is a valid, writable OSVERSIONINFOW whose `dwOSVersionInfoSize` names its own size, as
        // RtlGetVersion requires; the call only fills the structure (the RtlGetVersion page).
        let status = unsafe { RtlGetVersion(&mut info) };
        (status == STATUS_SUCCESS).then_some((
            info.dwMajorVersion,
            info.dwMinorVersion,
            info.dwBuildNumber,
        ))
    }
}
