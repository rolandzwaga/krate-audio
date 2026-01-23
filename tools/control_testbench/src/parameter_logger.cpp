// =============================================================================
// Parameter Logger Implementation
// =============================================================================

#include "parameter_logger.h"
#include <cstdio>

namespace Testbench {

// Global logger pointer
static ParameterLogView* g_logger = nullptr;

void setGlobalLogger(ParameterLogView* logger) {
    g_logger = logger;
}

ParameterLogView* getGlobalLogger() {
    return g_logger;
}

void logParameterChange(uint32_t paramId, float value) {
    if (g_logger) {
        // Determine parameter name from ID
        std::string name;
        if (paramId >= 3500 && paramId <= 3507) {
            name = "CustomTime" + std::to_string(paramId - 3500);
        } else if (paramId >= 3510 && paramId <= 3517) {
            name = "CustomLevel" + std::to_string(paramId - 3510);
        } else {
            name = "Param" + std::to_string(paramId);
        }
        g_logger->logParameter(paramId, value, name);
    }
}

// =============================================================================
// ParameterLogView Implementation
// =============================================================================

ParameterLogView::ParameterLogView(const VSTGUI::CRect& size)
    : CView(size)
{
}

void ParameterLogView::draw(VSTGUI::CDrawContext* context) {
    auto viewRect = getViewSize();

    // Fill background
    context->setFillColor(kBackgroundColor);
    context->drawRect(viewRect, VSTGUI::kDrawFilled);

    // Draw border
    context->setFrameColor(VSTGUI::CColor(50, 50, 55, 255));
    context->setLineWidth(1.0);
    context->drawRect(viewRect, VSTGUI::kDrawStroked);

    // Draw title
    auto titleFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 10, VSTGUI::kBoldFace);
    context->setFont(titleFont);
    context->setFontColor(kTextColor);
    VSTGUI::CRect titleRect(viewRect.left + 5, viewRect.top + 2, viewRect.right - 5, viewRect.top + 16);
    context->drawString("Parameter Log", titleRect, VSTGUI::kLeftText);

    // Draw entries
    auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Consolas", 9);
    context->setFont(font);

    std::lock_guard<std::mutex> lock(mutex_);

    float y = static_cast<float>(viewRect.top) + 20.0f;
    float lineHeight = 14.0f;

    for (const auto& entry : entries_) {
        if (y + lineHeight > viewRect.bottom) break;

        // Format: [ParamID] Name = Value
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "[%04u] %-12s = %.4f",
            entry.paramId, entry.paramName.c_str(), entry.value);

        VSTGUI::CRect lineRect(viewRect.left + 5, y, viewRect.right - 5, y + lineHeight);
        context->setFontColor(kTextColor);
        context->drawString(buffer, lineRect, VSTGUI::kLeftText);

        y += lineHeight;
    }

    setDirty(false);
}

void ParameterLogView::logParameter(uint32_t paramId, float value, const std::string& paramName) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        entries_.push_front({paramId, value, paramName});

        // Limit size
        while (entries_.size() > kMaxLogEntries) {
            entries_.pop_back();
        }
    }

    invalid();
}

void ParameterLogView::clear() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }
    invalid();
}

} // namespace Testbench
