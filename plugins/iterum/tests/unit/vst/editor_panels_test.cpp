// ==============================================================================
// Editor panel structure, one case per delay mode ([iterum][ui]; design-system.md 9.4 step 3)
// ==============================================================================
// Opens the real editor.uidesc headlessly, switches the Mode control through all ten delay modes and checks, for
// each mode, that every control-tag of section 7 of specs/iterum-ui-redesign/design-system.md is live and built as
// the widget the design names (an unregistered class comes out of the factory as a plain CViewContainer, which the
// container count catches). Further cases cover the conditional groups (8.2) and the Sync retag at panel build (9.3).
//
// Every search starts at the template root (frame child 0), never at the frame: the preset browser overlay is frame
// child 1 and reuses tags 0-31.

#include <editor_lifecycle_harness.h>

#include "controller/controller.h"
#include "controller/custom_views.h"
#include "controller/delay_time_sync_controller.h"
#include "delay_mode.h"
#include "plugin_ids.h"
#include "ui/tap_pattern_editor.h"
// The shared controls the uidesc names register their ViewCreators when their header is included; the plugin does
// that in entry.cpp, which plugin_tests does not compile.
#include "ui/arc_knob.h"
#include "ui/fieldset_container.h"
#include "ui/icon_segment_button.h"
#include "ui/toggle_button.h"

#include <base/source/updatehandler.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/lib/controls/ccontrol.h>
#include <vstgui/lib/controls/coptionmenu.h>
#include <vstgui/lib/controls/cparamdisplay.h>
#include <vstgui/lib/controls/csegmentbutton.h>
#include <vstgui/lib/controls/cslider.h>
#include <vstgui/lib/controls/ctextlabel.h>
#include <vstgui/plugin-bindings/vst3editor.h>
#include <vstgui/uidescription/uiattributes.h>
#include <vstgui/uidescription/uiviewswitchcontainer.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <typeinfo>
#include <vector>

namespace {

using VSTGUI::CControl;
using VSTGUI::CView;
using VSTGUI::CViewContainer;

constexpr std::int32_t kDigitalAgeLabelTag = 9902;  // the Age label's control-tag (controller.cpp, DigitalAgeLabel)
constexpr int kModeCount = static_cast<int>(Iterum::DelayMode::NumModes);

void forEachDescendant(CViewContainer* container, const std::function<void(CView*)>& fn) {
    container->forEachChild([&](CView* child) {
        fn(child);
        if (auto* sub = child->asViewContainer()) {
            forEachDescendant(sub, fn);
        }
    });
}

[[nodiscard]] std::vector<CControl*> controlsWithTag(CViewContainer* root, std::int32_t tag) {
    std::vector<CControl*> found;
    forEachDescendant(root, [&](CView* v) {
        if (auto* c = dynamic_cast<CControl*>(v); c != nullptr && c->getTag() == tag) {
            found.push_back(c);
        }
    });
    return found;
}

[[nodiscard]] CControl* firstWithTag(CViewContainer* root, std::int32_t tag) {
    const auto all = controlsWithTag(root, tag);
    return all.empty() ? nullptr : all.front();
}

/// A readout: a CParamDisplay that is not a CTextLabel (CTextLabel derives from CParamDisplay).
[[nodiscard]] bool isReadout(const CControl* c) {
    return dynamic_cast<const VSTGUI::CParamDisplay*>(c) != nullptr && dynamic_cast<const VSTGUI::CTextLabel*>(c) == nullptr;
}

/// The view and every ancestor up to (not including) @p root are visible.
[[nodiscard]] bool effectivelyVisible(const CView* view, const CView* root) {
    for (const CView* v = view; v != nullptr && v != root; v = v->getParentView()) {
        if (!v->isVisible()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] VSTGUI::UIViewSwitchContainer* findSwitchContainer(CViewContainer* root) {
    VSTGUI::UIViewSwitchContainer* found = nullptr;
    forEachDescendant(root, [&](CView* v) {
        if (found == nullptr) {
            found = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(v);
        }
    });
    return found;
}

/// The real editor, opened headlessly on a fresh controller.
class HeadlessEditor {
public:
    /// @p beforeOpen runs on the initialized controller before the editor is built (e.g. to set Sync state).
    explicit HeadlessEditor(const std::function<void(Iterum::Controller&)>& beforeOpen = {})
        : controller_(Steinberg::owned(new Iterum::Controller())) {
        REQUIRE(controller_->initialize(nullptr) == Steinberg::kResultOk);
        if (beforeOpen) {
            beforeOpen(*controller_);
        }
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
        // A headless frame has no window, so CFrame::open never attaches the tree.
        if (!root_->isAttached()) {
            root_->attached(frame);
        }
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();

        panels_ = findSwitchContainer(root_);
        REQUIRE(panels_ != nullptr);
        // No timer runs here: an animated swap would only complete at the next switch.
        panels_->setAnimationTime(0);
        modeControl_ = firstWithTag(root_, Iterum::kModeId);
        REQUIRE(modeControl_ != nullptr);
    }

    ~HeadlessEditor() {
        if (view_ != nullptr) {
            if (opened_) {
                view_->removed();
            }
            view_->release();
        }
        controller_->terminate();
    }

    HeadlessEditor(const HeadlessEditor&) = delete;
    HeadlessEditor& operator=(const HeadlessEditor&) = delete;
    HeadlessEditor(HeadlessEditor&&) = delete;
    HeadlessEditor& operator=(HeadlessEditor&&) = delete;

    void switchTo(int mode) {
        modeControl_->setValueNormalized(static_cast<float>(mode) / static_cast<float>(kModeCount - 1));
        modeControl_->valueChanged();
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
    }

    void setParam(Steinberg::Vst::ParamID id, double normalized) {
        controller_->setParamNormalized(id, normalized);
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
    }

    [[nodiscard]] CViewContainer* root() const { return root_; }
    /// The current mode panel: the switch container's first child.
    [[nodiscard]] CViewContainer* panel() const {
        CView* first = panels_->getView(0);
        return first != nullptr ? first->asViewContainer() : nullptr;
    }

private:
    Steinberg::IPtr<Iterum::Controller> controller_;
    VSTGUI::VST3Editor* editor_ = nullptr;
    Steinberg::IPlugView* view_ = nullptr;
    bool opened_ = false;
    CViewContainer* root_ = nullptr;
    VSTGUI::UIViewSwitchContainer* panels_ = nullptr;
    CControl* modeControl_ = nullptr;
};

/// Sync off in every mode that has it, so every time knob carries its free-time tag.
void syncOffEverywhere(Iterum::Controller& c) {
    for (auto id : {Iterum::kGranularTimeModeId, Iterum::kSpectralTimeModeId, Iterum::kShimmerTimeModeId,
                    Iterum::kBBDTimeModeId, Iterum::kDigitalTimeModeId, Iterum::kPingPongTimeModeId,
                    Iterum::kReverseTimeModeId, Iterum::kTapeTimeModeId}) {
        c.setParamNormalized(id, 0.0);
    }
}

// ------------------------------------------------------------------------------
// Section 7 tables
// ------------------------------------------------------------------------------

/// Widget kinds of design-system.md section 7 (K knob, S slider, B bipolar, SEG segment, M menu / core select,
/// T toggle, L the Digital Age label, and the three MultiTap custom views).
enum class W { K, S, B, SEG, M, T, L, TapEditor, CopyButton, ResetButton };

struct Row {
    std::int32_t tag;
    W kind;
};

struct PanelSpec {
    const char* name;
    int mode;
    std::vector<Row> rows;
    std::vector<std::int32_t> absentTags;  // the NoteValue tag while Sync is off
    int fieldsets;
    int plainContainers;  // core cells + wrappers + centre ticks
};

[[nodiscard]] const char* kindName(W k) {
    switch (k) {
        case W::K: return "ArcKnob";
        case W::S: return "CSlider";
        case W::B: return "CSlider (draw-value-from-center)";
        case W::SEG: return "CSegmentButton";
        case W::M: return "COptionMenu";
        case W::T: return "ToggleButton";
        case W::L: return "CTextLabel";
        case W::TapEditor: return "TapPatternEditor";
        case W::CopyButton: return "CopyPatternButton";
        case W::ResetButton: return "ResetPatternButton";
    }
    return "?";
}

[[nodiscard]] bool isKind(CControl* c, W k) {
    switch (k) {
        case W::K: return dynamic_cast<Krate::Plugins::ArcKnob*>(c) != nullptr;
        case W::S: return dynamic_cast<VSTGUI::CSlider*>(c) != nullptr;
        case W::B: {
            auto* s = dynamic_cast<VSTGUI::CSlider*>(c);
            return s != nullptr && (s->getDrawStyle() & VSTGUI::CSlider::kDrawValueFromCenter) != 0;
        }
        case W::SEG: return dynamic_cast<VSTGUI::CSegmentButton*>(c) != nullptr;
        case W::M: return dynamic_cast<VSTGUI::COptionMenu*>(c) != nullptr;
        case W::T: return dynamic_cast<Krate::Plugins::ToggleButton*>(c) != nullptr;
        case W::L: return dynamic_cast<VSTGUI::CTextLabel*>(c) != nullptr;
        case W::TapEditor: return dynamic_cast<Iterum::TapPatternEditor*>(c) != nullptr;
        case W::CopyButton: return dynamic_cast<CopyPatternButton*>(c) != nullptr;
        case W::ResetButton: return dynamic_cast<ResetPatternButton*>(c) != nullptr;
    }
    return false;
}

[[nodiscard]] bool needsReadout(W k) { return k == W::K || k == W::S || k == W::B; }

void checkPanel(const PanelSpec& spec) {
    HeadlessEditor ed(syncOffEverywhere);
    ed.switchTo(spec.mode);
    CViewContainer* panel = ed.panel();
    REQUIRE(panel != nullptr);
    INFO("panel " << spec.name);

    for (const Row& row : spec.rows) {
        INFO("tag " << row.tag << " expected " << kindName(row.kind));
        const auto controls = controlsWithTag(panel, row.tag);
        bool hasKind = false;
        bool hasReadout = false;
        for (auto* c : controls) {
            hasKind = hasKind || isKind(c, row.kind);
            hasReadout = hasReadout || isReadout(c);
        }
        CHECK(hasKind);
        if (needsReadout(row.kind)) {
            CHECK(hasReadout);
        }
    }
    for (std::int32_t tag : spec.absentTags) {
        INFO("tag " << tag << " must have no view while Sync is off");
        CHECK(controlsWithTag(panel, tag).empty());
    }

    int fieldsets = 0;
    int plain = 0;
    forEachDescendant(panel, [&](CView* v) {
        if (dynamic_cast<Krate::Plugins::FieldsetContainer*>(v) != nullptr) {
            ++fieldsets;
        }
        if (typeid(*v) == typeid(CViewContainer)) {
            ++plain;
        }
    });
    CHECK(fieldsets == spec.fieldsets);
    CHECK(plain == spec.plainContainers);
}

}  // namespace

using namespace Iterum;

// ==============================================================================
// Chrome
// ==============================================================================

TEST_CASE("Iterum editor chrome: Mode strip, Gain knob and Gain readout", "[iterum][ui]") {
    HeadlessEditor ed;
    CViewContainer* root = ed.root();

    bool modeStrip = false;
    for (auto* c : controlsWithTag(root, kModeId)) {
        modeStrip = modeStrip || dynamic_cast<Krate::Plugins::IconSegmentButton*>(c) != nullptr;
    }
    CHECK(modeStrip);

    bool gainKnob = false;
    bool gainReadout = false;
    for (auto* c : controlsWithTag(root, kGainId)) {
        gainKnob = gainKnob || dynamic_cast<Krate::Plugins::ArcKnob*>(c) != nullptr;
        gainReadout = gainReadout || isReadout(c);
    }
    CHECK(gainKnob);
    CHECK(gainReadout);

    // The editor is the fixed 940 x 608 window of section 2.1.
    CHECK(root->getViewSize().getWidth() == 940);
    CHECK(root->getViewSize().getHeight() == 608);
}

// ==============================================================================
// Every control of every mode is live and of the designed class (section 7)
// ==============================================================================

TEST_CASE("Iterum editor panel Granular: every control live and of the designed class", "[iterum][ui]") {
    checkPanel({"GranularPanel", 0,
                {{kGranularDelayTimeId, W::K}, {kGranularTimeModeId, W::T}, {kGranularFeedbackId, W::K},
                 {kGranularMixId, W::K}, {kGranularFreezeId, W::T}, {kGranularGrainSizeId, W::S},
                 {kGranularDensityId, W::S}, {kGranularEnvelopeTypeId, W::SEG}, {kGranularPitchId, W::B},
                 {kGranularPitchQuantId, W::M}, {kGranularPitchSprayId, W::S}, {kGranularPositionSprayId, W::S},
                 {kGranularPanSprayId, W::S}, {kGranularReverseProbId, W::S}, {kGranularTextureId, W::S},
                 {kGranularJitterId, W::S}, {kGranularStereoWidthId, W::S}},
                {kGranularNoteValueId}, 5, 4});
}

TEST_CASE("Iterum editor panel Spectral: every control live and of the designed class", "[iterum][ui]") {
    checkPanel({"SpectralPanel", 1,
                {{kSpectralBaseDelayId, W::K}, {kSpectralTimeModeId, W::T}, {kSpectralFeedbackId, W::K},
                 {kSpectralMixId, W::K}, {kSpectralFreezeId, W::T}, {kSpectralSpreadId, W::S},
                 {kSpectralSpreadDirectionId, W::SEG}, {kSpectralSpreadCurveId, W::SEG},
                 {kSpectralFeedbackTiltId, W::B}, {kSpectralDiffusionId, W::S}, {kSpectralFFTSizeId, W::SEG},
                 {kSpectralStereoWidthId, W::S}},
                {kSpectralNoteValueId}, 3, 4});
}

TEST_CASE("Iterum editor panel Shimmer: every control live and of the designed class", "[iterum][ui]") {
    checkPanel({"ShimmerPanel", 2,
                {{kShimmerDelayTimeId, W::K}, {kShimmerTimeModeId, W::T}, {kShimmerFeedbackId, W::K},
                 {kShimmerMixId, W::K}, {kShimmerPitchSemitonesId, W::B}, {kShimmerPitchCentsId, W::B},
                 {kShimmerPitchBlendId, W::S}, {kShimmerDiffusionSizeId, W::S}, {kShimmerFilterEnabledId, W::T},
                 {kShimmerFilterCutoffId, W::S}},
                {kShimmerNoteValueId}, 3, 5});
}

TEST_CASE("Iterum editor panel Tape: every control live and of the designed class", "[iterum][ui]") {
    checkPanel({"TapePanel", 3,
                {{kTapeMotorSpeedId, W::K}, {kTapeTimeModeId, W::T}, {kTapeFeedbackId, W::K}, {kTapeMixId, W::K},
                 {kTapeMotorInertiaId, W::S}, {kTapeWearId, W::S}, {kTapeSaturationId, W::S}, {kTapeAgeId, W::S},
                 {kTapeSpliceEnabledId, W::T}, {kTapeSpliceIntensityId, W::S}, {kTapeHead1EnabledId, W::T},
                 {kTapeHead1LevelId, W::S}, {kTapeHead1PanId, W::B}, {kTapeHead2EnabledId, W::T},
                 {kTapeHead2LevelId, W::S}, {kTapeHead2PanId, W::B}, {kTapeHead3EnabledId, W::T},
                 {kTapeHead3LevelId, W::S}, {kTapeHead3PanId, W::B}},
                {kTapeNoteValueId}, 4, 6});
}

TEST_CASE("Iterum editor panel BBD: every control live and of the designed class", "[iterum][ui]") {
    checkPanel({"BBDPanel", 4,
                {{kBBDDelayTimeId, W::K}, {kBBDTimeModeId, W::T}, {kBBDFeedbackId, W::K}, {kBBDMixId, W::K},
                 {kBBDEraId, W::SEG}, {kBBDAgeId, W::S}, {kBBDModDepthId, W::S}, {kBBDModRateId, W::S}},
                {kBBDNoteValueId}, 2, 3});
}

TEST_CASE("Iterum editor panel Digital: every control live and of the designed class", "[iterum][ui]") {
    checkPanel({"DigitalPanel", 5,
                {{kDigitalDelayTimeId, W::K}, {kDigitalTimeModeId, W::T}, {kDigitalFeedbackId, W::K},
                 {kDigitalMixId, W::K}, {kDigitalEraId, W::SEG}, {kDigitalAgeId, W::S},
                 {kDigitalAgeLabelTag, W::L}, {kDigitalLimiterCharacterId, W::SEG},
                 {kDigitalWavefoldAmountId, W::S}, {kDigitalWavefoldSymmetryId, W::B},
                 {kDigitalWavefoldTypeId, W::SEG}, {kDigitalModDepthId, W::S}, {kDigitalModRateId, W::S},
                 {kDigitalModWaveformId, W::M}, {kDigitalWidthId, W::S}},
                {kDigitalNoteValueId}, 4, 4});
}

TEST_CASE("Iterum editor panel PingPong: every control live and of the designed class", "[iterum][ui]") {
    checkPanel({"PingPongPanel", 6,
                {{kPingPongDelayTimeId, W::K}, {kPingPongTimeModeId, W::T}, {kPingPongFeedbackId, W::K},
                 {kPingPongMixId, W::K}, {kPingPongLRRatioId, W::M}, {kPingPongCrossFeedbackId, W::S},
                 {kPingPongModDepthId, W::S}, {kPingPongModRateId, W::S}, {kPingPongWidthId, W::S}},
                {kPingPongNoteValueId}, 3, 3});
}

TEST_CASE("Iterum editor panel Reverse: every control live and of the designed class", "[iterum][ui]") {
    checkPanel({"ReversePanel", 7,
                {{kReverseChunkSizeId, W::K}, {kReverseTimeModeId, W::T}, {kReverseFeedbackId, W::K},
                 {kReverseMixId, W::K}, {kReversePlaybackModeId, W::SEG}, {kReverseCrossfadeId, W::S},
                 {kReverseFilterEnabledId, W::T}, {kReverseFilterCutoffId, W::S}, {kReverseFilterTypeId, W::SEG}},
                {kReverseNoteValueId}, 2, 3});
}

TEST_CASE("Iterum editor panel MultiTap: every control live and of the designed class", "[iterum][ui]") {
    checkPanel({"MultiTapPanel", 8,
                {{kMultiTapTimingPatternId, W::M}, {kMultiTapFeedbackId, W::K}, {kMultiTapMixId, W::K},
                 {kMultiTapSpatialPatternId, W::M}, {kMultiTapTapCountId, W::S}, {kMultiTapMorphTimeId, W::S},
                 {kMultiTapNoteValueId, W::M}, {kMultiTapNoteModifierId, W::SEG},
                 {kMultiTapFeedbackLPCutoffId, W::S}, {kMultiTapFeedbackHPCutoffId, W::S},
                 {kMultiTapSnapDivisionId, W::M}, {kMultiTapPatternEditorTagId, W::TapEditor},
                 {kMultiTapCopyPatternButtonTagId, W::CopyButton},
                 {kMultiTapResetPatternButtonTagId, W::ResetButton}},
                {}, 4, 4});
}

TEST_CASE("Iterum editor panel Freeze: every control live and of the designed class", "[iterum][ui]") {
    checkPanel({"FreezePanel", 9,
                {{kFreezeSliceLengthId, W::K}, {kFreezePatternTypeId, W::M}, {kFreezeMixId, W::K},
                 {kFreezeSliceModeId, W::SEG}, {kFreezeEnvelopeAttackId, W::S}, {kFreezeEnvelopeReleaseId, W::S},
                 {kFreezeEnvelopeShapeId, W::SEG}, {kFreezeEuclideanStepsId, W::S},
                 {kFreezeEuclideanHitsId, W::S}, {kFreezeEuclideanRotationId, W::S},
                 {kFreezePatternRateId, W::M}, {kFreezeGranularDensityId, W::S},
                 {kFreezeGranularGrainSizeId, W::S}, {kFreezeGranularPositionJitterId, W::S},
                 {kFreezeGranularSizeJitterId, W::S}, {kFreezeDroneVoiceCountId, W::S},
                 {kFreezeDroneIntervalId, W::M}, {kFreezeDroneDriftId, W::S}, {kFreezeDroneDriftRateId, W::S},
                 {kFreezeNoiseColorId, W::M}, {kFreezeNoiseBurstRateId, W::M}, {kFreezeNoiseFilterTypeId, W::SEG},
                 {kFreezeNoiseFilterCutoffId, W::S}, {kFreezeNoiseFilterSweepId, W::S}},
                {}, 6, 3});
}

// ==============================================================================
// Conditional groups (section 8.2)
// ==============================================================================

TEST_CASE("Iterum editor Freeze: exactly one pattern variant fieldset is visible", "[iterum][ui]") {
    HeadlessEditor ed;
    ed.switchTo(9);
    CViewContainer* panel = ed.panel();
    REQUIRE(panel != nullptr);

    const std::int32_t anchors[4] = {kFreezeEuclideanStepsId, kFreezeGranularDensityId, kFreezeDroneVoiceCountId,
                                     kFreezeNoiseColorId};
    const double values[4] = {0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0};
    for (int p = 0; p < 4; ++p) {
        ed.setParam(kFreezePatternTypeId, values[p]);
        INFO("pattern type " << p);
        int visible = 0;
        for (int a = 0; a < 4; ++a) {
            CControl* anchor = firstWithTag(panel, anchors[a]);
            REQUIRE(anchor != nullptr);
            auto* fieldset = dynamic_cast<Krate::Plugins::FieldsetContainer*>(anchor->getParentView());
            INFO("variant anchor " << anchors[a]);
            REQUIRE(fieldset != nullptr);
            const bool shown = effectivelyVisible(fieldset, panel);
            CHECK(shown == (a == p));
            visible += shown ? 1 : 0;
        }
        CHECK(visible == 1);
    }
}

TEST_CASE("Iterum editor MultiTap: note value, custom pattern and copy button follow the timing pattern",
          "[iterum][ui]") {
    HeadlessEditor ed;
    ed.switchTo(8);
    CViewContainer* panel = ed.panel();
    REQUIRE(panel != nullptr);

    struct Step {
        double pattern;
        bool noteValue;
        bool custom;
        bool copy;
    };
    for (const Step s : {Step{2.0 / 19.0, false, false, true}, Step{15.0 / 19.0, true, false, true},
                         Step{19.0 / 19.0, false, true, false}}) {
        ed.setParam(kMultiTapTimingPatternId, s.pattern);
        INFO("timing pattern " << s.pattern * 19.0);

        CControl* noteValue = firstWithTag(panel, kMultiTapNoteValueId);
        REQUIRE(noteValue != nullptr);
        auto* noteFieldset = dynamic_cast<Krate::Plugins::FieldsetContainer*>(noteValue->getParentView());
        REQUIRE(noteFieldset != nullptr);
        CHECK(effectivelyVisible(noteFieldset, panel) == s.noteValue);

        CControl* tapEditor = firstWithTag(panel, kMultiTapPatternEditorTagId);
        REQUIRE(tapEditor != nullptr);
        auto* customFieldset = dynamic_cast<Krate::Plugins::FieldsetContainer*>(tapEditor->getParentView());
        REQUIRE(customFieldset != nullptr);
        CHECK(effectivelyVisible(customFieldset, panel) == s.custom);

        CControl* copy = firstWithTag(panel, kMultiTapCopyPatternButtonTagId);
        CControl* reset = firstWithTag(panel, kMultiTapResetPatternButtonTagId);
        REQUIRE(copy != nullptr);
        REQUIRE(reset != nullptr);
        CHECK(effectivelyVisible(copy, panel) == s.copy);
        CHECK_FALSE((effectivelyVisible(copy, panel) && effectivelyVisible(reset, panel)));
    }
}

TEST_CASE("Iterum editor Digital: the Age row hides while Era is Pristine", "[iterum][ui]") {
    HeadlessEditor ed;
    ed.switchTo(5);
    CViewContainer* panel = ed.panel();
    REQUIRE(panel != nullptr);

    for (const double era : {0.0, 0.5}) {
        ed.setParam(kDigitalEraId, era);
        INFO("era " << era);
        const bool expectVisible = era >= 0.25;
        const auto age = controlsWithTag(panel, kDigitalAgeId);
        const auto label = controlsWithTag(panel, kDigitalAgeLabelTag);
        bool slider = false;
        bool readout = false;
        for (auto* c : age) {
            slider = slider || dynamic_cast<VSTGUI::CSlider*>(c) != nullptr;
            readout = readout || isReadout(c);
            CHECK(effectivelyVisible(c, panel) == expectVisible);
        }
        CHECK(slider);
        CHECK(readout);
        REQUIRE(label.size() == 1u);
        CHECK(effectivelyVisible(label.front(), panel) == expectVisible);
    }
}

// ==============================================================================
// Sync retags at panel build and on toggle (section 9.3, "Read the Sync state at panel build")
// ==============================================================================

namespace {

struct TagCount {
    int knobs = 0;
    int readouts = 0;
    int total = 0;
};

[[nodiscard]] TagCount countTag(CViewContainer* root, std::int32_t tag) {
    TagCount n;
    for (auto* c : controlsWithTag(root, tag)) {
        ++n.total;
        n.knobs += dynamic_cast<Krate::Plugins::ArcKnob*>(c) != nullptr ? 1 : 0;
        n.readouts += isReadout(c) ? 1 : 0;
    }
    return n;
}

}  // namespace

TEST_CASE("Iterum editor: the time knob and readout carry the note value tag while Sync is on", "[iterum][ui]") {
    HeadlessEditor ed([](Iterum::Controller& c) { c.setParamNormalized(kGranularTimeModeId, 1.0); });

    // Granular, Sync set before the editor opened: the panel is built synced.
    ed.switchTo(0);
    {
        const TagCount note = countTag(ed.root(), kGranularNoteValueId);
        CHECK(note.total == 2);
        CHECK(note.knobs == 1);
        CHECK(note.readouts == 1);
        CHECK(countTag(ed.root(), kGranularDelayTimeId).total == 0);
    }

    // Tape registers Synced by default too: its Time knob shows the note value.
    ed.switchTo(3);
    {
        const TagCount note = countTag(ed.root(), kTapeNoteValueId);
        CHECK(note.total == 2);
        CHECK(note.knobs == 1);
        CHECK(note.readouts == 1);
        CHECK(countTag(ed.root(), kTapeMotorSpeedId).total == 0);
    }

    // Digital registers Synced by default: nothing is set.
    ed.switchTo(5);
    {
        const TagCount note = countTag(ed.root(), kDigitalNoteValueId);
        CHECK(note.total == 2);
        CHECK(note.knobs == 1);
        CHECK(note.readouts == 1);
        CHECK(countTag(ed.root(), kDigitalDelayTimeId).total == 0);
    }

    // Sync off through the toggle: the pair goes back to the free time.
    CControl* sync = firstWithTag(ed.root(), kDigitalTimeModeId);
    REQUIRE(sync != nullptr);
    sync->setValueNormalized(0.0f);
    sync->valueChanged();
    Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
    {
        const TagCount free = countTag(ed.root(), kDigitalDelayTimeId);
        CHECK(free.total == 2);
        CHECK(free.knobs == 1);
        CHECK(free.readouts == 1);
        CHECK(countTag(ed.root(), kDigitalNoteValueId).total == 0);
    }
}

// The case above passes through a side door: VST3Editor binds the Sync toggle and calls its valueChanged()
// (vst3editor.cpp updateControlValue), so a toggle declared AFTER the knob and readout retags them. verifyView itself
// must read the Sync state while the panel is being built, when no control has a frame yet (UIDescription calls
// verifyView before addView), whatever order the cell declares its views in.
TEST_CASE("Iterum DelayTimeSyncController retags a free-time control built before any frame exists", "[iterum][ui]") {
    auto controller = Steinberg::owned(new Iterum::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    controller->setParamNormalized(kGranularTimeModeId, 1.0);
    Krate::TestSupport::ensureVstguiInitialized();
    const std::string uidescPath = std::string(ITERUM_RESOURCES_DIR) + "/editor.uidesc";
    auto* editor = new VSTGUI::VST3Editor(controller.get(), "Editor", uidescPath.c_str());  // never opened
    {
        Iterum::DelayTimeSyncController sync(kGranularDelayTimeId, kGranularTimeModeId, kGranularNoteValueId, editor);
        auto readout = VSTGUI::owned(new VSTGUI::CParamDisplay(VSTGUI::CRect(0, 0, 120, 24)));
        readout->setTag(kGranularDelayTimeId);
        REQUIRE(readout->getFrame() == nullptr);
        VSTGUI::UIAttributes attributes;
        sync.verifyView(readout, attributes, nullptr);
        CHECK(readout->getTag() == kGranularNoteValueId);

        // Sync off: a control built now keeps the free-time tag.
        controller->setParamNormalized(kGranularTimeModeId, 0.0);
        Iterum::DelayTimeSyncController freeSync(kGranularDelayTimeId, kGranularTimeModeId, kGranularNoteValueId,
                                                 editor);
        auto knob = VSTGUI::owned(new VSTGUI::CParamDisplay(VSTGUI::CRect(0, 0, 56, 56)));
        knob->setTag(kGranularDelayTimeId);
        freeSync.verifyView(knob, attributes, nullptr);
        CHECK(knob->getTag() == kGranularDelayTimeId);
    }
    static_cast<Steinberg::IPlugView*>(editor)->release();
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}
