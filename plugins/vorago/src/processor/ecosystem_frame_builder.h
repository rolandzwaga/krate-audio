#pragma once

// ==============================================================================
// Vorago Phase 13 - EcosystemFrame fill helpers (spec C-2, FR-020a, FR-027,
// plan section 3.2)
// ==============================================================================
// PROCESSOR-ONLY. Never included by ui/ or controller/: this header pulls in
// the DSP EcosystemEngine, while the controller side sees only the POD in
// ecosystem_frame.h (FR-020).
//
// Pure, inline, noexcept and allocation-free: safe on the audio thread.
// ==============================================================================

#include "processor/ecosystem_frame.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/ecosystem_engine.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace Vorago {

static_assert(kMaxFrameAgents == Krate::DSP::EcosystemEngine::kMaxAgents);  // FR-020a
static_assert(Krate::DSP::EcosystemEngine::kMaxAgents <= 255);               // uint8 link / agent indices

/// FR-027. Non-finite (bit pattern, never std::isnan) -> 0.0f; |v| > FLT_MAX -> 0.0f
/// (the narrowing would be UB, [conv.double]); else static_cast<float>(v).
[[nodiscard]] inline float sanitizeFrameFloat(double v) noexcept {
    if (!Krate::DSP::detail::isFinite(v)) {
        return 0.0f;
    }
    constexpr double kMax = static_cast<double>(std::numeric_limits<float>::max());
    if (v > kMax || v < -kMax) {
        return 0.0f;
    }
    return static_cast<float>(v);
}

/// C-2 clause 3. share = energy * agentCount / energyBudget; glow = share / (1 + share).
/// Non-finite inputs, a non-positive budget and a non-positive share give 0.
[[nodiscard]] inline float ecosystemEnergyGlow(double energy, std::size_t agentCount,
                                               double energyBudget) noexcept {
    if (!Krate::DSP::detail::isFinite(energy) || !Krate::DSP::detail::isFinite(energyBudget)
        || !(energyBudget > 0.0)) {
        return 0.0f;
    }
    const double share = energy * static_cast<double>(agentCount) / energyBudget;
    if (!(share > 0.0) || !Krate::DSP::detail::isFinite(share)) {
        return 0.0f;  // negative / zero share
    }
    return sanitizeFrameFloat(share / (1.0 + share));
}

/// C-2 clause 5. Carries the (up to) kMaxFrameLinks strongest non-zero, finite
/// recorded pair flows of the most recent step: writes linkCount, linkA/B and
/// linkStrength (= |flow|, sanitized), and zero-fills [linkCount, kMaxFrameLinks).
/// Order within the carried set is unspecified. O(n), n <= kMaxPairs; @p scratch
/// is caller-owned so nothing is allocated.
inline void selectStrongestLinks(
    const Krate::DSP::EcosystemEngine& eco,
    std::span<std::uint16_t, Krate::DSP::EcosystemEngine::kMaxPairs> scratch,
    EcosystemFrame& frame) noexcept {
    const std::size_t n =
        std::min(eco.getPairInteractionCount(), Krate::DSP::EcosystemEngine::kMaxPairs);

    // Compact: exact zeros (C-2 clause 5) and non-finite flows (FR-027) excluded.
    std::size_t m = 0;
    for (std::size_t p = 0; p < n; ++p) {
        const double f = eco.getPairFlow(p);
        if (f != 0.0 && Krate::DSP::detail::isFinite(f)) {
            scratch[m++] = static_cast<std::uint16_t>(p);
        }
    }

    const std::size_t k = std::min(m, kMaxFrameLinks);
    if (m > k) {
        const auto stronger = [&eco](std::uint16_t a, std::uint16_t b) noexcept {
            return std::fabs(eco.getPairFlow(a)) > std::fabs(eco.getPairFlow(b));
        };
        std::nth_element(scratch.begin(), scratch.begin() + static_cast<std::ptrdiff_t>(k),
                         scratch.begin() + static_cast<std::ptrdiff_t>(m), stronger);
    }

    for (std::size_t l = 0; l < k; ++l) {
        const std::size_t p = scratch[l];
        frame.linkA[l] = static_cast<std::uint8_t>(eco.getPairAgentA(p));
        frame.linkB[l] = static_cast<std::uint8_t>(eco.getPairAgentB(p));
        frame.linkStrength[l] = sanitizeFrameFloat(std::fabs(eco.getPairFlow(p)));
    }
    for (std::size_t l = k; l < kMaxFrameLinks; ++l) {
        frame.linkA[l] = 0;
        frame.linkB[l] = 0;
        frame.linkStrength[l] = 0.0f;
    }
    frame.linkCount = static_cast<std::uint8_t>(k);
}

}  // namespace Vorago
