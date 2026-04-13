// ==============================================================================
// Session-scope tests for kUiModeId (Phase 6, T021)
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-030, FR-033, FR-081)
// ==============================================================================
//
// kUiModeId must be:
//   - Registered as a StringListParameter { "Acoustic", "Extended" }, default Acoustic
//   - Automatable (responds to setParamNormalized)
//   - NOT written to IBStream by Processor::getState
//   - Reset to Acoustic on Controller::setComponentState() regardless of blob content
// ==============================================================================

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "ui/ui_mode.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <catch2/catch_test_macros.hpp>

using namespace Membrum;

namespace {

// Read all bytes from a MemoryStream into a vector for inspection.
std::vector<uint8_t> drainStream(Steinberg::MemoryStream& ms)
{
    Steinberg::int64 pos = 0;
    ms.tell(&pos);
    const auto size = static_cast<std::size_t>(pos);
    ms.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<uint8_t> out(size);
    if (size > 0) {
        Steinberg::int32 got = 0;
        ms.read(out.data(), static_cast<Steinberg::int32>(size), &got);
    }
    return out;
}

} // namespace

TEST_CASE("kUiModeId registered as StringListParameter (Acoustic/Extended)",
          "[ui_mode_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    auto* param = ctl.getParameterObject(kUiModeId);
    REQUIRE(param != nullptr);

    const auto& info = param->getInfo();
    REQUIRE(info.stepCount == 1);  // 2 choices -> stepCount 1
    REQUIRE((info.flags & Steinberg::Vst::ParameterInfo::kCanAutomate) != 0);
    REQUIRE((info.flags & Steinberg::Vst::ParameterInfo::kIsList) != 0);

    // Default normalized value = 0 -> Acoustic
    REQUIRE(info.defaultNormalizedValue == 0.0);

    ctl.terminate();
}

TEST_CASE("kUiModeId responds to setParamNormalized (automatable)",
          "[ui_mode_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    REQUIRE(ctl.setParamNormalized(kUiModeId, 1.0) == Steinberg::kResultOk);
    REQUIRE(ctl.getParamNormalized(kUiModeId) == 1.0);
    REQUIRE(UI::uiModeFromNormalized(1.0f) == UI::UiMode::Extended);

    REQUIRE(ctl.setParamNormalized(kUiModeId, 0.0) == Steinberg::kResultOk);
    REQUIRE(ctl.getParamNormalized(kUiModeId) == 0.0);

    ctl.terminate();
}

// ----------------------------------------------------------------------------
// T021: setState always resets kUiModeId to Acoustic regardless of blob content.
// ----------------------------------------------------------------------------
TEST_CASE("Controller::setComponentState resets kUiModeId to Acoustic (T021)",
          "[ui_mode_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    // Prime the controller into Extended so we can prove the reset happens.
    REQUIRE(ctl.setParamNormalized(kUiModeId, 1.0) == Steinberg::kResultOk);
    REQUIRE(ctl.getParamNormalized(kUiModeId) == 1.0);

    // Build a minimal v6 state blob by producing one from a fresh Processor.
    Processor p;
    REQUIRE(p.initialize(nullptr) == Steinberg::kResultOk);
    Steinberg::MemoryStream blob;
    REQUIRE(p.getState(&blob) == Steinberg::kResultOk);
    blob.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    // setComponentState must unconditionally reset kUiModeId.
    REQUIRE(ctl.setComponentState(&blob) == Steinberg::kResultOk);
    REQUIRE(ctl.getParamNormalized(kUiModeId) == 0.0);

    p.terminate();
    ctl.terminate();
}

// ----------------------------------------------------------------------------
// T021: kit preset load with "uiMode":"Extended" drives kUiModeId to Extended.
// Here we exercise the Controller-visible preset-load code path by simulating
// the callback: the preset code calls setParamNormalized(kUiModeId, 1.0) when
// the JSON contains "uiMode":"Extended". Full JSON wiring lives in Phase 5 /
// T055; this test pins the contract the callback must satisfy.
// ----------------------------------------------------------------------------
TEST_CASE("Kit preset uiMode=Extended triggers kUiModeId change via callback (T021)",
          "[ui_mode_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    // Simulated preset-load callback behaviour (Phase 5 T055 will call
    // setParamNormalized on the UI thread when the JSON has "uiMode":"Extended").
    auto presetLoadCallback = [&ctl](const std::string& uiModeValue) {
        if (uiModeValue == "Extended")
            ctl.setParamNormalized(kUiModeId, 1.0);
        else if (uiModeValue == "Acoustic")
            ctl.setParamNormalized(kUiModeId, 0.0);
    };

    presetLoadCallback("Extended");
    REQUIRE(ctl.getParamNormalized(kUiModeId) == 1.0);
    presetLoadCallback("Acoustic");
    REQUIRE(ctl.getParamNormalized(kUiModeId) == 0.0);

    ctl.terminate();
}

TEST_CASE("Processor::getState does NOT write kUiModeId bytes", "[ui_mode_session]")
{
    Processor p;
    REQUIRE(p.initialize(nullptr) == Steinberg::kResultOk);

    Steinberg::MemoryStream ms;
    REQUIRE(p.getState(&ms) == Steinberg::kResultOk);
    auto bytes = drainStream(ms);

    // kUiModeId is session-scoped; the state blob is Phase 5 layout (pads,
    // coupling, overrides). We can't easily check "not present" generically
    // but we can check the blob does not contain the raw int32 280
    // (==kUiModeId) as a standalone tag. This is a sanity check -- the
    // authoritative assertion is just that state size matches v5 layout (no
    // extra 2 x float64 for kUiModeId/kEditorSizeId appended).
    //
    // v5 body (no overrides) = 4 version + 4 maxPoly + 4 stealPolicy
    //   + 32*(8 selector + 34*8 + 2 uint8) = 32*282 = 9024
    //   + 4 selectedPad + 4*8 globals + 32*8 pad-coupling + 2 overrideCount
    //   = 12 + 9024 + 4 + 32 + 256 + 2 = 9330.
    // Phase 6 (spec 141) appends 160 x float64 per-pad macros = 1280 bytes.
    // If kUiModeId/kEditorSizeId were appended as 2 x float64 it would be +16.
    // Phase 6 session-scoped params must NOT appear in the state blob.
    REQUIRE(bytes.size() == std::size_t{9330 + 1280});

    p.terminate();
}
