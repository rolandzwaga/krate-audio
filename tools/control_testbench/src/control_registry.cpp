// =============================================================================
// Control Registry Implementation
// =============================================================================

#include "control_registry.h"
#include "parameter_logger.h"
#include "ui/tap_pattern_editor.h"

// Shared UI controls - include triggers static ViewCreator registration
#include "ui/arc_knob.h"

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

} // namespace Testbench
