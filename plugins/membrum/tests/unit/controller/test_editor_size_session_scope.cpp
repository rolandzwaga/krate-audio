// ==============================================================================
// Session-scope tests for kEditorSizeId (Phase 6, T022)
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-001, FR-040)
// ==============================================================================

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "ui/editor_size_policy.h"

#include "public.sdk/source/common/memorystream.h"

#include <catch2/catch_test_macros.hpp>

using namespace Membrum;

TEST_CASE("kEditorSizeId registered as StringListParameter (Default/Compact)",
          "[editor_size_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    auto* param = ctl.getParameterObject(kEditorSizeId);
    REQUIRE(param != nullptr);

    const auto& info = param->getInfo();
    REQUIRE(info.stepCount == 1);
    REQUIRE((info.flags & Steinberg::Vst::ParameterInfo::kCanAutomate) != 0);
    REQUIRE((info.flags & Steinberg::Vst::ParameterInfo::kIsList) != 0);
    REQUIRE(info.defaultNormalizedValue == 0.0);

    ctl.terminate();
}

TEST_CASE("kEditorSizeId responds to setParamNormalized", "[editor_size_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    REQUIRE(ctl.setParamNormalized(kEditorSizeId, 1.0) == Steinberg::kResultOk);
    REQUIRE(ctl.getParamNormalized(kEditorSizeId) == 1.0);

    ctl.terminate();
}

// ----------------------------------------------------------------------------
// T022: setComponentState resets kEditorSizeId to Default regardless of blob.
// ----------------------------------------------------------------------------
TEST_CASE("Controller::setComponentState resets kEditorSizeId to Default (T022)",
          "[editor_size_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    // Prime the controller into Compact so we can prove the reset happens.
    REQUIRE(ctl.setParamNormalized(kEditorSizeId, 1.0) == Steinberg::kResultOk);
    REQUIRE(ctl.getParamNormalized(kEditorSizeId) == 1.0);

    Processor p;
    REQUIRE(p.initialize(nullptr) == Steinberg::kResultOk);
    Steinberg::MemoryStream blob;
    REQUIRE(p.getState(&blob) == Steinberg::kResultOk);
    blob.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    REQUIRE(ctl.setComponentState(&blob) == Steinberg::kResultOk);
    REQUIRE(ctl.getParamNormalized(kEditorSizeId) == 0.0);

    p.terminate();
    ctl.terminate();
}

// ----------------------------------------------------------------------------
// T022: Processor::getState does NOT write kEditorSizeId bytes. We rely on the
// known v6 state size (9330 bytes) -- if kEditorSizeId were appended as a
// float64 the blob would be 9338 bytes (or similar). See test_ui_mode for
// the shared v6 layout documentation.
// ----------------------------------------------------------------------------
TEST_CASE("Processor::getState does NOT write kEditorSizeId bytes (T022)",
          "[editor_size_session]")
{
    Processor p;
    REQUIRE(p.initialize(nullptr) == Steinberg::kResultOk);

    Steinberg::MemoryStream ms;
    REQUIRE(p.getState(&ms) == Steinberg::kResultOk);

    Steinberg::int64 pos = 0;
    ms.tell(&pos);
    // Phase 6 spec: session-scoped params add ZERO bytes to the state blob.
    // A change here indicates kEditorSizeId or kUiModeId leaked into persist.
    // Phase 6 (spec 141) appends 160 x float64 per-pad macros = 1280 bytes.
    REQUIRE(pos == Steinberg::int64{9330 + 1280});

    p.terminate();
}

// ----------------------------------------------------------------------------
// T022: kit preset load does NOT restore editor size (FR-040).
//
// Strategy: build a minimal v4 kit preset blob by hand (kitPresetStateProvider
// requires an IComponentHandler which the bare-controller test harness does
// not provide), set kEditorSizeId to Compact, run kitPresetLoadProvider, and
// assert kEditorSizeId is still Compact afterwards.
// ----------------------------------------------------------------------------
TEST_CASE("Kit preset load does NOT restore kEditorSizeId (T022)",
          "[editor_size_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    // User has picked Compact before loading a preset.
    REQUIRE(ctl.setParamNormalized(kEditorSizeId, 1.0) == Steinberg::kResultOk);

    // Build a minimal v4 kit preset blob. Layout mirrors the one produced by
    // Controller::kitPresetStateProvider(). We supply zeros for everything --
    // the loader clamps out-of-range values back into its defaults.
    Steinberg::MemoryStream ms;
    auto writeI32 = [&](Steinberg::int32 v) {
        ms.write(&v, sizeof(v), nullptr);
    };
    auto writeF64 = [&](double v) { ms.write(&v, sizeof(v), nullptr); };
    auto writeU8 = [&](std::uint8_t v) { ms.write(&v, sizeof(v), nullptr); };

    writeI32(4);   // version
    writeI32(8);   // maxPolyphony
    writeI32(0);   // stealPolicy

    for (int pad = 0; pad < kNumPads; ++pad)
    {
        writeI32(0); // exciterType
        writeI32(0); // bodyModel
        for (int j = 0; j < 34; ++j)
            writeF64(0.5);
        writeU8(0); // chokeGroup
        writeU8(0); // outputBus
    }

    ms.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    REQUIRE(ctl.kitPresetLoadProvider(&ms));

    // Editor size must be untouched -- preset only carries per-pad state.
    REQUIRE(ctl.getParamNormalized(kEditorSizeId) == 1.0);

    ctl.terminate();
}

TEST_CASE("EditorSizePolicy: default size is 1280x800", "[editor_size_session]")
{
    REQUIRE(UI::kDefaultWidth  == 1280);
    REQUIRE(UI::kDefaultHeight == 800);
    REQUIRE(UI::kCompactWidth  == 1024);
    REQUIRE(UI::kCompactHeight == 640);
    REQUIRE(UI::templateNameFor(UI::EditorSize::Default) == std::string("EditorDefault"));
    REQUIRE(UI::templateNameFor(UI::EditorSize::Compact) == std::string("EditorCompact"));
}
