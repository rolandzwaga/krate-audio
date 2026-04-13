// ==============================================================================
// Session-scope tests for kEditorSizeId (Phase 6, T022)
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-001, FR-040)
// ==============================================================================

#include "controller/controller.h"
#include "plugin_ids.h"
#include "ui/editor_size_policy.h"

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

TEST_CASE("EditorSizePolicy: default size is 1280x800", "[editor_size_session]")
{
    REQUIRE(UI::kDefaultWidth  == 1280);
    REQUIRE(UI::kDefaultHeight == 800);
    REQUIRE(UI::kCompactWidth  == 1024);
    REQUIRE(UI::kCompactHeight == 640);
    REQUIRE(UI::templateNameFor(UI::EditorSize::Default) == std::string("EditorDefault"));
    REQUIRE(UI::templateNameFor(UI::EditorSize::Compact) == std::string("EditorCompact"));
}
