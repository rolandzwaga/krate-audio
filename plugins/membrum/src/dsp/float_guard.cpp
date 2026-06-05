// ==============================================================================
// float_guard.cpp -- non-finite (NaN/Inf) detection, compiled -fno-fast-math
// ==============================================================================
// This translation unit MUST be built without fast-math (see CMakeLists.txt:
// the membrum_float_guard target sets -fno-fast-math / -fno-finite-math-only,
// or /fp:precise on MSVC). Compiled with the SDK's global -ffast-math, the
// exponent test below would be folded to a constant `false` because
// -ffinite-math-only tells the compiler NaN/Inf cannot occur.
// ==============================================================================

#include "dsp/float_guard.h"

#include <cstdint>
#include <cstring>

namespace Membrum::DSP {

bool isNonFinite(float x) noexcept
{
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &x, sizeof(bits));
    // IEEE-754 single precision: exponent field (bits 23..30) all ones means
    // the value is NaN (mantissa != 0) or +/-Inf (mantissa == 0). A single mask
    // + compare catches both.
    return (bits & 0x7F800000u) == 0x7F800000u;
}

}  // namespace Membrum::DSP
