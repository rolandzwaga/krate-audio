#pragma once

// ==============================================================================
// CachedViewRegistry: the controller's cached view pointers follow view lifetime
// ==============================================================================
// The controller caches raw pointers to views that VSTGUI owns. Many of them live
// in UIViewSwitchContainer templates, which a switch destroys at a moment the
// controller does not choose: inside EditController::setParamNormalized on a host
// change, after a 120 ms fade for MainTab, through a nested switch the controller
// never hears about. Clearing the pointers from parameter hooks missed whole
// destruction paths and, because the incoming template is built before the hook
// runs, nulled the NEW views (Ruinae audit R-079, R-080, R-129..R-131, R-193).
//
// Instead every cached pointer is registered here together with its view. The
// registry listens for the view's destruction (IViewListener::viewWillDelete) and
// nulls exactly the slots that still point at that view, so a cached pointer is
// non-null exactly while its view is alive, on every path.
//
// UI thread only. Fixed capacity: nothing allocates after construction (VSTGUI's
// own listener list does when a view is first registered).
// ==============================================================================

#include "vstgui/lib/cview.h"
#include "vstgui/lib/iviewlistener.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace Ruinae {

class CachedViewRegistry final : public VSTGUI::ViewListenerAdapter {
public:
    CachedViewRegistry() = default;
    ~CachedViewRegistry() override { clear(); }

    CachedViewRegistry(const CachedViewRegistry&) = delete;
    CachedViewRegistry& operator=(const CachedViewRegistry&) = delete;
    CachedViewRegistry(CachedViewRegistry&&) = delete;
    CachedViewRegistry& operator=(CachedViewRegistry&&) = delete;

    /// `*slot = view`, and `*slot = nullptr` again when `view` is destroyed. A slot holds one
    /// view at a time: re-assigning it forgets the previous view, so destroying the previous
    /// view later (an outgoing template removed after the incoming one was built) leaves the
    /// slot alone. T must derive from VSTGUI::CView.
    template <typename T>
    void track(T* view, T** slot) {
        void* const rawSlot = static_cast<void*>(slot);
        forgetSlot(rawSlot);
        *slot = nullptr;
        if (view == nullptr) return;
        assert(count_ < kCapacity && "CachedViewRegistry: raise kCapacity");
        if (count_ >= kCapacity) return;  // untracked would dangle; stay null instead
        auto* asView = static_cast<VSTGUI::CView*>(view);
        if (!isTracked(asView)) asView->registerViewListener(this);
        entries_[count_++] = Entry{asView, rawSlot, &nullSlotIfPointingAt<T>};
        *slot = view;
    }

    /// Null every slot and stop listening (editor close, terminate).
    void clear() {
        for (std::size_t i = 0; i < count_; ++i) {
            entries_[i].nullSlot(entries_[i].slot, nullptr);
            if (!trackedBefore(i)) entries_[i].view->unregisterViewListener(this);
        }
        count_ = 0;
    }

    void viewWillDelete(VSTGUI::CView* view) override {
        std::size_t i = 0;
        while (i < count_) {
            if (entries_[i].view == view) {
                entries_[i].nullSlot(entries_[i].slot, view);
                removeAt(i);
            } else {
                ++i;
            }
        }
        view->unregisterViewListener(this);
    }

private:
    struct Entry {
        VSTGUI::CView* view = nullptr;
        void* slot = nullptr;
        /// Nulls the slot if it points at `view`; a null `view` nulls it unconditionally.
        void (*nullSlot)(void* slot, const VSTGUI::CView* view) = nullptr;
    };

    // Entries are unique per slot, and the controller has fewer cached view members than this.
    static constexpr std::size_t kCapacity = 128;

    template <typename T>
    static void nullSlotIfPointingAt(void* slot, const VSTGUI::CView* view) {
        auto** typed = static_cast<T**>(slot);
        if (view == nullptr || (*typed != nullptr && static_cast<const VSTGUI::CView*>(*typed) == view))
            *typed = nullptr;
    }

    [[nodiscard]] bool isTracked(const VSTGUI::CView* view) const {
        for (std::size_t i = 0; i < count_; ++i)
            if (entries_[i].view == view) return true;
        return false;
    }

    [[nodiscard]] bool trackedBefore(std::size_t index) const {
        for (std::size_t i = 0; i < index; ++i)
            if (entries_[i].view == entries_[index].view) return true;
        return false;
    }

    void forgetSlot(const void* slot) {
        for (std::size_t i = 0; i < count_; ++i) {
            if (entries_[i].slot != slot) continue;
            VSTGUI::CView* view = entries_[i].view;
            removeAt(i);
            if (!isTracked(view)) view->unregisterViewListener(this);
            return;
        }
    }

    void removeAt(std::size_t index) {
        entries_[index] = entries_[count_ - 1];
        entries_[count_ - 1] = Entry{};
        --count_;
    }

    std::array<Entry, kCapacity> entries_{};
    std::size_t count_ = 0;
};

}  // namespace Ruinae
