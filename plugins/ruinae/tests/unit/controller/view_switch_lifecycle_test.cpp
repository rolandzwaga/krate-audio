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

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <initializer_list>

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
