#pragma once

// ==============================================================================
// Vorago - Engine / Cavern configuration (FR-053, FR-034a, FR-024a)
// ==============================================================================
// Thin free functions only, NO new type: every value returned is a DSP-owned
// struct. Plan section 2.2.
//
// Both configs are the shipped DSP defaults measured by the Phase 10 composed
// chain (dsp/tests/unit/effects/vorago_composed_chain_test.cpp:303-313); only
// maxBlockSamples (and the cavern seed, stated explicitly) are set here.
// ==============================================================================

#include <krate/dsp/effects/cavern_verb.h>
#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>

#include <cstddef>
#include <cstdint>

namespace Vorago {

/// One constant for the engine config, the cavern config, the slice bound (FR-026)
/// and any scratch (FR-028): == 2048 (vorago_engine.h:182).
inline constexpr std::size_t kMaxBlockSamples = Krate::DSP::VoragoEngine::kMaxBlockSamples;

/// Seeds (spec Conventions; Clarification SEED). Stated explicitly rather than
/// inherited from the DSP defaults, so a dsp/ default change cannot silently move
/// the plugin's sound. Not parameters in Phase 11.
inline constexpr std::uint32_t kEngineSeed = 1u;
inline constexpr std::uint32_t kCavernSeed = 1u;

/// FR-024a.2. Same 20 ms family as the Seraphis template.
inline constexpr float kMasterGainSmoothMs = 20.0f;

/// FR-053. The shipped VoragoEngineConfig (vorago_engine.h:105-156) with ONLY
/// maxBlockSamples set: smearEnabled = true, smearFftSize = 2048 (-> 2048 latency),
/// atmosCaptureSeconds = 20, both Phase 10a ghost fields inert. These are the
/// defaults the Phase 10 composed chain was measured with.
[[nodiscard]] inline Krate::DSP::VoragoEngineConfig
makeVoragoEngineConfig(std::size_t maxBlockSamples) noexcept {
    Krate::DSP::VoragoEngineConfig cfg{};
    cfg.maxBlockSamples = maxBlockSamples;  // engine clamps to [1, 2048] (:284-285)
    return cfg;
}

/// FR-053. CavernVerb::PrepareConfig's shipped defaults (cavern_verb.h:300-322) with
/// maxBlockSamples and seed set. spectralDiffusionEnabled MUST stay true and
/// diffusionFftSize MUST stay 1024 (-> 1024 latency, FR-033). These are the values the
/// Phase 10 composed chain and its SC-001b Cavern term were measured with
/// (dsp/tests/unit/effects/vorago_composed_chain_test.cpp:303-313: numChannels 8,
/// maxEarlySeconds 0.30, maxDelaySeconds 0.50, diffusion on at 1024 - all equal to
/// the PrepareConfig defaults).
[[nodiscard]] inline Krate::DSP::CavernVerb::PrepareConfig
makeVoragoCavernConfig(std::size_t maxBlockSamples, std::uint32_t seed = kCavernSeed) noexcept {
    Krate::DSP::CavernVerb::PrepareConfig cfg{};
    cfg.maxBlockSamples = maxBlockSamples;  // cavern clamps to [64, 8192] (:381)
    cfg.seed = seed;  // Phase 12 C-8: cavernSeedFor(seed index) (param_mapping.h)
    return cfg;
}

/// FR-034a. vorago_composed_chain_test.cpp:188-196 (pushCavernTargets, forceDry=false),
/// same seven setters in the same order.
inline void applyCavernTargets(Krate::DSP::CavernVerb& cavern,
                               const Krate::DSP::VoragoCavernTargets& t) noexcept {
    cavern.setSize(t.size);                  // cavern_verb.h:613
    cavern.setDarkness(t.darkness);          // :619
    cavern.setDecaySeconds(t.decaySeconds);  // :626
    cavern.setFog(t.fog);                    // :652
    cavern.setDamperDepth(t.damperDepth);    // :717
    cavern.setMix(t.mix);                    // :748
    cavern.setWidth(t.width);                // :741
}

}  // namespace Vorago
