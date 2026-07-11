// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "dsp/sweep_morph_link.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/note_value.h>

#include "display/shared_display_bridge.h"
#include "display/display_bridge_log.h"

#include <algorithm>  // for std::max, std::min
#include <cmath>      // for std::log10, std::pow
#include <cstring>    // for memcpy
#include <random>     // for instance ID generation

namespace Disrumpo {

// ==============================================================================
// Processor state persistence (getState / setState)
// ==============================================================================


// ==============================================================================
// IComponent - State Management
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
    // FR-018, FR-037: Serialize all parameters to IBStream
    // FR-020: Version field MUST be first for future migration

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Write version first (MUST be first per FR-020)
    if (!streamer.writeInt32(kPresetVersion)) {
        return Steinberg::kResultFalse;
    }

    // Write global parameters in order (per data-model.md Section 3)
    if (!streamer.writeFloat(inputGain_.load(std::memory_order_relaxed))) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.writeFloat(outputGain_.load(std::memory_order_relaxed))) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.writeFloat(globalMix_.load(std::memory_order_relaxed))) {
        return Steinberg::kResultFalse;
    }

    // FR-037: Band management state (v2+)
    // Band count
    if (!streamer.writeInt32(bandCount_.load(std::memory_order_relaxed))) {
        return Steinberg::kResultFalse;
    }

    // Per-band state for all 8 bands (fixed for format stability)
    for (int b = 0; b < kMaxBands; ++b) {
        const auto& bs = bandStates_[b];
        if (!streamer.writeFloat(bs.gainDb)) return Steinberg::kResultFalse;
        if (!streamer.writeFloat(bs.pan)) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(bs.solo ? 1 : 0))) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(bs.bypass ? 1 : 0))) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(bs.mute ? 1 : 0))) return Steinberg::kResultFalse;
    }

    // Crossover frequencies (7 floats)
    for (int c = 0; c < kMaxBands - 1; ++c) {
        float freq = crossoverL_.getCrossoverFrequency(c);
        if (!streamer.writeFloat(freq)) return Steinberg::kResultFalse;
    }

    // =========================================================================
    // Sweep System State (v4+) — SC-012
    // =========================================================================

    // Sweep Core (6 values)
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(sweepProcessor_.isEnabled() ? 1 : 0)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(normalizeSweepFrequency(sweepProcessor_.getTargetFrequency())))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((sweepProcessor_.getWidth() - 0.5f) / 3.5f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(sweepProcessor_.getIntensity() / 2.0f))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(sweepProcessor_.getFalloffMode())))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(sweepProcessor_.getMorphLinkMode())))
        return Steinberg::kResultFalse;

    // LFO (6 values)
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(sweepLFO_.isEnabled() ? 1 : 0)))
        return Steinberg::kResultFalse;
    // LFO Rate: denormalized Hz → normalized using inverse log formula
    {
        constexpr float kMinRateLog = -4.6052f;  // ln(0.01)
        constexpr float kMaxRateLog = 2.9957f;   // ln(20)
        float normalizedRate = (std::log(sweepLFO_.getRate()) - kMinRateLog) / (kMaxRateLog - kMinRateLog);
        normalizedRate = std::clamp(normalizedRate, 0.0f, 1.0f);
        if (!streamer.writeFloat(normalizedRate)) return Steinberg::kResultFalse;
    }
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(sweepLFO_.getWaveform())))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(sweepLFO_.getDepth()))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(sweepLFO_.isTempoSynced() ? 1 : 0)))
        return Steinberg::kResultFalse;
    {
        // Encode as standard dropdown index (0-20, matching kNoteValueDropdownMapping)
        const auto nv = sweepLFO_.getNoteValue();
        const auto nm = sweepLFO_.getNoteModifier();
        int noteIndex = Krate::DSP::kNoteValueDefaultIndex; // default 1/8
        for (int i = 0; i < Krate::DSP::kNoteValueDropdownCount; ++i) {
            if (Krate::DSP::kNoteValueDropdownMapping[i].note == nv &&
                Krate::DSP::kNoteValueDropdownMapping[i].modifier == nm) {
                noteIndex = i;
                break;
            }
        }
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(noteIndex)))
            return Steinberg::kResultFalse;
    }

    // Envelope (4 values)
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(sweepEnvelope_.isEnabled() ? 1 : 0)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((sweepEnvelope_.getAttackTime() - kMinSweepEnvAttackMs) /
                             (kMaxSweepEnvAttackMs - kMinSweepEnvAttackMs)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((sweepEnvelope_.getReleaseTime() - kMinSweepEnvReleaseMs) /
                             (kMaxSweepEnvReleaseMs - kMinSweepEnvReleaseMs)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(sweepEnvelope_.getSensitivity()))
        return Steinberg::kResultFalse;

    // Custom Curve breakpoints
    {
        int32_t pointCount = static_cast<int32_t>(customCurve_.getBreakpointCount());
        if (!streamer.writeInt32(pointCount)) return Steinberg::kResultFalse;
        for (int32_t i = 0; i < pointCount; ++i) {
            auto bp = customCurve_.getBreakpoint(i);
            if (!streamer.writeFloat(bp.x)) return Steinberg::kResultFalse;
            if (!streamer.writeFloat(bp.y)) return Steinberg::kResultFalse;
        }
    }

    // =========================================================================
    // Modulation System State (v5+) — SC-010
    // =========================================================================

    // --- Source Parameters ---

    // LFO 1 (7 values: rate[float], shape[int8], phase[float], sync[int8],
    //         noteValue[int8], unipolar[int8], retrigger[int8])
    {
        constexpr float kMinLog = -4.6052f;  // ln(0.01)
        constexpr float kMaxLog = 2.9957f;   // ln(20)
        float rateNorm = (std::log(modulationEngine_.getLFO1Rate()) - kMinLog) / (kMaxLog - kMinLog);
        if (!streamer.writeFloat(std::clamp(rateNorm, 0.0f, 1.0f))) return Steinberg::kResultFalse;
    }
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO1Waveform())))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getLFO1PhaseOffset() / 360.0f))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO1TempoSync() ? 1 : 0)))
        return Steinberg::kResultFalse;
    {
        const auto nv = modulationEngine_.getLFO1NoteValue();
        const auto nm = modulationEngine_.getLFO1NoteModifier();
        int noteIdx = Krate::DSP::kNoteValueDefaultIndex;
        for (int i = 0; i < Krate::DSP::kNoteValueDropdownCount; ++i) {
            if (Krate::DSP::kNoteValueDropdownMapping[i].note == nv &&
                Krate::DSP::kNoteValueDropdownMapping[i].modifier == nm) {
                noteIdx = i;
                break;
            }
        }
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(noteIdx)))
            return Steinberg::kResultFalse;
    }
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO1Unipolar() ? 1 : 0)))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO1Retrigger() ? 1 : 0)))
        return Steinberg::kResultFalse;

    // LFO 2 (7 values: same layout as LFO 1)
    {
        constexpr float kMinLog = -4.6052f;
        constexpr float kMaxLog = 2.9957f;
        float rateNorm = (std::log(modulationEngine_.getLFO2Rate()) - kMinLog) / (kMaxLog - kMinLog);
        if (!streamer.writeFloat(std::clamp(rateNorm, 0.0f, 1.0f))) return Steinberg::kResultFalse;
    }
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO2Waveform())))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getLFO2PhaseOffset() / 360.0f))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO2TempoSync() ? 1 : 0)))
        return Steinberg::kResultFalse;
    {
        const auto nv = modulationEngine_.getLFO2NoteValue();
        const auto nm = modulationEngine_.getLFO2NoteModifier();
        int noteIdx = Krate::DSP::kNoteValueDefaultIndex;
        for (int i = 0; i < Krate::DSP::kNoteValueDropdownCount; ++i) {
            if (Krate::DSP::kNoteValueDropdownMapping[i].note == nv &&
                Krate::DSP::kNoteValueDropdownMapping[i].modifier == nm) {
                noteIdx = i;
                break;
            }
        }
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(noteIdx)))
            return Steinberg::kResultFalse;
    }
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO2Unipolar() ? 1 : 0)))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO2Retrigger() ? 1 : 0)))
        return Steinberg::kResultFalse;

    // Envelope Follower (4 values: attack[float], release[float], sensitivity[float], source[int8])
    if (!streamer.writeFloat((modulationEngine_.getEnvFollowerAttack() - 1.0f) / 99.0f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((modulationEngine_.getEnvFollowerRelease() - 10.0f) / 490.0f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getEnvFollowerSensitivity()))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getEnvFollowerSource())))
        return Steinberg::kResultFalse;

    // Random (3 values: rate[float], smoothness[float], sync[int8])
    if (!streamer.writeFloat((modulationEngine_.getRandomRate() - 0.1f) / 49.9f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getRandomSmoothness()))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getRandomTempoSync() ? 1 : 0)))
        return Steinberg::kResultFalse;

    // Chaos (3 values: model[int8], speed[float], coupling[float])
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getChaosModel())))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((modulationEngine_.getChaosSpeed() - 0.05f) / 19.95f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getChaosCoupling()))
        return Steinberg::kResultFalse;

    // Sample & Hold (3 values: source[int8], rate[float], slew[float])
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getSampleHoldSource())))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((modulationEngine_.getSampleHoldRate() - 0.1f) / 49.9f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getSampleHoldSlew() / 500.0f))
        return Steinberg::kResultFalse;

    // Pitch Follower (4 values: minHz[float], maxHz[float], confidence[float], trackingSpeed[float])
    if (!streamer.writeFloat((modulationEngine_.getPitchFollowerMinHz() - 20.0f) / 480.0f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((modulationEngine_.getPitchFollowerMaxHz() - 200.0f) / 4800.0f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getPitchFollowerConfidence()))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((modulationEngine_.getPitchFollowerTrackingSpeed() - 10.0f) / 290.0f))
        return Steinberg::kResultFalse;

    // Transient (3 values: sensitivity[float], attack[float], decay[float])
    if (!streamer.writeFloat(modulationEngine_.getTransientSensitivity()))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((modulationEngine_.getTransientAttack() - 0.5f) / 9.5f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((modulationEngine_.getTransientDecay() - 20.0f) / 180.0f))
        return Steinberg::kResultFalse;

    // Rungler (v11+: 4 values: rate[float], depth[float], bits[int8], loop[int8])
    if (!streamer.writeFloat((modulationEngine_.getRunglerOsc1Freq() - 0.1f) / 49.9f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getRunglerDepth()))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getRunglerBits())))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getRunglerLoopMode() ? 1 : 0)))
        return Steinberg::kResultFalse;

    // Macros (4 × 4 = 16 values: value[float], min[float], max[float], curve[int8])
    for (size_t m = 0; m < Krate::DSP::kMaxMacros; ++m) {
        const auto& macro = modulationEngine_.getMacro(m);
        if (!streamer.writeFloat(macro.value)) return Steinberg::kResultFalse;
        if (!streamer.writeFloat(macro.minOutput)) return Steinberg::kResultFalse;
        if (!streamer.writeFloat(macro.maxOutput)) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(macro.curve)))
            return Steinberg::kResultFalse;
    }

    // --- Routing Parameters (32 × 4 values: source[int8], dest[int32], amount[float], curve[int8]) ---
    for (size_t r = 0; r < Krate::DSP::kMaxModRoutings; ++r) {
        const auto& routing = modulationEngine_.getRouting(r);
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(routing.source)))
            return Steinberg::kResultFalse;
        if (!streamer.writeInt32(static_cast<int32_t>(routing.destParamId)))
            return Steinberg::kResultFalse;
        if (!streamer.writeFloat(routing.amount))
            return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(routing.curve)))
            return Steinberg::kResultFalse;
    }

    // =========================================================================
    // Morph Node State (v6+)
    // =========================================================================
    for (int b = 0; b < kMaxBands; ++b) {
        const auto& cache = bandMorphCache_[b];

        // Band morph position & config (3 floats + 2 int8)
        if (!streamer.writeFloat(cache.morphX)) return Steinberg::kResultFalse;
        if (!streamer.writeFloat(cache.morphY)) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(0)))  // morphMode
            return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(cache.activeNodeCount)))
            return Steinberg::kResultFalse;
        if (!streamer.writeFloat(0.0f))  // morphSmoothing (ms)
            return Steinberg::kResultFalse;

        // Per-node state (4 nodes × 7 values each)
        for (int n = 0; n < kMaxMorphNodes; ++n) {
            const auto& mn = cache.nodes[static_cast<size_t>(n)];
            if (!streamer.writeInt8(static_cast<Steinberg::int8>(mn.type)))
                return Steinberg::kResultFalse;
            if (!streamer.writeFloat(mn.commonParams.drive)) return Steinberg::kResultFalse;
            if (!streamer.writeFloat(mn.commonParams.mix)) return Steinberg::kResultFalse;
            if (!streamer.writeFloat(mn.commonParams.toneHz)) return Steinberg::kResultFalse;
            if (!streamer.writeFloat(mn.params.bias)) return Steinberg::kResultFalse;
            if (!streamer.writeFloat(mn.params.folds)) return Steinberg::kResultFalse;
            if (!streamer.writeFloat(mn.params.bitDepth)) return Steinberg::kResultFalse;

            // v9: Shape parameter slots
            for (float slotValue : mn.shapeSlots) {
                if (!streamer.writeFloat(slotValue)) return Steinberg::kResultFalse;
            }

            // v9: Per-type shadow storage (26 types × 10 slots)
            const auto& shadow = bandMorphCache_[b].shapeShadow[static_cast<size_t>(n)];
            for (const auto& typeSlot : shadow.typeSlots) {
                for (float slotValue : typeSlot) {
                    if (!streamer.writeFloat(slotValue))
                        return Steinberg::kResultFalse;
                }
            }
        }
    }

    // SharedDisplayBridge: append instance ID for Tier 3 fallback
    streamer.writeInt32(kInstanceIdMarker);
    streamer.writeInt64(static_cast<Steinberg::int64>(instanceId_));

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
    // FR-019, FR-038: Deserialize parameters from IBStream
    // FR-021: Handle corrupted/invalid data gracefully

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read version first
    int32_t version = 0;
    if (!streamer.readInt32(version)) {
        // Corrupted state: return kResultFalse, plugin uses defaults
        return Steinberg::kResultFalse;
    }

    // FR-021: Version handling
    if (version < 1) {
        // Invalid version: corrupted data
        return Steinberg::kResultFalse;
    }

    if (version > kPresetVersion) {
        // Future version: read what we understand, skip unknown
    }

    // Read global parameters (v1+)
    float inputGain = 0.5f;
    float outputGain = 0.5f;
    float globalMix = 1.0f;

    if (!streamer.readFloat(inputGain)) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.readFloat(outputGain)) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.readFloat(globalMix)) {
        return Steinberg::kResultFalse;
    }

    // Apply global parameters
    inputGain_.store(inputGain, std::memory_order_relaxed);
    outputGain_.store(outputGain, std::memory_order_relaxed);
    globalMix_.store(globalMix, std::memory_order_relaxed);

    // FR-038: Band management state (v2+)
    if (version >= 2) {
        // Read band count
        int32_t bandCount = kDefaultBands;
        if (!streamer.readInt32(bandCount)) {
            // Use defaults if read fails
            return Steinberg::kResultOk;
        }
        bandCount = std::clamp(bandCount, kMinBands, 4);
        bandCount_.store(bandCount, std::memory_order_relaxed);

        // Read per-band state
        // v7 and earlier wrote 8 bands; v8+ writes 4 bands
        constexpr int kV7MaxBands = 8;
        const int streamBands = (version <= 7) ? kV7MaxBands : kMaxBands;
        for (int b = 0; b < streamBands; ++b) {
            float gainDb = 0.0f;
            float pan = 0.0f;
            Steinberg::int8 soloVal = 0;
            Steinberg::int8 bypassVal = 0;
            Steinberg::int8 muteVal = 0;

            if (!streamer.readFloat(gainDb)) gainDb = 0.0f;
            if (!streamer.readFloat(pan)) pan = 0.0f;
            if (!streamer.readInt8(soloVal)) soloVal = 0;
            if (!streamer.readInt8(bypassVal)) bypassVal = 0;
            if (!streamer.readInt8(muteVal)) muteVal = 0;

            if (b < kMaxBands) {
                auto& bs = bandStates_[b];
                bs.gainDb = std::clamp(gainDb, kMinBandGainDb, kMaxBandGainDb);
                bs.pan = std::clamp(pan, -1.0f, 1.0f);
                bs.solo = (soloVal != 0);
                bs.bypass = (bypassVal != 0);
                bs.mute = (muteVal != 0);

                bandProcessors_[b].setGainDb(bs.gainDb);
                bandProcessors_[b].setPan(bs.pan);
                bandProcessors_[b].setMute(bs.mute);
            }
            // else: discard data from bands 4-7 (v7 migration)
        }

        // Read crossover frequencies
        // v7 and earlier wrote 7 crossovers; v8+ writes 3
        const int streamCrossovers = (version <= 7) ? 7 : (kMaxBands - 1);
        for (int c = 0; c < streamCrossovers; ++c) {
            float freq = 1000.0f;  // Default
            if (!streamer.readFloat(freq)) break;

            if (c < kMaxBands - 1) {
                crossoverL_.setCrossoverFrequency(c, freq);
                crossoverR_.setCrossoverFrequency(c, freq);
            }
            // else: discard crossovers 3-6 (v7 migration)
        }

        // Update band counts in crossover networks
        crossoverL_.setBandCount(bandCount);
        crossoverR_.setBandCount(bandCount);
    }

    // =========================================================================
    // Sweep System State (v4+) — SC-012
    // =========================================================================
    if (version >= 4) {
        // Sweep Core
        Steinberg::int8 sweepEnable = 0;
        float sweepFreqNorm = 0.566f;
        float sweepWidthNorm = 0.286f;
        float sweepIntensityNorm = 0.25f;
        Steinberg::int8 sweepFalloff = 1;
        Steinberg::int8 sweepMorphLink = 0;

        if (streamer.readInt8(sweepEnable))
            sweepProcessor_.setEnabled(sweepEnable != 0);
        if (streamer.readFloat(sweepFreqNorm)) {
            float freqHz = denormalizeSweepFrequency(sweepFreqNorm);
            baseSweepFrequency_.store(freqHz, std::memory_order_relaxed);
            sweepProcessor_.setCenterFrequency(freqHz);
        }
        if (streamer.readFloat(sweepWidthNorm))
            sweepProcessor_.setWidth(0.5f + sweepWidthNorm * 3.5f);
        if (streamer.readFloat(sweepIntensityNorm))
            sweepProcessor_.setIntensity(sweepIntensityNorm * 2.0f);
        if (streamer.readInt8(sweepFalloff))
            sweepProcessor_.setFalloffMode(static_cast<SweepFalloff>(sweepFalloff));
        if (streamer.readInt8(sweepMorphLink))
            sweepProcessor_.setMorphLinkMode(static_cast<MorphLinkMode>(
                std::clamp(static_cast<int>(sweepMorphLink), 0, kMorphLinkModeCount - 1)));

        // LFO
        Steinberg::int8 lfoEnable = 0;
        float lfoRateNorm = 0.606f;
        Steinberg::int8 lfoWaveform = 0;
        float lfoDepth = 0.0f;
        Steinberg::int8 lfoSync = 0;
        Steinberg::int8 lfoNoteIndex = 0;

        if (streamer.readInt8(lfoEnable))
            sweepLFO_.setEnabled(lfoEnable != 0);
        if (streamer.readFloat(lfoRateNorm)) {
            constexpr float kMinRateLog = -4.6052f;
            constexpr float kMaxRateLog = 2.9957f;
            float rateHz = std::exp(kMinRateLog + lfoRateNorm * (kMaxRateLog - kMinRateLog));
            sweepLFO_.setRate(rateHz);
        }
        if (streamer.readInt8(lfoWaveform))
            sweepLFO_.setWaveform(static_cast<Krate::DSP::Waveform>(
                std::clamp(static_cast<int>(lfoWaveform), 0, 5)));
        if (streamer.readFloat(lfoDepth))
            sweepLFO_.setDepth(lfoDepth);
        if (streamer.readInt8(lfoSync))
            sweepLFO_.setTempoSync(lfoSync != 0);
        if (streamer.readInt8(lfoNoteIndex)) {
            int idx;
            if (version <= 9) {
                // v9: old 15-entry encoding (NoteValue*3+NoteModifier) → convert to 21-entry dropdown
                idx = kOldNoteIdxToNewDropdown[std::clamp(static_cast<int>(static_cast<unsigned char>(lfoNoteIndex)), 0, 14)];
            } else {
                idx = std::clamp(static_cast<int>(static_cast<unsigned char>(lfoNoteIndex)), 0, kNoteValueCount - 1);
            }
            const auto mapping = Krate::DSP::getNoteValueFromDropdown(idx);
            sweepLFO_.setNoteValue(mapping.note, mapping.modifier);
        }

        // Envelope
        Steinberg::int8 envEnable = 0;
        float envAttackNorm = 0.091f;
        float envReleaseNorm = 0.184f;
        float envSensitivity = 0.5f;

        if (streamer.readInt8(envEnable))
            sweepEnvelope_.setEnabled(envEnable != 0);
        if (streamer.readFloat(envAttackNorm))
            sweepEnvelope_.setAttackTime(kMinSweepEnvAttackMs +
                envAttackNorm * (kMaxSweepEnvAttackMs - kMinSweepEnvAttackMs));
        if (streamer.readFloat(envReleaseNorm))
            sweepEnvelope_.setReleaseTime(kMinSweepEnvReleaseMs +
                envReleaseNorm * (kMaxSweepEnvReleaseMs - kMinSweepEnvReleaseMs));
        if (streamer.readFloat(envSensitivity))
            sweepEnvelope_.setSensitivity(envSensitivity);

        // Custom Curve
        int32_t pointCount = 2;
        if (streamer.readInt32(pointCount)) {
            pointCount = std::clamp(pointCount, 2, 8);
            // Clear and rebuild custom curve
            while (customCurve_.getBreakpointCount() > 2) {
                customCurve_.removeBreakpoint(1);
            }
            // Read first point (endpoint x=0)
            float px = 0.0f;
            float py = 0.0f;
            if (pointCount >= 1 && streamer.readFloat(px) && streamer.readFloat(py)) {
                customCurve_.setBreakpoint(0, 0.0f, py);
            }
            // Read intermediate points
            for (int32_t i = 1; i < pointCount - 1; ++i) {
                if (streamer.readFloat(px) && streamer.readFloat(py)) {
                    customCurve_.addBreakpoint(px, py);
                }
            }
            // Read last point (endpoint x=1)
            if (pointCount >= 2 && streamer.readFloat(px) && streamer.readFloat(py)) {
                customCurve_.setBreakpoint(customCurve_.getBreakpointCount() - 1, 1.0f, py);
            }
        }
    }

    // =========================================================================
    // Modulation System State (v5+) — SC-010
    // =========================================================================
    if (version >= 5) {
        // --- Source Parameters ---

        // LFO 1 (7 values)
        float lfo1RateNorm = 0.5f;
        if (streamer.readFloat(lfo1RateNorm)) {
            constexpr float kMinLog = -4.6052f;
            constexpr float kMaxLog = 2.9957f;
            float rateHz = std::exp(kMinLog + lfo1RateNorm * (kMaxLog - kMinLog));
            modulationEngine_.setLFO1Rate(rateHz);
        }
        Steinberg::int8 lfo1Shape = 0;
        if (streamer.readInt8(lfo1Shape))
            modulationEngine_.setLFO1Waveform(static_cast<Krate::DSP::Waveform>(
                std::clamp(static_cast<int>(lfo1Shape), 0, 5)));
        float lfo1Phase = 0.0f;
        if (streamer.readFloat(lfo1Phase))
            modulationEngine_.setLFO1PhaseOffset(lfo1Phase * 360.0f);
        Steinberg::int8 lfo1Sync = 0;
        if (streamer.readInt8(lfo1Sync))
            modulationEngine_.setLFO1TempoSync(lfo1Sync != 0);
        Steinberg::int8 lfo1NoteIdx = 0;
        if (streamer.readInt8(lfo1NoteIdx)) {
            int idx;
            if (version <= 9) {
                idx = kOldNoteIdxToNewDropdown[std::clamp(static_cast<int>(static_cast<unsigned char>(lfo1NoteIdx)), 0, 14)];
            } else {
                idx = std::clamp(static_cast<int>(static_cast<unsigned char>(lfo1NoteIdx)), 0, kNoteValueCount - 1);
            }
            const auto mapping = Krate::DSP::getNoteValueFromDropdown(idx);
            modulationEngine_.setLFO1NoteValue(mapping.note, mapping.modifier);
        }
        Steinberg::int8 lfo1Unipolar = 0;
        if (streamer.readInt8(lfo1Unipolar))
            modulationEngine_.setLFO1Unipolar(lfo1Unipolar != 0);
        Steinberg::int8 lfo1Retrigger = 1;
        if (streamer.readInt8(lfo1Retrigger))
            modulationEngine_.setLFO1Retrigger(lfo1Retrigger != 0);

        // LFO 2 (7 values)
        float lfo2RateNorm = 0.5f;
        if (streamer.readFloat(lfo2RateNorm)) {
            constexpr float kMinLog = -4.6052f;
            constexpr float kMaxLog = 2.9957f;
            float rateHz = std::exp(kMinLog + lfo2RateNorm * (kMaxLog - kMinLog));
            modulationEngine_.setLFO2Rate(rateHz);
        }
        Steinberg::int8 lfo2Shape = 0;
        if (streamer.readInt8(lfo2Shape))
            modulationEngine_.setLFO2Waveform(static_cast<Krate::DSP::Waveform>(
                std::clamp(static_cast<int>(lfo2Shape), 0, 5)));
        float lfo2Phase = 0.0f;
        if (streamer.readFloat(lfo2Phase))
            modulationEngine_.setLFO2PhaseOffset(lfo2Phase * 360.0f);
        Steinberg::int8 lfo2Sync = 0;
        if (streamer.readInt8(lfo2Sync))
            modulationEngine_.setLFO2TempoSync(lfo2Sync != 0);
        Steinberg::int8 lfo2NoteIdx = 0;
        if (streamer.readInt8(lfo2NoteIdx)) {
            int idx;
            if (version <= 9) {
                idx = kOldNoteIdxToNewDropdown[std::clamp(static_cast<int>(static_cast<unsigned char>(lfo2NoteIdx)), 0, 14)];
            } else {
                idx = std::clamp(static_cast<int>(static_cast<unsigned char>(lfo2NoteIdx)), 0, kNoteValueCount - 1);
            }
            const auto mapping = Krate::DSP::getNoteValueFromDropdown(idx);
            modulationEngine_.setLFO2NoteValue(mapping.note, mapping.modifier);
        }
        Steinberg::int8 lfo2Unipolar = 0;
        if (streamer.readInt8(lfo2Unipolar))
            modulationEngine_.setLFO2Unipolar(lfo2Unipolar != 0);
        Steinberg::int8 lfo2Retrigger = 1;
        if (streamer.readInt8(lfo2Retrigger))
            modulationEngine_.setLFO2Retrigger(lfo2Retrigger != 0);

        // Envelope Follower (4 values)
        float envAttackNorm = 0.0f;
        if (streamer.readFloat(envAttackNorm))
            modulationEngine_.setEnvFollowerAttack(1.0f + envAttackNorm * 99.0f);
        float envReleaseNorm = 0.0f;
        if (streamer.readFloat(envReleaseNorm))
            modulationEngine_.setEnvFollowerRelease(10.0f + envReleaseNorm * 490.0f);
        float envSensitivity = 0.5f;
        if (streamer.readFloat(envSensitivity))
            modulationEngine_.setEnvFollowerSensitivity(envSensitivity);
        Steinberg::int8 envSource = 0;
        if (streamer.readInt8(envSource))
            modulationEngine_.setEnvFollowerSource(static_cast<Krate::DSP::EnvFollowerSourceType>(
                std::clamp(static_cast<int>(envSource), 0, 4)));

        // Random (3 values)
        float randomRateNorm = 0.0f;
        if (streamer.readFloat(randomRateNorm))
            modulationEngine_.setRandomRate(0.1f + randomRateNorm * 49.9f);
        float randomSmoothness = 0.0f;
        if (streamer.readFloat(randomSmoothness))
            modulationEngine_.setRandomSmoothness(randomSmoothness);
        Steinberg::int8 randomSync = 0;
        if (streamer.readInt8(randomSync))
            modulationEngine_.setRandomTempoSync(randomSync != 0);

        // Chaos (3 values)
        Steinberg::int8 chaosModel = 0;
        if (streamer.readInt8(chaosModel))
            modulationEngine_.setChaosModel(static_cast<Krate::DSP::ChaosModel>(
                std::clamp(static_cast<int>(chaosModel), 0, 3)));
        float chaosSpeedNorm = 0.0f;
        if (streamer.readFloat(chaosSpeedNorm))
            modulationEngine_.setChaosSpeed(0.05f + chaosSpeedNorm * 19.95f);
        float chaosCoupling = 0.0f;
        if (streamer.readFloat(chaosCoupling))
            modulationEngine_.setChaosCoupling(chaosCoupling);

        // Sample & Hold (3 values)
        Steinberg::int8 shSource = 0;
        if (streamer.readInt8(shSource))
            modulationEngine_.setSampleHoldSource(static_cast<Krate::DSP::SampleHoldInputType>(
                std::clamp(static_cast<int>(shSource), 0, 3)));
        float shRateNorm = 0.0f;
        if (streamer.readFloat(shRateNorm))
            modulationEngine_.setSampleHoldRate(0.1f + shRateNorm * 49.9f);
        float shSlewNorm = 0.0f;
        if (streamer.readFloat(shSlewNorm))
            modulationEngine_.setSampleHoldSlew(shSlewNorm * 500.0f);

        // Pitch Follower (4 values)
        float pitchMinNorm = 0.0f;
        if (streamer.readFloat(pitchMinNorm))
            modulationEngine_.setPitchFollowerMinHz(20.0f + pitchMinNorm * 480.0f);
        float pitchMaxNorm = 0.0f;
        if (streamer.readFloat(pitchMaxNorm))
            modulationEngine_.setPitchFollowerMaxHz(200.0f + pitchMaxNorm * 4800.0f);
        float pitchConfidence = 0.5f;
        if (streamer.readFloat(pitchConfidence))
            modulationEngine_.setPitchFollowerConfidence(pitchConfidence);
        float pitchTrackNorm = 0.0f;
        if (streamer.readFloat(pitchTrackNorm))
            modulationEngine_.setPitchFollowerTrackingSpeed(10.0f + pitchTrackNorm * 290.0f);

        // Transient (3 values)
        float transSensitivity = 0.5f;
        if (streamer.readFloat(transSensitivity))
            modulationEngine_.setTransientSensitivity(transSensitivity);
        float transAttackNorm = 0.0f;
        if (streamer.readFloat(transAttackNorm))
            modulationEngine_.setTransientAttack(0.5f + transAttackNorm * 9.5f);
        float transDecayNorm = 0.0f;
        if (streamer.readFloat(transDecayNorm))
            modulationEngine_.setTransientDecay(20.0f + transDecayNorm * 180.0f);

        // Rungler (v11+: 4 values: rate, depth, bits, loop)
        if (version >= 11) {
            float runglerRateNorm = 0.0f;
            if (streamer.readFloat(runglerRateNorm)) {
                float hz = 0.1f + runglerRateNorm * 49.9f;
                modulationEngine_.setRunglerOsc1Freq(hz);
                modulationEngine_.setRunglerOsc2Freq(hz * 1.5f);
            }
            float runglerDepth = 0.5f;
            if (streamer.readFloat(runglerDepth))
                modulationEngine_.setRunglerDepth(runglerDepth);
            Steinberg::int8 runglerBits = 8;
            if (streamer.readInt8(runglerBits))
                modulationEngine_.setRunglerBits(
                    static_cast<size_t>(std::clamp(static_cast<int>(runglerBits), 4, 16)));
            Steinberg::int8 runglerLoop = 0;
            if (streamer.readInt8(runglerLoop))
                modulationEngine_.setRunglerLoopMode(runglerLoop != 0);
        }

        // Macros (4 × 4 = 16 values)
        for (size_t m = 0; m < Krate::DSP::kMaxMacros; ++m) {
            float macroValue = 0.0f;
            if (streamer.readFloat(macroValue))
                modulationEngine_.setMacroValue(m, macroValue);
            float macroMin = 0.0f;
            if (streamer.readFloat(macroMin))
                modulationEngine_.setMacroMin(m, macroMin);
            float macroMax = 1.0f;
            if (streamer.readFloat(macroMax))
                modulationEngine_.setMacroMax(m, macroMax);
            Steinberg::int8 macroCurve = 0;
            if (streamer.readInt8(macroCurve))
                modulationEngine_.setMacroCurve(m, static_cast<Krate::DSP::ModCurve>(
                    std::clamp(static_cast<int>(macroCurve), 0, 3)));
        }

        // --- Routing Parameters (32 × 4 values) ---
        for (size_t r = 0; r < Krate::DSP::kMaxModRoutings; ++r) {
            Krate::DSP::ModRouting routing{};
            Steinberg::int8 source = 0;
            if (streamer.readInt8(source))
                routing.source = static_cast<Krate::DSP::ModSource>(
                    std::clamp(static_cast<int>(source), 0, static_cast<int>(Krate::DSP::kModSourceCount - 1)));
            int32_t dest = 0;
            if (streamer.readInt32(dest)) {
                // v12: kParamsPerBand changed from 6 to 8 (added Tone, Bias).
                // Migrate old dest indices: old band offsets were at 6 + band*6 + param,
                // new layout is 6 + band*8 + param.
                constexpr int32_t kBandBaseI = static_cast<int32_t>(ModDest::kBandBase);
                constexpr int32_t kParamsPerBandI = static_cast<int32_t>(ModDest::kParamsPerBand);
                constexpr int32_t kTotalDestsI = static_cast<int32_t>(ModDest::kTotalDestinations);
                if (version <= 11 && dest >= kBandBaseI) {
                    constexpr int32_t kOldParamsPerBand = 6;
                    const int32_t bandRelative = dest - kBandBaseI;
                    const int32_t oldBand = bandRelative / kOldParamsPerBand;
                    const int32_t oldOffset = bandRelative % kOldParamsPerBand;
                    dest = kBandBaseI
                         + oldBand * kParamsPerBandI
                         + oldOffset;
                }
                routing.destParamId = static_cast<uint32_t>(
                    std::clamp(dest, 0, kTotalDestsI - 1));
            }
            if (!streamer.readFloat(routing.amount))
                routing.amount = 0.0f;
            Steinberg::int8 curve = 0;
            if (streamer.readInt8(curve))
                routing.curve = static_cast<Krate::DSP::ModCurve>(
                    std::clamp(static_cast<int>(curve), 0, 3));
            routing.active = (routing.source != Krate::DSP::ModSource::None);
            modulationEngine_.setRouting(r, routing);
        }
    }

    // =========================================================================
    // Morph Node State (v6+)
    // =========================================================================
    if (version >= 6) {
        // v7 and earlier wrote 8 bands of morph state; v8+ writes 4
        constexpr int kV7MorphBands = 8;
        const int streamMorphBands = (version <= 7) ? kV7MorphBands : kMaxBands;
        for (int b = 0; b < streamMorphBands; ++b) {
            // Read band morph position & config (always read to advance stream)
            float morphX = 0.5f;
            float morphY = 0.5f;
            Steinberg::int8 morphMode = 0;
            auto activeNodes = static_cast<Steinberg::int8>(kDefaultActiveNodes);
            float morphSmoothing = 0.0f;

            streamer.readFloat(morphX);
            streamer.readFloat(morphY);
            streamer.readInt8(morphMode);
            streamer.readInt8(activeNodes);
            streamer.readFloat(morphSmoothing);

            if (b < kMaxBands) {
                auto& cache = bandMorphCache_[b];
                cache.morphX = morphX;
                cache.morphY = morphY;
                bandProcessors_[b].setMorphMode(
                    static_cast<MorphMode>(std::clamp(static_cast<int>(morphMode), 0, 2)));
                cache.activeNodeCount = std::clamp(
                    static_cast<int>(activeNodes), kMinActiveNodes, kMaxMorphNodes);
                bandProcessors_[b].setMorphSmoothingTime(morphSmoothing);
            }

            // Per-node state (always read to advance stream)
            for (int n = 0; n < kMaxMorphNodes; ++n) {
                Steinberg::int8 nodeType = 0;
                float drive = 1.0f;
                float mix = 1.0f;
                float toneHz = 4000.0f;
                float bias = 0.0f;
                float folds = 1.0f;
                float bitDepth = 16.0f;

                streamer.readInt8(nodeType);
                streamer.readFloat(drive);
                streamer.readFloat(mix);
                streamer.readFloat(toneHz);
                streamer.readFloat(bias);
                streamer.readFloat(folds);
                streamer.readFloat(bitDepth);

                if (b < kMaxBands) {
                    auto& mn = bandMorphCache_[b].nodes[static_cast<size_t>(n)];
                    mn.type = static_cast<DistortionType>(
                        std::clamp(static_cast<int>(nodeType), 0, 25));
                    mn.commonParams.drive = drive;
                    mn.commonParams.mix = mix;
                    mn.commonParams.toneHz = toneHz;
                    mn.params.bias = bias;
                    mn.params.folds = folds;
                    mn.params.bitDepth = bitDepth;
                }

                // v9: Shape parameter slots
                if (version >= 9) {
                    // NOLINTNEXTLINE(modernize-loop-convert) index needed: array access conditional on b < kMaxBands
                    for (int s = 0; s < MorphNode::kShapeSlotCount; ++s) {
                        float slotValue;
                        if (streamer.readFloat(slotValue)) {
                            if (b < kMaxBands) {
                                bandMorphCache_[b].nodes[static_cast<size_t>(n)].shapeSlots[s] = slotValue;
                            }
                        }
                    }

                    // v9: Per-type shadow storage (26 types × 10 slots)
                    // NOLINTNEXTLINE(modernize-loop-convert) index needed: array access conditional on b < kMaxBands
                    for (int t = 0; t < kDistortionTypeCount; ++t) {
                        for (int s = 0; s < MorphNode::kShapeSlotCount; ++s) {
                            float shadowValue;
                            if (streamer.readFloat(shadowValue)) {
                                if (b < kMaxBands) {
                                    bandMorphCache_[b].shapeShadow[static_cast<size_t>(n)]
                                        .typeSlots[t][s] = shadowValue;
                                }
                            }
                        }
                    }
                }
            }

            if (b < kMaxBands) {
                bandProcessors_[b].setMorphEnabled(true);
                bandProcessors_[b].setMorphNodes(
                    bandMorphCache_[b].nodes, bandMorphCache_[b].activeNodeCount);
                bandProcessors_[b].setMorphPosition(
                    bandMorphCache_[b].morphX, bandMorphCache_[b].morphY);
            }
            // else: discard morph data from bands 4-7 (v7 migration)
        }
    }

    // SharedDisplayBridge: try to read instance ID from state trailer
    {
        Steinberg::int32 marker = 0;
        Steinberg::int64 storedId = 0;
        if (streamer.readInt32(marker) && marker == kInstanceIdMarker
            && streamer.readInt64(storedId))
        {
            // Re-register with the stored ID (supports state restore)
            Krate::Plugins::SharedDisplayBridge::instance().unregisterInstance(instanceId_);
            instanceId_ = static_cast<uint64_t>(storedId);
            Krate::Plugins::SharedDisplayBridge::instance().registerInstance(
                instanceId_, &sharedDisplay_);
        }
        // If marker not found, keep the constructor-generated ID (old state format)
    }

    return Steinberg::kResultOk;
}
} // namespace Disrumpo
