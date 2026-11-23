#pragma once

// ==============================================================================
// Component state layout: the one writer, and the default state (R-212)
// ==============================================================================
// writeStatePacks writes a whole component state from any type with the
// Processor's pack member names: Processor::getState writes the processor's
// packs through it, and defaultStateBytes writes default-constructed packs.
//
// A state that ends at a pack boundary is legitimate (older versions wrote
// fewer packs), so the packs it lacks load their defaults, never the previous
// preset's values: Processor::readStatePacks and Controller::loadStateCore
// both parse defaultStateBytes() before the stream. One layout, one set of
// defaults, for both parsers.

#include "parameters/global_params.h"
#include "parameters/osc_a_params.h"
#include "parameters/osc_b_params.h"
#include "parameters/mixer_params.h"
#include "parameters/filter_params.h"
#include "parameters/distortion_params.h"
#include "parameters/trance_gate_params.h"
#include "parameters/amp_env_params.h"
#include "parameters/filter_env_params.h"
#include "parameters/mod_env_params.h"
#include "parameters/lfo1_params.h"
#include "parameters/lfo2_params.h"
#include "parameters/chaos_mod_params.h"
#include "parameters/mod_matrix_params.h"
#include "parameters/global_filter_params.h"
#include "parameters/delay_params.h"
#include "parameters/reverb_params.h"
#include "parameters/phaser_params.h"
#include "parameters/flanger_params.h"
#include "parameters/chorus_params.h"
#include "parameters/harmonizer_params.h"
#include "parameters/mono_mode_params.h"
#include "parameters/macro_params.h"
#include "parameters/rungler_params.h"
#include "parameters/settings_params.h"
#include "parameters/env_follower_params.h"
#include "parameters/sample_hold_params.h"
#include "parameters/random_params.h"
#include "parameters/pitch_follower_params.h"
#include "parameters/transient_params.h"
#include "parameters/voice_lfo_params.h"
#include "parameters/arpeggiator_params.h"

#include "ui/mod_matrix_types.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace Ruinae {

/// Writes a whole component state: the version, then every pack in the order
/// the parsers read them. `packs` is the Processor or a StatePackSet.
template <typename Packs>
void writeStatePacks(const Packs& packs, const ArpeggiatorParams& arp,
                     Steinberg::IBStreamer& streamer) {
    // Write state version first
    streamer.writeInt32(kCurrentStateVersion);

    // Save all 19 parameter packs in deterministic order
    saveGlobalParams(packs.globalParams_, streamer);
    saveOscAParams(packs.oscAParams_, streamer);
    saveOscBParams(packs.oscBParams_, streamer);
    saveMixerParams(packs.mixerParams_, streamer);
    saveFilterParams(packs.filterParams_, streamer);
    saveDistortionParams(packs.distortionParams_, streamer);
    saveTranceGateParams(packs.tranceGateParams_, streamer);
    saveAmpEnvParams(packs.ampEnvParams_, streamer);
    saveFilterEnvParams(packs.filterEnvParams_, streamer);
    saveModEnvParams(packs.modEnvParams_, streamer);
    saveLFO1Params(packs.lfo1Params_, streamer);
    saveLFO2Params(packs.lfo2Params_, streamer);
    saveChaosModParams(packs.chaosModParams_, streamer);
    saveModMatrixParams(packs.modMatrixParams_, streamer);
    saveGlobalFilterParams(packs.globalFilterParams_, streamer);
    saveDelayParams(packs.delayParams_, streamer);
    saveReverbParams(packs.reverbParams_, streamer);
    // Reverb type (125-dual-reverb, state version 5)
    streamer.writeInt32(packs.reverbParams_.reverbType.load(std::memory_order_relaxed));

    saveMonoModeParams(packs.monoModeParams_, streamer);

    // Voice routes (16 slots), one consistent snapshot
    for (const auto& r : packs.voiceRoutes_.loadAll()) {
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
    streamer.writeInt8(packs.delayEnabled_.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt8(packs.reverbEnabled_.load(std::memory_order_relaxed) ? 1 : 0);

    // Phaser params + modulation type (replaces legacy phaserEnabled_ int8)
    savePhaserParams(packs.phaserParams_, streamer);
    // Write modulationType as int8 for backward compatibility with state format:
    // 0 = None (legacy: phaser disabled), 1 = Phaser (legacy: phaser enabled), 2 = Flanger, 3 = Chorus
    streamer.writeInt8(static_cast<Steinberg::int8>(packs.modulationType_.load(std::memory_order_relaxed)));

    // Flanger params (version 6+)
    saveFlangerParams(packs.flangerParams_, streamer);

    // Chorus params (version 7+)
    saveChorusParams(packs.chorusParams_, streamer);

    // Extended LFO params
    saveLFO1ExtendedParams(packs.lfo1Params_, streamer);
    saveLFO2ExtendedParams(packs.lfo2Params_, streamer);

    // Macro and Rungler params
    saveMacroParams(packs.macroParams_, streamer);
    saveRunglerParams(packs.runglerParams_, streamer);

    // Settings params
    saveSettingsParams(packs.settingsParams_, streamer);

    // Mod source params
    saveEnvFollowerParams(packs.envFollowerParams_, streamer);
    saveSampleHoldParams(packs.sampleHoldParams_, streamer);
    saveRandomParams(packs.randomParams_, streamer);
    savePitchFollowerParams(packs.pitchFollowerParams_, streamer);
    saveTransientParams(packs.transientParams_, streamer);

    // Harmonizer params + enable flag
    saveHarmonizerParams(packs.harmonizerParams_, streamer);
    streamer.writeInt8(packs.harmonizerEnabled_.load(std::memory_order_relaxed) ? 1 : 0);

    // Arpeggiator params (FR-011)
    saveArpParams(arp, streamer);

    // Voice LFO rate + shape (version 9+), appended after the arp pack
    saveVoiceLfoParams(packs.voiceLfoParams_, streamer);

    // Velocity > Amp (version 9+), after the voice LFO
    saveAmpVelocityAmount(packs.ampEnvParams_, streamer);
}

/// Every pack of a component state except the arp, under the Processor's
/// member names, at their defaults. `RouteTable` is the Processor's
/// VoiceRouteTable for a scratch parse, or DefaultVoiceRoutes.
template <typename RouteTable>
struct StatePackSet {
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
    RouteTable voiceRoutes_;
};

/// The voice routes of a default state: what a fresh VoiceRouteTable holds.
struct DefaultVoiceRoutes {
    [[nodiscard]] static std::array<Krate::Plugins::VoiceModRoute, Krate::Plugins::kMaxVoiceRoutes>
    loadAll() noexcept {
        return {};
    }
};

/// A whole component state at every default (R-212). Allocates: message
/// thread only.
inline std::vector<char> defaultStateBytes() {
    const auto packs = std::make_unique<StatePackSet<DefaultVoiceRoutes>>();
    const auto arp = std::make_unique<ArpeggiatorParams>();
    Steinberg::MemoryStream stream;
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);
    writeStatePacks(*packs, *arp, streamer);
    return {stream.getData(), stream.getData() + stream.getSize()};
}

} // namespace Ruinae
