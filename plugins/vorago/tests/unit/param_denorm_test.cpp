// ==============================================================================
// Vorago - parameter denormalisation tests (SC-009)
// ==============================================================================
// T009: pack-level section "PackHandlersDirect" - the GlobalParams / MacroParams
// handlers, clamps, registration (incl. the P-1 polyphony default) and
// formatting, exercised directly with no processor.
// T012: section "ThroughProcess" - the same denormalisation reached through
// Processor::process() (FR-043 latch: last point wins, ID-band routing).
// T017: section "ControllerRegistersFourteen" - Controller::initialize registers
// exactly the 14 IDs with their defaults, and getParamStringByValue formats them.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "parameters/global_params.h"
#include "parameters/macro_params.h"
#include "plugin_ids.h"
#include "vorago_test_fixture.h"

#include <vst_param_changes.h>

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <array>
#include <bit>
#include <cstdint>
#include <set>
#include <string>

namespace {

std::string toAsciiString(const Steinberg::Vst::String128 s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

std::array<std::uint32_t, 12> snapshotMacroBits(const ::Vorago::MacroParams& m) {
    std::array<std::uint32_t, 12> bits{};
    for (int i = 0; i < 12; ++i) {
        bits[static_cast<std::size_t>(i)] =
            std::bit_cast<std::uint32_t>(::Vorago::macroField(m, i).load());
    }
    return bits;
}

// One single-point change delivered through a parameter-only process() call.
void sendThroughProcess(VoragoTest::ProcessorFixture& fx, Steinberg::Vst::ParamID id, double v) {
    Krate::Test::ParameterChanges pc;
    pc.addChange(id, v);
    REQUIRE(fx.processNoOutputs(&pc) == Steinberg::kResultOk);
}

Steinberg::Vst::ParamID macroId(int i) {
    return static_cast<Steinberg::Vst::ParamID>(::Vorago::kMacroDarknessId) +
           static_cast<Steinberg::Vst::ParamID>(i);
}

}  // namespace

TEST_CASE("Vorago_ParamDenormRoundTrip", "[vorago][params]") {
    using Catch::Approx;
    using namespace ::Vorago;

    SECTION("PackHandlersDirect") {
        constexpr std::array<double, 5> kValues{0.0, 0.25, 0.5, 0.75, 1.0};
        constexpr std::array<int, 5> kExpectedPoly{1, 2, 4, 5, 6};

        // ---- Global pack: master gain and polyphony denormalisation ----------
        GlobalParams g;
        for (std::size_t k = 0; k < kValues.size(); ++k) {
            const double v = kValues[k];
            handleGlobalParamChange(g, kMasterGainId, v);
            REQUIRE(g.masterGain.load() == Approx(2.0 * v).margin(1e-6));
            handleGlobalParamChange(g, kPolyphonyId, v);
            REQUIRE(g.polyphony.load() == kExpectedPoly[k]);
        }

        // Out-of-range master gain clamps into [0, 2].
        handleGlobalParamChange(g, kMasterGainId, 1.5);
        REQUIRE(g.masterGain.load() == 2.0f);
        handleGlobalParamChange(g, kMasterGainId, -0.5);
        REQUIRE(g.masterGain.load() == 0.0f);

        // clampPolyphony: the one conversion into the engine domain [1, 6].
        REQUIRE(clampPolyphony(-3) == 1u);
        REQUIRE(clampPolyphony(0) == 1u);
        REQUIRE(clampPolyphony(99) == 6u);
        REQUIRE(clampPolyphony(4) == 4u);

        // ---- Macro pack: initial values (FR-042 load-bearing initialisers) ---
        MacroParams m;
        for (int i = 0; i < 12; ++i) {
            const float expected = (i == 4) ? 0.5f : 0.0f;  // index 4 == Gravity
            REQUIRE(macroField(m, i).load() == expected);
        }
        REQUIRE(m.gravity.load() == 0.5f);

        // Each macro ID 100..111 maps to its field, identity in [0, 1].
        for (int i = 0; i < 12; ++i) {
            const auto id = static_cast<Steinberg::Vst::ParamID>(kMacroDarknessId) +
                            static_cast<Steinberg::Vst::ParamID>(i);
            for (const double v : kValues) {
                handleMacroParamChange(m, id, v);
                REQUIRE(macroField(m, i).load() == Approx(v).margin(1e-6));
            }
            handleMacroParamChange(m, id, 1.5);
            REQUIRE(macroField(m, i).load() == 1.0f);
        }

        // Unregistered in-band IDs change nothing (id > kMacroMassId early return).
        for (int i = 0; i < 12; ++i) {
            handleMacroParamChange(m, static_cast<Steinberg::Vst::ParamID>(kMacroDarknessId) +
                                          static_cast<Steinberg::Vst::ParamID>(i),
                                   0.1 + 0.05 * i);
        }
        const auto before = snapshotMacroBits(m);
        handleMacroParamChange(m, 150, 0.9);
        handleMacroParamChange(m, 199, 0.9);
        REQUIRE(snapshotMacroBits(m) == before);

        // ---- Registration: fourteen parameters, P-1 default detector --------
        Steinberg::Vst::ParameterContainer pc;
        registerGlobalParams(pc);
        registerMacroParams(pc);
        REQUIRE(pc.getParameterCount() == 14);

        auto* poly = pc.getParameter(kPolyphonyId);
        REQUIRE(poly != nullptr);
        REQUIRE(poly->getInfo().defaultNormalizedValue == Approx(0.6).margin(1e-9));
        REQUIRE(poly->toPlain(0.6) == 3.0);  // index 3 == four voices
        REQUIRE(poly->getInfo().stepCount == 5);

        auto* gain = pc.getParameter(kMasterGainId);
        REQUIRE(gain != nullptr);
        REQUIRE(gain->getInfo().defaultNormalizedValue == Approx(0.5).margin(1e-9));

        for (int i = 0; i < 12; ++i) {
            const auto id = static_cast<Steinberg::Vst::ParamID>(kMacroDarknessId) +
                            static_cast<Steinberg::Vst::ParamID>(i);
            auto* p = pc.getParameter(id);
            REQUIRE(p != nullptr);
            const double expected = (id == kMacroGravityId) ? 0.5 : 0.0;
            REQUIRE(p->getInfo().defaultNormalizedValue == Approx(expected).margin(1e-9));
        }

        // ---- Formatting ------------------------------------------------------
        Steinberg::Vst::String128 s{};
        REQUIRE(formatGlobalParam(kMasterGainId, 0.5, s) == Steinberg::kResultOk);
        REQUIRE(toAsciiString(s).rfind("0.0 dB", 0) == 0);

        Steinberg::Vst::String128 sPoly{};
        REQUIRE(formatGlobalParam(kPolyphonyId, 0.6, sPoly) == Steinberg::kResultFalse);

        Steinberg::Vst::String128 sFog{};
        REQUIRE(formatMacroParam(kMacroFogId, 0.25, sFog) == Steinberg::kResultOk);
        REQUIRE(toAsciiString(sFog) == "25%");
    }

    SECTION("ThroughProcess") {
        VoragoTest::ProcessorFixture fx;  // initialised, NOT prepared
        const GlobalParams& g = fx.proc->globalParamsForTest();
        const MacroParams& m = fx.proc->macroParamsForTest();

        constexpr std::array<double, 5> kValues{0.0, 0.25, 0.5, 0.75, 1.0};
        constexpr std::array<int, 5> kExpectedPoly{1, 2, 4, 5, 6};

        // ---- All 14 IDs x five normalized values -----------------------------
        for (std::size_t k = 0; k < kValues.size(); ++k) {
            const double v = kValues[k];
            sendThroughProcess(fx, kMasterGainId, v);
            REQUIRE(g.masterGain.load() == Approx(2.0 * v).margin(1e-6));
            sendThroughProcess(fx, kPolyphonyId, v);
            REQUIRE(g.polyphony.load() == kExpectedPoly[k]);
            for (int i = 0; i < 12; ++i) {
                sendThroughProcess(fx, macroId(i), v);
                REQUIRE(macroField(m, i).load() == Approx(v).margin(1e-6));
            }
        }

        // ---- Multi-point queue: the LAST point wins --------------------------
        {
            VoragoTest::MultiParamChanges mpc;
            auto& q = mpc.addQueue(kMacroFogId);
            q.addTestPoint(0, 0.1);
            q.addTestPoint(100, 0.9);
            REQUIRE(fx.processNoOutputs(&mpc) == Steinberg::kResultOk);
            REQUIRE(m.fog.load() == Approx(0.9f).margin(1e-6));
        }

        // ---- Unregistered in-band IDs 150 / 199 change nothing ---------------
        for (int i = 0; i < 12; ++i) {
            sendThroughProcess(fx, macroId(i), 0.1 + 0.05 * i);
        }
        sendThroughProcess(fx, kMasterGainId, 0.3);
        sendThroughProcess(fx, kPolyphonyId, 0.4);
        const auto macrosBefore = snapshotMacroBits(m);
        const auto gainBefore = std::bit_cast<std::uint32_t>(g.masterGain.load());
        const int polyBefore = g.polyphony.load();
        // Non-vacuity: the snapshot really holds non-defaults.
        REQUIRE(macroField(m, 0).load() == Approx(0.1f).margin(1e-6));
        REQUIRE(polyBefore == 3);

        sendThroughProcess(fx, 150, 0.9);
        sendThroughProcess(fx, 199, 0.9);
        REQUIRE(snapshotMacroBits(m) == macrosBefore);
        REQUIRE(std::bit_cast<std::uint32_t>(g.masterGain.load()) == gainBefore);
        REQUIRE(g.polyphony.load() == polyBefore);

        // ---- ID 200 (outside both packs) changes nothing ---------------------
        sendThroughProcess(fx, 200, 0.9);
        REQUIRE(snapshotMacroBits(m) == macrosBefore);
        REQUIRE(std::bit_cast<std::uint32_t>(g.masterGain.load()) == gainBefore);
        REQUIRE(g.polyphony.load() == polyBefore);
    }

    SECTION("ControllerRegistersFourteen") {  // SC-009 controller arm
        auto controller = Steinberg::owned(new ::Vorago::Controller());
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
        REQUIRE(controller->getParameterCount() == 14);

        std::set<Steinberg::Vst::ParamID> expectedIds{kMasterGainId, kPolyphonyId};
        for (int i = 0; i < 12; ++i) {
            expectedIds.insert(macroId(i));
        }
        REQUIRE(expectedIds.size() == 14u);
        for (const auto id : expectedIds) {
            INFO("id " << id);
            REQUIRE(controller->getParameterObject(id) != nullptr);
        }

        // FR-041: exactly the fourteen, no soft-limit, no extra.
        std::set<Steinberg::Vst::ParamID> registeredIds;
        for (Steinberg::int32 i = 0; i < 14; ++i) {
            Steinberg::Vst::ParameterInfo info{};
            REQUIRE(controller->getParameterInfo(i, info) == Steinberg::kResultOk);
            registeredIds.insert(info.id);
        }
        REQUIRE(registeredIds == expectedIds);

        // Defaults.
        auto* gain = controller->getParameterObject(kMasterGainId);
        REQUIRE(gain->getInfo().defaultNormalizedValue == Approx(0.5).margin(1e-9));
        REQUIRE(gain->getInfo().defaultNormalizedValue * 2.0 == Approx(1.0).margin(1e-9));
        auto* poly = controller->getParameterObject(kPolyphonyId);
        REQUIRE(poly->toPlain(poly->getInfo().defaultNormalizedValue) == 3.0);  // 4 voices
        for (int i = 0; i < 12; ++i) {
            const auto id = macroId(i);
            const double expected = (id == kMacroGravityId) ? 0.5 : 0.0;
            INFO("macro id " << id);
            REQUIRE(controller->getParameterObject(id)->getInfo().defaultNormalizedValue ==
                    Approx(expected).margin(1e-9));
        }

        // Formatting through the controller.
        Steinberg::Vst::String128 sGain{};
        REQUIRE(controller->getParamStringByValue(kMasterGainId, 0.5, sGain) ==
                Steinberg::kResultOk);
        REQUIRE(toAsciiString(sGain).rfind("0.0 dB", 0) == 0);

        Steinberg::Vst::String128 sPoly{};
        REQUIRE(controller->getParamStringByValue(kPolyphonyId, 0.6, sPoly) ==
                Steinberg::kResultOk);
        REQUIRE(toAsciiString(sPoly) == "4");

        Steinberg::Vst::String128 sFog{};
        REQUIRE(controller->getParamStringByValue(kMacroFogId, 0.25, sFog) ==
                Steinberg::kResultOk);
        REQUIRE(toAsciiString(sFog) == "25%");

        REQUIRE(controller->terminate() == Steinberg::kResultOk);
    }
}
