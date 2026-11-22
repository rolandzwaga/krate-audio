// ==============================================================================
// Processor State Management (getState, setState, applyPresetSnapshot)
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <krate/dsp/systems/voice_mod_types.h>

#include "parameters/dropdown_mappings.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace Ruinae {

// ==============================================================================
// IComponent - State Management
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Write state version first
    streamer.writeInt32(kCurrentStateVersion);

    // Save all 19 parameter packs in deterministic order
    saveGlobalParams(globalParams_, streamer);
    saveOscAParams(oscAParams_, streamer);
    saveOscBParams(oscBParams_, streamer);
    saveMixerParams(mixerParams_, streamer);
    saveFilterParams(filterParams_, streamer);
    saveDistortionParams(distortionParams_, streamer);
    saveTranceGateParams(tranceGateParams_, streamer);
    saveAmpEnvParams(ampEnvParams_, streamer);
    saveFilterEnvParams(filterEnvParams_, streamer);
    saveModEnvParams(modEnvParams_, streamer);
    saveLFO1Params(lfo1Params_, streamer);
    saveLFO2Params(lfo2Params_, streamer);
    saveChaosModParams(chaosModParams_, streamer);
    saveModMatrixParams(modMatrixParams_, streamer);
    saveGlobalFilterParams(globalFilterParams_, streamer);
    saveDelayParams(delayParams_, streamer);
    saveReverbParams(reverbParams_, streamer);
    // Reverb type (125-dual-reverb, state version 5)
    streamer.writeInt32(reverbParams_.reverbType.load(std::memory_order_relaxed));

    saveMonoModeParams(monoModeParams_, streamer);

    // Voice routes (16 slots) — atomic load per field
    for (const auto& ar : voiceRoutes_) {
        auto r = ar.load();
        streamer.writeInt8(static_cast<Steinberg::int8>(r.source));
        streamer.writeInt8(static_cast<Steinberg::int8>(r.destination));
        streamer.writeFloat(r.amount);
        streamer.writeInt8(static_cast<Steinberg::int8>(r.curve));
        streamer.writeFloat(r.smoothMs);
        streamer.writeInt8(static_cast<Steinberg::int8>(r.scale));
        streamer.writeInt8(static_cast<Steinberg::int8>(r.bypass));
        streamer.writeInt8(static_cast<Steinberg::int8>(r.active));
    }

    // FX enable flags
    streamer.writeInt8(delayEnabled_.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt8(reverbEnabled_.load(std::memory_order_relaxed) ? 1 : 0);

    // Phaser params + modulation type (replaces legacy phaserEnabled_ int8)
    savePhaserParams(phaserParams_, streamer);
    // Write modulationType as int8 for backward compatibility with state format:
    // 0 = None (legacy: phaser disabled), 1 = Phaser (legacy: phaser enabled), 2 = Flanger, 3 = Chorus
    streamer.writeInt8(static_cast<Steinberg::int8>(modulationType_.load(std::memory_order_relaxed)));

    // Flanger params (version 6+)
    saveFlangerParams(flangerParams_, streamer);

    // Chorus params (version 7+)
    saveChorusParams(chorusParams_, streamer);

    // Extended LFO params
    saveLFO1ExtendedParams(lfo1Params_, streamer);
    saveLFO2ExtendedParams(lfo2Params_, streamer);

    // Macro and Rungler params
    saveMacroParams(macroParams_, streamer);
    saveRunglerParams(runglerParams_, streamer);

    // Settings params
    saveSettingsParams(settingsParams_, streamer);

    // Mod source params
    saveEnvFollowerParams(envFollowerParams_, streamer);
    saveSampleHoldParams(sampleHoldParams_, streamer);
    saveRandomParams(randomParams_, streamer);
    savePitchFollowerParams(pitchFollowerParams_, streamer);
    saveTransientParams(transientParams_, streamer);

    // Harmonizer params + enable flag
    saveHarmonizerParams(harmonizerParams_, streamer);
    streamer.writeInt8(harmonizerEnabled_.load(std::memory_order_relaxed) ? 1 : 0);

    // Arpeggiator params (FR-011)
    saveArpParams(arpParams_, streamer);

    // Voice LFO rate + shape (version 9+), appended after the arp pack
    saveVoiceLfoParams(voiceLfoParams_, streamer);

    // Velocity > Amp (version 9+), after the voice LFO
    saveAmpVelocityAmount(ampEnvParams_, streamer);

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
    if (!state) return Steinberg::kResultTrue;

    // Read all bytes from the IBStream into a contiguous buffer.
    std::vector<char> bytes;
    constexpr Steinberg::int32 kChunkSize = 4096;
    char chunk[kChunkSize];
    Steinberg::int32 bytesRead = 0;

    while (true) {
        auto result = state->read(chunk, kChunkSize, &bytesRead);
        if (result != Steinberg::kResultTrue || bytesRead <= 0) break;
        bytes.insert(bytes.end(), chunk, chunk + bytesRead);
    }

    return loadStateBytes(std::move(bytes), false);
}

namespace {

/// Scratch target of an arp-only load: the stream's other packs are parsed into
/// it, only to reach the arp pack, and dropped. The member names are the
/// Processor's, so Processor::readStatePacks serves both.
struct DiscardedStatePacks {
    GlobalParams globalParams_;
    OscAParams oscAParams_;
    OscBParams oscBParams_;
    MixerParams mixerParams_;
    RuinaeFilterParams filterParams_;
    RuinaeDistortionParams distortionParams_;
    RuinaeTranceGateParams tranceGateParams_;
    AmpEnvParams ampEnvParams_;
    FilterEnvParams filterEnvParams_;
    ModEnvParams modEnvParams_;
    LFO1Params lfo1Params_;
    LFO2Params lfo2Params_;
    ChaosModParams chaosModParams_;
    ModMatrixParams modMatrixParams_;
    GlobalFilterParams globalFilterParams_;
    std::atomic<bool> delayEnabled_{false};
    std::atomic<bool> reverbEnabled_{false};
    std::atomic<int> modulationType_{0};
    std::atomic<bool> harmonizerEnabled_{false};
    RuinaeDelayParams delayParams_;
    RuinaeReverbParams reverbParams_;
    RuinaePhaserParams phaserParams_;
    RuinaeFlangerParams flangerParams_;
    RuinaeChorusParams chorusParams_;
    RuinaeHarmonizerParams harmonizerParams_;
    MonoModeParams monoModeParams_;
    MacroParams macroParams_;
    RunglerParams runglerParams_;
    SettingsParams settingsParams_;
    EnvFollowerParams envFollowerParams_;
    SampleHoldParams sampleHoldParams_;
    RandomParams randomParams_;
    PitchFollowerParams pitchFollowerParams_;
    TransientParams transientParams_;
    VoiceLfoParams voiceLfoParams_;
    std::array<AtomicVoiceModRoute, Krate::Plugins::kMaxVoiceRoutes> voiceRoutes_{};
};

} // namespace

Steinberg::tresult Processor::loadStateBytes(std::vector<char> bytes, bool arpOnly) {
    // =========================================================================
    // Hybrid crash-proof preset loading, one path for host and browser loads:
    //
    // 1. Parse into the ATOMIC parameters right here on the message thread, so
    //    that getState() returns the new values at once (host contract).
    //    Individual atomic writes are safe: worst case is one process() block
    //    with mixed old/new params (brief audio glitch, not a crash). Voice
    //    routes use per-field atomics (AtomicVoiceModRoute), safe from here,
    //    and their controller notification (setBinary allocates) stays off the
    //    audio thread.
    //
    // 2. Hand the engine/arp reset to the audio thread via RTTransferT.
    // =========================================================================

    if (bytes.empty()) return Steinberg::kResultTrue;

    {
        Steinberg::MemoryStream memStream(bytes.data(),
                                          static_cast<Steinberg::TSize>(bytes.size()));
        Steinberg::IBStreamer streamer(&memStream, kLittleEndian);

        if (arpOnly) {
            // The arp pack comes last but one: the packs before it are read
            // into scratch, so the synth, the voice routes and the voice LFO
            // keep the loaded synth preset's values.
            const auto scratch = std::make_unique<DiscardedStatePacks>();
            if (!readStatePacks(*scratch, arpParams_, streamer)) return Steinberg::kResultTrue;
        } else {
            if (!readStatePacks(*this, arpParams_, streamer)) return Steinberg::kResultTrue;
            sendVoiceModRouteState();
        }
    }

    auto snapshot = std::make_unique<PresetSnapshot>();
    snapshot->arpOnly = arpOnly;
    stateTransfer_.transferObject_ui(std::move(snapshot));

    return Steinberg::kResultTrue;
}

template <typename Packs>
bool Processor::readStatePacks(Packs& packs, ArpeggiatorParams& arp,
                               Steinberg::IBStreamer& streamer) {
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version))
        return false;
    if (version < 1 || version > kCurrentStateVersion)
        return false;

    // Load all parameter packs into atomics (safe from any thread)
    if (!loadGlobalParams(packs.globalParams_, streamer)) return false;
    if (!loadOscAParams(packs.oscAParams_, streamer, version)) return false;
    if (!loadOscBParams(packs.oscBParams_, streamer, version)) return false;
    if (!loadMixerParams(packs.mixerParams_, streamer, version)) return false;
    if (!loadFilterParams(packs.filterParams_, streamer)) return false;
    if (!loadDistortionParams(packs.distortionParams_, streamer)) return false;
    if (!loadTranceGateParams(packs.tranceGateParams_, streamer)) return false;
    if (!loadAmpEnvParams(packs.ampEnvParams_, streamer)) return false;
    if (!loadFilterEnvParams(packs.filterEnvParams_, streamer)) return false;
    if (!loadModEnvParams(packs.modEnvParams_, streamer)) return false;
    if (!loadLFO1Params(packs.lfo1Params_, streamer)) return false;
    if (!loadLFO2Params(packs.lfo2Params_, streamer)) return false;
    if (!loadChaosModParams(packs.chaosModParams_, streamer)) return false;
    // Before v9 the LFO and Chaos depths never reached the audio (R-041, R-183):
    // every source modulated at full depth, which these states keep.
    if (version < 9) {
        packs.lfo1Params_.depth.store(1.0f, std::memory_order_relaxed);
        packs.lfo2Params_.depth.store(1.0f, std::memory_order_relaxed);
        packs.chaosModParams_.depth.store(1.0f, std::memory_order_relaxed);
    }
    if (!loadModMatrixParams(packs.modMatrixParams_, streamer)) return false;
    // Before v9 the Scale saturated the amount (R-053): keep the saved depth
    if (version < 9) migratePreV9ModMatrixAmounts(packs.modMatrixParams_);
    if (!loadGlobalFilterParams(packs.globalFilterParams_, streamer)) return false;
    if (!loadDelayParams(packs.delayParams_, streamer)) return false;
    if (!loadReverbParams(packs.reverbParams_, streamer)) return false;
    // Reverb type (125-dual-reverb, state version 5)
    if (version >= 5) {
        Steinberg::int32 reverbType = 0;
        if (streamer.readInt32(reverbType)) {
            packs.reverbParams_.reverbType.store(
                std::clamp(static_cast<int32_t>(reverbType), 0, 1), std::memory_order_relaxed);
        }
    } else {
        // Backward compat: version < 5 defaults to Plate (FR-028)
        packs.reverbParams_.reverbType.store(0, std::memory_order_relaxed);
    }
    if (!loadMonoModeParams(packs.monoModeParams_, streamer)) return false;

    // Voice routes. Indices are clamped to the voice tab's ranges (R-003): a
    // damaged state must not hand the voice router an index past its tables.
    const auto clampedIndex = [](Steinberg::int8 v, int hi) {
        return static_cast<uint8_t>(std::clamp(static_cast<int>(static_cast<uint8_t>(v)), 0, hi));
    };
    for (auto& ar : packs.voiceRoutes_) {
        Krate::Plugins::VoiceModRoute r{};
        Steinberg::int8 i8 = 0;
        float f = 0.0f;
        if (!streamer.readInt8(i8)) break;
        r.source = clampedIndex(
            i8, static_cast<int>(Krate::DSP::VoiceModSource::NumSources) - 1);
        if (!streamer.readInt8(i8)) break;
        r.destination = clampedIndex(i8, Krate::Plugins::kNumVoiceDestinations - 1);
        if (!streamer.readFloat(f)) break;
        r.amount = f;
        if (!streamer.readInt8(i8)) break;
        r.curve = clampedIndex(i8, 3);
        if (!streamer.readFloat(f)) break;
        r.smoothMs = f;
        if (!streamer.readInt8(i8)) break;
        r.scale = clampedIndex(i8, 4);
        if (!streamer.readInt8(i8)) break;
        r.bypass = static_cast<uint8_t>(i8 != 0 ? 1 : 0);
        if (!streamer.readInt8(i8)) break;
        r.active = static_cast<uint8_t>(i8 != 0 ? 1 : 0);
        // Before v9: Scale saturated the amount and Filter Resonance spanned 1 Q
        if (version < 9) r.amount = preV9VoiceRouteAmount(r.amount, r.destination, r.scale);
        ar.store(r);
    }

    // FX enable flags
    Steinberg::int8 i8 = 0;
    if (streamer.readInt8(i8))
        packs.delayEnabled_.store(i8 != 0, std::memory_order_relaxed);
    if (streamer.readInt8(i8))
        packs.reverbEnabled_.store(i8 != 0, std::memory_order_relaxed);

    // Phaser params + enable flag
    loadPhaserParams(packs.phaserParams_, streamer);
    // Legacy format: 0 = disabled (None), 1 = enabled (Phaser)
    // New format: 0 = None, 1 = Phaser, 2 = Flanger
    // Default to Phaser (1) for very old presets with no byte (FR-011)
    const int modType = streamer.readInt8(i8) ? std::clamp(static_cast<int>(i8), 0, 3) : 1;
    packs.modulationType_.store(modType, std::memory_order_relaxed);

    // Flanger params (version 6+)
    if (version >= 6) {
        loadFlangerParams(packs.flangerParams_, streamer);
    } else {
        // Old preset: no flanger data, reset to defaults (the controller
        // loads the same registered defaults, loadStateCore)
        resetModEffectParams(kFlangerConfig, packs.flangerParams_);
    }

    // Chorus params (version 7+)
    if (version >= 7) {
        loadChorusParams(packs.chorusParams_, streamer);
    } else {
        // Old preset: no chorus data, reset to defaults
        resetModEffectParams(kChorusConfig, packs.chorusParams_);
    }

    // Extended LFO params
    loadLFO1ExtendedParams(packs.lfo1Params_, streamer);
    loadLFO2ExtendedParams(packs.lfo2Params_, streamer);

    // Macro and Rungler params
    loadMacroParams(packs.macroParams_, streamer);
    loadRunglerParams(packs.runglerParams_, streamer);

    // Settings params
    loadSettingsParams(packs.settingsParams_, streamer);

    // Mod source params
    loadEnvFollowerParams(packs.envFollowerParams_, streamer);
    loadSampleHoldParams(packs.sampleHoldParams_, streamer);
    loadRandomParams(packs.randomParams_, streamer);
    loadPitchFollowerParams(packs.pitchFollowerParams_, streamer);
    loadTransientParams(packs.transientParams_, streamer);

    // Harmonizer params + enable flag
    loadHarmonizerParams(packs.harmonizerParams_, streamer);
    if (streamer.readInt8(i8))
        packs.harmonizerEnabled_.store(i8 != 0, std::memory_order_relaxed);

    // Arpeggiator params
    loadArpParams(arp, streamer, version);

    // Voice LFO (version 9+); older states load 1 Hz Sine, their sound
    loadVoiceLfoParams(packs.voiceLfoParams_, streamer, version);

    // Velocity > Amp (version 9+); older states load 0, the level they were saved with
    loadAmpVelocityAmount(packs.ampEnvParams_, streamer, version);

    return true;
}

// ==============================================================================
// Preset Snapshot Application (audio thread)
// ==============================================================================

void Processor::applyPresetSnapshot(const PresetSnapshot& snapshot) {
    // =========================================================================
    // Audio-thread-only operations for preset loading. The parameters and the
    // voice routes were stored by loadStateBytes on the message thread; no
    // stream is read here (R-197).
    // =========================================================================

    // Drop the old preset's voices and arp state; an arp-only preset leaves
    // the synth (and the notes it holds outside the arp) alone.
    resetPlayingState(!snapshot.arpOnly);

    // Restore reverb and delay types from loaded state (125-dual-reverb)
    // Set directly to avoid a crossfade on state load: the engine reset above
    // emptied both slots, so there is nothing to fade from (T2.31).
    if (!snapshot.arpOnly) {
        engine_.setReverbTypeDirect(
            reverbParams_.reverbType.load(std::memory_order_relaxed));
        engine_.setDelayTypeDirect(static_cast<Krate::DSP::RuinaeDelayType>(
            delayParams_.type.load(std::memory_order_relaxed)));
    }

    // Force arp tracking variables to sentinel values so that
    // applyParamsToEngine() will unconditionally re-apply all arp setters.
    // Using -1/invalid values ensures the dirty check always triggers.
    prevArpMode_ = static_cast<Krate::DSP::ArpMode>(-1);
    prevArpOctaveRange_ = -1;
    prevArpOctaveMode_ = static_cast<Krate::DSP::OctaveMode>(-1);
    prevArpNoteValue_ = -1;
    prevArpLatchMode_ = static_cast<Krate::DSP::LatchMode>(-1);
    prevArpRetrigger_ = static_cast<Krate::DSP::ArpRetriggerMode>(-1);
}

} // namespace Ruinae
