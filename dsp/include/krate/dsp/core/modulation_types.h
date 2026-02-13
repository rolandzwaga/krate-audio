// ==============================================================================
// Layer 0: Core Types - Modulation Types
// ==============================================================================
// Enumerations and value types for the modulation system.
// All modulation sources, curves, routing structs, and configuration types.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (POD types, no allocations)
// - Principle III: Modern C++ (C++20, enum class, constexpr)
// - Principle IX: Layer 0 (no dependencies on higher layers)
//
// Reference: specs/008-modulation-system/spec.md (FR-001 to FR-006)
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Modulation Source Enumeration (FR-002)
// =============================================================================

/// @brief Identifies a modulation source for routing.
///
/// 13 values including None (inactive). Used in ModRouting to specify
/// which source drives a destination parameter.
///
/// @par Output Ranges
/// - Bipolar [-1, +1]: LFO1, LFO2, Random, Chaos, SampleHold (Random/LFO mode)
/// - Unipolar [0, +1]: EnvFollower, Macro1-4, PitchFollower, Transient, SampleHold (External mode)
/// - None: always returns 0.0
enum class ModSource : uint8_t {
    None = 0,           ///< No source (routing inactive, returns 0.0)
    LFO1 = 1,           ///< LFO 1 (FR-007)
    LFO2 = 2,           ///< LFO 2 (FR-007)
    EnvFollower = 3,    ///< Envelope Follower (FR-015)
    Random = 4,         ///< Random source (FR-021)
    Macro1 = 5,         ///< Macro 1 (FR-026)
    Macro2 = 6,         ///< Macro 2 (FR-026)
    Macro3 = 7,         ///< Macro 3 (FR-026)
    Macro4 = 8,         ///< Macro 4 (FR-026)
    Chaos = 9,          ///< Chaos attractor (FR-030)
    SampleHold = 10,    ///< Sample & Hold (FR-036)
    PitchFollower = 11, ///< Pitch Follower (FR-041)
    Transient = 12      ///< Transient Detector (FR-048)
};

/// Total number of ModSource values (including None).
inline constexpr uint8_t kModSourceCount = 13;

// =============================================================================
// Modulation Curve Enumeration (FR-058)
// =============================================================================

/// @brief Response curve shape applied to modulation routing.
///
/// Curves shape how source values map to destination offsets.
/// Applied to abs(sourceValue), then sign from amount applied afterward (FR-059).
///
/// @par Formulas (input x in [0, 1]):
/// - Linear: y = x
/// - Exponential: y = x^2
/// - SCurve: y = x^2 * (3 - 2x) (smoothstep)
/// - Stepped: y = floor(x * 4) / 3 (4 discrete levels: 0, 0.333, 0.667, 1.0)
enum class ModCurve : uint8_t {
    Linear = 0,       ///< y = x (transparent)
    Exponential = 1,  ///< y = x^2 (slow start, fast end)
    SCurve = 2,       ///< y = x^2 * (3 - 2x) (smoothstep)
    Stepped = 3       ///< y = floor(x * 4) / 3 (4 levels)
};

/// Total number of ModCurve values.
inline constexpr uint8_t kModCurveCount = 4;

// =============================================================================
// Modulation Routing Structure (FR-003, FR-056)
// =============================================================================

/// @brief Describes a single source-to-destination modulation connection.
///
/// Up to kMaxModRoutings (32) can be active simultaneously (FR-004).
/// Amount is bipolar [-1, +1] per FR-057. Curve shapes the response per FR-058.
///
/// @par Processing Formula (FR-059):
/// @code
/// rawSource = source.getCurrentValue()            // [-1, +1] or [0, +1]
/// absSource = abs(rawSource)                      // [0, +1]
/// curvedSource = applyModCurve(curve, absSource)  // [0, +1] shaped
/// output = curvedSource * amount                  // amount carries sign
/// @endcode
struct ModRouting {
    ModSource source = ModSource::None;    ///< Which source drives this routing
    uint32_t destParamId = 0;              ///< Destination VST parameter ID
    float amount = 0.0f;                   ///< Bipolar amount [-1.0, +1.0]
    ModCurve curve = ModCurve::Linear;     ///< Response curve shape
    float smoothMs = 0.0f;                 ///< Per-route output smoothing time (0 = off)
    bool active = false;                   ///< Whether this slot is in use
};

/// Maximum number of simultaneous modulation routings (FR-004).
inline constexpr size_t kMaxModRoutings = 32;

// =============================================================================
// Macro Configuration (FR-026 to FR-029a)
// =============================================================================

/// @brief Configuration for a single macro parameter.
///
/// Processing order (per clarification session 2026-01-29):
/// 1. Min/Max mapping FIRST: mappedValue = min + value * (max - min)  (FR-028)
/// 2. Curve applied AFTER: output = applyModCurve(curve, mappedValue) (FR-029)
///
/// Output range: [0, +1] (unipolar) per FR-029a.
struct MacroConfig {
    float value = 0.0f;                    ///< Current knob position [0, 1]
    float minOutput = 0.0f;                ///< Minimum output range [0, 1] (FR-028)
    float maxOutput = 1.0f;                ///< Maximum output range [0, 1] (FR-028)
    ModCurve curve = ModCurve::Linear;     ///< Response curve (FR-029)
};

/// Maximum number of macro parameters (FR-026).
inline constexpr size_t kMaxMacros = 4;

// =============================================================================
// Envelope Follower Source Type (FR-020a)
// =============================================================================

/// @brief Selects which audio signal feeds the Envelope Follower.
///
/// Per FR-020a (added from specs-overview.md FR-MOD-001 "Source" parameter).
/// Default: InputSum.
enum class EnvFollowerSourceType : uint8_t {
    InputL = 0,     ///< Left channel only
    InputR = 1,     ///< Right channel only
    InputSum = 2,   ///< L + R (default)
    Mid = 3,        ///< (L + R) / 2
    Side = 4        ///< (L - R) / 2
};

// =============================================================================
// Sample & Hold Input Type (FR-037)
// =============================================================================

/// @brief Selects which signal the Sample & Hold module samples.
///
/// Per FR-037: user-selectable via dropdown with 4 options.
/// Per clarification: LFO 1 and LFO 2 are independently selectable.
enum class SampleHoldInputType : uint8_t {
    Random = 0,     ///< White noise [-1, +1]
    LFO1 = 1,       ///< Current LFO 1 output
    LFO2 = 2,       ///< Current LFO 2 output
    External = 3    ///< Input audio amplitude [0, +1]
};

}  // namespace DSP
}  // namespace Krate
