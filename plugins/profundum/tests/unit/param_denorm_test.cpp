// ==============================================================================
// Profundum - parameter latching and denormalisation (Phase 10 T009)
// ==============================================================================
// SC-010 processor arm (FR-024.1 - .2, FR-042, FR-043). Every check sends a
// one-change Krate::Test::ParameterChanges through a numOutputs = 0 process() call on an
// initialised, UNPREPARED processor and reads the latched atomics back through
// the const test seams. T010 appends the controller sections (RegisteredInfo,
// Display: SC-010 controller arm, FR-047) on a heap Controller.
// ==============================================================================

#include "profundum_test_fixture.h"

#include "controller/controller.h"
#include "parameters/global_params.h"
#include "parameters/macro_params.h"
#include "plugin_ids.h"

#include "base/source/fobject.h"
#include "pluginterfaces/base/ustring.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <string>

using namespace ProfundumPluginTest;

namespace {

using Steinberg::Vst::ParamID;

constexpr std::array<ParamID, 8> kAllIds{
    ::Profundum::kOutputGainId,      ::Profundum::kVoiceModeId,
    ::Profundum::kPolyphonyId,       ::Profundum::kOutputCeilingId,
    ::Profundum::kMacroWeightId,     ::Profundum::kMacroDarkBrightId,
    ::Profundum::kMacroTightLooseId, ::Profundum::kMacroCleanViolentId,
};

constexpr std::array<ParamID, 4> kMacroIds{
    ::Profundum::kMacroWeightId, ::Profundum::kMacroDarkBrightId,
    ::Profundum::kMacroTightLooseId, ::Profundum::kMacroCleanViolentId,
};

// The eight latched atomics, read relaxed.
struct Snapshot {
    float gain = 0.0f;
    int voiceMode = 0;
    int polyphony = 0;
    float ceilingDb = 0.0f;
    std::array<float, 4> macros{};

    bool operator==(const Snapshot&) const = default;
};

Snapshot snapshot(const ::Profundum::Processor& p) {
    const auto& g = p.globalParamsForTest();
    const auto& m = p.macroParamsForTest();
    Snapshot s;
    s.gain = g.outputGain.load(std::memory_order_relaxed);
    s.voiceMode = g.voiceMode.load(std::memory_order_relaxed);
    s.polyphony = g.polyphony.load(std::memory_order_relaxed);
    s.ceilingDb = g.outputCeilingDb.load(std::memory_order_relaxed);
    for (int i = 0; i < ::Profundum::kNumMacroParams; ++i) {
        s.macros[static_cast<std::size_t>(i)] =
            ::Profundum::macroField(m, i).load(std::memory_order_relaxed);
    }
    return s;
}

// One queue holding one point at offset 0, sent through a parameter-only call.
void sendOne(ProcessorFixture& fx, Krate::Test::ParameterChanges& changes, ParamID id, double value) {
    changes.setChange(id, value);
    REQUIRE(fx.processNoOutputs(&changes) == Steinberg::kResultOk);
}

std::string toAscii(const Steinberg::Vst::TChar* s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

// Heap Controller (FR-064), initialised with a null context, terminated on scope exit.
struct ControllerHandle {
    Steinberg::IPtr<::Profundum::Controller> ctl =
        Steinberg::owned(new ::Profundum::Controller());

    ControllerHandle() { REQUIRE(ctl->initialize(nullptr) == Steinberg::kResultOk); }
    ~ControllerHandle() { ctl->terminate(); }
    ControllerHandle(const ControllerHandle&) = delete;
    ControllerHandle& operator=(const ControllerHandle&) = delete;
    ControllerHandle(ControllerHandle&&) = delete;
    ControllerHandle& operator=(ControllerHandle&&) = delete;

    std::string str(ParamID id, double v) const {
        Steinberg::Vst::String128 s{};
        REQUIRE(ctl->getParamStringByValue(id, v, s) == Steinberg::kResultOk);
        return toAscii(s);
    }
};

double expectedDefault(ParamID id) {
    switch (id) {
        case ::Profundum::kOutputGainId: return 1.0;
        case ::Profundum::kVoiceModeId: return 0.0;
        case ::Profundum::kPolyphonyId: return 3.0 / 7.0;
        case ::Profundum::kOutputCeilingId: return 11.0 / 12.0;
        default: return 0.5;  // the four macros, 100..103
    }
}

}  // namespace

TEST_CASE("Profundum_ParamDenormRoundTrip", "[profundum][params]") {
    ProcessorFixture fx;  // initialised, NOT prepared
    Krate::Test::ParameterChanges changes;
    const ::Profundum::Processor& proc = *fx.proc;

    SECTION("GridValues") {
        constexpr std::array<double, 5> kGrid{0.0, 0.25, 0.5, 0.75, 1.0};
        constexpr std::array<int, 5> kExpectedMode{0, 0, 1, 1, 1};
        constexpr std::array<int, 5> kExpectedPoly{1, 3, 5, 6, 8};

        for (std::size_t k = 0; k < kGrid.size(); ++k) {
            const double v = kGrid[k];
            CAPTURE(v);

            // Output gain: exact 0 / 1 at the ends, the -60 dB taper in between.
            sendOne(fx, changes, ::Profundum::kOutputGainId, v);
            const float g = proc.globalParamsForTest().outputGain.load(std::memory_order_relaxed);
            CAPTURE(g);
            REQUIRE(g >= 0.0f);
            REQUIRE(g <= 1.0f);
            if (k == 0) {
                REQUIRE(g == 0.0f);
            } else if (k == kGrid.size() - 1) {
                REQUIRE(g == 1.0f);
            } else {
                const double e = static_cast<double>(
                    static_cast<float>(std::pow(10.0, (-60.0 + 60.0 * v) / 20.0)));
                REQUIRE(std::abs(static_cast<double>(g) - e) <= 1e-5 * e);
            }

            // Voice mode: v >= 0.5 -> Poly.
            sendOne(fx, changes, ::Profundum::kVoiceModeId, v);
            REQUIRE(proc.globalParamsForTest().voiceMode.load(std::memory_order_relaxed) ==
                    kExpectedMode[k]);

            // Polyphony: "1".."8", round-to-nearest of v * 7 + 1.
            sendOne(fx, changes, ::Profundum::kPolyphonyId, v);
            REQUIRE(proc.globalParamsForTest().polyphony.load(std::memory_order_relaxed) ==
                    kExpectedPoly[k]);

            // Ceiling: [-12, 0] dBTP, linear.
            sendOne(fx, changes, ::Profundum::kOutputCeilingId, v);
            const float c =
                proc.globalParamsForTest().outputCeilingDb.load(std::memory_order_relaxed);
            REQUIRE(std::abs(static_cast<double>(c) - (-12.0 + 12.0 * v)) <= 1e-5);

            // Macros: identity.
            for (int i = 0; i < ::Profundum::kNumMacroParams; ++i) {
                const auto idx = static_cast<std::size_t>(i);
                sendOne(fx, changes, kMacroIds[idx], v);
                const float m = ::Profundum::macroField(proc.macroParamsForTest(), i)
                                    .load(std::memory_order_relaxed);
                CAPTURE(i, m);
                REQUIRE(std::abs(static_cast<double>(m) - v) <= 1e-6);
            }
        }
    }

    SECTION("LastPointWins") {
        MultiParamChanges multi;  // two points per queue: the shared single-point mock cannot carry them
        multi.reserve(1);
        {
            auto& q = multi.addQueue(::Profundum::kMacroWeightId);
            q.addTestPoint(0, 0.1);
            q.addTestPoint(100, 0.9);
        }
        REQUIRE(fx.processNoOutputs(&multi) == Steinberg::kResultOk);
        const float w = proc.macroParamsForTest().weight.load(std::memory_order_relaxed);
        REQUIRE(std::abs(static_cast<double>(w) - 0.9) <= 1e-6);

        multi.clear();
        {
            auto& q = multi.addQueue(::Profundum::kOutputCeilingId);
            q.addTestPoint(0, 0.1);
            q.addTestPoint(100, 0.9);
        }
        REQUIRE(fx.processNoOutputs(&multi) == Steinberg::kResultOk);
        const float c = proc.globalParamsForTest().outputCeilingDb.load(std::memory_order_relaxed);
        REQUIRE(std::abs(static_cast<double>(c) - (-12.0 + 12.0 * 0.9)) <= 1e-5);
    }

    SECTION("UnregisteredIds") {
        const Snapshot before = snapshot(proc);
        constexpr std::array<ParamID, 6> kUnregistered{4, 99, 104, 199, 200, 1000};
        for (const ParamID id : kUnregistered) {
            CAPTURE(id);
            sendOne(fx, changes, id, 0.3);
            REQUIRE(snapshot(proc) == before);
        }
    }

    SECTION("NaNIgnored") {
        // Known non-default values first, so a stray store of a default is visible too.
        sendOne(fx, changes, ::Profundum::kOutputGainId, 0.5);
        sendOne(fx, changes, ::Profundum::kVoiceModeId, 1.0);
        sendOne(fx, changes, ::Profundum::kPolyphonyId, 1.0);
        sendOne(fx, changes, ::Profundum::kOutputCeilingId, 0.25);
        for (const ParamID id : kMacroIds) {
            sendOne(fx, changes, id, 0.75);
        }
        const Snapshot known = snapshot(proc);
        REQUIRE(known.voiceMode == 1);
        REQUIRE(known.polyphony == 8);

        const double nan = makeNaND();
        const double posInf = static_cast<double>(makePosInf());
        for (const ParamID id : kAllIds) {
            CAPTURE(id);
            sendOne(fx, changes, id, nan);
            REQUIRE(snapshot(proc) == known);
            sendOne(fx, changes, id, posInf);
            REQUIRE(snapshot(proc) == known);
        }
    }

    SECTION("RegisteredInfo") {
        const ControllerHandle h;
        REQUIRE(h.ctl->getParameterCount() == 8);

        std::array<bool, kAllIds.size()> seen{};
        for (Steinberg::int32 i = 0; i < h.ctl->getParameterCount(); ++i) {
            Steinberg::Vst::ParameterInfo info{};
            REQUIRE(h.ctl->getParameterInfo(i, info) == Steinberg::kResultOk);
            CAPTURE(i, info.id, info.defaultNormalizedValue);
            bool known = false;
            for (std::size_t k = 0; k < kAllIds.size(); ++k) {
                if (kAllIds[k] == info.id) {
                    REQUIRE_FALSE(seen[k]);
                    seen[k] = true;
                    known = true;
                }
            }
            REQUIRE(known);
            REQUIRE(std::abs(info.defaultNormalizedValue - expectedDefault(info.id)) <= 1e-12);
            if (info.id == ::Profundum::kPolyphonyId) {
                REQUIRE(info.stepCount == 7);
            }
            if (info.id == ::Profundum::kVoiceModeId) {
                REQUIRE(info.stepCount == 1);
            }
        }
        for (const bool s : seen) {
            REQUIRE(s);
        }

        for (int k = 0; k <= 7; ++k) {
            CAPTURE(k);
            REQUIRE(h.str(::Profundum::kPolyphonyId, static_cast<double>(k) / 7.0) ==
                    std::to_string(k + 1));
        }
        REQUIRE(h.str(::Profundum::kVoiceModeId, 0.0) == "Mono");
        REQUIRE(h.str(::Profundum::kVoiceModeId, 1.0) == "Poly");
    }

    SECTION("Display") {
        const ControllerHandle h;
        REQUIRE(h.str(::Profundum::kOutputGainId, 0.0) == "-inf dB");
        REQUIRE(h.str(::Profundum::kOutputGainId, 1.0) == "0.0 dB");
        REQUIRE(h.str(::Profundum::kOutputGainId, 0.9) == "-6.0 dB");
        REQUIRE(h.str(::Profundum::kOutputCeilingId, 11.0 / 12.0) == "-1.0 dBTP");
        REQUIRE(h.str(::Profundum::kMacroWeightId, 0.5) == "50%");
    }
}
