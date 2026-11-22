#pragma once

// ==============================================================================
// Required state prefix (R-076)
// ==============================================================================
// A component state is the version int32, then the 17 packs Global ... Reverb,
// the reverb-type int32 (version >= 5) and the Mono Mode pack: everything before
// the voice routes. A stream that ends inside this prefix is truncated and is
// rejected whole (Processor::loadStateBytes, Controller::setComponentState and
// loadComponentStateWithNotify), so no half-old, half-new patch is applied.
//
// The packs after it (voice routes, FX flags, phaser ... arp) stay tolerant on
// purpose: they were appended without version bumps, so a short tail cannot be
// told from a legitimate older stream.
//
// Processor::readStatePacks reads the prefix through readRequiredStatePrefix,
// and the validator runs the same function over scratch packs: one definition
// of "truncated" for the processor and the controller.

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
#include "parameters/mono_mode_params.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Ruinae {

/// Reads the required prefix after the version int32 into `packs` (any type with
/// the Processor's pack member names). Returns false when the stream ends inside
/// it. Version < 5 has no reverb-type field and loads Plate (FR-028).
template <typename Packs>
bool readRequiredStatePrefix(Packs& packs, Steinberg::IBStreamer& streamer,
                             Steinberg::int32 version) {
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
    if (!loadModMatrixParams(packs.modMatrixParams_, streamer)) return false;
    if (!loadGlobalFilterParams(packs.globalFilterParams_, streamer)) return false;
    if (!loadDelayParams(packs.delayParams_, streamer)) return false;
    if (!loadReverbParams(packs.reverbParams_, streamer)) return false;
    // Reverb type (125-dual-reverb, state version 5)
    if (version >= 5) {
        Steinberg::int32 reverbType = 0;
        if (!streamer.readInt32(reverbType)) return false;
        packs.reverbParams_.reverbType.store(
            std::clamp(static_cast<int32_t>(reverbType), 0, 1), std::memory_order_relaxed);
    } else {
        packs.reverbParams_.reverbType.store(0, std::memory_order_relaxed);
    }
    return loadMonoModeParams(packs.monoModeParams_, streamer);
}

namespace detail {

/// Throwaway targets of the validator's dry parse; heap-allocated (the packs
/// are large).
struct PrefixScratch {
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
    RuinaeDelayParams delayParams_;
    RuinaeReverbParams reverbParams_;
    MonoModeParams monoModeParams_;
};

} // namespace detail

/// True when `bytes` (a whole state, version int32 first) holds the complete
/// required prefix for `version`. Parses into scratch; nothing live is touched.
inline bool isRequiredStatePrefixComplete(const char* bytes, size_t size,
                                          Steinberg::int32 version) {
    if (bytes == nullptr || size < sizeof(Steinberg::int32)) return false;
    // MemoryStream only reads here; its constructor takes a non-const pointer.
    Steinberg::MemoryStream memStream(
        const_cast<char*>(bytes), // NOLINT(cppcoreguidelines-pro-type-const-cast): read-only use
        static_cast<Steinberg::TSize>(size));
    Steinberg::IBStreamer streamer(&memStream, kLittleEndian);
    Steinberg::int32 streamVersion = 0;
    if (!streamer.readInt32(streamVersion)) return false;
    const auto scratch = std::make_unique<detail::PrefixScratch>();
    return readRequiredStatePrefix(*scratch, streamer, version);
}

} // namespace Ruinae
