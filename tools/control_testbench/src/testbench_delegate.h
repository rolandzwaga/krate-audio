// =============================================================================
// Testbench Delegate - Main application delegate for the control testbench
// =============================================================================

#pragma once

#include "vstgui/standalone/include/helpers/appdelegate.h"
#include "vstgui/standalone/include/helpers/windowlistener.h"
#include "vstgui/standalone/include/icommand.h"
#include <memory>

namespace Testbench {

class TestbenchController;

// =============================================================================
// TestbenchDelegate - Application delegate
// =============================================================================
class TestbenchDelegate : public VSTGUI::Standalone::Application::DelegateAdapter,
                          public VSTGUI::Standalone::ICommandHandler,
                          public VSTGUI::Standalone::WindowListenerAdapter
{
public:
    TestbenchDelegate();
    ~TestbenchDelegate() override;

    // Application::IDelegate
    void finishLaunching() override;
    void showAboutDialog() override;
    bool hasAboutDialog() override;
    VSTGUI::UTF8StringPtr getSharedUIResourceFilename() const override;

    // ICommandHandler
    bool canHandleCommand(const VSTGUI::Standalone::Command& command) override;
    bool handleCommand(const VSTGUI::Standalone::Command& command) override;

    // WindowListenerAdapter
    void onClosed(const VSTGUI::Standalone::IWindow& window) override;

private:
    void createMainWindow();

    std::shared_ptr<TestbenchController> controller_;
};

} // namespace Testbench
