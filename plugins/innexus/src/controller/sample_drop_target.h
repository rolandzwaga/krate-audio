#pragma once

// ==============================================================================
// Sample Drop Target & Overlay View
// ==============================================================================
// Enables drag-and-drop of audio files onto the Innexus plugin window.
// The overlay is transparent and passes through all mouse events, but
// intercepts drag-and-drop via VSTGUI's IDropTarget mechanism.
//
// Supported formats: .wav, .aiff, .aif (matching SampleAnalyzer/dr_wav)
// ==============================================================================

#include "controller.h"

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/dragging.h"
#include "vstgui/lib/idatapackage.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace Innexus {

// ==============================================================================
// isSupportedAudioFile
// ==============================================================================
inline bool isSupportedAudioFile(std::string_view path)
{
    // Find last dot
    auto dotPos = path.rfind('.');
    if (dotPos == std::string_view::npos)
        return false;

    auto ext = path.substr(dotPos);

    // Case-insensitive comparison via lowercase copy
    std::string lower(ext);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return lower == ".wav" || lower == ".aiff" || lower == ".aif";
}

// ==============================================================================
// Drag state for visual feedback
// ==============================================================================
enum class DragState
{
    None,
    Valid,
    Invalid
};

// Forward declaration
class SampleDropOverlayView;

// ==============================================================================
// SampleDropTarget — IDropTarget implementation for file drops
// ==============================================================================
class SampleDropTarget : public VSTGUI::DropTargetAdapter,
                         public VSTGUI::NonAtomicReferenceCounted
{
public:
    SampleDropTarget(Controller* controller, SampleDropOverlayView* overlay)
        : controller_(controller), overlay_(overlay) {}

    VSTGUI::DragOperation onDragEnter(VSTGUI::DragEventData data) override;
    VSTGUI::DragOperation onDragMove(VSTGUI::DragEventData data) override;
    void onDragLeave(VSTGUI::DragEventData data) override;
    bool onDrop(VSTGUI::DragEventData data) override;

private:
    // Check if the drag data contains a supported audio file, and return its path.
    std::string findSupportedFile(VSTGUI::IDataPackage* pkg) const
    {
        for (auto it = VSTGUI::begin(pkg); it != VSTGUI::end(pkg); ++it)
        {
            const auto& item = *it;
            if (item.type == VSTGUI::IDataPackage::kFilePath && item.data)
            {
                std::string filePath(static_cast<const char*>(item.data), item.dataSize);
                // Remove trailing null if present
                if (!filePath.empty() && filePath.back() == '\0')
                    filePath.pop_back();
                if (isSupportedAudioFile(filePath))
                    return filePath;
            }
        }
        return {};
    }

    Controller* controller_;
    SampleDropOverlayView* overlay_;
};

// ==============================================================================
// SampleDropOverlayView — transparent full-window overlay for drag feedback
// ==============================================================================
class SampleDropOverlayView : public VSTGUI::CView
{
public:
    SampleDropOverlayView(const VSTGUI::CRect& size, Controller* controller)
        : CView(size), controller_(controller) {}

    void setDragState(DragState state)
    {
        if (dragState_ != state)
        {
            dragState_ = state;
            invalid();
        }
    }

    DragState getDragState() const { return dragState_; }

    // --- CView overrides ---

    void draw(VSTGUI::CDrawContext* context) override
    {
        if (dragState_ == DragState::None)
        {
            setDirty(false);
            return;
        }

        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
        auto r = getViewSize();

        // Draw semi-transparent overlay background
        VSTGUI::CColor bgColor;
        VSTGUI::CColor borderColor;
        const char* message = nullptr;

        if (dragState_ == DragState::Valid)
        {
            bgColor = VSTGUI::CColor(0, 188, 212, 15);    // accent with low alpha
            borderColor = VSTGUI::CColor(0, 188, 212, 180); // accent (#00BCD4)
            message = "Drop to load sample";
        }
        else
        {
            bgColor = VSTGUI::CColor(220, 50, 50, 15);
            borderColor = VSTGUI::CColor(220, 50, 50, 180);
            message = "Unsupported format";
        }

        // Fill
        context->setFillColor(bgColor);
        context->drawRect(r, VSTGUI::kDrawFilled);

        // Border (inset slightly for clean edges)
        auto borderRect = r;
        borderRect.inset(2, 2);
        context->setFrameColor(borderColor);
        context->setLineWidth(2.5);
        context->setLineStyle(VSTGUI::kLineSolid);
        context->drawRect(borderRect, VSTGUI::kDrawStroked);

        // Message text
        if (message)
        {
            auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 16, VSTGUI::kBoldFace);
            context->setFont(font);
            context->setFontColor(borderColor);
            context->drawString(VSTGUI::UTF8String(message), r, VSTGUI::kCenterText);
        }

        setDirty(false);
    }

    VSTGUI::SharedPointer<VSTGUI::IDropTarget> getDropTarget() override
    {
        if (!dropTarget_)
            dropTarget_ = VSTGUI::makeOwned<SampleDropTarget>(controller_, this);
        return dropTarget_;
    }

    // Pass through all mouse events so controls underneath remain interactive.
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override
    {
        return VSTGUI::kMouseEventNotHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override
    {
        return VSTGUI::kMouseEventNotHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override
    {
        return VSTGUI::kMouseEventNotHandled;
    }

    VSTGUI::CMouseEventResult onMouseEntered(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override
    {
        return VSTGUI::kMouseEventNotHandled;
    }

    VSTGUI::CMouseEventResult onMouseExited(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override
    {
        return VSTGUI::kMouseEventNotHandled;
    }

private:
    Controller* controller_;
    DragState dragState_ = DragState::None;
    VSTGUI::SharedPointer<SampleDropTarget> dropTarget_;
};

// ==============================================================================
// SampleDropTarget method implementations (need SampleDropOverlayView defined)
// ==============================================================================

inline VSTGUI::DragOperation SampleDropTarget::onDragEnter(VSTGUI::DragEventData data)
{
    auto filePath = findSupportedFile(data.drag);
    if (!filePath.empty())
    {
        overlay_->setDragState(DragState::Valid);
        return VSTGUI::DragOperation::Copy;
    }

    // Check if there are ANY file paths (just unsupported format)
    for (auto it = VSTGUI::begin(data.drag); it != VSTGUI::end(data.drag); ++it)
    {
        if ((*it).type == VSTGUI::IDataPackage::kFilePath)
        {
            overlay_->setDragState(DragState::Invalid);
            return VSTGUI::DragOperation::None;
        }
    }

    // Not a file drag at all — don't show feedback
    return VSTGUI::DragOperation::None;
}

inline VSTGUI::DragOperation SampleDropTarget::onDragMove(VSTGUI::DragEventData data)
{
    // Maintain current state; onDragEnter already set it
    if (overlay_->getDragState() == DragState::Valid)
        return VSTGUI::DragOperation::Copy;
    return VSTGUI::DragOperation::None;
}

inline void SampleDropTarget::onDragLeave(VSTGUI::DragEventData data)
{
    overlay_->setDragState(DragState::None);
}

inline bool SampleDropTarget::onDrop(VSTGUI::DragEventData data)
{
    auto filePath = findSupportedFile(data.drag);
    overlay_->setDragState(DragState::None);

    if (!filePath.empty() && controller_)
    {
        controller_->onSampleFileSelected(filePath);
        return true;
    }
    return false;
}

} // namespace Innexus
