#pragma once

// ==============================================================================
// Vorago - the one component-state reader and version migration (R-205)
// ==============================================================================
// Processor::setState and Controller::setComponentState both read the state
// through loadStateStream, each with its own loaders (the processor stores plain
// values in its packs, the controller sets normalized parameters), so the
// version handling exists once. Neither a processor nor a controller header:
// both include it, neither includes the other.
//
// Layout (plugin_ids.h, CLAUDE.md "State"): int32 version, the 60-byte v1 block
// (global + macros), the v2 tail (global extension + 14 packs, to byte 428),
// the v3 extension (ecosystem rule knobs, to byte 436).
//   - version > kCurrentStateVersion: rejected, nothing loaded (FR-046).
//   - version < 2: the v1 block only (a version < 1 is read as v1: there has
//     never been another layout); every v2 field takes its REGISTERED DEFAULT,
//     whatever was held before (C-7 / FR-040).
//   - version < 3: both v3 fields take their registered default (FR-072).
// A missing field is fed from the default-constructed packs, serialized into
// stack memory and read back through the caller's own loader, so the defaults
// pass the same mapping as a stored value. No allocation.
// ==============================================================================

#include "parameters/bloom_params.h"
#include "parameters/body_params.h"
#include "parameters/cloud_params.h"
#include "parameters/ecology_params.h"
#include "parameters/ecosystem_params.h"
#include "parameters/envelope_params.h"
#include "parameters/events_params.h"
#include "parameters/ghost_params.h"
#include "parameters/global_params.h"
#include "parameters/life_params.h"
#include "parameters/macro_params.h"
#include "parameters/noise_params.h"
#include "parameters/resonance_params.h"
#include "parameters/smear_params.h"
#include "parameters/space_params.h"
#include "parameters/sub_params.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace Vorago {

/// Reads a component state. `loadV1`, `loadV2Tail` and `loadV3Ext` each read
/// their block from the IBStreamer they are given and return false at the first
/// failed read (the fields from there on keep their current value); a block
/// after a short one is not read. A loader that cannot tell (the controller's
/// per-field setters) returns true: once a read fails the stream is at its end,
/// so every later read fails too and sets nothing.
/// @return kResultFalse, with no loader called, for a null stream, a missing
///         version word or a future version; kResultOk otherwise.
template <typename LoadV1, typename LoadV2Tail, typename LoadV3Ext>
[[nodiscard]] Steinberg::tresult loadStateStream(Steinberg::IBStream* state, const LoadV1& loadV1,
                                                 const LoadV2Tail& loadV2Tail,
                                                 const LoadV3Ext& loadV3Ext) {
    if (state == nullptr) {
        return Steinberg::kResultFalse;
    }
    Steinberg::IBStreamer in(state, kLittleEndian);
    Steinberg::int32 version = 0;
    if (!in.readInt32(version)) {
        return Steinberg::kResultFalse;
    }
    if (version > kCurrentStateVersion) {
        return Steinberg::kResultFalse;
    }

    const bool v1Complete = loadV1(in);

    bool v2Complete = false;
    if (version >= 2) {
        v2Complete = v1Complete && loadV2Tail(in);
    } else {
        constexpr std::size_t kV2TailBytes = kStateV2Bytes - 60u;  // minus version + v1 block
        std::array<char, kV2TailBytes> tail{};
        Steinberg::MemoryStream tailStream(tail.data(), static_cast<Steinberg::TSize>(tail.size()));
        Steinberg::IBStreamer defaults(&tailStream, kLittleEndian);
        saveGlobalParamsV2Ext(GlobalParams{}, defaults);
        saveCloudParams(CloudParams{}, defaults);
        saveNoiseParams(NoiseParams{}, defaults);
        saveResonanceParams(ResonanceParams{}, defaults);
        saveEcologyParams(EcologyParams{}, defaults);
        saveSubParams(SubParams{}, defaults);
        saveSmearParams(SmearParams{}, defaults);
        saveEventsParams(EventsParams{}, defaults);
        saveEcosystemParams(EcosystemParams{}, defaults);
        saveBodyParams(BodyParams{}, defaults);
        saveSpaceParams(SpaceParams{}, defaults);
        saveEnvelopeParams(EnvelopeParams{}, defaults);
        saveBloomParams(BloomParams{}, defaults);
        saveGhostParams(GhostParams{}, defaults);
        saveLifeParams(LifeParams{}, defaults);
        tailStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        [[maybe_unused]] const bool defaultsLoaded = loadV2Tail(defaults);
        assert(defaultsLoaded);
        v2Complete = true;
    }

    if (version >= 3) {
        if (v2Complete) {
            [[maybe_unused]] const bool v3Complete = loadV3Ext(in);
        }
    } else {
        std::array<char, kStateV3Bytes - kStateV2Bytes> ext{};
        Steinberg::MemoryStream extStream(ext.data(), static_cast<Steinberg::TSize>(ext.size()));
        Steinberg::IBStreamer defaults(&extStream, kLittleEndian);
        saveEcosystemParamsV3Ext(EcosystemParams{}, defaults);
        extStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        [[maybe_unused]] const bool defaultsLoaded = loadV3Ext(defaults);
        assert(defaultsLoaded);
    }
    return Steinberg::kResultOk;
}

}  // namespace Vorago
