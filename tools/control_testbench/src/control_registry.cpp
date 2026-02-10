// =============================================================================
// Control Registry Implementation
// =============================================================================

#include "control_registry.h"
#include "parameter_logger.h"
#include "ui/tap_pattern_editor.h"

// Shared UI controls - include triggers static ViewCreator registration
#include "ui/adsr_display.h"
#include "ui/arc_knob.h"
#include "ui/fieldset_container.h"
#include "ui/step_pattern_editor.h"
#include "ui/xy_morph_pad.h"

namespace Testbench {

// =============================================================================
// ControlRegistry Implementation
// =============================================================================

ControlRegistry& ControlRegistry::instance() {
    static ControlRegistry registry;
    return registry;
}

void ControlRegistry::registerControl(
    const std::string& id,
    const ControlInfo& info,
    ControlFactory factory)
{
    controls_[id] = {info, std::move(factory)};
}

std::vector<std::string> ControlRegistry::getControlIds() const {
    std::vector<std::string> ids;
    ids.reserve(controls_.size());
    for (const auto& [id, _] : controls_) {
        ids.push_back(id);
    }
    return ids;
}

const ControlInfo* ControlRegistry::getControlInfo(const std::string& id) const {
    auto it = controls_.find(id);
    if (it != controls_.end()) {
        return &it->second.info;
    }
    return nullptr;
}

VSTGUI::CView* ControlRegistry::createControl(const std::string& id, const VSTGUI::CRect& size) const {
    auto it = controls_.find(id);
    if (it != controls_.end()) {
        return it->second.factory(size);
    }
    return nullptr;
}

// =============================================================================
// TestbenchController Implementation
// =============================================================================

TestbenchController::TestbenchController()
    : currentControlId_("tap_pattern_editor")
{
}

void TestbenchController::setCurrentControl(const std::string& controlId) {
    currentControlId_ = controlId;
}

VSTGUI::CView* TestbenchController::createView(
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* description)
{
    // Check for custom view name
    if (auto customViewName = attributes.getAttributeValue(VSTGUI::IUIDescription::kCustomViewName)) {
        if (*customViewName == "TapPatternEditor") {
            // Get size from attributes or use default
            VSTGUI::CRect size(0, 0, 400, 200);
            if (auto sizeAttr = attributes.getAttributeValue("size")) {
                // Parse size string "width, height"
            }

            auto* editor = new Iterum::TapPatternEditor(size);

            // Set up parameter callback to log changes
            editor->setParameterCallback([](Steinberg::Vst::ParamID paramId, float value) {
                logParameterChange(paramId, value);
            });

            // Set some default taps for testing
            editor->setActiveTapCount(4);

            return editor;
        }
        else if (*customViewName == "ArcKnobModDemo") {
            // ArcKnob with modulation range preset for demo
            VSTGUI::CRect size(0, 0, 60, 60);
            auto* knob = new Krate::Plugins::ArcKnob(size, nullptr, -1);
            knob->setArcColor(VSTGUI::CColor(78, 205, 196, 255));   // Cyan
            knob->setModColor(VSTGUI::CColor(255, 107, 107, 180));  // Coral
            knob->setGuideColor(VSTGUI::CColor(255, 255, 255, 40));
            knob->setIndicatorLength(6.0);
            knob->setValue(0.5f);
            knob->setModulationRange(0.25f);
            return knob;
        }
        else if (*customViewName == "StepPatternEditor") {
            VSTGUI::CRect size(0, 0, 500, 200);

            auto* editor = new Krate::Plugins::StepPatternEditor(size, nullptr, -1);

            // Wire parameter callback to logger
            editor->setStepLevelBaseParamId(kTranceGateStepLevel0Id);
            editor->setParameterCallback([](uint32_t paramId, float value) {
                logParameterChange(paramId, value);
            });
            editor->setBeginEditCallback([](uint32_t paramId) {
                logParameterChange(paramId, -1.0f);  // sentinel for "begin edit"
            });
            editor->setEndEditCallback([](uint32_t paramId) {
                logParameterChange(paramId, -2.0f);  // sentinel for "end edit"
            });

            // Set 16 steps as default for testing
            editor->setNumSteps(16);

            return editor;
        }
        else if (*customViewName == "XYMorphPad") {
            VSTGUI::CRect size(0, 0, 250, 160);
            auto* pad = new Krate::Plugins::XYMorphPad(size, nullptr, -1);
            // Default Ruinae colors: blue-ish (OSC A) to gold (OSC B), dark to bright
            pad->setColorBottomLeft(VSTGUI::CColor(48, 84, 120, 255));
            pad->setColorBottomRight(VSTGUI::CColor(132, 102, 36, 255));
            pad->setColorTopLeft(VSTGUI::CColor(80, 140, 200, 255));
            pad->setColorTopRight(VSTGUI::CColor(220, 170, 60, 255));
            pad->setMorphPosition(0.5f, 0.5f);
            return pad;
        }
        else if (*customViewName == "XYMorphPadModDemo") {
            VSTGUI::CRect size(0, 0, 250, 160);
            auto* pad = new Krate::Plugins::XYMorphPad(size, nullptr, -1);
            pad->setColorBottomLeft(VSTGUI::CColor(48, 84, 120, 255));
            pad->setColorBottomRight(VSTGUI::CColor(132, 102, 36, 255));
            pad->setColorTopLeft(VSTGUI::CColor(80, 140, 200, 255));
            pad->setColorTopRight(VSTGUI::CColor(220, 170, 60, 255));
            pad->setMorphPosition(0.5f, 0.5f);
            pad->setModulationRange(0.3f, 0.2f);
            return pad;
        }
        else if (*customViewName == "ADSRDisplayAmp") {
            VSTGUI::CRect size(0, 0, 200, 120);
            auto* display = new Krate::Plugins::ADSRDisplay(size, nullptr, -1);
            display->setFillColor(VSTGUI::CColor(80, 140, 200, 77));
            display->setStrokeColor(VSTGUI::CColor(80, 140, 200, 255));
            display->setBackgroundColor(VSTGUI::CColor(30, 30, 33, 255));
            display->setGridColor(VSTGUI::CColor(255, 255, 255, 25));
            display->setControlPointColor(VSTGUI::CColor(255, 255, 255, 255));
            display->setTextColor(VSTGUI::CColor(255, 255, 255, 180));
            display->setAdsrBaseParamId(kAmpEnvAttackId);
            display->setCurveBaseParamId(kAmpEnvAttackCurveId);
            display->setBezierEnabledParamId(kAmpEnvBezierEnabledId);
            display->setBezierBaseParamId(kAmpEnvBezierAttackCp1xId);
            display->setAttackMs(10.0f);
            display->setDecayMs(80.0f);
            display->setSustainLevel(0.7f);
            display->setReleaseMs(200.0f);
            display->setParameterCallback([](uint32_t paramId, float value) {
                logParameterChange(paramId, value);
            });
            display->setBeginEditCallback([](uint32_t paramId) {
                logParameterChange(paramId, -1.0f);
            });
            display->setEndEditCallback([](uint32_t paramId) {
                logParameterChange(paramId, -2.0f);
            });
            return display;
        }
        else if (*customViewName == "ADSRDisplayFilter") {
            VSTGUI::CRect size(0, 0, 200, 120);
            auto* display = new Krate::Plugins::ADSRDisplay(size, nullptr, -1);
            display->setFillColor(VSTGUI::CColor(220, 170, 60, 77));
            display->setStrokeColor(VSTGUI::CColor(220, 170, 60, 255));
            display->setBackgroundColor(VSTGUI::CColor(30, 30, 33, 255));
            display->setGridColor(VSTGUI::CColor(255, 255, 255, 25));
            display->setControlPointColor(VSTGUI::CColor(255, 255, 255, 255));
            display->setTextColor(VSTGUI::CColor(255, 255, 255, 180));
            display->setAdsrBaseParamId(kFilterEnvAttackId);
            display->setCurveBaseParamId(kFilterEnvAttackCurveId);
            display->setBezierEnabledParamId(kFilterEnvBezierEnabledId);
            display->setBezierBaseParamId(kFilterEnvBezierAttackCp1xId);
            display->setAttackMs(1.0f);
            display->setDecayMs(150.0f);
            display->setSustainLevel(0.4f);
            display->setReleaseMs(500.0f);
            display->setAttackCurve(-0.5f);
            display->setParameterCallback([](uint32_t paramId, float value) {
                logParameterChange(paramId, value);
            });
            display->setBeginEditCallback([](uint32_t paramId) {
                logParameterChange(paramId, -1.0f);
            });
            display->setEndEditCallback([](uint32_t paramId) {
                logParameterChange(paramId, -2.0f);
            });
            return display;
        }
        else if (*customViewName == "ADSRDisplayMod") {
            VSTGUI::CRect size(0, 0, 200, 120);
            auto* display = new Krate::Plugins::ADSRDisplay(size, nullptr, -1);
            display->setFillColor(VSTGUI::CColor(160, 90, 200, 77));
            display->setStrokeColor(VSTGUI::CColor(160, 90, 200, 255));
            display->setBackgroundColor(VSTGUI::CColor(30, 30, 33, 255));
            display->setGridColor(VSTGUI::CColor(255, 255, 255, 25));
            display->setControlPointColor(VSTGUI::CColor(255, 255, 255, 255));
            display->setTextColor(VSTGUI::CColor(255, 255, 255, 180));
            display->setAdsrBaseParamId(kModEnvAttackId);
            display->setCurveBaseParamId(kModEnvAttackCurveId);
            display->setBezierEnabledParamId(kModEnvBezierEnabledId);
            display->setBezierBaseParamId(kModEnvBezierAttackCp1xId);
            display->setAttackMs(50.0f);
            display->setDecayMs(200.0f);
            display->setSustainLevel(0.5f);
            display->setReleaseMs(1000.0f);
            display->setDecayCurve(0.6f);
            display->setParameterCallback([](uint32_t paramId, float value) {
                logParameterChange(paramId, value);
            });
            display->setBeginEditCallback([](uint32_t paramId) {
                logParameterChange(paramId, -1.0f);
            });
            display->setEndEditCallback([](uint32_t paramId) {
                logParameterChange(paramId, -2.0f);
            });
            return display;
        }
        else if (*customViewName == "ParameterLog") {
            VSTGUI::CRect size(0, 0, 300, 300);
            auto* logger = new ParameterLogView(size);
            setGlobalLogger(logger);
            return logger;
        }
    }

    return nullptr;
}

void TestbenchController::valueChanged(VSTGUI::CControl* control) {
    // Handle control value changes if needed
    (void)control;
}

VSTGUI::CView* TestbenchController::verifyView(
    VSTGUI::CView* view,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* description)
{
    return view;
}

VSTGUI::IController* TestbenchController::createSubController(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::IUIDescription* description)
{
    return nullptr;
}

// =============================================================================
// Built-in Control Registrations
// =============================================================================

// Register TapPatternEditor
static bool _tap_editor_registered = []() {
    ControlRegistry::instance().registerControl(
        "tap_pattern_editor",
        {
            "Tap Pattern Editor",
            "Custom tap pattern editor for MultiTap delay mode",
            "tap_pattern_editor.uidesc"
        },
        [](const VSTGUI::CRect& size) -> VSTGUI::CView* {
            auto* editor = new Iterum::TapPatternEditor(size);
            editor->setParameterCallback([](Steinberg::Vst::ParamID paramId, float value) {
                logParameterChange(paramId, value);
            });
            editor->setActiveTapCount(4);
            return editor;
        }
    );
    return true;
}();

// Register StepPatternEditor
static bool _step_editor_registered = []() {
    ControlRegistry::instance().registerControl(
        "step_pattern_editor",
        {
            "Step Pattern Editor",
            "Step pattern bar chart editor for TranceGate (shared component)",
            "step_pattern_editor.uidesc"
        },
        [](const VSTGUI::CRect& size) -> VSTGUI::CView* {
            auto* editor = new Krate::Plugins::StepPatternEditor(size, nullptr, -1);
            editor->setStepLevelBaseParamId(kTranceGateStepLevel0Id);
            editor->setParameterCallback([](uint32_t paramId, float value) {
                logParameterChange(paramId, value);
            });
            editor->setNumSteps(16);
            return editor;
        }
    );
    return true;
}();

} // namespace Testbench
