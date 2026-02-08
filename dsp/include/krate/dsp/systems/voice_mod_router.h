// ==============================================================================
// Layer 3: System Component - VoiceModRouter
// ==============================================================================
// Lightweight per-voice modulation router with fixed-size storage.
// Computes modulated parameter offsets once per block from up to 16 routes.
//
// Feature: 041-ruinae-voice-architecture
// Layer: 3 (Systems)
// Dependencies:
//   - ruinae_types.h (VoiceModRoute, VoiceModSource, VoiceModDest enums)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations, fixed-size arrays)
// - Principle III: Modern C++ (C++20, scoped enums, std::array)
// - Principle IX: Layer 3 (depends on Layer 0 types only)
// - Principle XIV: ODR Prevention (unique class name verified)
//
// Reference: specs/041-ruinae-voice-architecture/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/systems/ruinae_types.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// VoiceModRouter Class (FR-024 through FR-027)
// =============================================================================

/// @brief Per-voice modulation router with fixed-size storage.
///
/// Manages up to 16 modulation routes that map source values (envelopes, LFO,
/// velocity, key tracking, gate) to destination parameter offsets. All offsets
/// are computed once per block via computeOffsets() and retrieved via getOffset().
///
/// @par Route Management
/// Routes are stored in a fixed std::array<VoiceModRoute, 16>. A route is
/// considered active if its amount is non-zero. Routes can be set, cleared, or
/// bulk-cleared at any time.
///
/// @par Amount Clamping
/// Route amounts are clamped to [-1.0, +1.0] on setRoute(). The computed offset
/// for each route is: sourceValue * clampedAmount. Multiple routes to the same
/// destination are summed (FR-027).
///
/// @par Thread Safety
/// Single-threaded model. All methods called from the audio thread.
///
/// @par Real-Time Safety
/// All methods are noexcept with zero heap allocations.
class VoiceModRouter {
public:
    /// Maximum number of modulation routes per voice.
    static constexpr int kMaxRoutes = 16;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    VoiceModRouter() noexcept = default;
    ~VoiceModRouter() noexcept = default;

    // Copyable and movable (all fixed-size arrays)
    VoiceModRouter(const VoiceModRouter&) noexcept = default;
    VoiceModRouter& operator=(const VoiceModRouter&) noexcept = default;
    VoiceModRouter(VoiceModRouter&&) noexcept = default;
    VoiceModRouter& operator=(VoiceModRouter&&) noexcept = default;

    // =========================================================================
    // Route Management (FR-024)
    // =========================================================================

    /// @brief Set a modulation route at the given index.
    ///
    /// The route amount is clamped to [-1.0, +1.0].
    /// Out-of-range indices are silently ignored.
    ///
    /// @param index Route index [0, kMaxRoutes)
    /// @param route The modulation route to set
    void setRoute(int index, VoiceModRoute route) noexcept {
        if (index < 0 || index >= kMaxRoutes) return;

        // Clamp amount to [-1.0, +1.0]
        route.amount = std::clamp(route.amount, -1.0f, 1.0f);
        routes_[static_cast<size_t>(index)] = route;
        active_[static_cast<size_t>(index)] = true;
        updateRouteCount();
    }

    /// @brief Clear a modulation route at the given index.
    ///
    /// Sets the route's amount to zero and marks it inactive.
    /// Out-of-range indices are silently ignored.
    ///
    /// @param index Route index [0, kMaxRoutes)
    void clearRoute(int index) noexcept {
        if (index < 0 || index >= kMaxRoutes) return;

        routes_[static_cast<size_t>(index)] = VoiceModRoute{};
        active_[static_cast<size_t>(index)] = false;
        updateRouteCount();
    }

    /// @brief Clear all modulation routes.
    void clearAllRoutes() noexcept {
        for (size_t i = 0; i < kMaxRoutes; ++i) {
            routes_[i] = VoiceModRoute{};
            active_[i] = false;
        }
        routeCount_ = 0;
    }

    /// @brief Get the number of active routes.
    [[nodiscard]] int getRouteCount() const noexcept {
        return routeCount_;
    }

    // =========================================================================
    // Per-Block Modulation Computation (FR-024, FR-025, FR-026)
    // =========================================================================

    /// @brief Compute modulation offsets for all destinations.
    ///
    /// Called once at the start of each processBlock with current source values.
    /// Iterates all active routes, reads the source value for each, multiplies
    /// by the route amount, and accumulates to the destination offset.
    ///
    /// After accumulation, all offsets are sanitized: NaN/Inf replaced with 0.0f,
    /// denormals flushed to zero (FR-024, 042-ext-modulation-system).
    ///
    /// Source value ranges:
    /// - env1, env2, env3: [0, 1] (envelope output)
    /// - lfo: [-1, +1] (bipolar LFO)
    /// - gate: [0, 1] (TranceGate smoothed value)
    /// - velocity: [0, 1] (constant per note)
    /// - keyTrack: [-1, +1] ((midiNote - 60) / 60)
    /// - aftertouch: [0, 1] (channel aftertouch)
    ///
    /// @param env1 Amplitude envelope value (ENV 1)
    /// @param env2 Filter envelope value (ENV 2)
    /// @param env3 General modulation envelope value (ENV 3)
    /// @param lfo Per-voice LFO value
    /// @param gate TranceGate output value
    /// @param velocity Note velocity (constant per note)
    /// @param keyTrack Key tracking value
    /// @param aftertouch Channel aftertouch value (FR-003, 042-ext-modulation-system)
    void computeOffsets(float env1, float env2, float env3,
                        float lfo, float gate,
                        float velocity, float keyTrack,
                        float aftertouch) noexcept {
        // Clear all destination offsets
        offsets_.fill(0.0f);

        // Store source values in array for indexed access
        sourceValues_[static_cast<size_t>(VoiceModSource::Env1)] = env1;
        sourceValues_[static_cast<size_t>(VoiceModSource::Env2)] = env2;
        sourceValues_[static_cast<size_t>(VoiceModSource::Env3)] = env3;
        sourceValues_[static_cast<size_t>(VoiceModSource::VoiceLFO)] = lfo;
        sourceValues_[static_cast<size_t>(VoiceModSource::GateOutput)] = gate;
        sourceValues_[static_cast<size_t>(VoiceModSource::Velocity)] = velocity;
        sourceValues_[static_cast<size_t>(VoiceModSource::KeyTrack)] = keyTrack;
        sourceValues_[static_cast<size_t>(VoiceModSource::Aftertouch)] = aftertouch;

        // Accumulate each active route's contribution
        for (size_t i = 0; i < kMaxRoutes; ++i) {
            if (!active_[i]) continue;

            const auto& route = routes_[i];
            const auto srcIdx = static_cast<size_t>(route.source);
            const auto destIdx = static_cast<size_t>(route.destination);

            // Safety: validate enum indices
            if (srcIdx >= static_cast<size_t>(VoiceModSource::NumSources)) continue;
            if (destIdx >= static_cast<size_t>(VoiceModDest::NumDestinations)) continue;

            const float sourceValue = sourceValues_[srcIdx];
            const float contribution = sourceValue * route.amount;

            offsets_[destIdx] += contribution;
        }

        // Sanitize all offsets: replace NaN/Inf with 0.0, flush denormals (FR-024)
        for (auto& offset : offsets_) {
            if (detail::isNaN(offset) || detail::isInf(offset)) {
                offset = 0.0f;
            }
            offset = detail::flushDenormal(offset);
        }
    }

    // =========================================================================
    // Offset Retrieval (FR-027)
    // =========================================================================

    /// @brief Get the computed modulation offset for a destination.
    ///
    /// Returns the summed modulation offset from all routes targeting the
    /// given destination. For pitch/cutoff destinations, the value is in
    /// semitones (scaled by the caller). For normalized destinations, the
    /// value is in linear space.
    ///
    /// Out-of-range destinations return 0.0.
    ///
    /// @param dest The modulation destination
    /// @return The summed modulation offset
    [[nodiscard]] float getOffset(VoiceModDest dest) const noexcept {
        const auto idx = static_cast<size_t>(dest);
        if (idx >= static_cast<size_t>(VoiceModDest::NumDestinations)) {
            return 0.0f;
        }
        return offsets_[idx];
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Recount active routes.
    void updateRouteCount() noexcept {
        routeCount_ = 0;
        for (size_t i = 0; i < kMaxRoutes; ++i) {
            if (active_[i]) {
                ++routeCount_;
            }
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    /// Fixed-size route storage (FR-024: up to 16 routes)
    std::array<VoiceModRoute, kMaxRoutes> routes_{};

    /// Active flags for each route slot
    std::array<bool, kMaxRoutes> active_{};

    /// Computed offsets per destination (FR-027: summed)
    static constexpr size_t kNumDests = static_cast<size_t>(VoiceModDest::NumDestinations);
    std::array<float, kNumDests> offsets_{};

    /// Source values cache (indexed by VoiceModSource)
    static constexpr size_t kNumSources = static_cast<size_t>(VoiceModSource::NumSources);
    std::array<float, kNumSources> sourceValues_{};

    /// Number of active routes
    int routeCount_{0};
};

} // namespace Krate::DSP
