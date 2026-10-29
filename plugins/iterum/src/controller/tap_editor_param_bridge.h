#pragma once

// ==============================================================================
// TapEditorParamBridge: every parameter change the tap pattern editor shows, pushed to it as it happens
// ==============================================================================
// specs/iterum-tap-editor/design.md 3.6. Built the way VisibilityController is: it registers as a dependent of each
// watched Parameter. Delivery is SYNCHRONOUS: Parameter::setNormalized calls FObject::changed(), which runs every
// dependent's update() at once on the calling thread (only an explicit deferUpdate() queues). That is safe because
// setParamNormalized is a [UI-thread & Connected] call (ivsteditcontroller.h), and every other caller in Iterum (the
// editor's edit sink, Copy, editParamWithNotify) runs on the UI thread too.
//
// The bridge holds no view pointer: its handler looks the view up by tag on every change, so a view the
// UIViewSwitchContainer destroyed is never touched. The owner deactivates it before the editor closes.
// ==============================================================================

#include "base/source/fobject.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <atomic>
#include <functional>
#include <utility>
#include <vector>

namespace Iterum {

class TapEditorParamBridge : public Steinberg::FObject {
public:
    using Handler = std::function<void(Steinberg::Vst::ParamID)>;

    TapEditorParamBridge(std::vector<Steinberg::Vst::Parameter*> watched, Handler handler)
        : watched_(std::move(watched)), handler_(std::move(handler)) {
        for (auto* param : watched_) {
            if (param != nullptr) {
                param->addRef();
                param->addDependent(this);
            }
        }
    }

    ~TapEditorParamBridge() override {
        deactivate();
        for (auto* param : watched_) {
            if (param != nullptr) {
                param->release();
            }
        }
        watched_.clear();
    }

    TapEditorParamBridge(const TapEditorParamBridge&) = delete;
    TapEditorParamBridge& operator=(const TapEditorParamBridge&) = delete;
    TapEditorParamBridge(TapEditorParamBridge&&) = delete;
    TapEditorParamBridge& operator=(TapEditorParamBridge&&) = delete;

    /// Stops every delivery and removes the dependencies. Idempotent; called before the editor closes.
    void deactivate() {
        if (active_.exchange(false, std::memory_order_acq_rel)) {
            for (auto* param : watched_) {
                if (param != nullptr) {
                    param->removeDependent(this);
                }
            }
        }
    }

    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override {
        if (!active_.load(std::memory_order_acquire) || message != IDependent::kChanged || !handler_) {
            return;
        }
        // FObject::changed() passes the parameter's own unknownCast(), so the pointers compare directly
        for (auto* param : watched_) {
            if (param != nullptr && param->unknownCast() == changedUnknown) {
                handler_(param->getInfo().id);
                return;
            }
        }
    }

    OBJ_METHODS(TapEditorParamBridge, FObject)

private:
    std::vector<Steinberg::Vst::Parameter*> watched_;
    Handler handler_;
    std::atomic<bool> active_{true};
};

} // namespace Iterum
