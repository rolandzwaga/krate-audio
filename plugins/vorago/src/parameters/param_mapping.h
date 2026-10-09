#pragma once

// ==============================================================================
// Vorago Phase 12 - mapping functions, seed table, noise-type list (plan section 3.3)
// ==============================================================================
// All mappings are in double, clamp in and out (the logMapFromNormalized convention,
// plugins/shared/src/ui/parameter_helpers.h:74-88), and are cast to float exactly
// once at the store (by the pack handlers).
// ==============================================================================

#include "engine/vorago_engine_config.h"

#include "ui/parameter_helpers.h"  // plugins/shared/src/ui/parameter_helpers.h

#include <krate/dsp/processors/noise_generator.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Vorago {

enum class Taper : std::uint8_t { Linear, Log, OffsetLog, Discrete };

// ------------------------------------------------------------------------------
// Linear
// ------------------------------------------------------------------------------
[[nodiscard]] inline double linearFromNormalized(double n, double mn, double mx) noexcept {
    return mn + std::clamp(n, 0.0, 1.0) * (mx - mn);
}

[[nodiscard]] inline double linearToNormalized(double u, double mn, double mx) noexcept {
    return std::clamp((std::clamp(u, mn, mx) - mn) / (mx - mn), 0.0, 1.0);
}

// ------------------------------------------------------------------------------
// Log: Krate::Plugins::logMapFromNormalized / logMapToNormalized, unchanged.
// Offset-log (Q4, zero-floored ranges): units + eps is log-mapped over
// [mn + eps, mx + eps]. log(0) is never evaluated (argument always >= eps > 0).
// eps per ID (plan section 3.3.1): envelope times 10 ms, bloom spawn 1e-4 Hz,
// space damper rate 0.01.
// ------------------------------------------------------------------------------
[[nodiscard]] inline double offsetLogFromNormalized(double n, double mn, double mx,
                                                    double eps) noexcept {
    return std::clamp(Krate::Plugins::logMapFromNormalized(n, mn + eps, mx + eps) - eps, mn, mx);
}

[[nodiscard]] inline double offsetLogToNormalized(double u, double mn, double mx,
                                                  double eps) noexcept {
    return Krate::Plugins::logMapToNormalized(std::clamp(u, mn, mx) + eps, mn + eps, mx + eps);
}

// ------------------------------------------------------------------------------
// Discrete (StringListParameter: index = round(n * (count - 1)))
// ------------------------------------------------------------------------------
[[nodiscard]] inline int indexFromNormalized(double n, int count) noexcept {
    return std::clamp(static_cast<int>(std::clamp(n, 0.0, 1.0) * (count - 1) + 0.5), 0,
                      count - 1);
}

[[nodiscard]] inline double indexToNormalized(int i, int count) noexcept {
    return (count > 1) ? static_cast<double>(std::clamp(i, 0, count - 1)) / (count - 1) : 0.0;
}

// ------------------------------------------------------------------------------
// Seed table (spec C-8, plan section 3.3.2). Labels "Seed 1" ... "Seed 16".
// ------------------------------------------------------------------------------
// Index 0 is pinned to 1u (== kEngineSeed, so the Phase 11 default sound holds).
// Entries 1-15 are the first 15 values > 1 of the low 32 bits of a splitmix64
// stream seeded with 0x5641524F474F3132 ("VARAGO12"), generated once by:
//
//   node -e 'let s=0x5641524F474F3132n;const M=(1n<<64n)-1n;const o=[];
//     while(o.length<15){s=(s+0x9E3779B97F4A7C15n)&M;let z=s;
//     z=((z^(z>>30n))*0xBF58476D1CE4E5B9n)&M;z=((z^(z>>27n))*0x94D049BB133111EBn)&M;
//     z=z^(z>>31n);const v=Number(z&0xFFFFFFFFn);if(v>1)o.push(v.toString(16));}
//     console.log(o.join(" "))'
//
// The table is a result, not a starting point: if SC-014's spread gate finds a
// pair too close, the offending entry is replaced by the next stream value and
// the gate re-run. The gate is never lowered.
inline constexpr std::array<std::uint32_t, 16> kVoragoSeedValues = {
    0x00000001u, 0x82D2B16Eu, 0xE9705B48u, 0x2F922331u, 0xB7E9DB62u, 0x7EC32EA4u,
    0x6F8F6B83u, 0x3EE6AA57u, 0xD1B7E8C1u, 0x7902AADAu, 0x6B3EBF4Au, 0xAF33581Eu,
    0x2673DE0Cu, 0x8F9D4B1Du, 0x95BD2F37u, 0x1DB23302u};

inline constexpr int kNumSeeds = static_cast<int>(kVoragoSeedValues.size());

static_assert(kVoragoSeedValues[0] == kEngineSeed,
              "seed index 0 must equal the Phase 11 engine seed");

inline constexpr std::uint32_t kCavernSeedSalt = 0x43415645u;  // 'CAVE'

/// C-8: the cavern seed for seed index `index`; index 0 keeps the Phase 11 kCavernSeed.
[[nodiscard]] constexpr std::uint32_t cavernSeedFor(int index) noexcept {
    return (index == 0) ? kCavernSeed
                        : (kVoragoSeedValues[static_cast<std::size_t>(
                               std::clamp(index, 0, kNumSeeds - 1))] ^
                           kCavernSeedSalt);
}

// ------------------------------------------------------------------------------
// Noise-type list (plan section 3.3.3): 12 entries, ModulationNoise excluded.
// Indices 0-10 equal the enum values 0-10; index 11 -> RadioStatic (enum 12).
// noise_generator.h:44-58.
// ------------------------------------------------------------------------------
inline constexpr std::array<Krate::DSP::NoiseType, 12> kNoiseTypeByIndex = {
    Krate::DSP::NoiseType::White,        Krate::DSP::NoiseType::Pink,
    Krate::DSP::NoiseType::TapeHiss,     Krate::DSP::NoiseType::VinylCrackle,
    Krate::DSP::NoiseType::Asperity,     Krate::DSP::NoiseType::Brown,
    Krate::DSP::NoiseType::Blue,         Krate::DSP::NoiseType::Violet,
    Krate::DSP::NoiseType::Grey,         Krate::DSP::NoiseType::Velvet,
    Krate::DSP::NoiseType::VinylRumble,  Krate::DSP::NoiseType::RadioStatic};

inline constexpr int kNumNoiseTypeChoices = static_cast<int>(kNoiseTypeByIndex.size());

/// Default noise type Brown = index 5 -> n0 = 5/11.
inline constexpr int kDefaultNoiseTypeIndex = 5;

/// Inverse of kNoiseTypeByIndex. ModulationNoise maps to TapeHiss's index 2,
/// matching the organism's own substitution (noise_organism.h:1239).
[[nodiscard]] constexpr int noiseTypeToIndex(Krate::DSP::NoiseType t) noexcept {
    if (t == Krate::DSP::NoiseType::ModulationNoise)
        return 2;
    for (int i = 0; i < kNumNoiseTypeChoices; ++i) {
        if (kNoiseTypeByIndex[static_cast<std::size_t>(i)] == t)
            return i;
    }
    return 0;
}

static_assert(kNoiseTypeByIndex[kDefaultNoiseTypeIndex] == Krate::DSP::NoiseType::Brown,
              "default noise type index must be Brown");
static_assert(std::find(kNoiseTypeByIndex.begin(), kNoiseTypeByIndex.end(),
                        Krate::DSP::NoiseType::ModulationNoise) == kNoiseTypeByIndex.end(),
              "ModulationNoise must not be user-selectable");
static_assert(noiseTypeToIndex(Krate::DSP::NoiseType::ModulationNoise) == 2,
              "ModulationNoise substitutes TapeHiss");
static_assert(kNoiseTypeByIndex[2] == Krate::DSP::NoiseType::TapeHiss);

}  // namespace Vorago
