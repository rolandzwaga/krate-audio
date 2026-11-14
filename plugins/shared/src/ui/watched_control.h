#pragma once

// ==============================================================================
// WatchedControl: a control a template sub-controller listens to
// ==============================================================================
// VSTGUI frees a template's views before the template's sub-controller
// (CViewContainer::beforeDelete frees the children, then CView::beforeDelete
// deletes the controller attribute), on every UIViewSwitchContainer rebuild and
// at editor close. A sub-controller that unregisters its listener from the
// control in its destructor therefore writes into a freed control. The control
// tells this when it is deleted, so the listener is dropped then; on destruction
// only a control still alive is touched.
//
// Usage: a member `Krate::Plugins::WatchedControl toggle_{this};` in the
// sub-controller, `toggle_.watch(control)` in verifyView, `toggle_.get()` to
// compare or read the control.
// ==============================================================================

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/iviewlistener.h"

namespace Krate::Plugins {

class WatchedControl : private VSTGUI::ViewListenerAdapter {
public:
    explicit WatchedControl(VSTGUI::IControlListener* listener) : listener_(listener) {}
    ~WatchedControl() override { release(); }
    WatchedControl(const WatchedControl&) = delete;
    WatchedControl& operator=(const WatchedControl&) = delete;
    WatchedControl(WatchedControl&&) = delete;
    WatchedControl& operator=(WatchedControl&&) = delete;

    /// Registers the listener on @p control (and drops it from the control watched before).
    void watch(VSTGUI::CControl* control) {
        release();
        control_ = control;
        control_->registerControlListener(listener_);
        control_->registerViewListener(this);
    }

    /// The watched control, or nullptr once it was deleted.
    [[nodiscard]] VSTGUI::CControl* get() const { return control_; }

private:
    void viewWillDelete(VSTGUI::CView* view) override {
        if (view == control_)
            release();
    }

    void release() {
        if (!control_)
            return;
        control_->unregisterControlListener(listener_);
        control_->unregisterViewListener(this);
        control_ = nullptr;
    }

    VSTGUI::IControlListener* listener_;
    VSTGUI::CControl* control_ = nullptr;
};

} // namespace Krate::Plugins
