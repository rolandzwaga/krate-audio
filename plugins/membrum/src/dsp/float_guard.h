#pragma once

namespace Membrum::DSP {

/// Returns true if @p x is NaN or +/-Inf.
///
/// Defined in float_guard.cpp, which is compiled WITHOUT fast-math
/// (-fno-fast-math / -fno-finite-math-only). The VST3 SDK enables -ffast-math
/// globally; under -ffinite-math-only the compiler is entitled to assume floats
/// are never NaN/Inf and fold inline NaN/Inf bit-checks to a constant `false`
/// (observed: Apple clang / arm64 silently disabled a real-time-safety guard
/// this way, letting an Inf body-feedback sample become Inf*0 = NaN and poison a
/// voice). A non-inline function in a -fno-fast-math translation unit is the only
/// portable, optimizer-independent way to guarantee the check is actually
/// evaluated. See CLAUDE.md / db_utils.h for the broader -ffast-math caveats.
[[nodiscard]] bool isNonFinite(float x) noexcept;

}  // namespace Membrum::DSP
