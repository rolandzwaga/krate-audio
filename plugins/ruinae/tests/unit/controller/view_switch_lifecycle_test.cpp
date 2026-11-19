// ==============================================================================
// Controller cached view pointers follow view lifetime (fix plan T1.3)
// ==============================================================================
// R-193/R-129: a ModSourceViewMode switch left the rate and note-value groups of
//              the outgoing ModSource_* template dangling.
// R-080/R-130: a DistortionViewMode switch left spectralCurveDropdown_ and
//              spectralBitsGroup_ dangling.
// R-079:       leaving the FX tab left eight FX-tab group pointers dangling.
// R-131:       the pointer-nulling hooks ran after the base setParamNormalized,
//              inside which the switch had already built the incoming template,
//              so they nulled the NEW views.
// Every switch here is driven the way a host or a click drives it (see
// view_lifecycle_rig.h). The ASan build turns any remaining dangling deref
// into a hard failure; the inventory checks catch it in every build.
// ==============================================================================

#include "view_lifecycle_rig.h"

#include "ui/arp_lane_editor.h"
#include "ui/arp_lane_header.h"
#include "ui/mod_matrix_grid.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <initializer_list>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace RuinaeViewLifecycleTest;

namespace {

constexpr int kModSourceMacrosIndex = 3;
constexpr double kDistortionSpectral = 2.0 / 6.0;  // Clean, Chaos, Spectral, Granular, Wavefolder, Tape, RingMod

void requireNoDanglingPointer(const ViewLifecycleRig& rig) {
    const auto dangling = rig.danglingPointers();
    INFO("non-null pointers not in the live frame tree: " << joinNames(dangling));
    REQUIRE(dangling.empty());
}

void requireLive(const ViewLifecycleRig& rig, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        INFO(name);
        REQUIRE(rig.isLive(rig.pointer(name)));
    }
}

void checkNull(const ViewLifecycleRig& rig, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        INFO(name);
        CHECK(rig.pointer(name) == nullptr);
    }
}

// Open on MOD showing ModSource_<srcIndex>, switch the dropdown to Macros, then automate
// that source's Sync parameter (whose branch in setParamNormalized derefs the groups).
void modSourceDestroyedThenSync(int srcIndex, Steinberg::Vst::ParamID syncId,
                                std::initializer_list<const char*> destroyed) {
    ViewLifecycleRig rig({{Ruinae::kMainTabTag, kTabMod}, {Ruinae::kModSourceViewModeTag, modSourceNorm(srcIndex)}});
    requireLive(rig, destroyed);  // the pointers exist before the switch, so the test is not vacuous

    rig.clickControl(Ruinae::kModSourceViewModeTag, modSourceNorm(kModSourceMacrosIndex));
    checkNull(rig, destroyed);
    requireNoDanglingPointer(rig);

    rig.ctl.setParamNormalized(syncId, 1.0);
    rig.ctl.setParamNormalized(syncId, 0.0);
    checkMatchesFreshOpen(rig, {{Ruinae::kMainTabTag, kTabMod},
                                {Ruinae::kModSourceViewModeTag, modSourceNorm(kModSourceMacrosIndex)}});
}

}  // namespace

TEST_CASE("ModSource template destroyed, then Sync automation does not touch freed views",
          "[controller][lifecycle]") {
    SECTION("LFO1") {
        modSourceDestroyedThenSync(0, Ruinae::kLFO1SyncId,
                                   {"lfo1RateGroup_", "lfo1NoteValueGroup_", "lfo1WaveformDisplay_"});
    }
    SECTION("LFO2") {
        modSourceDestroyedThenSync(1, Ruinae::kLFO2SyncId,
                                   {"lfo2RateGroup_", "lfo2NoteValueGroup_", "lfo2WaveformDisplay_"});
    }
    SECTION("Chaos") {
        modSourceDestroyedThenSync(2, Ruinae::kChaosModSyncId,
                                   {"chaosRateGroup_", "chaosNoteValueGroup_", "chaosModDisplay_"});
    }
    SECTION("S&H") {
        modSourceDestroyedThenSync(6, Ruinae::kSampleHoldSyncId,
                                   {"shRateGroup_", "shNoteValueGroup_", "sampleHoldDisplay_"});
    }
    SECTION("Random") {
        modSourceDestroyedThenSync(7, Ruinae::kRandomSyncId,
                                   {"randomRateGroup_", "randomNoteValueGroup_", "randomModDisplay_"});
    }
}

TEST_CASE("ModSource LFO1 template destroyed, then setComponentState", "[controller][lifecycle]") {
    const auto state = defaultProcessorState();
    ViewLifecycleRig rig({{Ruinae::kMainTabTag, kTabMod}, {Ruinae::kModSourceViewModeTag, modSourceNorm(0)}});
    requireLive(rig, {"lfo1RateGroup_", "lfo1NoteValueGroup_", "lfo1WaveformDisplay_"});

    rig.clickControl(Ruinae::kModSourceViewModeTag, modSourceNorm(kModSourceMacrosIndex));
    requireNoDanglingPointer(rig);

    CHECK(rig.loadState(state) == Steinberg::kResultTrue);  // syncAllViews derefs every group
    requireNoDanglingPointer(rig);
}

TEST_CASE("DistortionViewMode switch clears the spectral pointers", "[controller][lifecycle]") {
    ViewLifecycleRig rig;  // Tab_Sound, distortion General view
    rig.ctl.setParamNormalized(Ruinae::kDistortionTypeId, kDistortionSpectral);
    rig.clickControl(Ruinae::kDistortionViewModeTag, 1.0);  // Type-specific: Dist_Spectral is built
    requireLive(rig, {"spectralCurveDropdown_", "spectralBitsGroup_"});

    rig.clickControl(Ruinae::kDistortionViewModeTag, 0.0);  // General: Dist_Spectral is destroyed
    checkNull(rig, {"spectralCurveDropdown_", "spectralBitsGroup_"});
    requireNoDanglingPointer(rig);

    rig.ctl.setParamNormalized(Ruinae::kDistortionSpectralModeId, 1.0);  // its branch derefs both
    checkMatchesFreshOpen(rig, {{Ruinae::kDistortionTypeId, kDistortionSpectral}});
}

TEST_CASE("Leaving the FX tab clears every FX-tab group pointer", "[controller][lifecycle]") {
    const std::initializer_list<const char*> fxGroups{
        "noModulationGroup_", "phaserControlsGroup_", "flangerControlsGroup_", "chorusControlsGroup_",
        "flangerRateGroup_",  "flangerNoteValueGroup_", "chorusRateGroup_",   "chorusNoteValueGroup_"};
    const auto state = defaultProcessorState();
    ViewLifecycleRig rig({{Ruinae::kMainTabTag, kTabFx}});
    requireLive(rig, fxGroups);

    rig.clickControl(Ruinae::kMainTabTag, kTabSound);
    checkNull(rig, fxGroups);
    requireNoDanglingPointer(rig);

    CHECK(rig.loadState(state) == Steinberg::kResultTrue);
    checkMatchesFreshOpen(rig, {{Ruinae::kMainTabTag, kTabSound}});
}

TEST_CASE("Host-driven MainTab change keeps the incoming tab's pointers", "[controller][lifecycle]") {
    ViewLifecycleRig rig;  // Tab_Sound
    rig.ctl.setParamNormalized(Ruinae::kMainTabTag, kTabSeq);  // host automation, no control editing

    for (const char* name : {"stepPatternEditor_", "velocityLane_", "gateLane_", "pitchLane_", "ratchetLane_",
                             "modifierLane_", "conditionLane_", "chordLane_", "inversionLane_"}) {
        INFO(name);
        CHECK(rig.isLive(rig.pointer(name)));
    }

    // The visible velocity lane follows a step parameter change.
    Krate::Plugins::ArpLaneEditor* velocity = nullptr;
    forEachDescendant(rig.frame(), [&velocity](VSTGUI::CView* v) {
        auto* lane = dynamic_cast<Krate::Plugins::ArpLaneEditor*>(v);
        if (!velocity && lane && lane->getLaneType() == Krate::Plugins::ArpLaneType::kVelocity) velocity = lane;
    });
    REQUIRE(velocity != nullptr);
    const double cur = rig.ctl.getParamNormalized(Ruinae::kArpVelocityLaneStep0Id);
    const double next = cur > 0.5 ? 0.25 : 0.75;
    rig.ctl.setParamNormalized(Ruinae::kArpVelocityLaneStep0Id, next);
    CHECK(std::abs(static_cast<double>(velocity->getStepLevel(0)) - next) < 1e-3);

    checkMatchesFreshOpen(rig, {{Ruinae::kMainTabTag, kTabSeq}});
}

namespace {

/// Records the parameter ids the controller reports through performEdit.
class PerformEditRecorder final : public Steinberg::Vst::IComponentHandler {
public:
    std::set<Steinberg::Vst::ParamID> performed;

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID /*id*/) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue /*value*/) override {
        performed.insert(id);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID /*id*/) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 /*flags*/) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/, void** obj) override {
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    // Owned by the test case's stack frame.
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

/// A left click on the ShiftLeft transform button of a lane's header, the way the lane receives it.
/// ArpLaneHeader lays the four buttons out from the right: [Invert][ShiftLeft][ShiftRight][Randomize].
void clickShiftLeft(VSTGUI::CView* lane) {
    using Header = Krate::Plugins::ArpLaneHeader;
    const VSTGUI::CRect vs = lane->getViewSize();
    const double pitch = static_cast<double>(Header::kButtonSize + Header::kButtonGap);
    VSTGUI::CPoint where(vs.right - static_cast<double>(Header::kButtonsRightMargin) - 2.0 * pitch -
                             static_cast<double>(Header::kButtonSize) / 2.0,
                         vs.top + static_cast<double>(Header::kHeight) / 2.0);
    lane->onMouseDown(where, VSTGUI::CButtonState(VSTGUI::kLButton));
}

const std::vector<std::pair<const char*, Steinberg::Vst::ParamID>>& laneStepBases() {
    static const std::vector<std::pair<const char*, Steinberg::Vst::ParamID>> lanes{
        {"velocityLane_", Ruinae::kArpVelocityLaneStep0Id},   {"gateLane_", Ruinae::kArpGateLaneStep0Id},
        {"pitchLane_", Ruinae::kArpPitchLaneStep0Id},         {"ratchetLane_", Ruinae::kArpRatchetLaneStep0Id},
        {"modifierLane_", Ruinae::kArpModifierLaneStep0Id},   {"conditionLane_", Ruinae::kArpConditionLaneStep0Id},
        {"chordLane_", Ruinae::kArpChordLaneStep0Id},         {"inversionLane_", Ruinae::kArpInversionLaneStep0Id}};
    return lanes;
}

}  // namespace

// R-086: the lane transform buttons and the voice route-removed callback belong to live views. They must work
// through the view that owns them, never through a controller member another path nulls while the view lives
// (terminate() with the editor still open clears every cached pointer; a null deref there is a crash).
TEST_CASE("Lane transform and voice route-removed callbacks work through their own views",
          "[controller][lifecycle]") {
    SECTION("arp lane transform buttons") {
        PerformEditRecorder handler;
        ViewLifecycleRig rig;  // Tab_Sound
        rig.ctl.setComponentHandler(&handler);
        rig.ctl.setParamNormalized(Ruinae::kMainTabTag, kTabSeq);  // host-driven tab change

        std::vector<std::pair<VSTGUI::CView*, Steinberg::Vst::ParamID>> lanes;
        for (const auto& [name, stepBase] : laneStepBases()) {
            INFO(name);
            VSTGUI::CView* lane = rig.pointer(name);
            REQUIRE(rig.isLive(lane));
            lanes.emplace_back(lane, stepBase);
        }

        for (const auto& [lane, stepBase] : lanes) {
            INFO("lane step base " << stepBase);
            clickShiftLeft(lane);
            CHECK(handler.performed.count(stepBase) == 1);  // the click reached the transform callback
        }

        // A host may terminate the controller before it closes the editor: the lanes stay alive and clickable.
        REQUIRE(rig.ctl.terminate() == Steinberg::kResultOk);
        for (const auto& [lane, stepBase] : lanes) {
            INFO("lane step base " << stepBase);
            REQUIRE(rig.isLive(lane));
            clickShiftLeft(lane);
        }
    }

    SECTION("voice route removed") {
        ViewLifecycleRig rig;  // Tab_Sound
        rig.ctl.setParamNormalized(Ruinae::kMainTabTag, kTabMod);  // host-driven tab change

        auto* grid = dynamic_cast<Krate::Plugins::ModMatrixGrid*>(rig.pointer("modMatrixGrid_"));
        REQUIRE(grid != nullptr);
        REQUIRE(rig.isLive(grid));
        grid->setActiveTab(1);  // Voice routes
        grid->removeRoute(0);

        REQUIRE(rig.ctl.terminate() == Steinberg::kResultOk);
        REQUIRE(rig.isLive(grid));
        grid->removeRoute(0);
    }
}
