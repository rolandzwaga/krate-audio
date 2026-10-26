#pragma once

// ==============================================================================
// Profundum Phase 12 - Profundum::ProfundumSpectrumFrame (FR-071, FR-073 - FR-075;
// plan 4.2)
// ==============================================================================
// Plain-old-data snapshot of the focus voice's harmonic spectrum, published at
// most once per process() call from the processor to the controller's
// HarmonicBarView.
//
// Shared by processor and controller (like plugin_ids.h): this header includes
// only the standard library -- never a processor, controller or dsp/ header.
//
// One-way, processor -> controller. Sent as raw bytes in native endianness
// (as Vorago's EcosystemFrame is); the 288-byte layout below is pinned by
// static_asserts. The frame carries both the sample rate and f0, so partial n's
// frequency is always n * f0 at the frame's own rate (FR-073).
// ==============================================================================

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Profundum {

inline constexpr std::uint32_t kProfundumFrameUserContextId = 0x50524644u;  // 'PRFD'; != 0x4D425452 / 0x53434C44 / 0x5645434F
inline constexpr std::size_t kSpectrumMaxPartials = 64;                     // FR-075 capacity
inline constexpr std::uint8_t kFrameFlagSilent = 0x01;         // no focus voice: numPartials == 0
inline constexpr std::uint8_t kFrameFlagPitchUnstable = 0x02;  // f0 drifted > kPitchStableTolerance in the window
inline constexpr std::uint8_t kFrameFlagPoly = 0x04;           // applied voice mode POLY

struct ProfundumSpectrumFrame {
    std::uint32_t sequence = 0;        // 0  monotonic per producer
    float sampleRate = 0.0f;           // 4  FR-073
    float f0Hz = 0.0f;                 // 8  the latched f0 of the window
    float anchorPower = 0.0f;          // 12 (sum aL^2 + sum aR^2) / N: a centred sine of amplitude a reads a^2
    std::uint32_t windowSamples = 0;   // 16 N
    std::uint8_t numPartials = 0;      // 20 count of plan 5.3; 0 when silent
    std::uint8_t flags = 0;            // 21
    std::uint8_t focusVoice = 0;       // 22
    std::uint8_t reserved = 0;         // 23
    std::uint64_t windowEndSample = 0; // 24 engine sample position one past the window's last sample
    float partialPower[kSpectrumMaxPartials] = {};  // 32 power at n*f0, n = index + 1 (plan 5.2); 0 past numPartials
};

static_assert(sizeof(ProfundumSpectrumFrame) == 288 && sizeof(ProfundumSpectrumFrame) <= 320);  // SC-030
static_assert(sizeof(ProfundumSpectrumFrame) % 32 == 0);  // the DataExchange block needs no padding (SC-023)
static_assert(std::is_trivially_copyable_v<ProfundumSpectrumFrame> &&
              std::is_standard_layout_v<ProfundumSpectrumFrame>);
static_assert(offsetof(ProfundumSpectrumFrame, windowEndSample) == 24 &&
              offsetof(ProfundumSpectrumFrame, partialPower) == 32);

}  // namespace Profundum
