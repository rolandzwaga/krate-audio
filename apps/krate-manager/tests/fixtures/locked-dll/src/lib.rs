//! Phase 10 FR-060 / FR-101: the fixture plug-in DLL of the `inno-locked-file` CI job. It embeds the version it was
//! built with (`LOCKED_DLL_VERSION`, set by the job to 1.0.0 or 1.0.1), so the two fixture setups ship different bytes,
//! as a real update does. Never shipped, never loaded outside that job.

use core::ffi::c_char;

/// The build-time version, NUL-terminated so a C caller can read it.
static VERSION: &str = concat!(env!("LOCKED_DLL_VERSION"), "\0");

/// Returns the version this DLL was built with, as a static NUL-terminated string.
#[unsafe(no_mangle)]
pub extern "C" fn fixture_version() -> *const c_char {
    VERSION.as_ptr().cast()
}
