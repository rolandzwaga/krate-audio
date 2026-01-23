// =============================================================================
// Control Testbench - Main Entry Point
// =============================================================================
// Standalone application for rapid iteration on custom VSTGUI controls.
//
// Usage:
//   control_testbench[.exe]
//
// The application provides a test environment for custom controls with:
// - Real-time parameter logging
// - Interactive control testing
// - No DAW required
// =============================================================================

#include "testbench_delegate.h"
#include "vstgui/standalone/include/appinit.h"

// =============================================================================
// Application Entry Point
// =============================================================================
// VSTGUI Standalone uses this static initialization pattern to set up the
// application before main() is called.

static VSTGUI::Standalone::Application::Init gAppDelegate(
    std::make_unique<Testbench::TestbenchDelegate>(),
    {
        // Configuration options
        {VSTGUI::Standalone::Application::ConfigKey::ShowCommandsInContextMenu, 1}
    }
);
