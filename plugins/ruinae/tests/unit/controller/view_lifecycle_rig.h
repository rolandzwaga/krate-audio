#pragma once

// ==============================================================================
// Ruinae editor view-lifecycle rig (fix plan T1.3: R-079, R-080, R-129, R-130,
// R-131, R-193, R-194)
// ==============================================================================
// Opens the real editor headlessly and makes every UIViewSwitchContainer behave
// as it does in an attached editor in a host:
//
//   * attach: each switch's UIDescriptionViewSwitchController listens to its
//     switch control and shows the template for the parameter's value, and a
//     switch inside a newly built template is bound the moment it is added;
//   * a parameter change (host automation) or a click on the bound control
//     therefore switches the template INSIDE EditController::setParamNormalized,
//     before Controller::setParamNormalized's own code runs (vst3editor.cpp
//     ParameterChangeListener::update -> CControl::valueChanged -> switch);
//   * removed(): the outgoing template's controls are handed to
//     VST3Editor::onViewRemoved, which drops the ParameterChangeListener's
//     reference, so they are destroyed with the template as in a host.
//
// The headless frame is never attached (no platform window), which is why the
// rig wires these by hand: without them no switch reacts to its control, and a
// parameter-bound control outlives its template.
//
// The controller's cached view pointers are read through
// Controller::cachedViewPointerInventoryForTest() and compared with the live
// frame tree: a pointer that is non-null but not in the tree is the defect.
// ==============================================================================

#include <editor_lifecycle_harness.h>

#include <catch2/catch_test_macros.hpp>

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/iviewlistener.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/uidescription/uiviewswitchcontainer.h"

// Register the view classes of the switch controls (MainTab, DistortionViewMode,
// FilterViewMode, OscAType, OscBType), as the plugin's entry.cpp does. Without
// them those controls are not created and nothing can drive their switches.
#include "ui/icon_segment_button.h"
#include "ui/oscillator_type_selector.h"

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace RuinaeViewLifecycleTest {

using Steinberg::Vst::ParamID;
using ParamList = std::vector<std::pair<ParamID, double>>;

// MainTab: template-names="Tab_Sound,Tab_Mod,Tab_Fx,Tab_Seq"
inline constexpr double kTabSound = 0.0;
inline constexpr double kTabMod = 1.0 / 3.0;
inline constexpr double kTabFx = 2.0 / 3.0;
inline constexpr double kTabSeq = 1.0;

// ModSourceViewMode: LFO1, LFO2, Chaos, Macros, Rungler, EnvFollower, SampleHold,
// Random, PitchFollower, Transient
inline constexpr int kModSourceCount = 10;
inline double modSourceNorm(int index) { return static_cast<double>(index) / (kModSourceCount - 1); }

inline void forEachDescendant(VSTGUI::CViewContainer* parent,
                              const std::function<void(VSTGUI::CView*)>& fn) {
    if (!parent) return;
    parent->forEachChild([&fn](VSTGUI::CView* child) {
        fn(child);
        if (auto* c = child->asViewContainer()) forEachDescendant(c, fn);
    });
}

inline VSTGUI::CControl* findControl(VSTGUI::CViewContainer* root, ParamID tag) {
    VSTGUI::CControl* found = nullptr;
    forEachDescendant(root, [&](VSTGUI::CView* v) {
        if (found) return;
        if (auto* c = dynamic_cast<VSTGUI::CControl*>(v); c && c->getTag() == static_cast<int32_t>(tag))
            found = c;
    });
    return found;
}

// Processor state with every parameter at its default.
inline std::vector<char> defaultProcessorState() {
    auto p = std::make_unique<Ruinae::Processor>();
    REQUIRE(p->initialize(nullptr) == Steinberg::kResultOk);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);
    Steinberg::MemoryStream s;
    REQUIRE(p->getState(&s) == Steinberg::kResultTrue);
    std::vector<char> bytes(s.getData(), s.getData() + s.getSize());
    p->terminate();
    return bytes;
}

class ViewLifecycleRig final : public VSTGUI::ViewListenerAdapter,
                               public VSTGUI::ViewContainerListenerAdapter {
public:
    Ruinae::Controller ctl;

    explicit ViewLifecycleRig(const ParamList& preOpenParams = {}) {
        REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);
        for (const auto& [id, v] : preOpenParams) ctl.setParamNormalized(id, v);
        Krate::TestSupport::ensureVstguiInitialized();
        editor_ = new VSTGUI::VST3Editor(&ctl, "editor",
                                         (std::string(RUINAE_RESOURCES_DIR) + "/editor.uidesc").c_str());
        view_ = editor_;
        REQUIRE(view_->attached(nullptr, Krate::TestSupport::nativePlatformType()) == Steinberg::kResultTrue);
        frame_ = editor_->getFrame();
        REQUIRE(frame_ != nullptr);
        open_ = true;
        bindSwitches(frame_);
    }

    ~ViewLifecycleRig() override {
        closeEditor();
        ctl.terminate();
    }

    ViewLifecycleRig(const ViewLifecycleRig&) = delete;
    ViewLifecycleRig& operator=(const ViewLifecycleRig&) = delete;
    ViewLifecycleRig(ViewLifecycleRig&&) = delete;
    ViewLifecycleRig& operator=(ViewLifecycleRig&&) = delete;

    [[nodiscard]] VSTGUI::CFrame* frame() const { return frame_; }

    /// IPlugView::removed() -> VST3Editor::close() -> Controller::willClose(); the controller stays initialized.
    void closeEditor() {
        if (!open_) return;
        open_ = false;
        while (!bindings_.empty()) unbind(bindings_.back().sw);
        view_->removed();
        view_->release();
        view_ = nullptr;
        editor_ = nullptr;
        frame_ = nullptr;
    }

    /// A user edit of the control bound to `tag`, in the order CControl reports a mouse edit.
    void clickControl(ParamID tag, double normalized) {
        VSTGUI::CControl* control = nullptr;
        for (const auto& b : bindings_)
            if (b.control && b.dc->getSwitchControlTag() == static_cast<int32_t>(tag)) control = b.control;
        if (!control) control = findControl(frame_, tag);
        REQUIRE(control != nullptr);
        control->beginEdit();
        control->setValueNormalized(static_cast<float>(normalized));
        control->valueChanged();
        control->endEdit();
    }

    /// Loads a processor state as the host does (setComponentState -> syncAllViews).
    Steinberg::tresult loadState(const std::vector<char>& bytes) {
        Steinberg::MemoryStream ms(const_cast<char*>(bytes.data()), static_cast<Steinberg::TSize>(bytes.size()));
        return ctl.setComponentState(&ms);
    }

    [[nodiscard]] std::set<const VSTGUI::CView*> liveViews() const {
        std::set<const VSTGUI::CView*> live;
        if (frame_) {
            live.insert(frame_);
            forEachDescendant(frame_, [&live](VSTGUI::CView* v) { live.insert(v); });
        }
        return live;
    }

    /// Cached pointers that are non-null but point at no view in the frame tree.
    [[nodiscard]] std::vector<std::string> danglingPointers() const {
        const auto live = liveViews();
        std::vector<std::string> out;
        for (const auto& [name, view] : ctl.cachedViewPointerInventoryForTest())
            if (view && !live.count(view)) out.emplace_back(name);
        return out;
    }

    [[nodiscard]] std::set<std::string> nonNullPointers() const {
        std::set<std::string> out;
        for (const auto& [name, view] : ctl.cachedViewPointerInventoryForTest())
            if (view) out.emplace(name);
        return out;
    }

    [[nodiscard]] VSTGUI::CView* pointer(const std::string& name) const {
        bool known = false;
        VSTGUI::CView* found = nullptr;
        for (const auto& [n, view] : ctl.cachedViewPointerInventoryForTest()) {
            if (name == n) {
                known = true;
                found = view;
            }
        }
        INFO("cached view pointer " << name);
        REQUIRE(known);
        return found;
    }

    [[nodiscard]] bool isLive(const VSTGUI::CView* view) const { return view && liveViews().count(view) != 0; }

    // --- IViewContainerListener (bound switch containers only) -------------------
    void viewContainerViewAdded(VSTGUI::CViewContainer* /*container*/, VSTGUI::CView* view) override {
        // attached(): a switch inside the new template binds to its control now.
        if (auto* c = view->asViewContainer()) bindSwitches(c);
    }

    void viewContainerViewRemoved(VSTGUI::CViewContainer* /*container*/, VSTGUI::CView* view) override {
        // removed(): what an attached tree does for every view of the outgoing template.
        std::vector<VSTGUI::CView*> subtree{view};
        if (auto* c = view->asViewContainer())
            forEachDescendant(c, [&subtree](VSTGUI::CView* v) { subtree.push_back(v); });
        for (auto* v : subtree) {
            if (auto* sw = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(v)) unbind(sw);
            if (auto* c = dynamic_cast<VSTGUI::CControl*>(v); c && c->getTag() != -1)
                frame_->getViewAddedRemovedObserver()->onViewRemoved(frame_, c);
        }
    }

    // --- IViewListener (bound switches and their controls) -----------------------
    void viewWillDelete(VSTGUI::CView* view) override {
        if (auto* sw = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(view)) unbind(sw);
        for (auto& b : bindings_)
            if (b.control == view) b.control = nullptr;
        if (listenedControls_.erase(view) != 0) view->unregisterViewListener(this);
    }

private:
    struct Binding {
        VSTGUI::UIViewSwitchContainer* sw = nullptr;
        VSTGUI::UIDescriptionViewSwitchController* dc = nullptr;
        VSTGUI::CControl* control = nullptr;
    };

    void bindSwitches(VSTGUI::CViewContainer* root) {
        std::vector<VSTGUI::UIViewSwitchContainer*> found;
        forEachDescendant(root, [&found](VSTGUI::CView* v) {
            if (auto* sw = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(v)) found.push_back(sw);
        });
        for (auto* sw : found) bind(sw);
    }

    void bind(VSTGUI::UIViewSwitchContainer* sw) {
        for (const auto& b : bindings_)
            if (b.sw == sw) return;
        auto* dc = dynamic_cast<VSTGUI::UIDescriptionViewSwitchController*>(sw->getController());
        if (!dc) return;
        const int32_t tag = dc->getSwitchControlTag();
        VSTGUI::CControl* control = tag < 0 ? nullptr : findControl(frame_, static_cast<ParamID>(tag));
        INFO("switch control tag " << tag);
        REQUIRE(control != nullptr);  // every switch in editor.uidesc has a control in the tree
        control->registerControlListener(dc);
        if (listenedControls_.insert(control).second) control->registerViewListener(this);
        sw->registerViewContainerListener(this);
        sw->registerViewListener(this);
        bindings_.push_back(Binding{sw, dc, control});

        // switchContainerAttached(): show the template for the parameter's current value
        // (same index formula as UIDescriptionViewSwitchController::valueChanged).
        std::string names;
        dc->getTemplateNames(names);
        const int n = 1 + static_cast<int>(std::count(names.begin(), names.end(), ','));
        const auto norm = static_cast<float>(ctl.getParamNormalized(static_cast<ParamID>(tag)));
        sw->setCurrentViewIndex(std::min(static_cast<int>(norm * static_cast<float>(n)), n - 1));
    }

    void unbind(VSTGUI::UIViewSwitchContainer* sw) {
        auto it = std::find_if(bindings_.begin(), bindings_.end(), [sw](const Binding& b) { return b.sw == sw; });
        if (it == bindings_.end()) return;
        const Binding b = *it;
        bindings_.erase(it);
        if (b.control) {
            b.control->unregisterControlListener(b.dc);
            const bool stillUsed = std::any_of(bindings_.begin(), bindings_.end(),
                                               [&b](const Binding& o) { return o.control == b.control; });
            if (!stillUsed && listenedControls_.erase(b.control) != 0) b.control->unregisterViewListener(this);
        }
        sw->unregisterViewContainerListener(this);
        sw->unregisterViewListener(this);
    }

    VSTGUI::VST3Editor* editor_ = nullptr;
    Steinberg::IPlugView* view_ = nullptr;
    VSTGUI::CFrame* frame_ = nullptr;
    bool open_ = false;
    std::vector<Binding> bindings_;
    std::set<const VSTGUI::CView*> listenedControls_;
};

/// Names of the cached pointers a fresh editor open on the same view state holds.
inline std::set<std::string> freshOpenNonNull(const ParamList& params) {
    ViewLifecycleRig fresh(params);
    return fresh.nonNullPointers();
}

inline std::string joinNames(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& n : names) out += n + " ";
    return out;
}

/// Every pointer a fresh open on `params` caches is non-null and live here too (R-131: a switch must
/// not leave the incoming template's views unreachable), and no pointer dangles.
inline void checkMatchesFreshOpen(const ViewLifecycleRig& rig, const ParamList& params) {
    std::vector<std::string> missing;
    for (const auto& name : freshOpenNonNull(params))
        if (!rig.isLive(rig.pointer(name))) missing.push_back(name);
    INFO("pointers a fresh open caches that are null or not live: " << joinNames(missing));
    CHECK(missing.empty());
    const auto dangling = rig.danglingPointers();
    INFO("non-null pointers not in the live frame tree: " << joinNames(dangling));
    CHECK(dangling.empty());
}

}  // namespace RuinaeViewLifecycleTest
