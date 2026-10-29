// ==============================================================================
// Tap pattern editor wiring through the controller ([iterum][ui][tap-editor]; specs/iterum-tap-editor/design.md)
// ==============================================================================
// The real editor, opened headlessly as editor_panels_test.cpp opens it, on MultiTap, with a recording
// IComponentHandler. Covers Copy to Custom (2.6), the parameter bridge (3.6: synchronous delivery, one push after a
// bulk load), placement on a user Taps gesture only (3.3), the tempo and the read-only pattern (2.4, 5.9), the
// Length tail on every load path (7.1) and the host string parse (4.4).
//
// No triggerDeferedUpdates is called between a parameter set and the view check: the bridge's delivery is
// synchronous (Parameter::setNormalized -> changed() -> every dependent's update on the calling thread).

#include <editor_lifecycle_harness.h>

#include "controller/controller.h"
#include "controller/custom_views.h"
#include "delay_mode.h"
#include "plugin_ids.h"
#include "parameters/bbd_params.h"
#include "parameters/digital_params.h"
#include "parameters/freeze_params.h"
#include "parameters/granular_params.h"
#include "parameters/multitap_params.h"
#include "parameters/pattern_calculator.h"
#include "parameters/pingpong_params.h"
#include "parameters/reverse_params.h"
#include "parameters/shimmer_params.h"
#include "parameters/spectral_params.h"
#include "parameters/tape_params.h"
#include "ui/tap_pattern_editor.h"
// The shared controls the uidesc names register their ViewCreators when their header is included; the plugin does
// that in entry.cpp, which plugin_tests does not compile.
#include "ui/arc_knob.h"
#include "ui/fieldset_container.h"
#include "ui/icon_segment_button.h"
#include "ui/toggle_button.h"

#include <krate/dsp/core/db_utils.h>

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include <base/source/updatehandler.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/lib/controls/ccontrol.h>
#include <vstgui/plugin-bindings/vst3editor.h>
#include <vstgui/uidescription/uiviewswitchcontainer.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

using Catch::Approx;
using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;

namespace {

// ------------------------------------------------------------------------------
// Recording component handler
// ------------------------------------------------------------------------------

struct Edit {
    char kind;  // 'b' begin, 'p' perform, 'e' end
    ParamID id;
    ParamValue value;
};

class RecordingHandler : public Steinberg::Vst::IComponentHandler {
public:
    std::vector<Edit> edits;

    Steinberg::tresult PLUGIN_API beginEdit(ParamID id) override {
        edits.push_back({'b', id, 0.0});
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override {
        edits.push_back({'p', id, v});
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(ParamID id) override {
        edits.push_back({'e', id, 0.0});
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void** obj) override {
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return ++refCount_; }
    Steinberg::uint32 PLUGIN_API release() override { return --refCount_; }

    void clear() { edits.clear(); }

    [[nodiscard]] size_t count(char kind, ParamID id) const {
        size_t n = 0;
        for (const auto& e : edits) n += (e.kind == kind && e.id == id) ? 1u : 0u;
        return n;
    }
    [[nodiscard]] ParamValue lastPerform(ParamID id) const {
        ParamValue v = -1.0;
        for (const auto& e : edits) {
            if (e.kind == 'p' && e.id == id) v = e.value;
        }
        return v;
    }
    /// One begin, at least one perform, one end, in that order, for @p id.
    [[nodiscard]] bool bracketed(ParamID id) const {
        int state = 0;
        bool performed = false;
        for (const auto& e : edits) {
            if (e.id != id) continue;
            if (e.kind == 'b') {
                if (state != 0) return false;
                state = 1;
            } else if (e.kind == 'p') {
                if (state != 1) return false;
                performed = true;
            } else {
                if (state != 1) return false;
                state = 2;
            }
        }
        return state == 2 && performed;
    }
    [[nodiscard]] std::set<ParamID> performedIds() const {
        std::set<ParamID> ids;
        for (const auto& e : edits) {
            if (e.kind == 'p') ids.insert(e.id);
        }
        return ids;
    }

private:
    Steinberg::uint32 refCount_{1};
};

// ------------------------------------------------------------------------------
// The editor, headless, on MultiTap
// ------------------------------------------------------------------------------

void forEachDescendant(VSTGUI::CViewContainer* container, const std::function<void(VSTGUI::CView*)>& fn) {
    container->forEachChild([&](VSTGUI::CView* child) {
        fn(child);
        if (auto* sub = child->asViewContainer()) forEachDescendant(sub, fn);
    });
}

[[nodiscard]] VSTGUI::CControl* firstWithTag(VSTGUI::CViewContainer* root, std::int32_t tag) {
    VSTGUI::CControl* found = nullptr;
    forEachDescendant(root, [&](VSTGUI::CView* v) {
        if (found == nullptr) {
            if (auto* c = dynamic_cast<VSTGUI::CControl*>(v); c != nullptr && c->getTag() == tag) found = c;
        }
    });
    return found;
}

class TapEditorRig {
public:
    explicit TapEditorRig(const std::function<void(Iterum::Controller&)>& beforeOpen = {})
        : controller_(Steinberg::owned(new Iterum::Controller())) {
        REQUIRE(controller_->initialize(nullptr) == Steinberg::kResultOk);
        controller_->setComponentHandler(&handler);
        if (beforeOpen) beforeOpen(*controller_);
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidescPath = std::string(ITERUM_RESOURCES_DIR) + "/editor.uidesc";
        editor_ = new VSTGUI::VST3Editor(controller_.get(), "Editor", uidescPath.c_str());
        view_ = editor_;
        opened_ = view_->attached(nullptr, Krate::TestSupport::nativePlatformType()) == Steinberg::kResultTrue;
        REQUIRE(opened_);
        VSTGUI::CFrame* frame = editor_->getFrame();
        REQUIRE(frame != nullptr);
        root_ = frame->getView(0) != nullptr ? frame->getView(0)->asViewContainer() : nullptr;
        REQUIRE(root_ != nullptr);
        if (!root_->isAttached()) root_->attached(frame);
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();

        VSTGUI::UIViewSwitchContainer* panels = nullptr;
        forEachDescendant(root_, [&](VSTGUI::CView* v) {
            if (panels == nullptr) panels = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(v);
        });
        REQUIRE(panels != nullptr);
        panels->setAnimationTime(0);
        VSTGUI::CControl* mode = firstWithTag(root_, Iterum::kModeId);
        REQUIRE(mode != nullptr);
        mode->setValueNormalized(static_cast<float>(static_cast<int>(Iterum::DelayMode::MultiTap)) / 9.0f);
        mode->valueChanged();
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
        REQUIRE(view() != nullptr);
        handler.clear();
    }

    ~TapEditorRig() {
        if (view_ != nullptr) {
            if (opened_) view_->removed();
            view_->release();
        }
        controller_->terminate();
    }

    TapEditorRig(const TapEditorRig&) = delete;
    TapEditorRig& operator=(const TapEditorRig&) = delete;

    [[nodiscard]] Iterum::Controller& controller() { return *controller_; }

    /// The tap pattern editor of the MultiTap panel, looked up by its tag every time.
    [[nodiscard]] Iterum::TapPatternEditor* view() const {
        return dynamic_cast<Iterum::TapPatternEditor*>(firstWithTag(root_, Iterum::kMultiTapPatternEditorTagId));
    }

    /// A host or automation set: setParamNormalized alone.
    void set(ParamID id, ParamValue v) { controller_->setParamNormalized(id, v); }

    /// A user gesture on a bound control, as VST3Editor delivers it: setParamNormalized, then performEdit.
    void userEdit(ParamID id, ParamValue v) {
        controller_->beginEdit(id);
        controller_->setParamNormalized(id, v);
        controller_->performEdit(id, v);
        controller_->endEdit(id);
    }

    RecordingHandler handler;  // declared before the controller: it outlives it

private:
    Steinberg::IPtr<Iterum::Controller> controller_;
    VSTGUI::VST3Editor* editor_ = nullptr;
    Steinberg::IPlugView* view_ = nullptr;
    bool opened_ = false;
    VSTGUI::CViewContainer* root_ = nullptr;
};

// ------------------------------------------------------------------------------
// Values
// ------------------------------------------------------------------------------

constexpr ParamID timeId(size_t i) { return Iterum::kMultiTapCustomTime0Id + static_cast<ParamID>(i); }
constexpr ParamID levelId(size_t i) { return Iterum::kMultiTapCustomLevel0Id + static_cast<ParamID>(i); }
[[nodiscard]] ParamValue patternNorm(int p) { return static_cast<double>(p) / 19.0; }
[[nodiscard]] ParamValue spatialNorm(int s) { return static_cast<double>(s) / 6.0; }
[[nodiscard]] ParamValue tapCountNorm(int n) { return static_cast<double>(n - 2) / 14.0; }
[[nodiscard]] ParamValue tempoNorm(double bpm) { return (bpm - 20.0) / 280.0; }
[[nodiscard]] ParamValue lengthNorm(int index) { return static_cast<double>(index) / 5.0; }
[[nodiscard]] float dbGain(float dB) { return Krate::DSP::dbToGain(dB); }

constexpr int kQuarter = 2;
constexpr int kWhole = 0;
constexpr int kGolden = 14;
constexpr int kExponential = 16;
constexpr int kLinear = 18;
constexpr int kCustom = 19;
constexpr int kSpatialDecaying = 4;
constexpr int kSpatialFlat = 5;
constexpr int kSnapEighth = 11;

/// A full component state as Processor::getState writes it, MultiTap mode, with @p mt as the MultiTap block and the
/// Custom Length tail when @p withLength.
Steinberg::IPtr<Steinberg::MemoryStream> makeComponentState(const Iterum::MultiTapParams& mt, bool withLength) {
    using namespace Iterum;
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    Steinberg::IBStreamer s(stream, kLittleEndian);
    s.writeFloat(1.0f);                                                // gain
    s.writeInt32(static_cast<Steinberg::int32>(DelayMode::MultiTap));  // mode
    saveGranularParams(GranularParams{}, s);
    saveSpectralParams(SpectralParams{}, s);
    saveFreezeParams(FreezeParams{}, s);
    saveReverseParams(ReverseParams{}, s);
    saveShimmerParams(ShimmerParams{}, s);
    const TapeParams tape;
    saveTapeParams(tape, s);
    saveBBDParams(BBDParams{}, s);
    saveDigitalParams(DigitalParams{}, s);
    savePingPongParams(PingPongParams{}, s);
    saveMultiTapParams(mt, s);
    saveTapeSyncParams(tape, s);
    if (withLength) saveMultiTapLengthParams(mt, s);
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return stream;
}

/// Writes @p componentState into a .vstpreset and loads it through the PresetManager's load provider, which is the
/// controller's loadComponentStateWithNotify.
bool loadThroughPresetManager(Iterum::Controller& controller, Steinberg::MemoryStream& componentState,
                              const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / "iterum-tap-editor-test";
    std::filesystem::create_directories(dir);
    const auto path = dir / (std::string(name) + ".vstpreset");
    {
        auto* file = Steinberg::Vst::FileStream::open(path.string().c_str(), "wb");
        REQUIRE(file != nullptr);
        componentState.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        const bool saved = Steinberg::Vst::PresetFile::savePreset(file, Iterum::kProcessorUID, &componentState);
        file->release();
        REQUIRE(saved);
    }
    Krate::Plugins::PresetInfo info;
    info.name = name;
    info.path = path;
    REQUIRE(controller.getPresetManager() != nullptr);
    const bool loaded = controller.getPresetManager()->loadPreset(info);
    std::filesystem::remove(path);
    return loaded;
}

void requireCopiedTap(const RecordingHandler& h, size_t i, double ratio, double level) {
    INFO("tap " << i + 1);
    REQUIRE(h.bracketed(timeId(i)));
    REQUIRE(h.bracketed(levelId(i)));
    REQUIRE(h.lastPerform(timeId(i)) == Approx(ratio).margin(1e-4));
    REQUIRE(h.lastPerform(levelId(i)) == Approx(level).margin(1e-4));
}

void requireHiddenTapsUntouched(const RecordingHandler& h, size_t n) {
    for (size_t i = n; i < 16; ++i) {
        INFO("hidden tap " << i + 1);
        REQUIRE(h.count('b', timeId(i)) == 0u);
        REQUIRE(h.count('p', timeId(i)) == 0u);
        REQUIRE(h.count('b', levelId(i)) == 0u);
        REQUIRE(h.count('p', levelId(i)) == 0u);
    }
}

void requireCopiedLengthAndCustom(const RecordingHandler& h, double length) {
    REQUIRE(h.bracketed(Iterum::kMultiTapCustomLengthId));
    REQUIRE(h.lastPerform(Iterum::kMultiTapCustomLengthId) == Approx(length).margin(1e-9));
    REQUIRE(h.bracketed(Iterum::kMultiTapTimingPatternId));
    REQUIRE(h.lastPerform(Iterum::kMultiTapTimingPatternId) == Approx(1.0).margin(1e-9));
}

} // namespace

using namespace Iterum;

// ==============================================================================
// Copy to Custom (design 2.6)
// ==============================================================================

TEST_CASE("Copy of Quarter with 6 taps writes 6 times, 6 levels, Length and Custom, each bracketed, count decoded 2 + 14n",
          "[iterum][ui][tap-editor]") {
    TapEditorRig rig;
    rig.set(kMultiTapTimingPatternId, patternNorm(kQuarter));
    rig.set(kMultiTapTapCountId, tapCountNorm(6));
    rig.handler.clear();
    rig.controller().copyCurrentPatternToCustom();

    for (size_t i = 0; i < 6; ++i) {
        requireCopiedTap(rig.handler, i, 0.125 * static_cast<double>(i + 1), dbGain(-3.0f * static_cast<float>(i)));
        REQUIRE(rig.controller().getParamNormalized(timeId(i)) == Approx(0.125 * static_cast<double>(i + 1)).margin(1e-4));
    }
    requireHiddenTapsUntouched(rig.handler, 6);
    requireCopiedLengthAndCustom(rig.handler, lengthNorm(5));  // 2/1
    REQUIRE(rig.handler.performedIds().size() == 14u);         // 2n + 2
}

TEST_CASE("Copy under Spatial Flat writes level 1.0 for every tap", "[iterum][ui][tap-editor]") {
    TapEditorRig rig;
    rig.set(kMultiTapTimingPatternId, patternNorm(kQuarter));
    rig.set(kMultiTapTapCountId, tapCountNorm(6));
    rig.set(kMultiTapSpatialPatternId, spatialNorm(kSpatialFlat));
    rig.handler.clear();
    rig.controller().copyCurrentPatternToCustom();
    for (size_t i = 0; i < 6; ++i) {
        requireCopiedTap(rig.handler, i, 0.125 * static_cast<double>(i + 1), 1.0);
    }
}

TEST_CASE("Copy of Exponential keeps -3 dB per tap", "[iterum][ui][tap-editor]") {
    // Note Value 1/4 at 120 BPM: 500, 1000, 2000, 4000 ms = 1, 2, 4, 8 beats, no clamp: Length 2/1.
    TapEditorRig rig;
    rig.set(kMultiTapTimingPatternId, patternNorm(kExponential));
    rig.set(kMultiTapNoteValueId, 2.0 / 9.0);
    rig.set(kMultiTapTapCountId, tapCountNorm(4));
    rig.handler.clear();
    rig.controller().copyCurrentPatternToCustom();
    const double ratios[4] = {0.125, 0.25, 0.5, 1.0};
    for (size_t i = 0; i < 4; ++i) {
        requireCopiedTap(rig.handler, i, ratios[i], dbGain(-3.0f * static_cast<float>(i)));
    }
    requireCopiedLengthAndCustom(rig.handler, lengthNorm(5));
}

TEST_CASE("Copy of Quarter with 16 taps at 120 BPM goes to Free at the played times", "[iterum][ui][tap-editor]") {
    TapEditorRig rig;
    rig.set(kMultiTapTimingPatternId, patternNorm(kQuarter));
    rig.set(kMultiTapTapCountId, tapCountNorm(16));
    rig.handler.clear();
    rig.controller().copyCurrentPatternToCustom();
    for (size_t i = 0; i < 16; ++i) {
        const double ratio = i < 9 ? 0.1 * static_cast<double>(i + 1) : 1.0;  // 500 ms steps, then 5 s
        requireCopiedTap(rig.handler, i, ratio, dbGain(-3.0f * static_cast<float>(i)));
    }
    requireCopiedLengthAndCustom(rig.handler, lengthNorm(0));
}

TEST_CASE("Copy of Whole with 4 taps at 120 BPM", "[iterum][ui][tap-editor]") {
    TapEditorRig rig;
    rig.set(kMultiTapTimingPatternId, patternNorm(kWhole));
    rig.set(kMultiTapTapCountId, tapCountNorm(4));
    rig.handler.clear();
    rig.controller().copyCurrentPatternToCustom();
    const double ratios[4] = {0.4, 0.8, 1.0, 1.0};
    for (size_t i = 0; i < 4; ++i) {
        requireCopiedTap(rig.handler, i, ratios[i], dbGain(-3.0f * static_cast<float>(i)));
    }
    requireCopiedLengthAndCustom(rig.handler, lengthNorm(0));
}

TEST_CASE("Copy of Golden with 8 taps at 120 BPM, Note Value 1/4", "[iterum][ui][tap-editor]") {
    TapEditorRig rig;
    rig.set(kMultiTapTimingPatternId, patternNorm(kGolden));
    rig.set(kMultiTapNoteValueId, 2.0 / 9.0);
    rig.set(kMultiTapTapCountId, tapCountNorm(8));
    rig.handler.clear();
    rig.controller().copyCurrentPatternToCustom();
    const double ratios[8] = {0.1, 0.1618, 0.2618, 0.4236, 0.6854, 1.0, 1.0, 1.0};
    for (size_t i = 0; i < 8; ++i) {
        INFO("tap " << i + 1);
        REQUIRE(rig.handler.bracketed(timeId(i)));
        REQUIRE(rig.handler.lastPerform(timeId(i)) == Approx(ratios[i]).margin(1e-3));
    }
    requireCopiedLengthAndCustom(rig.handler, lengthNorm(0));
}

TEST_CASE("Copy of Linear with 16 taps at 90 BPM goes to Free at the played times", "[iterum][ui][tap-editor]") {
    TapEditorRig rig;
    rig.set(kMultiTapTempoId, tempoNorm(90.0));
    rig.set(kMultiTapTimingPatternId, patternNorm(kLinear));
    rig.set(kMultiTapNoteValueId, 2.0 / 9.0);
    rig.set(kMultiTapTapCountId, tapCountNorm(16));
    rig.handler.clear();
    rig.controller().copyCurrentPatternToCustom();

    REQUIRE(rig.handler.lastPerform(timeId(1)) == Approx(955.6 / 5000.0).margin(1e-4));
    for (size_t i = 0; i < 16; ++i) {
        INFO("tap " << i + 1);
        REQUIRE(rig.handler.lastPerform(timeId(i)) ==
                Approx(patternTap(kLinear, 2, 0, i, 16, 90.0).ms / 5000.0).margin(1e-4));
    }
    requireCopiedLengthAndCustom(rig.handler, lengthNorm(0));
}

TEST_CASE("Copy does not move the taps on screen", "[iterum][ui][tap-editor]") {
    TapEditorRig rig;
    rig.set(kMultiTapTimingPatternId, patternNorm(kQuarter));
    rig.set(kMultiTapTapCountId, tapCountNorm(6));
    auto* view = rig.view();
    REQUIRE(view != nullptr);
    REQUIRE_FALSE(view->isEditable());
    REQUIRE(view->getPresetLength() == CustomLength::N2x1);
    float before[6] = {};
    for (size_t i = 0; i < 6; ++i) before[i] = view->getPresetTapTimeRatio(i);

    rig.controller().copyCurrentPatternToCustom();
    view = rig.view();
    REQUIRE(view != nullptr);
    REQUIRE(view->isEditable());
    REQUIRE(view->getCustomLength() == CustomLength::N2x1);
    for (size_t i = 0; i < 6; ++i) {
        INFO("tap " << i + 1);
        REQUIRE(view->getTapTimeRatio(i) == Approx(before[i]).margin(1e-6));
    }
}

// ==============================================================================
// The parameter bridge (design 3.6)
// ==============================================================================

TEST_CASE("a set of 950 or 966 is reflected in the view immediately", "[iterum][ui][tap-editor]") {
    TapEditorRig rig;
    rig.set(kMultiTapTimingPatternId, patternNorm(kCustom));
    auto* view = rig.view();
    REQUIRE(view != nullptr);

    rig.controller().setParamNormalized(kMultiTapCustomTime0Id, 0.3);
    REQUIRE(view->getTapTimeRatio(0) == Approx(0.3f));
    rig.controller().setParamNormalized(kMultiTapCustomLevel0Id, 0.4);
    REQUIRE(view->getTapLevel(0) == Approx(0.4f));

    SECTION("Snap, Length and Taps arrive the same way") {
        rig.controller().setParamNormalized(kMultiTapSnapDivisionId, static_cast<double>(kSnapEighth) / 21.0);
        REQUIRE(view->getSnapDivision() == SnapDivision::N8);
        rig.controller().setParamNormalized(kMultiTapCustomLengthId, lengthNorm(2));
        REQUIRE(view->getCustomLength() == CustomLength::N4);
        rig.controller().setParamNormalized(kMultiTapTapCountId, tapCountNorm(9));
        REQUIRE(view->getActiveTapCount() == 9u);
    }
}

TEST_CASE("a bulk load pushes once, after the load", "[iterum][ui][tap-editor]") {
    TapEditorRig rig;
    rig.set(kMultiTapTimingPatternId, patternNorm(kCustom));
    REQUIRE(rig.view() != nullptr);

    MultiTapParams mt;
    mt.timingPattern.store(kCustom);
    for (size_t i = 0; i < 16; ++i) {
        mt.customTimeRatios[i].store(0.03f * static_cast<float>(i + 1));
        mt.customLevels[i].store(0.5f);
    }
    auto state = makeComponentState(mt, true);

    const auto fullBefore = rig.controller().tapEditorFullPushCountForTest();
    const auto idBefore = rig.controller().tapEditorIdPushCountForTest();
    REQUIRE(rig.controller().setComponentState(state) == Steinberg::kResultOk);
    CHECK(rig.controller().tapEditorIdPushCountForTest() == idBefore);
    CHECK(rig.controller().tapEditorFullPushCountForTest() == fullBefore + 1);

    auto* view = rig.view();
    REQUIRE(view != nullptr);
    for (size_t i = 0; i < 16; ++i) {
        INFO("tap " << i + 1);
        CHECK(view->getTapTimeRatio(i) == Approx(0.03f * static_cast<float>(i + 1)).margin(1e-6));
        CHECK(view->getTapLevel(i) == Approx(0.5f));
    }
}

TEST_CASE("a MultiTap preset loaded while MultiTap is open still drives the view afterwards", "[iterum][ui][tap-editor]") {
    TapEditorRig rig;
    MultiTapParams mt;
    mt.timingPattern.store(kCustom);
    auto first = makeComponentState(mt, true);
    auto second = makeComponentState(mt, true);
    REQUIRE(rig.controller().setComponentState(first) == Steinberg::kResultOk);
    REQUIRE(rig.controller().setComponentState(second) == Steinberg::kResultOk);

    rig.controller().setParamNormalized(kMultiTapSnapDivisionId, static_cast<double>(kSnapEighth) / 21.0);
    auto* view = rig.view();
    REQUIRE(view != nullptr);
    CHECK(view->getSnapDivision() == SnapDivision::N8);
    rig.controller().setParamNormalized(kMultiTapCustomTime0Id + 2, 0.66);
    CHECK(view->getTapTimeRatio(2) == Approx(0.66f));
}

// ==============================================================================
// Placement on a user Taps gesture only (design 3.3)
// ==============================================================================

TEST_CASE("performEdit on Taps after a user-path increase places colliding taps, automation does not",
          "[iterum][ui][tap-editor]") {
    TapEditorRig rig;
    rig.set(kMultiTapTimingPatternId, patternNorm(kCustom));
    rig.set(timeId(4), 0.5);  // tap 5 stored on tap 2 (registered taps 1-4: 0.25, 0.5, 0.75, 1.0)
    rig.handler.clear();

    rig.set(kMultiTapTapCountId, tapCountNorm(5));  // automation: reveal only
    REQUIRE(rig.view()->getActiveTapCount() == 5u);
    CHECK(rig.handler.count('p', timeId(4)) == 0u);
    CHECK(rig.controller().getParamNormalized(timeId(4)) == Approx(0.5));
    rig.set(kMultiTapTapCountId, tapCountNorm(4));
    rig.handler.clear();

    rig.userEdit(kMultiTapTapCountId, tapCountNorm(5));
    CHECK(rig.handler.bracketed(timeId(4)));
    // Every 1/4 line is taken; the 1/16 retry from the ideal 1.0 lands on 0.9375.
    CHECK(rig.handler.lastPerform(timeId(4)) == Approx(0.9375).margin(1e-4));
    CHECK(rig.controller().getParamNormalized(timeId(4)) == Approx(0.9375).margin(1e-4));
    CHECK(rig.view()->getTapTimeRatio(4) == Approx(0.9375f).margin(1e-4));
    CHECK(rig.handler.count('p', levelId(4)) == 0u);
}

TEST_CASE("automation 4 -> 8, then a user 8 -> 9 moves only tap 9", "[iterum][ui][tap-editor]") {
    TapEditorRig rig;
    rig.set(kMultiTapTimingPatternId, patternNorm(kCustom));
    for (size_t i = 4; i < 8; ++i) rig.set(timeId(i), 0.5);  // taps 5-8 stored as a stack
    rig.set(timeId(8), 0.25);                                // tap 9 collides with tap 1
    rig.set(kMultiTapTapCountId, tapCountNorm(8));           // automation
    rig.handler.clear();

    rig.userEdit(kMultiTapTapCountId, tapCountNorm(9));
    for (size_t i = 0; i < 16; ++i) {
        INFO("tap " << i + 1);
        CHECK(rig.handler.count('p', timeId(i)) == (i == 8 ? 1u : 0u));
        CHECK(rig.handler.count('p', levelId(i)) == 0u);
    }
    CHECK(rig.handler.bracketed(timeId(8)));
    for (size_t i = 4; i < 8; ++i) {
        CHECK(rig.controller().getParamNormalized(timeId(i)) == Approx(0.5));
    }
    CHECK(rig.controller().getParamNormalized(timeId(8)) != Approx(0.25));
}

TEST_CASE("a decrease gesture writes nothing, even after a non-gesture increase or editor open", "[iterum][ui][tap-editor]") {
    // The editor opens at 8 taps (raised from the initial 4 without a gesture) over stacked stored taps.
    TapEditorRig rig([](Controller& c) {
        c.setParamNormalized(kMultiTapTimingPatternId, 1.0);
        for (size_t i = 4; i < 8; ++i) c.setParamNormalized(timeId(i), 0.5);
        c.setParamNormalized(kMultiTapTapCountId, tapCountNorm(8));
    });
    REQUIRE(rig.view()->getActiveTapCount() == 8u);

    rig.userEdit(kMultiTapTapCountId, tapCountNorm(7));
    for (size_t i = 0; i < 16; ++i) {
        INFO("tap " << i + 1);
        CHECK(rig.handler.count('b', timeId(i)) == 0u);
        CHECK(rig.handler.count('p', timeId(i)) == 0u);
        CHECK(rig.handler.count('p', levelId(i)) == 0u);
    }
    for (size_t i = 4; i < 8; ++i) {
        CHECK(rig.controller().getParamNormalized(timeId(i)) == Approx(0.5));
    }
}

// ==============================================================================
// Tempo and the read-only pattern (design 2.4, 5.9)
// ==============================================================================

TEST_CASE("the tempo parameter reaches the view and the read-only pattern", "[iterum][ui][tap-editor]") {
    TapEditorRig rig;
    rig.set(kMultiTapTimingPatternId, patternNorm(kQuarter));
    rig.set(kMultiTapTapCountId, tapCountNorm(4));
    auto* view = rig.view();
    REQUIRE(view != nullptr);
    REQUIRE(view->getTempo() == Approx(120.0));  // the parameter's default before any block

    rig.controller().setParamNormalized(kMultiTapTempoId, tempoNorm(90.0));
    CHECK(view->getTempo() == Approx(90.0).margin(1e-6));
    CHECK_FALSE(view->isEditable());
    CHECK(view->getPresetLength() == CustomLength::N1);
    CHECK(view->getPresetTapTimeMs(0) == Approx(666.7f).margin(0.5));
    CHECK(view->getPresetTapTimeMs(3) == Approx(2666.7f).margin(0.5));
}

TEST_CASE("Spatial changes recompute the read-only levels", "[iterum][ui][tap-editor]") {
    TapEditorRig rig;
    rig.set(kMultiTapTimingPatternId, patternNorm(kQuarter));
    rig.set(kMultiTapTapCountId, tapCountNorm(4));
    auto* view = rig.view();
    REQUIRE(view != nullptr);

    rig.controller().setParamNormalized(kMultiTapSpatialPatternId, spatialNorm(kSpatialDecaying));
    for (size_t i = 0; i < 4; ++i) {
        CHECK(view->getPresetTapLevel(i) == Approx(dbGain(-3.0f * static_cast<float>(i))).margin(1e-4));
    }
    rig.controller().setParamNormalized(kMultiTapSpatialPatternId, spatialNorm(kSpatialFlat));
    for (size_t i = 0; i < 4; ++i) {
        CHECK(view->getPresetTapLevel(i) == Approx(1.0f));
    }
}

// ==============================================================================
// The Length tail on every load path (design 7.1)
// ==============================================================================

TEST_CASE("setComponentState of a stream without the Length tail sets 914 to 0.0 (Free); a stream with it sets v/5",
          "[iterum][ui][tap-editor]") {
    auto controller = Steinberg::owned(new Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    REQUIRE(controller->getParamNormalized(kMultiTapCustomLengthId) == Approx(0.8));  // fresh: 1/1

    MultiTapParams mt;
    mt.customLength.store(2);

    SECTION("setComponentState") {
        auto without = makeComponentState(mt, false);
        REQUIRE(controller->setComponentState(without) == Steinberg::kResultOk);
        CHECK(controller->getParamNormalized(kMultiTapCustomLengthId) == 0.0);

        auto with = makeComponentState(mt, true);
        REQUIRE(controller->setComponentState(with) == Steinberg::kResultOk);
        CHECK(controller->getParamNormalized(kMultiTapCustomLengthId) == Approx(0.4));
    }

    SECTION("loadComponentStateWithNotify (the preset browser's load)") {
        auto without = makeComponentState(mt, false);
        REQUIRE(loadThroughPresetManager(*controller, *without, "length-tail-absent"));
        CHECK(controller->getParamNormalized(kMultiTapCustomLengthId) == 0.0);

        auto with = makeComponentState(mt, true);
        REQUIRE(loadThroughPresetManager(*controller, *with, "length-tail-present"));
        CHECK(controller->getParamNormalized(kMultiTapCustomLengthId) == Approx(0.4));
    }

    controller->terminate();
}

// ==============================================================================
// Host strings (design 4.4)
// ==============================================================================

TEST_CASE("typed host strings parse on Custom Time and Level", "[iterum][ui][tap-editor]") {
    auto controller = Steinberg::owned(new Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

    auto parse = [&](ParamID id, const char* text) {
        Steinberg::Vst::String128 s = {};
        Steinberg::UString(s, 128).fromAscii(text);
        ParamValue v = -1.0;
        REQUIRE(controller->getParamValueByString(id, s, v) == Steinberg::kResultOk);
        return v;
    };

    controller->setParamNormalized(kMultiTapCustomLengthId, lengthNorm(0));  // Free
    CHECK(parse(kMultiTapCustomTime0Id, "750.0 ms") == Approx(0.15).margin(1e-6));
    controller->setParamNormalized(kMultiTapCustomLengthId, lengthNorm(4));  // 1/1 at the default 120 BPM
    CHECK(parse(kMultiTapCustomTime0Id, "750.0 ms") == Approx(0.375).margin(1e-6));
    CHECK(parse(kMultiTapCustomLevel0Id, "-6.0 dB") == Approx(0.501).margin(1e-3));

    SECTION("and the host shows them in ms of the span") {
        Steinberg::Vst::String128 s = {};
        controller->setParamNormalized(kMultiTapCustomLengthId, lengthNorm(0));
        REQUIRE(controller->getParamStringByValue(kMultiTapCustomTime0Id, 0.15, s) == Steinberg::kResultOk);
        std::string text;
        for (int i = 0; i < 128 && s[i] != 0; ++i) text += static_cast<char>(s[i]);
        CHECK(text == "750.0 ms");
    }

    controller->terminate();
}
