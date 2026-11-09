// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"
#include "state_codec.h"

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
#include <memory>     // for std::make_unique
#include <random>     // for instance ID generation
#include <thread>     // for std::this_thread::yield

namespace Disrumpo {

// ==============================================================================
// Processor state persistence (getState / setState)
// ==============================================================================
//
// State hand-off (D-003). VST3 lets the host call setState on its UI thread
// while process() runs on the audio thread, so setState must not write the
// crossovers, band processors, morph engines, sweep or mod engine.
//
// - pendingState_ (PendingState, a fixed-size member, no heap) holds a full
//   snapshot of the saved state. Only the thread that moved pendingSlot_ to
//   kSlotWriting (setState / getState: the host's UI thread) writes or reads it;
//   only the thread that moved it to kSlotApplying (process() on the audio thread,
//   or setActive / setupProcessing while processing is stopped) reads it.
// - pendingSlot_ (std::atomic<int>) hands it over:
//     Idle/Ready -> Writing   host thread, CAS, acquire (sees the last apply done)
//     Writing    -> Ready     host thread, store, release (publishes the snapshot)
//     Ready      -> Applying  consumer, CAS, acquire (sees the snapshot)
//     Applying   -> Idle      consumer, store, release
//   The audio thread never waits: a slot held for Writing is applied a block
//   later. The host thread yields while a slot is Applying (one apply).
// - A second setState before a block applied the first overwrites it (last load
//   wins); the section flags of both stay set, because both are still owed.
// - While processing is not active (active_ false) setState applies on its own
//   thread at once, through the same applyPendingState().
// - getState saves a pending snapshot when there is one (so it returns a load the
//   audio thread has not applied yet), else the live state, read on the host
//   thread as getState always did. setState seeds the snapshot from the same live
//   reads, so fields an older or truncated stream lacks keep their values.
// ==============================================================================

// ==============================================================================
// IComponent - State Management
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
    // FR-018, FR-037: Serialize all parameters to IBStream
    // FR-020: Version field MUST be first for future migration

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // A load the audio thread has not applied yet is the newest state (D-003)
    for (;;) {
        int slot = pendingSlot_.load(std::memory_order_acquire);
        if (slot == kSlotReady) {
            if (!pendingSlot_.compare_exchange_weak(slot, kSlotWriting, std::memory_order_acquire,
                                                    std::memory_order_relaxed)) {
                continue;
            }
            const auto result = writeState(pendingState_, streamer);
            pendingSlot_.store(kSlotReady, std::memory_order_release);
            return result;
        }
        if (slot == kSlotIdle) {
            break;
        }
        std::this_thread::yield();  // being applied or written: wait for it
    }

    auto live = std::make_unique<PendingState>();
    captureState(*live);
    return writeState(*live, streamer);
}

void Processor::captureState(PendingState& s) const noexcept {
    s.inputGain = inputGain_.load(std::memory_order_relaxed);
    s.outputGain = outputGain_.load(std::memory_order_relaxed);
    s.globalMix = globalMix_.load(std::memory_order_relaxed);
    s.bandCount = bandCount_.load(std::memory_order_relaxed);
    s.bandStates = bandStates_;
    for (int c = 0; c < kMaxBands - 1; ++c) {
        s.crossoverHz[static_cast<size_t>(c)] = crossoverL_.getCrossoverFrequency(c);
    }

    // Sweep: frequency, width and intensity are the user's base values; the
    // sweepProcessor_ getters return the per-block modulated ones.
    s.sweepEnabled = sweepProcessor_.isEnabled();
    s.sweepFrequencyHz = baseSweepFrequency_.load(std::memory_order_relaxed);
    s.sweepWidthNorm = baseSweepWidthNorm_.load(std::memory_order_relaxed);
    s.sweepIntensityNorm = baseSweepIntensityNorm_.load(std::memory_order_relaxed);
    s.sweepFalloff = sweepProcessor_.getFalloffMode();
    s.sweepMorphLink = sweepProcessor_.getMorphLinkMode();
    s.sweepLfoEnabled = sweepLFO_.isEnabled();
    s.sweepLfoRateHz = sweepLFO_.getRate();
    s.sweepLfoWaveform = sweepLFO_.getWaveform();
    s.sweepLfoDepth = sweepLFO_.getDepth();
    s.sweepLfoSync = sweepLFO_.isTempoSynced();
    s.sweepLfoNoteValue = sweepLFO_.getNoteValue();
    s.sweepLfoNoteModifier = sweepLFO_.getNoteModifier();
    s.sweepEnvEnabled = sweepEnvelope_.isEnabled();
    s.sweepEnvAttackMs = sweepEnvelope_.getAttackTime();
    s.sweepEnvReleaseMs = sweepEnvelope_.getReleaseTime();
    s.sweepEnvSensitivity = sweepEnvelope_.getSensitivity();
    s.customCurve = customCurve_;

    const auto& me = modulationEngine_;
    s.lfo1 = ModLfoState{.rateHz = me.getLFO1Rate(), .waveform = me.getLFO1Waveform(),
                         .phaseDeg = me.getLFO1PhaseOffset(), .tempoSync = me.getLFO1TempoSync(),
                         .noteValue = me.getLFO1NoteValue(), .noteModifier = me.getLFO1NoteModifier(),
                         .unipolar = me.getLFO1Unipolar(), .retrigger = me.getLFO1Retrigger()};
    s.lfo2 = ModLfoState{.rateHz = me.getLFO2Rate(), .waveform = me.getLFO2Waveform(),
                         .phaseDeg = me.getLFO2PhaseOffset(), .tempoSync = me.getLFO2TempoSync(),
                         .noteValue = me.getLFO2NoteValue(), .noteModifier = me.getLFO2NoteModifier(),
                         .unipolar = me.getLFO2Unipolar(), .retrigger = me.getLFO2Retrigger()};
    s.envFollowerAttackMs = me.getEnvFollowerAttack();
    s.envFollowerReleaseMs = me.getEnvFollowerRelease();
    s.envFollowerSensitivity = me.getEnvFollowerSensitivity();
    s.envFollowerSource = me.getEnvFollowerSource();
    s.randomRateHz = me.getRandomRate();
    s.randomSmoothness = me.getRandomSmoothness();
    s.randomTempoSync = me.getRandomTempoSync();
    s.chaosModel = me.getChaosModel();
    s.chaosSpeed = me.getChaosSpeed();
    s.chaosCoupling = me.getChaosCoupling();
    s.sampleHoldSource = me.getSampleHoldSource();
    s.sampleHoldRateHz = me.getSampleHoldRate();
    s.sampleHoldSlewMs = me.getSampleHoldSlew();
    s.pitchFollowerMinHz = me.getPitchFollowerMinHz();
    s.pitchFollowerMaxHz = me.getPitchFollowerMaxHz();
    s.pitchFollowerConfidence = me.getPitchFollowerConfidence();
    s.pitchFollowerTrackingMs = me.getPitchFollowerTrackingSpeed();
    s.transientSensitivity = me.getTransientSensitivity();
    s.transientAttackMs = me.getTransientAttack();
    s.transientDecayMs = me.getTransientDecay();
    s.runglerRateHz = me.getRunglerOsc1Freq();
    s.runglerDepth = me.getRunglerDepth();
    s.runglerBits = me.getRunglerBits();
    s.runglerLoop = me.getRunglerLoopMode();
    for (size_t m = 0; m < Krate::DSP::kMaxMacros; ++m) {
        s.macros[m] = me.getMacro(m);
    }
    for (size_t r = 0; r < Krate::DSP::kMaxModRoutings; ++r) {
        s.routings[r] = me.getRouting(r);
    }

    s.morph = bandMorphCache_;
}

Steinberg::tresult Processor::writeState(const PendingState& s, Steinberg::IBStreamer& streamer) const {
    // Write version first (MUST be first per FR-020)
    if (!streamer.writeInt32(kPresetVersion)) {
        return Steinberg::kResultFalse;
    }

    // Write global parameters in order (per data-model.md Section 3)
    if (!streamer.writeFloat(s.inputGain)) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.writeFloat(s.outputGain)) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.writeFloat(s.globalMix)) {
        return Steinberg::kResultFalse;
    }

    // FR-037: Band management state (v2+)
    // Band count
    if (!streamer.writeInt32(s.bandCount)) {
        return Steinberg::kResultFalse;
    }

    // Per-band state for all 8 bands (fixed for format stability)
    for (int b = 0; b < kMaxBands; ++b) {
        const auto& bs = s.bandStates[static_cast<size_t>(b)];
        if (!streamer.writeFloat(bs.gainDb)) return Steinberg::kResultFalse;
        if (!streamer.writeFloat(bs.pan)) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(bs.solo ? 1 : 0))) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(bs.bypass ? 1 : 0))) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(bs.mute ? 1 : 0))) return Steinberg::kResultFalse;
    }

    // Crossover frequencies (7 floats)
    for (float freq : s.crossoverHz) {
        if (!streamer.writeFloat(freq)) return Steinberg::kResultFalse;
    }

    // =========================================================================
    // Sweep System State (v4+) — SC-012
    // =========================================================================

    // Sweep Core (6 values). Frequency, width and intensity are the user's base values.
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.sweepEnabled ? 1 : 0)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(normalizeSweepFrequency(s.sweepFrequencyHz)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(s.sweepWidthNorm))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(s.sweepIntensityNorm))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.sweepFalloff)))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.sweepMorphLink)))
        return Steinberg::kResultFalse;

    // LFO (6 values)
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.sweepLfoEnabled ? 1 : 0)))
        return Steinberg::kResultFalse;
    // LFO Rate: denormalized Hz → normalized using inverse log formula
    if (!streamer.writeFloat(lfoRateHzToNorm(s.sweepLfoRateHz))) return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.sweepLfoWaveform)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(s.sweepLfoDepth))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.sweepLfoSync ? 1 : 0)))
        return Steinberg::kResultFalse;
    {
        // Encode as standard dropdown index (0-20, matching kNoteValueDropdownMapping)
        const auto nv = s.sweepLfoNoteValue;
        const auto nm = s.sweepLfoNoteModifier;
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
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.sweepEnvEnabled ? 1 : 0)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((s.sweepEnvAttackMs - kMinSweepEnvAttackMs) /
                             (kMaxSweepEnvAttackMs - kMinSweepEnvAttackMs)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((s.sweepEnvReleaseMs - kMinSweepEnvReleaseMs) /
                             (kMaxSweepEnvReleaseMs - kMinSweepEnvReleaseMs)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(s.sweepEnvSensitivity))
        return Steinberg::kResultFalse;

    // Custom Curve breakpoints
    {
        int32_t pointCount = static_cast<int32_t>(s.customCurve.getBreakpointCount());
        if (!streamer.writeInt32(pointCount)) return Steinberg::kResultFalse;
        for (int32_t i = 0; i < pointCount; ++i) {
            auto bp = s.customCurve.getBreakpoint(i);
            if (!streamer.writeFloat(bp.x)) return Steinberg::kResultFalse;
            if (!streamer.writeFloat(bp.y)) return Steinberg::kResultFalse;
        }
    }

    // =========================================================================
    // Modulation System State (v5+) — SC-010
    // =========================================================================

    // --- Source Parameters ---

    // LFO 1 and LFO 2 (7 values each: rate[float], shape[int8], phase[float],
    //         sync[int8], noteValue[int8], unipolar[int8], retrigger[int8])
    for (const ModLfoState* lfo : {&s.lfo1, &s.lfo2}) {
        if (!streamer.writeFloat(lfoRateHzToNorm(lfo->rateHz))) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(lfo->waveform)))
            return Steinberg::kResultFalse;
        if (!streamer.writeFloat(kLfoPhaseDegRange.toNorm(lfo->phaseDeg)))
            return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(lfo->tempoSync ? 1 : 0)))
            return Steinberg::kResultFalse;
        {
            const auto nv = lfo->noteValue;
            const auto nm = lfo->noteModifier;
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
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(lfo->unipolar ? 1 : 0)))
            return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(lfo->retrigger ? 1 : 0)))
            return Steinberg::kResultFalse;
    }

    // Envelope Follower (4 values: attack[float], release[float], sensitivity[float], source[int8])
    if (!streamer.writeFloat(kEnvFollowerAttackMsRange.toNorm(s.envFollowerAttackMs)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(kEnvFollowerReleaseMsRange.toNorm(s.envFollowerReleaseMs)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(s.envFollowerSensitivity))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.envFollowerSource)))
        return Steinberg::kResultFalse;

    // Random (3 values: rate[float], smoothness[float], sync[int8])
    if (!streamer.writeFloat(kRandomRateHzRange.toNorm(s.randomRateHz)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(s.randomSmoothness))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.randomTempoSync ? 1 : 0)))
        return Steinberg::kResultFalse;

    // Chaos (3 values: model[int8], speed[float], coupling[float])
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.chaosModel)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(kChaosSpeedRange.toNorm(s.chaosSpeed)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(s.chaosCoupling))
        return Steinberg::kResultFalse;

    // Sample & Hold (3 values: source[int8], rate[float], slew[float])
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.sampleHoldSource)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(kSampleHoldRateHzRange.toNorm(s.sampleHoldRateHz)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(kSampleHoldSlewMsRange.toNorm(s.sampleHoldSlewMs)))
        return Steinberg::kResultFalse;

    // Pitch Follower (4 values: minHz[float], maxHz[float], confidence[float], trackingSpeed[float])
    if (!streamer.writeFloat(kPitchFollowerMinHzRange.toNorm(s.pitchFollowerMinHz)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(kPitchFollowerMaxHzRange.toNorm(s.pitchFollowerMaxHz)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(s.pitchFollowerConfidence))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(kPitchFollowerTrackingMsRange.toNorm(s.pitchFollowerTrackingMs)))
        return Steinberg::kResultFalse;

    // Transient (3 values: sensitivity[float], attack[float], decay[float])
    if (!streamer.writeFloat(s.transientSensitivity))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(kTransientAttackMsRange.toNorm(s.transientAttackMs)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(kTransientDecayMsRange.toNorm(s.transientDecayMs)))
        return Steinberg::kResultFalse;

    // Rungler (v11+: 4 values: rate[float], depth[float], bits[int8], loop[int8])
    if (!streamer.writeFloat(kRunglerRateHzRange.toNorm(s.runglerRateHz)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(s.runglerDepth))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.runglerBits)))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.runglerLoop ? 1 : 0)))
        return Steinberg::kResultFalse;

    // Macros (4 × 4 = 16 values: value[float], min[float], max[float], curve[int8])
    for (const auto& macro : s.macros) {
        if (!streamer.writeFloat(macro.value)) return Steinberg::kResultFalse;
        if (!streamer.writeFloat(macro.minOutput)) return Steinberg::kResultFalse;
        if (!streamer.writeFloat(macro.maxOutput)) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(macro.curve)))
            return Steinberg::kResultFalse;
    }

    // --- Routing Parameters (32 × 4 values: source[int8], dest[int32], amount[float], curve[int8]) ---
    for (const auto& routing : s.routings) {
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
        const auto& cache = s.morph[static_cast<size_t>(b)];

        // Band morph position & config (3 floats + 2 int8)
        if (!streamer.writeFloat(cache.morphX)) return Steinberg::kResultFalse;
        if (!streamer.writeFloat(cache.morphY)) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(cache.morphMode)))
            return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(cache.activeNodeCount)))
            return Steinberg::kResultFalse;
        if (!streamer.writeFloat(cache.morphSmoothingMs))
            return Steinberg::kResultFalse;

        // v13: sweep -> morph link modes (X, Y)
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.bandStates[static_cast<size_t>(b)].xLink)))
            return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(s.bandStates[static_cast<size_t>(b)].yLink)))
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
            const auto& shadow = cache.shapeShadow[static_cast<size_t>(n)];
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

    // Parse into pendingState_, seeded from the newest state: an unapplied load
    // (its section flags still owed) or the live one (D-003)
    const bool deferred = active_.load(std::memory_order_acquire);
    PendingState& s = pendingState_;
    if (acquireStateSlot() != kSlotReady) {
        captureState(s);
        s.hasBands = false;
        s.hasCrossover = {};
        s.hasSweep = false;
        s.hasModulation = false;
        s.hasRungler = false;
        s.hasMorph = false;
    }
    s.inputGain = inputGain;
    s.outputGain = outputGain;
    s.globalMix = globalMix;
    const bool complete = parseStateSections(streamer, version, s);

    if (deferred) {
        // process() applies it at the top of its next call
        pendingSlot_.store(kSlotReady, std::memory_order_release);
    } else {
        // Processing is stopped: nothing else reads the live objects
        applyPendingState();
        pendingSlot_.store(kSlotIdle, std::memory_order_release);
    }

    if (!complete) {
        // Stream ended before the band count: globals only, as always
        return Steinberg::kResultOk;
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

bool Processor::parseStateSections(Steinberg::IBStreamer& streamer, int32_t version,
                                   PendingState& s) noexcept {
    // FR-038: Band management state (v2+)
    if (version >= 2) {
        // Read band count
        int32_t bandCount = kDefaultBands;
        if (!streamer.readInt32(bandCount)) {
            // Use defaults if read fails
            return false;
        }
        s.hasBands = true;
        s.bandCount = std::clamp(bandCount, kMinBands, 4);

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
                auto& bs = s.bandStates[static_cast<size_t>(b)];
                bs.gainDb = std::clamp(gainDb, kMinBandGainDb, kMaxBandGainDb);
                bs.pan = std::clamp(pan, -1.0f, 1.0f);
                bs.solo = (soloVal != 0);
                bs.bypass = (bypassVal != 0);
                bs.mute = (muteVal != 0);
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
                s.crossoverHz[static_cast<size_t>(c)] = freq;
                s.hasCrossover[static_cast<size_t>(c)] = true;
            }
            // else: discard crossovers 3-6 (v7 migration)
        }
    }

    // =========================================================================
    // Sweep System State (v4+) — SC-012
    // =========================================================================
    if (version >= 4) {
        s.hasSweep = true;

        // Sweep Core
        Steinberg::int8 sweepEnable = 0;
        float sweepFreqNorm = 0.566f;
        float sweepWidthNorm = 0.286f;
        float sweepIntensityNorm = 0.25f;
        Steinberg::int8 sweepFalloff = 1;
        Steinberg::int8 sweepMorphLink = 0;

        if (streamer.readInt8(sweepEnable))
            s.sweepEnabled = (sweepEnable != 0);
        if (streamer.readFloat(sweepFreqNorm))
            s.sweepFrequencyHz = denormalizeSweepFrequency(sweepFreqNorm);
        if (streamer.readFloat(sweepWidthNorm))
            s.sweepWidthNorm = sweepWidthNorm;
        if (streamer.readFloat(sweepIntensityNorm))
            s.sweepIntensityNorm = sweepIntensityNorm;
        if (streamer.readInt8(sweepFalloff))
            s.sweepFalloff = static_cast<SweepFalloff>(sweepFalloff);
        if (streamer.readInt8(sweepMorphLink))
            s.sweepMorphLink = static_cast<MorphLinkMode>(
                std::clamp(static_cast<int>(sweepMorphLink), 0, kMorphLinkModeCount - 1));

        // LFO
        Steinberg::int8 lfoEnable = 0;
        float lfoRateNorm = 0.606f;
        Steinberg::int8 lfoWaveform = 0;
        float lfoDepth = 0.0f;
        Steinberg::int8 lfoSync = 0;
        Steinberg::int8 lfoNoteIndex = 0;

        if (streamer.readInt8(lfoEnable))
            s.sweepLfoEnabled = (lfoEnable != 0);
        if (streamer.readFloat(lfoRateNorm))
            s.sweepLfoRateHz = lfoRateNormToHz(lfoRateNorm);
        if (streamer.readInt8(lfoWaveform))
            s.sweepLfoWaveform = static_cast<Krate::DSP::Waveform>(
                std::clamp(static_cast<int>(lfoWaveform), 0, 5));
        if (streamer.readFloat(lfoDepth))
            s.sweepLfoDepth = lfoDepth;
        if (streamer.readInt8(lfoSync))
            s.sweepLfoSync = (lfoSync != 0);
        if (streamer.readInt8(lfoNoteIndex)) {
            int idx;
            if (version <= 9) {
                // v9: old 15-entry encoding (NoteValue*3+NoteModifier) → convert to 21-entry dropdown
                idx = kOldNoteIdxToNewDropdown[std::clamp(static_cast<int>(static_cast<unsigned char>(lfoNoteIndex)), 0, 14)];
            } else {
                idx = std::clamp(static_cast<int>(static_cast<unsigned char>(lfoNoteIndex)), 0, kNoteValueCount - 1);
            }
            const auto mapping = Krate::DSP::getNoteValueFromDropdown(idx);
            s.sweepLfoNoteValue = mapping.note;
            s.sweepLfoNoteModifier = mapping.modifier;
        }

        // Envelope
        Steinberg::int8 envEnable = 0;
        float envAttackNorm = 0.091f;
        float envReleaseNorm = 0.184f;
        float envSensitivity = 0.5f;

        if (streamer.readInt8(envEnable))
            s.sweepEnvEnabled = (envEnable != 0);
        if (streamer.readFloat(envAttackNorm))
            s.sweepEnvAttackMs = kMinSweepEnvAttackMs +
                envAttackNorm * (kMaxSweepEnvAttackMs - kMinSweepEnvAttackMs);
        if (streamer.readFloat(envReleaseNorm))
            s.sweepEnvReleaseMs = kMinSweepEnvReleaseMs +
                envReleaseNorm * (kMaxSweepEnvReleaseMs - kMinSweepEnvReleaseMs);
        if (streamer.readFloat(envSensitivity))
            s.sweepEnvSensitivity = envSensitivity;

        // Custom Curve
        int32_t pointCount = 2;
        if (streamer.readInt32(pointCount)) {
            auto& curve = s.customCurve;
            pointCount = std::clamp(pointCount, 2, 8);
            // Clear and rebuild custom curve
            while (curve.getBreakpointCount() > 2) {
                curve.removeBreakpoint(1);
            }
            // Read first point (endpoint x=0)
            float px = 0.0f;
            float py = 0.0f;
            if (pointCount >= 1 && streamer.readFloat(px) && streamer.readFloat(py)) {
                curve.setBreakpoint(0, 0.0f, py);
            }
            // Read intermediate points
            for (int32_t i = 1; i < pointCount - 1; ++i) {
                if (streamer.readFloat(px) && streamer.readFloat(py)) {
                    curve.addBreakpoint(px, py);
                }
            }
            // Read last point (endpoint x=1)
            if (pointCount >= 2 && streamer.readFloat(px) && streamer.readFloat(py)) {
                curve.setBreakpoint(curve.getBreakpointCount() - 1, 1.0f, py);
            }
        }
    }

    // =========================================================================
    // Modulation System State (v5+) — SC-010
    // =========================================================================
    if (version >= 5) {
        s.hasModulation = true;

        // --- Source Parameters ---

        // LFO 1 and LFO 2 (7 values each)
        for (ModLfoState* lfo : {&s.lfo1, &s.lfo2}) {
            float rateNorm = 0.5f;
            if (streamer.readFloat(rateNorm))
                lfo->rateHz = lfoRateNormToHz(rateNorm);
            Steinberg::int8 shape = 0;
            if (streamer.readInt8(shape))
                lfo->waveform = static_cast<Krate::DSP::Waveform>(std::clamp(static_cast<int>(shape), 0, 5));
            float phase = 0.0f;
            if (streamer.readFloat(phase))
                lfo->phaseDeg = kLfoPhaseDegRange.toPlain(phase);
            Steinberg::int8 sync = 0;
            if (streamer.readInt8(sync))
                lfo->tempoSync = (sync != 0);
            Steinberg::int8 noteIdx = 0;
            if (streamer.readInt8(noteIdx)) {
                int idx;
                if (version <= 9) {
                    idx = kOldNoteIdxToNewDropdown[std::clamp(static_cast<int>(static_cast<unsigned char>(noteIdx)), 0, 14)];
                } else {
                    idx = std::clamp(static_cast<int>(static_cast<unsigned char>(noteIdx)), 0, kNoteValueCount - 1);
                }
                const auto mapping = Krate::DSP::getNoteValueFromDropdown(idx);
                lfo->noteValue = mapping.note;
                lfo->noteModifier = mapping.modifier;
            }
            Steinberg::int8 unipolar = 0;
            if (streamer.readInt8(unipolar))
                lfo->unipolar = (unipolar != 0);
            Steinberg::int8 retrigger = 1;
            if (streamer.readInt8(retrigger))
                lfo->retrigger = (retrigger != 0);
        }

        // Envelope Follower (4 values)
        float envAttackNorm = 0.0f;
        if (streamer.readFloat(envAttackNorm))
            s.envFollowerAttackMs = kEnvFollowerAttackMsRange.toPlain(envAttackNorm);
        float envReleaseNorm = 0.0f;
        if (streamer.readFloat(envReleaseNorm))
            s.envFollowerReleaseMs = kEnvFollowerReleaseMsRange.toPlain(envReleaseNorm);
        float envSensitivity = 0.5f;
        if (streamer.readFloat(envSensitivity))
            s.envFollowerSensitivity = envSensitivity;
        Steinberg::int8 envSource = 0;
        if (streamer.readInt8(envSource))
            s.envFollowerSource = static_cast<Krate::DSP::EnvFollowerSourceType>(
                std::clamp(static_cast<int>(envSource), 0, 4));

        // Random (3 values)
        float randomRateNorm = 0.0f;
        if (streamer.readFloat(randomRateNorm))
            s.randomRateHz = kRandomRateHzRange.toPlain(randomRateNorm);
        float randomSmoothness = 0.0f;
        if (streamer.readFloat(randomSmoothness))
            s.randomSmoothness = randomSmoothness;
        Steinberg::int8 randomSync = 0;
        if (streamer.readInt8(randomSync))
            s.randomTempoSync = (randomSync != 0);

        // Chaos (3 values)
        Steinberg::int8 chaosModel = 0;
        if (streamer.readInt8(chaosModel))
            s.chaosModel = static_cast<Krate::DSP::ChaosModel>(std::clamp(static_cast<int>(chaosModel), 0, 3));
        float chaosSpeedNorm = 0.0f;
        if (streamer.readFloat(chaosSpeedNorm))
            s.chaosSpeed = kChaosSpeedRange.toPlain(chaosSpeedNorm);
        float chaosCoupling = 0.0f;
        if (streamer.readFloat(chaosCoupling))
            s.chaosCoupling = chaosCoupling;

        // Sample & Hold (3 values)
        Steinberg::int8 shSource = 0;
        if (streamer.readInt8(shSource))
            s.sampleHoldSource = static_cast<Krate::DSP::SampleHoldInputType>(
                std::clamp(static_cast<int>(shSource), 0, 3));
        float shRateNorm = 0.0f;
        if (streamer.readFloat(shRateNorm))
            s.sampleHoldRateHz = kSampleHoldRateHzRange.toPlain(shRateNorm);
        float shSlewNorm = 0.0f;
        if (streamer.readFloat(shSlewNorm))
            s.sampleHoldSlewMs = kSampleHoldSlewMsRange.toPlain(shSlewNorm);

        // Pitch Follower (4 values)
        float pitchMinNorm = 0.0f;
        if (streamer.readFloat(pitchMinNorm))
            s.pitchFollowerMinHz = kPitchFollowerMinHzRange.toPlain(pitchMinNorm);
        float pitchMaxNorm = 0.0f;
        if (streamer.readFloat(pitchMaxNorm))
            s.pitchFollowerMaxHz = kPitchFollowerMaxHzRange.toPlain(pitchMaxNorm);
        float pitchConfidence = 0.5f;
        if (streamer.readFloat(pitchConfidence))
            s.pitchFollowerConfidence = pitchConfidence;
        float pitchTrackNorm = 0.0f;
        if (streamer.readFloat(pitchTrackNorm))
            s.pitchFollowerTrackingMs = kPitchFollowerTrackingMsRange.toPlain(pitchTrackNorm);

        // Transient (3 values)
        float transSensitivity = 0.5f;
        if (streamer.readFloat(transSensitivity))
            s.transientSensitivity = transSensitivity;
        float transAttackNorm = 0.0f;
        if (streamer.readFloat(transAttackNorm))
            s.transientAttackMs = kTransientAttackMsRange.toPlain(transAttackNorm);
        float transDecayNorm = 0.0f;
        if (streamer.readFloat(transDecayNorm))
            s.transientDecayMs = kTransientDecayMsRange.toPlain(transDecayNorm);

        // Rungler (v11+: 4 values: rate, depth, bits, loop)
        if (version >= 11) {
            s.hasRungler = true;
            float runglerRateNorm = 0.0f;
            if (streamer.readFloat(runglerRateNorm))
                s.runglerRateHz = kRunglerRateHzRange.toPlain(runglerRateNorm);
            float runglerDepth = 0.5f;
            if (streamer.readFloat(runglerDepth))
                s.runglerDepth = runglerDepth;
            Steinberg::int8 runglerBits = 8;
            if (streamer.readInt8(runglerBits))
                s.runglerBits = static_cast<size_t>(std::clamp(static_cast<int>(runglerBits), 4, 16));
            Steinberg::int8 runglerLoop = 0;
            if (streamer.readInt8(runglerLoop))
                s.runglerLoop = (runglerLoop != 0);
        }

        // Macros (4 × 4 = 16 values)
        for (auto& macro : s.macros) {
            float macroValue = 0.0f;
            if (streamer.readFloat(macroValue))
                macro.value = macroValue;
            float macroMin = 0.0f;
            if (streamer.readFloat(macroMin))
                macro.minOutput = macroMin;
            float macroMax = 1.0f;
            if (streamer.readFloat(macroMax))
                macro.maxOutput = macroMax;
            Steinberg::int8 macroCurve = 0;
            if (streamer.readInt8(macroCurve))
                macro.curve = static_cast<Krate::DSP::ModCurve>(std::clamp(static_cast<int>(macroCurve), 0, 3));
        }

        // --- Routing Parameters (32 × 4 values) ---
        for (auto& slot : s.routings) {
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
            slot = routing;
        }
    }

    // =========================================================================
    // Morph Node State (v6+)
    // =========================================================================
    if (version >= 6) {
        s.hasMorph = true;

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

            // v13: sweep -> morph link modes; older streams have none (None)
            Steinberg::int8 xLink = 0;
            Steinberg::int8 yLink = 0;
            if (version >= 13) {
                streamer.readInt8(xLink);
                streamer.readInt8(yLink);
            }

            const auto band = static_cast<size_t>(b);
            if (b < kMaxBands) {
                const auto toLinkMode = [](Steinberg::int8 v) {
                    return static_cast<MorphLinkMode>(
                        std::clamp(static_cast<int>(v), 0, kMorphLinkModeCount - 1));
                };
                s.bandStates[band].xLink = toLinkMode(xLink);
                s.bandStates[band].yLink = toLinkMode(yLink);
                auto& cache = s.morph[band];
                cache.morphX = morphX;
                cache.morphY = morphY;
                cache.morphMode =
                    static_cast<MorphMode>(std::clamp(static_cast<int>(morphMode), 0, 2));
                cache.activeNodeCount = std::clamp(
                    static_cast<int>(activeNodes), kMinActiveNodes, kMaxMorphNodes);
                cache.morphSmoothingMs = morphSmoothing;
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
                    auto& mn = s.morph[band].nodes[static_cast<size_t>(n)];
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
                    for (int sl = 0; sl < MorphNode::kShapeSlotCount; ++sl) {
                        float slotValue;
                        if (streamer.readFloat(slotValue)) {
                            if (b < kMaxBands) {
                                s.morph[band].nodes[static_cast<size_t>(n)].shapeSlots[sl] = slotValue;
                            }
                        }
                    }

                    // v9: Per-type shadow storage (26 types × 10 slots)
                    // NOLINTNEXTLINE(modernize-loop-convert) index needed: array access conditional on b < kMaxBands
                    for (int t = 0; t < kDistortionTypeCount; ++t) {
                        for (int sl = 0; sl < MorphNode::kShapeSlotCount; ++sl) {
                            float shadowValue;
                            if (streamer.readFloat(shadowValue)) {
                                if (b < kMaxBands) {
                                    s.morph[band].shapeShadow[static_cast<size_t>(n)]
                                        .typeSlots[t][sl] = shadowValue;
                                }
                            }
                        }
                    }

                    // Re-derive the type-specific params from the restored slots, as
                    // replaying the parameters does, so nothing of the previous patch
                    // (or of the defaults) survives the load (D-050, D-156).
                    if (b < kMaxBands) {
                        auto& mn = s.morph[band].nodes[static_cast<size_t>(n)];
                        mn.params = DistortionParams{};
                        mn.params.bias = bias;
                        mn.params.folds = folds;
                        mn.params.bitDepth = bitDepth;
                        mapShapeSlotsToParams(mn.type, mn.shapeSlots, mn.params);
                    }
                }
            }
            // else (b >= kMaxBands): discard morph data from bands 4-7 (v7 migration)
        }
    }

    return true;
}

// ==============================================================================
// State hand-off (D-003): see the contract at the top of this file
// ==============================================================================

int Processor::acquireStateSlot() noexcept {
    for (;;) {
        int slot = pendingSlot_.load(std::memory_order_acquire);
        if ((slot == kSlotIdle || slot == kSlotReady) &&
            pendingSlot_.compare_exchange_weak(slot, kSlotWriting, std::memory_order_acquire,
                                               std::memory_order_relaxed)) {
            return slot;
        }
        std::this_thread::yield();  // the slot is being applied (or written): wait for it
    }
}

void Processor::consumePendingState() noexcept {
    int expected = kSlotReady;
    if (!pendingSlot_.compare_exchange_strong(expected, kSlotApplying, std::memory_order_acquire,
                                              std::memory_order_relaxed)) {
        return;  // nothing published, or a host-thread call holds the slot: next block
    }
    applyPendingState();
    pendingSlot_.store(kSlotIdle, std::memory_order_release);
}

void Processor::applyPendingState() noexcept {
    const PendingState& s = pendingState_;

    inputGain_.store(s.inputGain, std::memory_order_relaxed);
    outputGain_.store(s.outputGain, std::memory_order_relaxed);
    globalMix_.store(s.globalMix, std::memory_order_relaxed);

    if (s.hasBands) {
        bandCount_.store(s.bandCount, std::memory_order_relaxed);
        for (size_t b = 0; b < static_cast<size_t>(kMaxBands); ++b) {
            auto& bs = bandStates_[b];
            const auto& from = s.bandStates[b];
            bs.gainDb = from.gainDb;
            bs.pan = from.pan;
            bs.solo = from.solo;
            bs.bypass = from.bypass;
            bs.mute = from.mute;
            bandProcessors_[b].setGainDb(bs.gainDb);
            bandProcessors_[b].setPan(bs.pan);
            bandProcessors_[b].setMute(bs.mute);
            bandProcessors_[b].setBypassed(bs.bypass);
        }

        // The crossovers take the band count in process() (through the output dip
        // when audio is running, D-023), setActive or setupProcessing. The
        // frequencies restored here are marked set, so that switch keeps them (D-048)
        for (int c = 0; c < kMaxBands - 1; ++c) {
            if (s.hasCrossover[static_cast<size_t>(c)]) {
                crossoverL_.setCrossoverFrequency(c, s.crossoverHz[static_cast<size_t>(c)]);
                crossoverR_.setCrossoverFrequency(c, s.crossoverHz[static_cast<size_t>(c)]);
            }
        }
    }

    if (s.hasSweep) {
        sweepProcessor_.setEnabled(s.sweepEnabled);
        baseSweepFrequency_.store(s.sweepFrequencyHz, std::memory_order_relaxed);
        sweepProcessor_.setCenterFrequency(s.sweepFrequencyHz);
        // process() re-applies width/intensity from the base atomics every block.
        baseSweepWidthNorm_.store(s.sweepWidthNorm, std::memory_order_relaxed);
        sweepProcessor_.setWidth(0.5f + s.sweepWidthNorm * 3.5f);
        baseSweepIntensityNorm_.store(s.sweepIntensityNorm, std::memory_order_relaxed);
        sweepProcessor_.setIntensity(s.sweepIntensityNorm * 2.0f);
        sweepProcessor_.setFalloffMode(s.sweepFalloff);
        sweepProcessor_.setMorphLinkMode(s.sweepMorphLink);

        sweepLFO_.setEnabled(s.sweepLfoEnabled);
        sweepLFO_.setRate(s.sweepLfoRateHz);
        sweepLFO_.setWaveform(s.sweepLfoWaveform);
        sweepLFO_.setDepth(s.sweepLfoDepth);
        sweepLFO_.setTempoSync(s.sweepLfoSync);
        sweepLFO_.setNoteValue(s.sweepLfoNoteValue, s.sweepLfoNoteModifier);

        sweepEnvelope_.setEnabled(s.sweepEnvEnabled);
        sweepEnvelope_.setAttackTime(s.sweepEnvAttackMs);
        sweepEnvelope_.setReleaseTime(s.sweepEnvReleaseMs);
        sweepEnvelope_.setSensitivity(s.sweepEnvSensitivity);

        customCurve_ = s.customCurve;  // fixed-size value type; sweepProcessor_ keeps its pointer
    }

    if (s.hasModulation) {
        auto& me = modulationEngine_;
        me.setLFO1Rate(s.lfo1.rateHz);
        me.setLFO1Waveform(s.lfo1.waveform);
        me.setLFO1PhaseOffset(s.lfo1.phaseDeg);
        me.setLFO1TempoSync(s.lfo1.tempoSync);
        me.setLFO1NoteValue(s.lfo1.noteValue, s.lfo1.noteModifier);
        me.setLFO1Unipolar(s.lfo1.unipolar);
        me.setLFO1Retrigger(s.lfo1.retrigger);
        me.setLFO2Rate(s.lfo2.rateHz);
        me.setLFO2Waveform(s.lfo2.waveform);
        me.setLFO2PhaseOffset(s.lfo2.phaseDeg);
        me.setLFO2TempoSync(s.lfo2.tempoSync);
        me.setLFO2NoteValue(s.lfo2.noteValue, s.lfo2.noteModifier);
        me.setLFO2Unipolar(s.lfo2.unipolar);
        me.setLFO2Retrigger(s.lfo2.retrigger);
        me.setEnvFollowerAttack(s.envFollowerAttackMs);
        me.setEnvFollowerRelease(s.envFollowerReleaseMs);
        me.setEnvFollowerSensitivity(s.envFollowerSensitivity);
        me.setEnvFollowerSource(s.envFollowerSource);
        me.setRandomRate(s.randomRateHz);
        me.setRandomSmoothness(s.randomSmoothness);
        me.setRandomTempoSync(s.randomTempoSync);
        me.setChaosModel(s.chaosModel);
        me.setChaosSpeed(s.chaosSpeed);
        me.setChaosCoupling(s.chaosCoupling);
        me.setSampleHoldSource(s.sampleHoldSource);
        me.setSampleHoldRate(s.sampleHoldRateHz);
        me.setSampleHoldSlew(s.sampleHoldSlewMs);
        me.setPitchFollowerMinHz(s.pitchFollowerMinHz);
        me.setPitchFollowerMaxHz(s.pitchFollowerMaxHz);
        me.setPitchFollowerConfidence(s.pitchFollowerConfidence);
        me.setPitchFollowerTrackingSpeed(s.pitchFollowerTrackingMs);
        me.setTransientSensitivity(s.transientSensitivity);
        me.setTransientAttack(s.transientAttackMs);
        me.setTransientDecay(s.transientDecayMs);
        if (s.hasRungler) {
            me.setRunglerOsc1Freq(s.runglerRateHz);
            me.setRunglerOsc2Freq(s.runglerRateHz * 1.5f);
            me.setRunglerDepth(s.runglerDepth);
            me.setRunglerBits(s.runglerBits);
            me.setRunglerLoopMode(s.runglerLoop);
        }
        for (size_t m = 0; m < Krate::DSP::kMaxMacros; ++m) {
            me.setMacroValue(m, s.macros[m].value);
            me.setMacroMin(m, s.macros[m].minOutput);
            me.setMacroMax(m, s.macros[m].maxOutput);
            me.setMacroCurve(m, s.macros[m].curve);
        }
        for (size_t r = 0; r < Krate::DSP::kMaxModRoutings; ++r) {
            me.setRouting(r, s.routings[r]);
        }
    }

    if (s.hasMorph) {
        for (size_t b = 0; b < static_cast<size_t>(kMaxBands); ++b) {
            bandStates_[b].xLink = s.bandStates[b].xLink;
            bandStates_[b].yLink = s.bandStates[b].yLink;
            auto& cache = bandMorphCache_[b];
            cache = s.morph[b];
            bandProcessors_[b].setMorphMode(cache.morphMode);
            bandProcessors_[b].setMorphSmoothingTime(cache.morphSmoothingMs);
            bandProcessors_[b].setMorphEnabled(true);
            bandProcessors_[b].setMorphNodes(cache.nodes, cache.activeNodeCount);
            bandProcessors_[b].setMorphPosition(cache.morphX, cache.morphY);
        }
    }
}

} // namespace Disrumpo
