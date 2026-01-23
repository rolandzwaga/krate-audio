// =============================================================================
// Parameter Logger - Displays parameter changes in the testbench
// =============================================================================

#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include <deque>
#include <string>
#include <mutex>
#include <cstdint>

namespace Testbench {

// Maximum number of log entries to display
constexpr size_t kMaxLogEntries = 20;

// =============================================================================
// LogEntry - A single parameter change event
// =============================================================================
struct LogEntry {
    uint32_t paramId;
    float value;
    std::string paramName;
};

// =============================================================================
// ParameterLogView - Scrolling log of parameter changes
// =============================================================================
class ParameterLogView : public VSTGUI::CView {
public:
    explicit ParameterLogView(const VSTGUI::CRect& size);

    void draw(VSTGUI::CDrawContext* context) override;

    // Add a new log entry
    void logParameter(uint32_t paramId, float value, const std::string& paramName);

    // Clear all entries
    void clear();

    CLASS_METHODS_NOCOPY(ParameterLogView, CView)

private:
    std::deque<LogEntry> entries_;
    std::mutex mutex_;

    static constexpr VSTGUI::CColor kBackgroundColor{25, 25, 28, 255};
    static constexpr VSTGUI::CColor kTextColor{180, 180, 185, 255};
    static constexpr VSTGUI::CColor kValueColor{100, 180, 100, 255};
    static constexpr VSTGUI::CColor kParamIdColor{180, 140, 100, 255};
};

// =============================================================================
// Global logger instance (for callback access)
// =============================================================================
void setGlobalLogger(ParameterLogView* logger);
ParameterLogView* getGlobalLogger();

// Convenience function to log from anywhere
void logParameterChange(uint32_t paramId, float value);

} // namespace Testbench
