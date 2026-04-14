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

#include <string>
#include <vector>

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
// T022 (post-refactor): kit preset now carries session fields (uiMode +
// editorSize) via the hasSession flag. After the state codec unification,
// the kit preset load path DOES restore kEditorSizeId to whatever the
// preset captured. This guards that behavior: save preset with Compact,
// flip user state to Default, reload, expect Compact.
// ----------------------------------------------------------------------------
TEST_CASE("Kit preset load restores kEditorSizeId (unified state codec, T022)",
          "[editor_size_session]")
{
    Controller saver;
    REQUIRE(saver.initialize(nullptr) == Steinberg::kResultOk);

    // Capture Compact into a kit preset.
    REQUIRE(saver.setParamNormalized(kEditorSizeId, 1.0) == Steinberg::kResultOk);
    Steinberg::IBStream* presetStream = saver.kitPresetStateProvider();
    REQUIRE(presetStream != nullptr);

    // Load into a fresh controller that starts at Default.
    Controller loader;
    REQUIRE(loader.initialize(nullptr) == Steinberg::kResultOk);
    REQUIRE(loader.getParamNormalized(kEditorSizeId) == 0.0);

    presetStream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(loader.kitPresetLoadProvider(presetStream));

    // Editor size was restored from the preset's session block.
    REQUIRE(loader.getParamNormalized(kEditorSizeId) == 1.0);

    presetStream->release();
    saver.terminate();
    loader.terminate();
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

// ==============================================================================
// T092: Editor-size switching smoke test.
//
// Toggle kEditorSizeId between Default (0.0) and Compact (1.0) repeatedly and
// verify:
//   * The template name derived from the EditorSizePolicy flips correctly
//     (this is the string the controller passes to VST3Editor::exchangeView).
//   * The controller accepts the change without mutating any other registered
//     parameter -- the toggle is visual-only (FR-040).
//   * Rapid toggling does not break subsequent kEditorSizeId reads, which is
//     the failure mode if IDependent subscribers (PadGridView,
//     CouplingMatrixView) were not cleanly re-registered after a template
//     swap. Here we exercise the state machine; the actual VSTGUI re-register
//     path is covered by the ASan lifecycle test (T091).
// ==============================================================================
TEST_CASE("kEditorSizeId toggling flips template name and preserves other params",
          "[editor_size_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    const int paramCount = ctl.getParameterCount();
    REQUIRE(paramCount > 0);

    struct Snap { Steinberg::Vst::ParamID id; Steinberg::Vst::ParamValue value; };
    std::vector<Snap> before;
    before.reserve(static_cast<std::size_t>(paramCount));
    for (int i = 0; i < paramCount; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(ctl.getParameterInfo(i, info) == Steinberg::kResultOk);
        if (info.id == kEditorSizeId)
            continue;
        before.push_back({ info.id, ctl.getParamNormalized(info.id) });
    }

    for (int t = 0; t < 20; ++t) {
        const bool wantCompact = (t % 2 == 1);
        const double v = wantCompact ? 1.0 : 0.0;
        REQUIRE(ctl.setParamNormalized(kEditorSizeId, v) == Steinberg::kResultOk);
        REQUIRE(ctl.getParamNormalized(kEditorSizeId) == v);

        const auto expectedTemplate = wantCompact
            ? std::string("EditorCompact")
            : std::string("EditorDefault");
        const auto mode = wantCompact ? UI::EditorSize::Compact
                                      : UI::EditorSize::Default;
        REQUIRE(UI::templateNameFor(mode) == expectedTemplate);

        // Other parameters must be untouched (session-scope guarantee).
        for (const auto& s : before) {
            REQUIRE(ctl.getParamNormalized(s.id) == s.value);
        }
    }

    ctl.terminate();
}

// ==============================================================================
// T092 (IDependent resubscribe): after a size-mode flip the controller must
// continue to accept the inverse flip. Failure mode for dangling IDependent
// subscribers is usually silent -- the state API still works but automation
// no longer repaints. We can't observe repaints headless, so we pin the
// weaker invariant: kEditorSizeId round-trips across N flips with no drift.
// ==============================================================================
TEST_CASE("kEditorSizeId survives rapid bidirectional flips",
          "[editor_size_session]")
{
    Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);

    for (int i = 0; i < 100; ++i) {
        const double v = (i & 1) ? 1.0 : 0.0;
        REQUIRE(ctl.setParamNormalized(kEditorSizeId, v) == Steinberg::kResultOk);
        REQUIRE(ctl.getParamNormalized(kEditorSizeId) == v);
    }

    ctl.terminate();
}
