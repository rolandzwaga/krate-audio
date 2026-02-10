// =============================================================================
// Testbench Delegate Implementation
// =============================================================================

#include "testbench_delegate.h"
#include "control_registry.h"
#include "parameter_logger.h"

#include "vstgui/standalone/include/iapplication.h"
#include "vstgui/standalone/include/iuidescwindow.h"
#include "vstgui/standalone/include/ialertbox.h"
#include "vstgui/standalone/include/helpers/uidesc/customization.h"
#include "vstgui/lib/cframe.h"

namespace Testbench {

using namespace VSTGUI;
using namespace VSTGUI::Standalone;

// =============================================================================
// Commands
// =============================================================================
static Command ClearLog{CommandGroup::Edit, "Clear Log"};
static Command ResetControl{CommandGroup::Edit, "Reset Control"};

// =============================================================================
// TestbenchDelegate Implementation
// =============================================================================

TestbenchDelegate::TestbenchDelegate()
    : Application::DelegateAdapter({
        "Control Testbench",
        "1.0.0",
        "com.krateaudio.controltestbench"
    })
{
    // Set default knob mode
    CFrame::kDefaultKnobMode = CKnobMode::kLinearMode;
}

TestbenchDelegate::~TestbenchDelegate() = default;

void TestbenchDelegate::finishLaunching() {
    // Register commands
    IApplication::instance().registerCommand(Commands::NewDocument, 'n');
    IApplication::instance().registerCommand(ClearLog, 'l');
    IApplication::instance().registerCommand(ResetControl, 'r');

    // Create initial window
    createMainWindow();
}

void TestbenchDelegate::createMainWindow() {
    controller_ = std::make_shared<TestbenchController>();

    UIDesc::Config config;
    config.windowConfig.title = "Control Testbench";
    config.windowConfig.autoSaveFrameName = "ControlTestbenchFrame2";
    config.windowConfig.style.close().size().border();
    config.windowConfig.size = {750, 1200};
    config.uiDescFileName = "testbench.uidesc";
    config.viewName = "view";

    // Create customization to register our view factory
    auto customization = UIDesc::Customization::make();
    customization->addCreateViewControllerFunc(
        "TestbenchController",
        [this](const UTF8StringView&, IController* parent, const IUIDescription*) {
            return controller_.get();
        }
    );
    config.customization = customization;

    if (auto window = UIDesc::makeWindow(config)) {
        window->show();
        window->registerWindowListener(this);
    }
}

void TestbenchDelegate::onClosed(const IWindow& window) {
    // Quit when last window closes
    if (IApplication::instance().getWindows().empty()) {
        IApplication::instance().quit();
    }
}

bool TestbenchDelegate::canHandleCommand(const Command& command) {
    if (command == Commands::NewDocument) return true;
    if (command == ClearLog) return true;
    if (command == ResetControl) return true;
    return false;
}

bool TestbenchDelegate::handleCommand(const Command& command) {
    if (command == Commands::NewDocument) {
        createMainWindow();
        return true;
    }
    if (command == ClearLog) {
        if (auto* logger = getGlobalLogger()) {
            logger->clear();
        }
        return true;
    }
    if (command == ResetControl) {
        // TODO: Reset the current control to defaults
        return true;
    }
    return false;
}

void TestbenchDelegate::showAboutDialog() {
    AlertBoxConfig config;
    config.headline = "Control Testbench";
    config.description = "A standalone application for testing VSTGUI custom controls.\n\n"
                         "Part of the Krate Audio plugin development toolkit.";
    config.defaultButton = "OK";
    IApplication::instance().showAlertBox(config);
}

bool TestbenchDelegate::hasAboutDialog() {
    return true;
}

UTF8StringPtr TestbenchDelegate::getSharedUIResourceFilename() const {
    return nullptr;
}

} // namespace Testbench
