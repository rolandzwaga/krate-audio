// ==============================================================================
// Kit preset uiMode + macros round-trip tests (Phase 6 / T050)
// ==============================================================================
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-040, FR-041, FR-070..FR-072)
// Verifies:
//   - Kit preset blob v5 carries `uiMode` (int32 after globals) and per-pad
//     macros (5 float64 per pad appended to each pad row).
//   - Loading a v5 kit blob with uiMode=Extended sets kUiModeId.
//   - Loading a v5 kit blob with uiMode=Acoustic also sets kUiModeId.
//   - Loading a v4 kit blob (no uiMode/macros) leaves kUiModeId at session
//     default and assigns macros = 0.5 to every pad.
//   - All five macro fields round-trip through save -> load.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"

#include <cstdint>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

constexpr ParamID macroParamId(int padIndex, int macroOffset)
{
    return static_cast<ParamID>(Membrum::padParamId(padIndex, macroOffset));
}

} // namespace

// ==============================================================================
// T050: Kit preset v5 blob carries uiMode and round-trips it.
// ==============================================================================

TEST_CASE("Kit preset v5: uiMode=Extended round-trips through save/load",
          "[membrum][preset][kit_preset][kit_uimode]")
{
    Membrum::Controller saver;
    REQUIRE(saver.initialize(nullptr) == kResultOk);

    // Set uiMode=Extended (1.0 normalised, since kUiModeId is a 2-step list).
    saver.setParamNormalized(Membrum::kUiModeId, 1.0);

    IBStream* stream = saver.kitPresetStateProvider();
    REQUIRE(stream != nullptr);

    // Load into a fresh controller whose uiMode is Acoustic by default.
    Membrum::Controller loader;
    REQUIRE(loader.initialize(nullptr) == kResultOk);
    CHECK(loader.getParamNormalized(Membrum::kUiModeId) == Approx(0.0));

    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    bool ok = loader.kitPresetLoadProvider(stream);
    CHECK(ok);

    CHECK(loader.getParamNormalized(Membrum::kUiModeId) == Approx(1.0));

    stream->release();
    REQUIRE(loader.terminate() == kResultOk);
    REQUIRE(saver.terminate() == kResultOk);
}

TEST_CASE("Kit preset v5: per-pad macros round-trip through save/load",
          "[membrum][preset][kit_preset][kit_uimode]")
{
    Membrum::Controller saver;
    REQUIRE(saver.initialize(nullptr) == kResultOk);

    // Set distinctive macro values on a few pads.
    struct PadMacros {
        int   pad;
        double tightness;
        double brightness;
        double bodySize;
        double punch;
        double complexity;
    };
    const PadMacros expected[] = {
        { 0,  0.10, 0.20, 0.30, 0.40, 0.50 },
        { 7,  0.15, 0.25, 0.35, 0.45, 0.55 },
        { 15, 0.60, 0.70, 0.80, 0.90, 0.95 },
        { 31, 0.05, 0.95, 0.50, 0.05, 0.95 },
    };
    for (const auto& e : expected) {
        saver.setParamNormalized(macroParamId(e.pad, Membrum::kPadMacroTightness),  e.tightness);
        saver.setParamNormalized(macroParamId(e.pad, Membrum::kPadMacroBrightness), e.brightness);
        saver.setParamNormalized(macroParamId(e.pad, Membrum::kPadMacroBodySize),   e.bodySize);
        saver.setParamNormalized(macroParamId(e.pad, Membrum::kPadMacroPunch),      e.punch);
        saver.setParamNormalized(macroParamId(e.pad, Membrum::kPadMacroComplexity), e.complexity);
    }

    IBStream* stream = saver.kitPresetStateProvider();
    REQUIRE(stream != nullptr);

    Membrum::Controller loader;
    REQUIRE(loader.initialize(nullptr) == kResultOk);

    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    bool ok = loader.kitPresetLoadProvider(stream);
    CHECK(ok);

    for (const auto& e : expected) {
        INFO("pad " << e.pad);
        CHECK(loader.getParamNormalized(macroParamId(e.pad, Membrum::kPadMacroTightness))
              == Approx(e.tightness).margin(1e-6));
        CHECK(loader.getParamNormalized(macroParamId(e.pad, Membrum::kPadMacroBrightness))
              == Approx(e.brightness).margin(1e-6));
        CHECK(loader.getParamNormalized(macroParamId(e.pad, Membrum::kPadMacroBodySize))
              == Approx(e.bodySize).margin(1e-6));
        CHECK(loader.getParamNormalized(macroParamId(e.pad, Membrum::kPadMacroPunch))
              == Approx(e.punch).margin(1e-6));
        CHECK(loader.getParamNormalized(macroParamId(e.pad, Membrum::kPadMacroComplexity))
              == Approx(e.complexity).margin(1e-6));
    }

    stream->release();
    REQUIRE(loader.terminate() == kResultOk);
    REQUIRE(saver.terminate() == kResultOk);
}

// ==============================================================================
// T050: Loading v4 kit blob leaves uiMode unchanged and sets macros = 0.5.
// ==============================================================================

TEST_CASE("Kit preset v4 (no uiMode): uiMode unchanged, macros default to 0.5",
          "[membrum][preset][kit_preset][kit_uimode]")
{
    // Build a minimal v4 kit blob manually: version=4, two int32 globals, then
    // 32 pad rows of (exciter int32 + body int32 + 34 float64 + 2 uint8) =
    // 4 + 4 + 4 + 4 + 32*(4+4+34*8+1+1) = 12 + 32*282 = 12 + 9024 = 9036.
    auto* stream = new MemoryStream();

    int32 version = 4;
    stream->write(&version, sizeof(version), nullptr);

    int32 maxPoly = 8;
    stream->write(&maxPoly, sizeof(maxPoly), nullptr);

    int32 stealPolicy = 0;
    stream->write(&stealPolicy, sizeof(stealPolicy), nullptr);

    for (int pad = 0; pad < Membrum::kNumPads; ++pad) {
        int32 et = 0;
        stream->write(&et, sizeof(et), nullptr);
        int32 bm = 0;
        stream->write(&bm, sizeof(bm), nullptr);
        for (int i = 0; i < 34; ++i) {
            double v = 0.5;
            stream->write(&v, sizeof(v), nullptr);
        }
        std::uint8_t cg = 0;
        stream->write(&cg, sizeof(cg), nullptr);
        std::uint8_t ob = 0;
        stream->write(&ob, sizeof(ob), nullptr);
    }

    Membrum::Controller loader;
    REQUIRE(loader.initialize(nullptr) == kResultOk);

    // Move uiMode away from default to verify loader does NOT touch it.
    loader.setParamNormalized(Membrum::kUiModeId, 1.0);
    const double uiModeBefore = loader.getParamNormalized(Membrum::kUiModeId);

    // Pre-set a macro to a non-default value to verify it is reset by v4 load.
    loader.setParamNormalized(macroParamId(3, Membrum::kPadMacroTightness), 0.123);

    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    bool ok = loader.kitPresetLoadProvider(stream);
    CHECK(ok);

    // uiMode must NOT have been changed by a v4 load.
    CHECK(loader.getParamNormalized(Membrum::kUiModeId) == Approx(uiModeBefore));

    // All macro params must be 0.5 (neutral default) after a v4 load.
    for (int pad = 0; pad < Membrum::kNumPads; ++pad) {
        INFO("pad " << pad);
        CHECK(loader.getParamNormalized(macroParamId(pad, Membrum::kPadMacroTightness))
              == Approx(0.5));
        CHECK(loader.getParamNormalized(macroParamId(pad, Membrum::kPadMacroBrightness))
              == Approx(0.5));
        CHECK(loader.getParamNormalized(macroParamId(pad, Membrum::kPadMacroBodySize))
              == Approx(0.5));
        CHECK(loader.getParamNormalized(macroParamId(pad, Membrum::kPadMacroPunch))
              == Approx(0.5));
        CHECK(loader.getParamNormalized(macroParamId(pad, Membrum::kPadMacroComplexity))
              == Approx(0.5));
    }

    stream->release();
    REQUIRE(loader.terminate() == kResultOk);
}

// ==============================================================================
// T051: Per-pad preset load preserves outputBus and couplingAmount on the pad,
// and does not modify other pads' macros. Per-pad presets do NOT carry
// outputBus/couplingAmount/macros.
// ==============================================================================

TEST_CASE("Per-pad preset load preserves outputBus and couplingAmount",
          "[membrum][preset][pad_preset][pad_isolation]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    constexpr int kPad = 5;

    // Establish distinctive outputBus / couplingAmount / macro values on pad 5.
    const auto busId    = static_cast<ParamID>(Membrum::padParamId(kPad, Membrum::kPadOutputBus));
    const auto couplingId = static_cast<ParamID>(Membrum::padParamId(kPad, Membrum::kPadCouplingAmount));
    const auto macroT   = macroParamId(kPad, Membrum::kPadMacroTightness);
    const auto macroP   = macroParamId(kPad, Membrum::kPadMacroPunch);

    controller.setParamNormalized(busId,      4.0 / 15.0);
    controller.setParamNormalized(couplingId, 0.823);
    controller.setParamNormalized(macroT,     0.42);
    controller.setParamNormalized(macroP,     0.91);

    const double busBefore     = controller.getParamNormalized(busId);
    const double couplingBefore = controller.getParamNormalized(couplingId);
    const double macroTBefore  = controller.getParamNormalized(macroT);
    const double macroPBefore  = controller.getParamNormalized(macroP);

    // Build a per-pad preset blob with distinct sound values.
    auto* stream = new MemoryStream();
    int32 version = 1;
    stream->write(&version, sizeof(version), nullptr);
    int32 et = 1;
    stream->write(&et, sizeof(et), nullptr);
    int32 bm = 2;
    stream->write(&bm, sizeof(bm), nullptr);
    for (int i = 0; i < 34; ++i) {
        double v = 0.7;  // distinctive sound value
        stream->write(&v, sizeof(v), nullptr);
    }
    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    // Select pad 5 and load the per-pad preset.
    controller.setParamNormalized(Membrum::kSelectedPadId,
        static_cast<double>(kPad) / static_cast<double>(Membrum::kNumPads - 1));

    bool ok = controller.padPresetLoadProvider(stream);
    CHECK(ok);

    // outputBus, couplingAmount, and macros for pad 5 must be unchanged.
    CHECK(controller.getParamNormalized(busId)      == Approx(busBefore).margin(1e-9));
    CHECK(controller.getParamNormalized(couplingId) == Approx(couplingBefore).margin(1e-9));
    CHECK(controller.getParamNormalized(macroT)     == Approx(macroTBefore).margin(1e-9));
    CHECK(controller.getParamNormalized(macroP)     == Approx(macroPBefore).margin(1e-9));

    // Sound parameter (material) for pad 5 SHOULD be the loaded value (0.7).
    const auto matId = static_cast<ParamID>(Membrum::padParamId(kPad, Membrum::kPadMaterial));
    CHECK(controller.getParamNormalized(matId) == Approx(0.7).margin(1e-6));

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}
