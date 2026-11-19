// ==============================================================================
// Layer 3: System Component - VoiceModRouter
// ==============================================================================
// Lightweight per-voice modulation router with fixed-size storage.
// Computes modulated parameter offsets once per block from up to 16 routes.
//
// Feature: 041-ruinae-voice-architecture
// Layer: 3 (Systems)
// Dependencies:
//   - voice_mod_types.h (VoiceModRoute, VoiceModSource, VoiceModDest enums)
//   - core/modulation_curves.h (applyBipolarModulation)
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

#include <krate/dsp/systems/voice_mod_types.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/modulation_curves.h>

#include <algorithm>
#include <array>
#include <cmath>
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
/// @par Amount, Curve and Scale
/// Route amounts are clamped to [-1.0, +1.0] on setRoute(). The contribution of
/// each route is applyBipolarModulation(curve, sourceValue, clampedAmount) * scale
/// (a Linear curve is the plain product sourceValue * clampedAmount). The scale
/// is applied after the clamp, so x2 / x4 extend the depth instead of saturating
/// the amount. Multiple routes to the same destination are summed (FR-027).
///
/// @par Smoothing
/// A route with smoothMs > 0 passes its contribution through a one-pole low-pass
/// whose 10-90 % rise time is smoothMs. Each computeOffsets() call advances the
/// smoothers by one sample, so call it once per sample and call prepare() with
/// the sample rate first.
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

    /// @brief Set the sample rate the route smoothers run at and clear their state.
    void prepare(double sampleRate) noexcept {
        if (sampleRate > 0.0) sampleRate_ = sampleRate;
        for (size_t i = 0; i < kMaxRoutes; ++i) {
            coefficients_[i] = smoothingCoefficient(routes_[i].smoothMs);
        }
        resetSmoothing();
    }

    /// @brief Clear the route smoothers (every route restarts from 0).
    void resetSmoothing() noexcept {
        smoothed_.fill(0.0f);
    }

    // =========================================================================
    // Route Management (FR-024)
    // =========================================================================

    /// @brief Set a modulation route at the given index.
    ///
    /// The route amount is clamped to [-1.0, +1.0] and smoothMs to
    /// [0, kMaxSmoothMs]; a non-finite smoothMs or scale is replaced by 0 / 1.
    /// A route that keeps its source, destination and curve keeps its smoother
    /// state; any other change restarts the smoother from 0.
    /// Out-of-range indices are silently ignored.
    ///
    /// @param index Route index [0, kMaxRoutes)
    /// @param route The modulation route to set
    void setRoute(int index, VoiceModRoute route) noexcept {
        if (index < 0 || index >= kMaxRoutes) return;
        const auto i = static_cast<size_t>(index);

        // Clamp amount to [-1.0, +1.0]
        route.amount = std::clamp(route.amount, -1.0f, 1.0f);
        route.smoothMs = detail::isFinite(route.smoothMs)
            ? std::clamp(route.smoothMs, 0.0f, kMaxSmoothMs) : 0.0f;
        if (!detail::isFinite(route.scale)) route.scale = 1.0f;

        const VoiceModRoute& old = routes_[i];
        const bool sameRoute = active_[i] && old.source == route.source
                               && old.destination == route.destination
                               && old.curve == route.curve;
        if (!sameRoute) smoothed_[i] = 0.0f;
        if (!active_[i] || old.smoothMs != route.smoothMs) {
            coefficients_[i] = smoothingCoefficient(route.smoothMs);
        }

        routes_[i] = route;
        active_[i] = true;
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
        smoothed_[static_cast<size_t>(index)] = 0.0f;
        coefficients_[static_cast<size_t>(index)] = 0.0f;
        updateRouteCount();
    }

    /// @brief Clear all modulation routes.
    void clearAllRoutes() noexcept {
        for (size_t i = 0; i < kMaxRoutes; ++i) {
            routes_[i] = VoiceModRoute{};
            active_[i] = false;
        }
        smoothed_.fill(0.0f);
        coefficients_.fill(0.0f);
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
    /// Iterates all active routes, reads the source value for each, shapes it
    /// with the route curve, multiplies by the route amount and scale, advances
    /// the route smoother by one sample, and accumulates to the destination offset.
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
            float contribution = (route.curve == ModCurve::Linear)
                ? sourceValue * route.amount
                : applyBipolarModulation(route.curve, sourceValue, route.amount);
            contribution *= route.scale;
            if (!detail::isFinite(contribution)) contribution = 0.0f;

            // One-pole smoothing; coefficient 0 (smoothMs 0) passes the
            // contribution through and keeps the state on it.
            float& state = smoothed_[i];
            state = detail::flushDenormal(
                contribution + coefficients_[i] * (state - contribution));

            offsets_[destIdx] += state;
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

    /// Longest accepted route smoothing time in milliseconds.
    static constexpr float kMaxSmoothMs = 1000.0f;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// One-pole coefficient whose step response rises 10-90 % in smoothMs
    /// (time constant smoothMs / ln 9); 0 for no smoothing.
    [[nodiscard]] float smoothingCoefficient(float smoothMs) const noexcept {
        if (smoothMs <= 0.0f) return 0.0f;
        constexpr double kLn9 = 2.1972245773362196;
        const double samples = static_cast<double>(smoothMs) * 0.001 * sampleRate_;
        return static_cast<float>(std::exp(-kLn9 / samples));
    }

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

    /// Per-route smoother state (the smoothed contribution) and coefficient
    std::array<float, kMaxRoutes> smoothed_{};
    std::array<float, kMaxRoutes> coefficients_{};

    /// Sample rate the smoothers run at (set by prepare())
    double sampleRate_{44100.0};

    /// Number of active routes
    int routeCount_{0};
};

} // namespace Krate::DSP
