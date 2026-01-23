// =============================================================================
// Control Registry - Factory for custom controls in the testbench
// =============================================================================

#pragma once

#include "vstgui/uidescription/icontroller.h"
#include "vstgui/uidescription/iuidescription.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/lib/cview.h"
#include <string>
#include <functional>
#include <map>

namespace Testbench {

// =============================================================================
// ControlInfo - Metadata about a registered control
// =============================================================================
struct ControlInfo {
    std::string name;
    std::string description;
    std::string uidescFile;  // The .uidesc file for this control
};

// =============================================================================
// ControlFactory - Creates instances of custom controls
// =============================================================================
using ControlFactory = std::function<VSTGUI::CView*(const VSTGUI::CRect&)>;

// =============================================================================
// ControlRegistry - Manages available controls for testing
// =============================================================================
class ControlRegistry {
public:
    static ControlRegistry& instance();

    // Register a control with the testbench
    void registerControl(
        const std::string& id,
        const ControlInfo& info,
        ControlFactory factory);

    // Get list of all registered controls
    std::vector<std::string> getControlIds() const;

    // Get info for a control
    const ControlInfo* getControlInfo(const std::string& id) const;

    // Create an instance of a control
    VSTGUI::CView* createControl(const std::string& id, const VSTGUI::CRect& size) const;

private:
    ControlRegistry() = default;

    struct Registration {
        ControlInfo info;
        ControlFactory factory;
    };

    std::map<std::string, Registration> controls_;
};

// =============================================================================
// TestbenchController - Controller for the testbench UI
// =============================================================================
class TestbenchController : public VSTGUI::IController {
public:
    TestbenchController();
    ~TestbenchController() override = default;

    // Set the currently selected control
    void setCurrentControl(const std::string& controlId);

    // IController interface
    VSTGUI::CView* createView(
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description) override;

    void valueChanged(VSTGUI::CControl* control) override;

    VSTGUI::CView* verifyView(
        VSTGUI::CView* view,
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description) override;

    VSTGUI::IController* createSubController(
        VSTGUI::UTF8StringPtr name,
        const VSTGUI::IUIDescription* description) override;

private:
    std::string currentControlId_;
};

// =============================================================================
// Registration helper macro
// =============================================================================
#define REGISTER_CONTROL(id, name, desc, uidesc, factory) \
    static bool _registered_##id = []() { \
        Testbench::ControlRegistry::instance().registerControl( \
            #id, \
            {name, desc, uidesc}, \
            factory); \
        return true; \
    }()

} // namespace Testbench
