// ==============================================================================
// Vorago Phase 14 - getTailSamples() estimate; FR-060, SC-030; filled by T014, T030
// ==============================================================================
// T014: cases (i)-(iv) of Vorago_Processor_GetTailSamplesMatchesState on a
// processor prepared at 48 kHz. Each state is delivered through IParameterChanges
// in one process() call. Expected tails (plan 5.3, FR-060: Rel + RT60_eff + G):
//   release 45 s (vorago_voice.h:327 kDefaultReleaseMs), default decay 20 s
//   (space_params.h:42), G = AtmosphereEngine::kMaxGrainSeconds = 30 s
//   (atmosphere_engine.h:311).
//   (iv) default surface       -> (45 + 20  + 30) s * 48 000 = 4 560 000
//   (i)  decay 0.5 s           -> (45 + 0.5 + 30) s * 48 000 = 3 624 000
//   (ii) decay 60 s, Depth=Age=1 -> RT60_eff clamped to 60 -> 6 480 000
//   (iii) Freeze On            -> Steinberg::Vst::kInfiniteTail
// T030: (v) every factory preset - a processor loaded with the definition's
//   component state; expected from decodePresetState(comp) (independent of the
//   processor's atomics): kInfiniteTail when the decoded freeze is On, else
//   llround(tailSeconds(decoded release ms, effectiveCavernDecaySeconds(decoded
//   KNOB macros, decoded decay)) * 48 000), within +/-1 sample.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "parameters/space_params.h"
#include "preset_test_support.h"
#include "plugin_ids.h"
#include "processor/tail_estimate.h"
#include "vorago_test_fixture.h"
#include "vorago_preset_defs.h"

#include <krate/dsp/systems/atmosphere_engine.h>

#include "public.sdk/source/common/memorystream.h"

#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlock = 512;

/// Prepares at 48 kHz and applies every (id, normalized) pair as an offset-0
/// point in ONE process() call.
void applyState(VoragoTest::ProcessorFixture& fx,
                std::initializer_list<std::pair<Steinberg::Vst::ParamID, double>> changes) {
    fx.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock));
    VoragoTest::MultiParamChanges pc;
    pc.reserve(changes.size());
    for (const auto& [id, value] : changes) {
        pc.addQueue(id).addTestPoint(0, value);
    }
    fx.reserveCapture(kBlock);
    REQUIRE(fx.processBlock(kBlock, nullptr, &pc) == Steinberg::kResultOk);
}

/// Finite tail within +/-1 sample of the expected count.
void requireTailNear(VoragoTest::ProcessorFixture& fx, std::int64_t expected) {
    const Steinberg::uint32 tail = fx.proc->getTailSamples();
    INFO("getTailSamples() = " << tail << ", expected " << expected);
    REQUIRE(tail != Steinberg::Vst::kInfiniteTail);
    const std::int64_t diff = static_cast<std::int64_t>(tail) - expected;
    REQUIRE(diff >= -1);
    REQUIRE(diff <= 1);
}

/// T030: the tail a decoded component state implies, independent of the
/// processor's atomics. -1 stands for kInfiniteTail (decoded freeze On).
[[nodiscard]] std::int64_t expectedTailFromDecoded(const VoragoTest::DecodedPresetState& st) {
    constexpr auto kR = std::memory_order_relaxed;
    if (st.space.freeze.load(kR) != 0) {
        return -1;
    }
    Krate::DSP::VoragoMacroValues knobs{};
    knobs.darkness = st.macros.darkness.load(kR);
    knobs.age = st.macros.age.load(kR);
    knobs.density = st.macros.density.load(kR);
    knobs.movement = st.macros.movement.load(kR);
    knobs.gravity = st.macros.gravity.load(kR);
    knobs.entropy = st.macros.entropy.load(kR);
    knobs.pressure = st.macros.pressure.load(kR);
    knobs.weight = st.macros.weight.load(kR);
    knobs.fog = st.macros.fog.load(kR);
    knobs.life = st.macros.life.load(kR);
    knobs.depth = st.macros.depth.load(kR);
    knobs.mass = st.macros.mass.load(kR);
    const float rt60 = ::Vorago::effectiveCavernDecaySeconds(knobs, st.space.decaySeconds.load(kR));
    const double seconds = ::Vorago::tailSeconds(st.envelope.releaseMs.load(kR), rt60);
    return static_cast<std::int64_t>(std::llround(seconds * kSampleRate));
}

}  // namespace

TEST_CASE("Vorago_Processor_GetTailSamplesMatchesState", "[vorago][tail]") {
    REQUIRE(::Vorago::kGhostGrainCeilingSeconds ==
            static_cast<double>(Krate::DSP::AtmosphereEngine::kMaxGrainSeconds));

    VoragoTest::ProcessorFixture fx;

    SECTION("(iv) default surface -> (45 + 20 + 30) s") {
        applyState(fx, {});
        requireTailNear(fx, 4560000);
    }

    SECTION("(i) decay 0.5 s, macros at default -> (45 + 0.5 + 30) s") {
        const double decayNorm = ::Vorago::spaceFloatToNormalized(::Vorago::kSpaceDecayId, 0.5);
        applyState(fx, {{::Vorago::kSpaceDecayId, decayNorm}});
        requireTailNear(fx, 3624000);
    }

    SECTION("(ii) decay 60 s with Depth = Age = 1 -> RT60 clamped to 60 s") {
        const double decayNorm = ::Vorago::spaceFloatToNormalized(::Vorago::kSpaceDecayId, 60.0);
        applyState(fx, {{::Vorago::kMacroAgeId, 1.0},
                        {::Vorago::kMacroDepthId, 1.0},
                        {::Vorago::kSpaceDecayId, decayNorm}});
        requireTailNear(fx, 6480000);
    }

    SECTION("(iii) Freeze On -> kInfiniteTail") {
        applyState(fx, {{::Vorago::kSpaceFreezeId, 1.0}});
        REQUIRE(fx.proc->getTailSamples() == Steinberg::Vst::kInfiniteTail);
    }

    SECTION("(v) every factory preset -> tail from its independently decoded state") {
        // Empty until the library is authored (T037); (i)-(iv) bite meanwhile.
        for (const ::Vorago::PresetDefs::VoragoPresetDef& def : ::Vorago::PresetDefs::allPresets()) {
            INFO("preset " << std::string(def.category) << "/" << std::string(def.name));
            std::vector<std::uint8_t> comp;
            std::string why;
            const bool built = VoragoTest::buildPresetComponentState(def, comp, why);
            INFO(why);
            REQUIRE(built);

            VoragoTest::DecodedPresetState st;
            REQUIRE(VoragoTest::decodePresetState(std::span<const std::uint8_t>(comp), st));
            const std::int64_t expected = expectedTailFromDecoded(st);

            VoragoTest::ProcessorFixture pfx;
            pfx.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock));
            auto stream = Steinberg::owned(new Steinberg::MemoryStream());
            REQUIRE(stream->write(comp.data(), static_cast<Steinberg::int32>(comp.size()), nullptr) ==
                    Steinberg::kResultOk);
            REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
            REQUIRE(pfx.proc->setState(stream) == Steinberg::kResultOk);

            if (expected < 0) {
                REQUIRE(pfx.proc->getTailSamples() == Steinberg::Vst::kInfiniteTail);
            } else {
                requireTailNear(pfx, expected);
            }
        }
    }
}
