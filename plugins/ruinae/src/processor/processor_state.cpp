// ==============================================================================
// Processor State Management (getState, setState, applyPresetSnapshot)
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <krate/dsp/systems/voice_mod_types.h>

#include "parameters/dropdown_mappings.h"

#include <cstdint>

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

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
    // =========================================================================
    // Hybrid crash-proof preset loading:
    //
    // 1. Apply all ATOMIC parameters immediately on the UI thread, so that
    //    getState() returns the correct values right away (host compatibility).
    //    Individual atomic writes are safe — worst case is one process() block
    //    with mixed old/new params (brief audio glitch, not a crash).
    //
    // 2. Defer engine/arp reset and voice route deserialization to the
    //    audio thread via RTTransferT. Voice routes use per-field atomics
    //    (AtomicVoiceModRoute) so writes are safe from any thread.
    // =========================================================================

    if (!state) return Steinberg::kResultTrue;

    // Read all bytes from the IBStream into a contiguous buffer.
    auto snapshot = std::make_unique<PresetSnapshot>();
    constexpr Steinberg::int32 kChunkSize = 4096;
    char chunk[kChunkSize];
    Steinberg::int32 bytesRead = 0;

    while (true) {
        auto result = state->read(chunk, kChunkSize, &bytesRead);
        if (result != Steinberg::kResultTrue || bytesRead <= 0) break;
        snapshot->bytes.insert(snapshot->bytes.end(), chunk, chunk + bytesRead);
    }

    if (snapshot->bytes.empty()) return Steinberg::kResultTrue;

    // --- Phase 1: Apply atomic params immediately (UI thread) ---
    // This preserves the host's setState/getState contract: after setState()
    // returns, getState() must reflect the new values.
    {
        Steinberg::MemoryStream memStream(
            snapshot->bytes.data(),
            static_cast<Steinberg::TSize>(snapshot->bytes.size()));
        Steinberg::IBStreamer streamer(&memStream, kLittleEndian);

        Steinberg::int32 version = 0;
        if (!streamer.readInt32(version))
            return Steinberg::kResultTrue;
        if (version < 1 || version > kCurrentStateVersion)
            return Steinberg::kResultTrue;

        // Load all parameter packs into atomics (safe from any thread)
        if (!loadGlobalParams(globalParams_, streamer)) return Steinberg::kResultTrue;
        if (!loadOscAParams(oscAParams_, streamer)) return Steinberg::kResultTrue;
        if (!loadOscBParams(oscBParams_, streamer)) return Steinberg::kResultTrue;
        if (!loadMixerParams(mixerParams_, streamer)) return Steinberg::kResultTrue;
        if (!loadFilterParams(filterParams_, streamer)) return Steinberg::kResultTrue;
        if (!loadDistortionParams(distortionParams_, streamer)) return Steinberg::kResultTrue;
        if (!loadTranceGateParams(tranceGateParams_, streamer)) return Steinberg::kResultTrue;
        if (!loadAmpEnvParams(ampEnvParams_, streamer)) return Steinberg::kResultTrue;
        if (!loadFilterEnvParams(filterEnvParams_, streamer)) return Steinberg::kResultTrue;
        if (!loadModEnvParams(modEnvParams_, streamer)) return Steinberg::kResultTrue;
        if (!loadLFO1Params(lfo1Params_, streamer)) return Steinberg::kResultTrue;
        if (!loadLFO2Params(lfo2Params_, streamer)) return Steinberg::kResultTrue;
        if (!loadChaosModParams(chaosModParams_, streamer)) return Steinberg::kResultTrue;
        if (!loadModMatrixParams(modMatrixParams_, streamer)) return Steinberg::kResultTrue;
        if (!loadGlobalFilterParams(globalFilterParams_, streamer)) return Steinberg::kResultTrue;
        if (!loadDelayParams(delayParams_, streamer)) return Steinberg::kResultTrue;
        if (!loadReverbParams(reverbParams_, streamer)) return Steinberg::kResultTrue;
        // Reverb type (125-dual-reverb, state version 5)
        if (version >= 5) {
            Steinberg::int32 reverbType = 0;
            if (streamer.readInt32(reverbType)) {
                reverbParams_.reverbType.store(
                    static_cast<int32_t>(reverbType), std::memory_order_relaxed);
            }
        } else {
            // Backward compat: version < 5 defaults to Plate (FR-028)
            reverbParams_.reverbType.store(0, std::memory_order_relaxed);
        }
        if (!loadMonoModeParams(monoModeParams_, streamer)) return Steinberg::kResultTrue;

        // SKIP voiceRoutes_ here — deferred to audio thread via RTTransferT

        // Skip past voiceRoutes bytes in the stream (16 routes x 8 fields)
        for (int i = 0; i < Krate::Plugins::kMaxVoiceRoutes; ++i) {
            Steinberg::int8 i8 = 0;
            float f = 0.0f;
            if (!streamer.readInt8(i8)) break; // source
            if (!streamer.readInt8(i8)) break; // dest
            if (!streamer.readFloat(f)) break; // amount
            if (!streamer.readInt8(i8)) break; // curve
            if (!streamer.readFloat(f)) break; // smoothMs
            if (!streamer.readInt8(i8)) break; // scale
            if (!streamer.readInt8(i8)) break; // bypass
            if (!streamer.readInt8(i8)) break; // active
        }

        // FX enable flags
        Steinberg::int8 i8 = 0;
        if (streamer.readInt8(i8))
            delayEnabled_.store(i8 != 0, std::memory_order_relaxed);
        if (streamer.readInt8(i8))
            reverbEnabled_.store(i8 != 0, std::memory_order_relaxed);

        // Phaser params + enable flag
        loadPhaserParams(phaserParams_, streamer);
        // Legacy format: 0 = disabled (None), 1 = enabled (Phaser)
        // New format: 0 = None, 1 = Phaser, 2 = Flanger
        // Default to Phaser (1) for very old presets with no byte (FR-011)
        const int modType = streamer.readInt8(i8) ? static_cast<int>(i8) : 1;
        modulationType_.store(modType, std::memory_order_relaxed);

        // Flanger params (version 6+)
        if (version >= 6) {
            loadFlangerParams(flangerParams_, streamer);
        } else {
            // Old preset: no flanger data, reset to defaults
            flangerParams_.rateHz.store(0.5f, std::memory_order_relaxed);
            flangerParams_.depth.store(0.5f, std::memory_order_relaxed);
            flangerParams_.feedback.store(0.0f, std::memory_order_relaxed);
            flangerParams_.mix.store(0.5f, std::memory_order_relaxed);
            flangerParams_.stereoSpread.store(90.0f, std::memory_order_relaxed);
            flangerParams_.waveform.store(1, std::memory_order_relaxed);
            flangerParams_.sync.store(false, std::memory_order_relaxed);
            flangerParams_.noteValue.store(Parameters::kNoteValueDefaultIndex, std::memory_order_relaxed);
        }

        // Chorus params (version 7+)
        if (version >= 7) {
            loadChorusParams(chorusParams_, streamer);
        } else {
            // Old preset: no chorus data, reset to defaults
            chorusParams_.rateHz.store(0.5f, std::memory_order_relaxed);
            chorusParams_.depth.store(0.5f, std::memory_order_relaxed);
            chorusParams_.feedback.store(0.0f, std::memory_order_relaxed);
            chorusParams_.mix.store(0.5f, std::memory_order_relaxed);
            chorusParams_.stereoSpread.store(180.0f, std::memory_order_relaxed);
            chorusParams_.voices.store(2, std::memory_order_relaxed);
            chorusParams_.waveform.store(1, std::memory_order_relaxed);
            chorusParams_.sync.store(false, std::memory_order_relaxed);
            chorusParams_.noteValue.store(Parameters::kNoteValueDefaultIndex, std::memory_order_relaxed);
        }

        // Extended LFO params
        loadLFO1ExtendedParams(lfo1Params_, streamer);
        loadLFO2ExtendedParams(lfo2Params_, streamer);

        // Macro and Rungler params
        loadMacroParams(macroParams_, streamer);
        loadRunglerParams(runglerParams_, streamer);

        // Settings params
        loadSettingsParams(settingsParams_, streamer);

        // Mod source params
        loadEnvFollowerParams(envFollowerParams_, streamer);
        loadSampleHoldParams(sampleHoldParams_, streamer);
        loadRandomParams(randomParams_, streamer);
        loadPitchFollowerParams(pitchFollowerParams_, streamer);
        loadTransientParams(transientParams_, streamer);

        // Harmonizer params + enable flag
        loadHarmonizerParams(harmonizerParams_, streamer);
        if (streamer.readInt8(i8))
            harmonizerEnabled_.store(i8 != 0, std::memory_order_relaxed);

        // Arpeggiator params
        loadArpParams(arpParams_, streamer, version);
    }

    // --- Phase 2: Defer voiceRoutes + engine/arp reset to audio thread ---
    stateTransfer_.transferObject_ui(std::move(snapshot));

    return Steinberg::kResultTrue;
}

// ==============================================================================
// Preset Snapshot Application (audio thread)
// ==============================================================================

void Processor::applyPresetSnapshot(const PresetSnapshot& snapshot) {
    // =========================================================================
    // Audio-thread-only operations for preset loading:
    //   1. voiceRoutes_ deserialization (atomic store per field)
    //   2. engine_.reset() + arpCore_.reset() (kill stale voices)
    //   3. Force arp tracking re-application
    //
    // Atomic parameter writes are done immediately in setState() for host
    // compatibility. This method only handles the unsafe/deferred operations.
    // =========================================================================

    if (snapshot.bytes.empty()) return;

    Steinberg::MemoryStream memStream(
        const_cast<char*>(snapshot.bytes.data()),
        static_cast<Steinberg::TSize>(snapshot.bytes.size()));
    Steinberg::IBStreamer streamer(&memStream, kLittleEndian);

    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version)) return;
    if (version < 1 || version > kCurrentStateVersion) return;

    // Skip past all atomic parameter packs to reach voiceRoutes_
    // (params were already applied in setState on the UI thread)
    if (!loadGlobalParams(globalParams_, streamer)) return;
    if (!loadOscAParams(oscAParams_, streamer)) return;
    if (!loadOscBParams(oscBParams_, streamer)) return;
    if (!loadMixerParams(mixerParams_, streamer)) return;
    if (!loadFilterParams(filterParams_, streamer)) return;
    if (!loadDistortionParams(distortionParams_, streamer)) return;
    if (!loadTranceGateParams(tranceGateParams_, streamer)) return;
    if (!loadAmpEnvParams(ampEnvParams_, streamer)) return;
    if (!loadFilterEnvParams(filterEnvParams_, streamer)) return;
    if (!loadModEnvParams(modEnvParams_, streamer)) return;
    if (!loadLFO1Params(lfo1Params_, streamer)) return;
    if (!loadLFO2Params(lfo2Params_, streamer)) return;
    if (!loadChaosModParams(chaosModParams_, streamer)) return;
    if (!loadModMatrixParams(modMatrixParams_, streamer)) return;
    if (!loadGlobalFilterParams(globalFilterParams_, streamer)) return;
    if (!loadDelayParams(delayParams_, streamer)) return;
    if (!loadReverbParams(reverbParams_, streamer)) return;
    // Reverb type (125-dual-reverb, state version 5)
    if (version >= 5) {
        Steinberg::int32 reverbType = 0;
        if (streamer.readInt32(reverbType)) {
            reverbParams_.reverbType.store(
                static_cast<int32_t>(reverbType), std::memory_order_relaxed);
        }
    }
    if (!loadMonoModeParams(monoModeParams_, streamer)) return;

    // Voice routes — atomic store per field (safe from any thread)
    for (auto& ar : voiceRoutes_) {
        Krate::Plugins::VoiceModRoute r{};
        Steinberg::int8 i8 = 0;
        float f = 0.0f;
        if (!streamer.readInt8(i8)) break;
        r.source = static_cast<uint8_t>(i8);
        if (!streamer.readInt8(i8)) break;
        r.destination = static_cast<uint8_t>(i8);
        if (!streamer.readFloat(f)) break;
        r.amount = f;
        if (!streamer.readInt8(i8)) break;
        r.curve = static_cast<uint8_t>(i8);
        if (!streamer.readFloat(f)) break;
        r.smoothMs = f;
        if (!streamer.readInt8(i8)) break;
        r.scale = static_cast<uint8_t>(i8);
        if (!streamer.readInt8(i8)) break;
        r.bypass = static_cast<uint8_t>(i8);
        if (!streamer.readInt8(i8)) break;
        r.active = static_cast<uint8_t>(i8);
        ar.store(r);
    }

    // Reset DSP state to prevent stale voices/state from the old preset
    engine_.reset();
    arpCore_.reset();

    // Restore reverb type from loaded state (125-dual-reverb)
    // Use setReverbTypeDirect to avoid triggering a crossfade on state load.
    engine_.setReverbTypeDirect(
        reverbParams_.reverbType.load(std::memory_order_relaxed));

    // Force arp tracking variables to sentinel values so that
    // applyParamsToEngine() will unconditionally re-apply all arp setters.
    // Using -1/invalid values ensures the dirty check always triggers.
    prevArpMode_ = static_cast<Krate::DSP::ArpMode>(-1);
    prevArpOctaveRange_ = -1;
    prevArpOctaveMode_ = static_cast<Krate::DSP::OctaveMode>(-1);
    prevArpNoteValue_ = -1;
    prevArpLatchMode_ = static_cast<Krate::DSP::LatchMode>(-1);
    prevArpRetrigger_ = static_cast<Krate::DSP::ArpRetriggerMode>(-1);

    // Signal process() to send voice route state to controller
    needVoiceRouteSync_.store(true, std::memory_order_relaxed);
}

} // namespace Ruinae
