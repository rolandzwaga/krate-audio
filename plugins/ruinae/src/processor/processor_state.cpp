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
    writeStatePacks(*this, arpParams_, streamer);
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
using DiscardedStatePacks = StatePackSet<VoiceRouteTable>;

} // namespace

Steinberg::tresult Processor::loadStateBytes(std::vector<char> bytes, bool arpOnly) {
    // =========================================================================
    // Hybrid crash-proof preset loading, one path for host and browser loads:
    //
    // 1. Parse into the ATOMIC parameters right here on the message thread, so
    //    that getState() returns the new values at once (host contract).
    //    Individual atomic writes are safe: worst case is one process() block
    //    with mixed old/new params (brief audio glitch, not a crash). Voice
    //    routes are published as one table (VoiceRouteTable), safe from here,
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

        // A stream this version does not read applies nothing and keeps the
        // current state (an empty or unknown-version stream is not an error).
        Steinberg::int32 version = 0;
        if (!streamer.readInt32(version) || version < 1 || version > kCurrentStateVersion)
            return Steinberg::kResultTrue;
        // A stream that ends inside the required prefix is rejected before
        // anything is stored (R-076): no half-old, half-new patch.
        if (!isRequiredStatePrefixComplete(bytes.data(), bytes.size(), version))
            return Steinberg::kResultFalse;
        memStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

        if (arpOnly) {
            // The arp pack comes last but one: the packs before it are read
            // into scratch, so the synth, the voice routes and the voice LFO
            // keep the loaded synth preset's values.
            const auto scratch = std::make_unique<DiscardedStatePacks>();
            if (!readStatePacks(*scratch, arpParams_, streamer)) return Steinberg::kResultFalse;
        } else {
            if (!readStatePacks(*this, arpParams_, streamer)) return Steinberg::kResultFalse;
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

    // Every pack starts at its default (R-212): a stream that ends at a pack
    // boundary (older versions legitimately do) leaves the packs it lacks at
    // their defaults, never at the previous preset's values. The controller
    // parses the same defaults first (Controller::loadStateCore).
    std::vector<char> defaults = defaultStateBytes();
    Steinberg::MemoryStream defaultStream(defaults.data(),
                                          static_cast<Steinberg::TSize>(defaults.size()));
    Steinberg::IBStreamer defaultStreamer(&defaultStream, kLittleEndian);
    Steinberg::int32 defaultVersion = 0;
    defaultStreamer.readInt32(defaultVersion);
    readStatePackFields(packs, arp, defaultStreamer, defaultVersion);

    return readStatePackFields(packs, arp, streamer, version);
}

template <typename Packs>
bool Processor::readStatePackFields(Packs& packs, ArpeggiatorParams& arp,
                                    Steinberg::IBStreamer& streamer, Steinberg::int32 version) {
    // Load the required prefix (Global ... Mono Mode) into atomics (safe from
    // any thread); the same reader the truncation check runs (R-076)
    if (!readRequiredStatePrefix(packs, streamer, version)) return false;
    // Before v9 the LFO and Chaos depths never reached the audio (R-041, R-183):
    // every source modulated at full depth, which these states keep.
    if (version < 9) {
        packs.lfo1Params_.depth.store(1.0f, std::memory_order_relaxed);
        packs.lfo2Params_.depth.store(1.0f, std::memory_order_relaxed);
        packs.chaosModParams_.depth.store(1.0f, std::memory_order_relaxed);
    }
    // Before v9 the Scale saturated the amount (R-053): keep the saved depth
    if (version < 9) migratePreV9ModMatrixAmounts(packs.modMatrixParams_);

    // Voice routes, sanitised by the reader the controller uses (R-003, R-211):
    // a damaged state must not hand the voice router an index past its tables.
    // All 16 are published in one write (R-007); slots the stream does not
    // reach keep the routes readStatePacks's default pass gave them.
    auto routes = packs.voiceRoutes_.loadAll();
    for (auto& route : routes) {
        LoadedVoiceRoute loaded;
        if (!readVoiceRoute(streamer, version, loaded)) break;
        Krate::Plugins::VoiceModRoute r{};
        r.source = static_cast<uint8_t>(loaded.source);
        r.destination = static_cast<uint8_t>(loaded.destination);
        r.amount = loaded.amount;
        r.curve = static_cast<uint8_t>(loaded.curve);
        r.smoothMs = loaded.smoothMs;
        r.scale = static_cast<uint8_t>(loaded.scale);
        r.bypass = static_cast<uint8_t>(loaded.bypass ? 1 : 0);
        r.active = static_cast<uint8_t>(loaded.active ? 1 : 0);
        route = r;
    }
    packs.voiceRoutes_.storeAll(routes);

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
    const int modType = streamer.readInt8(i8) ? kModulationTypeLoad(i8) : 1;
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
