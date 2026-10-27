#pragma once

// ==============================================================================
// Profundum - Edit Controller (UI thread)   Plan section 2.6
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation. This header includes
// no processor header; processor and controller share only plugin_ids.h and the
// parameters/*.h packs.
//
// No INoteExpressionController (Phase 10 FR-019). Phase 11 FR-050 (T021) adds
// IMidiMapping: on bus 0, every channel, CC 1 -> Mod Wheel (6), CC 64 -> Sustain
// Pedal (5), channel pressure -> Channel Pressure (7), pitch bend -> Pitch Bend (4).
// Phase 11 T014: initialize registers all 84 parameters (FR-001; 93 since Phase 14
// T012, which adds Morph Modulation 1003-1011); the getParamStringByValue chain
// is global -> macro -> sound -> note (Portamento suffix from the current Glide
// Mode, FR-007) -> morph control -> morph modulation -> performance -> base class.
// Phase 11 T016 (plan 4.6; FR-021, FR-022, FR-024, FR-025 controller side): the
// endpoint mirror is replaced only from the processor's kMsgEndpoint messages
// (the processor is authoritative); an Edit Target switch re-displays the 60
// E-parameters from the selected mirror, then calls restartComponent
// (kParamValuesChanged) once, with no beginEdit / performEdit / endEdit.
// Phase 11 T020 (plan 4.6; FR-025, FR-030, FR-031): copyEndpoint and setCompare
// send requests to the processor (the mirror and the compare mode change only
// from its replies); an Edit Target switch first sends kMsgRequestSync (the
// pull), and a reply that replaces the Edit Target endpoint AFTER the re-display
// re-displays and restarts again.
// FR-050: owns a PresetManager built from makeProfundumPresetConfig().
// Phase 13 T013 (plan 4.6; FR-030, FR-031, FR-032, FR-059): the PresetManager's state provider saves the
// processor's own getState bytes (through the host's IComponent) and its load provider is loadPresetStream, which
// sends the whole stream to the processor (kMsgLoadPresetState) before it mirrors it. The PresetBrowserView overlay
// is built in didOpen (the frame owns it) and dropped in willClose; "PresetBrowserButton" is the shared
// OutlineBrowserButton. No current-preset name, no previous / next, no new state.
//
// The implementation is controller.cpp (plan section 2.6).
// ==============================================================================

#include "parameters/endpoint_names.h"  // Phase 12 T019 (FR-047)
#include "parameters/morph_params.h"
#include "preset/preset_manager.h"
#include "update/update_checker.h"
#include "processor/spectrum_frame.h"  // Phase 12 T021: POD only, shared like plugin_ids.h (FR-071)
#include "ui/spread_curve_view.h"      // Phase 12 T024: UI::SpreadCurveModel (plan 4.6.8)

#include "pluginterfaces/vst/ivstdataexchange.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/utility/dataexchange.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Krate::Plugins {
class ADSRDisplay;  // plugins/shared/src/ui/adsr_display.h; held by pointer only (Phase 12 T025)
class PresetBrowserView;  // plugins/shared/src/ui/preset_browser_view.h; held by pointer only (Phase 13 T013)
class UpdateBannerView;   // plugins/shared/src/ui/update_banner_view.h; held by pointer only
}  // namespace Krate::Plugins

namespace Profundum {

class OffsetRingKnob;  // ui/offset_ring_knob.h (Phase 12 P-5 ruling); held by pointer only
class MorphIndicatorSlider;  // ui/morph_indicator_slider.h (Phase 14 FR-035); held by pointer only

namespace UI {
class HarmonicBarView;               // ui/harmonic_bar_view.h; held by pointer only
class ProfundumPanelSubController;   // ui/panel_sub_controller.h; held by pointer only
}  // namespace UI

// Phase 12 T021 (plan 4.6.1; FR-071, FR-076, FR-091): IDataExchangeReceiver consumer of the one-way 'PRFD'
// ProfundumSpectrumFrame queue (Vorago controller.h:56-59 shape). The frame is cached on the UI thread whether or not
// an editor is open; the VST3 MIDI-learn interface is NOT implemented (FR-098).
// Phase 12 T026 (plan 4.6.2, 4.6.4, 4.6.7, 4.6.9; FR-014, FR-090 - FR-092): VST3EditorDelegate - the custom views,
// the "ProfundumPanel" sub-controller, the 100 ms editor sync timer (open editor only) and the session zoom.
class Controller : public Steinberg::Vst::EditControllerEx1,
                   public Steinberg::Vst::IMidiMapping,              // FR-050
                   public Steinberg::Vst::IDataExchangeReceiver,     // FR-076
                   public VSTGUI::VST3EditorDelegate {               // FR-092
public:
    /// FR-090 (D-10): the editor sync period, ruled 100 ms (<= 100 ms).
    static constexpr std::uint32_t kEditorSyncIntervalMs = 100;
    /// FR-014 (plan 4.6.9): the zoom steps 100 / 125 / 150 / 200 %.
    static constexpr std::array<double, 4> kZoomSteps{1.0, 1.25, 1.5, 2.0};
    static constexpr int kNumPages = 2;  ///< Main, Advanced (session tag 9100)
    static constexpr int kNumTabs = 3;   ///< Sections, Modulation, Settings (session tag 9110)

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    /// FR-022 / FR-024: the Edit Target (1001) hook.
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
                                                     Steinberg::Vst::ParamValue value) override;
    Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag,
                                                        Steinberg::Vst::ParamValue valueNormalized,
                                                        Steinberg::Vst::String128 string) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    /// Phase 12 FR-076: the DataExchange fallback messages go to the receiver handler first. FR-025: kMsgEndpoint
    /// replaces a mirror, kMsgCompareMode sets the compare mode; others -> base.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    /// FR-050 (IMidiMapping): bus 0 only, every channel; CC 1 -> 6, CC 64 -> 5, kAfterTouch -> 7, kPitchBend -> 4;
    /// anything else, or bus != 0 -> kResultFalse.
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                              Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                              Steinberg::Vst::ParamID& id) override;

    /// FR-076 (plan 4.6.1): for kProfundumFrameUserContextId, resets the cached frame to a silence frame, counts the
    /// open and records the block size (SC-023); asks for UI-thread dispatch, so the cache needs no lock.
    void PLUGIN_API queueOpened(Steinberg::Vst::DataExchangeUserContextID userContextID, Steinberg::uint32 blockSize,
                                Steinberg::TBool& dispatchOnBackgroundThread) override;
    /// Counts the close of the 'PRFD' queue (SC-023). The cache is a POD: nothing to release.
    void PLUGIN_API queueClosed(Steinberg::Vst::DataExchangeUserContextID userContextID) override;
    /// Filters on the user context and the block size; the newest valid block wins (Vorago controller.cpp:314-329).
    /// Each accepted block is copied into the cache and counted, with or without an open editor (FR-091).
    void PLUGIN_API onDataExchangeBlocksReceived(Steinberg::Vst::DataExchangeUserContextID userContextID,
                                                 Steinberg::uint32 numBlocks, Steinberg::Vst::DataExchangeBlock* blocks,
                                                 Steinberg::TBool onBackgroundThread) override;

    /// Plan 4.6.4: counts the open gestures (the sync tick skips while one is open, R-6) and forwards a macro
    /// (100 - 103) gesture to the panel's highlight (SC-009), then the base class.
    Steinberg::tresult beginEdit(Steinberg::Vst::ParamID tag) override;
    Steinberg::tresult endEdit(Steinberg::Vst::ParamID tag) override;
    /// VSTGUI echo guard (SC-007 (e)): ParameterChangeListener::performEdit calls setParamNormalized, whose
    /// immediate parameter change re-enters VST3Editor::valueChanged for the control still being edited and sends
    /// a second performEdit of the same value (vst3editor.cpp:226-233, :349; vstparameters.cpp:73-76). A
    /// performEdit of the tag currently inside setParamNormalized is that echo and is dropped: the outer call sends
    /// the same value right after it returns.
    Steinberg::tresult performEdit(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue valueNormalized) override;

    // ---- VST3EditorDelegate (plan 4.6.2; FR-092) ----
    /// "HarmonicBarView" (attribute bar-variant main / core), "SpreadCurveView" and (Phase 13 FR-059)
    /// "PresetBrowserButton" (a shared OutlineBrowserButton, no listener, tag -1: the panel sub-controller gives it
    /// its session tag), the rect from origin / size; any other name -> nullptr.
    VSTGUI::CView* createCustomView(VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
                                    const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor* editor) override;
    /// "ProfundumPanel" -> a new UI::ProfundumPanelSubController (owned by the view tree); anything else -> nullptr.
    VSTGUI::IController* createSubController(VSTGUI::UTF8StringPtr name, const VSTGUI::IUIDescription* description,
                                             VSTGUI::VST3Editor* editor) override;
    /// Stores the editor, adds the (closed) PresetBrowserView overlay to the frame once (Phase 13 FR-030), applies
    /// the allowed zoom steps and the session zoom, starts the sync timer, refreshes the rings, the SPREAD model,
    /// the captions and the dimming.
    void didOpen(VSTGUI::VST3Editor* editor) override;
    /// Closes the overlay if it is open and forgets it (the frame owns it), stops and releases the sync timer;
    /// forgets every view pointer (FR-091).
    void willClose(VSTGUI::VST3Editor* editor) override;
    /// The editor's own zoom menu (allowed factors) keeps the session step and the zoom controls in step.
    void onZoomChanged(VSTGUI::VST3Editor* editor, double newZoom) override;

    // ---- Session UI state (never a parameter, never persisted; FR-009, FR-014, SC-017, SC-032) ----
    [[nodiscard]] int activePage() const noexcept { return activePage_; }
    void setActivePage(int page) noexcept;  ///< clamps to [0, kNumPages)
    [[nodiscard]] int activeTab() const noexcept { return activeTab_; }
    void setActiveTab(int tab) noexcept;    ///< clamps to [0, kNumTabs)
    [[nodiscard]] int zoomStep() const noexcept { return zoomIndex_; }
    /// Plan 4.6.9: clamps and stores the step; with the editor open, setZoomFactor(kZoomSteps[step]). Writes no
    /// parameter, no state and no message.
    void setZoomStep(int step);

    /// The panel registers itself from its verifyView; it is forgotten in willClose and by its destructor.
    void registerPanel(UI::ProfundumPanelSubController* panel) noexcept { panel_ = panel; }
    void unregisterPanel(const UI::ProfundumPanelSubController* panel) noexcept {
        if (panel_ == panel) {
            panel_ = nullptr;
        }
    }
    /// Plan 4.6.6: the panel re-reads both captions and names (every mirror replacement and name change).
    void refreshEndpointCaptions();

    // ---- Phase 13 preset browser (plan 4.6; FR-030, FR-031, FR-032, FR-059) ----
    /// The PresetManager load provider. Reads the whole chunk; refuses one larger than 4096 bytes, shorter than the
    /// version word or whose first int32 is above kCurrentStateVersion. Then sends kMsgLoadPresetState (binary
    /// "state") to the processor, mirrors the same bytes through setComponentState and calls
    /// restartComponent(kParamValuesChanged). False, with nothing changed, when no message can be allocated (no
    /// host context) or the processor refuses it; otherwise true iff setComponentState returned kResultOk.
    bool loadPresetStream(Steinberg::IBStream* stream);
    /// Opens the overlay when it exists (an open editor) and is closed; otherwise a no-op.
    void openPresetBrowser();

    // ---- Test seams (SC-015, SC-016) ----
    [[nodiscard]] bool hasEditorSyncTimerForTest() const noexcept { return editorSyncTimer_ != nullptr; }
    /// The configured period of the running timer (getFireTime()), 0 while none exists.
    [[nodiscard]] std::uint32_t editorSyncIntervalForTest() const noexcept {
        return editorSyncTimer_ != nullptr ? editorSyncTimer_->getFireTime() : 0u;
    }
    void editorSyncTickForTest() { onEditorSyncTick(); }
    /// True when no raw view pointer is held (FR-091): bar views, SPREAD view, panel, editor, ADSRDisplay, ring knobs,
    /// (Phase 13) the preset browser overlay and (Phase 14) the morph indicator slider.
    [[nodiscard]] bool viewPointersNullForTest() const noexcept;
    [[nodiscard]] UI::ProfundumPanelSubController* panelForTest() const noexcept { return panel_; }
    /// Phase 13 T013: the overlay, non-null only while an editor is open.
    [[nodiscard]] const Krate::Plugins::PresetBrowserView* presetBrowserViewForTest() const noexcept {
        return presetBrowserView_;
    }
    /// Phase 13 T013: the tab labels didOpen passes to the overlay.
    [[nodiscard]] const std::vector<std::string>& presetTabLabelsForTest() const noexcept { return presetTabLabels_; }

    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
        DEF_INTERFACE(Steinberg::Vst::IDataExchangeReceiver)
    END_DEFINE_INTERFACES(EditControllerEx1)
    REFCOUNT_METHODS(EditControllerEx1)

    /// FR-030: sends kMsgCopyEndpoint ("from") and calls setDirty(true). The mirror is NOT edited locally: the
    /// processor reports the changed destination endpoint (FR-025).
    void copyEndpoint(Krate::DSP::BassMorphEngine::Endpoint from);
    /// FR-031: sends kMsgCompare ("mode"). Only the processor's kMsgCompareMode reply updates compareModeForTest().
    void setCompare(CompareMode m);
    /// Phase 12 FR-046 (plan 4.6.3): the compare mode the processor last reported (production getter).
    [[nodiscard]] CompareMode compareMode() const noexcept { return compare_; }
    /// FR-046: the number of accepted kMsgCompareMode messages (one change notification each, SC-007 (b)).
    [[nodiscard]] std::uint32_t compareModeChangeCount() const noexcept { return compareChanges_; }
    /// Phase 12 FR-049 (plan 4.6.3): the compare control's entry point. setCompare(m); for A / B also one
    /// beginEdit / performEdit / setParamNormalized / endEdit on Edit Target 1001 (the FR-043 path). A.B sends the
    /// message only.
    void pressCompare(CompareMode m);
    /// Phase 12 FR-047 (plan 4.5): sanitises @p text (an empty result becomes the default "A" / "B"), stores it so
    /// the UI shows it at once, sends kMsgEndpointName (int "endpoint", binary "name") and calls setDirty(true).
    /// Copy A -> B / B -> A never copies a name (plan OQ-4).
    void setEndpointName(Krate::DSP::BassMorphEngine::Endpoint e, std::string_view text);
    /// FR-047: the name the controller holds for endpoint @p e.
    [[nodiscard]] EndpointName endpointName(Krate::DSP::BassMorphEngine::Endpoint e) const noexcept {
        return names_[e == Krate::DSP::BassMorphEngine::Endpoint::B ? 1u : 0u];
    }
    [[nodiscard]] EndpointName endpointNameForTest(Krate::DSP::BassMorphEngine::Endpoint e) const noexcept {
        return endpointName(e);
    }

    /// Phase 12 FR-040 / FR-048 (plan 4.6.6): "<name> -- w1 . w2 . w3" (an em dash and middle dots, UTF-8), the three words of
    /// UI::describeEndpoint(mirror of @p e). Computed from the current mirror and name on every call, so it follows
    /// every mirror replacement and name change; never stored or sent (SC-027 (f)).
    [[nodiscard]] std::string endpointCaption(Krate::DSP::BassMorphEngine::Endpoint e) const;

    /// Phase 12 FR-061 (plan 4.6.5, 5.5): the signed normalised ring of macro target @p id, norm(mod) - norm(base),
    /// with the engine's own contributions() at the current macros 100 - 103 and its apply rule (amp times
    /// clamp(base * 2^c, 0.1, 10000), everything else clamp(base + c, lo, hi)). 0 for an ID outside
    /// kMacroTargetParamIds, an inert row (501) or a zero contribution (every macro at 0.5). Writes nothing.
    /// Not const: EditController::getParamNormalized is non-const.
    [[nodiscard]] float macroRingRange(Steinberg::Vst::ParamID id) noexcept;
    /// FR-061 (P-5 ruling): setOffset(macroRingRange(id)) on each registered OffsetRingKnob; a no-op while none is
    /// registered (no editor open). The knob's value is never written (FR-005).
    void refreshMacroRings() noexcept;
    /// An editor knob for macro target @p id (registered while the editor is open; the panel's verifyView, T026).
    /// A target may carry several knobs (the uidesc binds 200, 201, 204, 302 twice and 500 three times), so each is
    /// kept, up to kMaxRingKnobsPerTarget; the same knob twice is kept once. An ID outside kMacroTargetParamIds is
    /// ignored. Sets the knob's offset at once.
    void registerRingKnob(Steinberg::Vst::ParamID id, OffsetRingKnob* knob) noexcept;
    /// Forgets every registered knob (willClose, FR-091).
    void clearRingKnobs() noexcept {
        for (auto& slots : ringKnobs_) {
            slots.fill(nullptr);
        }
    }

    /// Phase 14 FR-035 (plan 4.9): the Main-page Morph Position slider (the panel's verifyView, the registerRingKnob
    /// route). Stores it, then shows the cached frame's resolved position if a frame arrived since the queue opened,
    /// otherwise clears the marker (it falls back to the slider value). nullptr forgets it. UI thread only; never
    /// edits the parameter.
    void registerMorphIndicator(MorphIndicatorSlider* slider) noexcept;
    [[nodiscard]] MorphIndicatorSlider* morphIndicatorForTest() const noexcept { return morphIndicator_; }

    /// Phase 12 FR-080 (plan 4.6.8; SC-014): rebuilds the SPREAD model from the displayed 800 - 805 (each through its
    /// row's law) on the never-prepared field_: setControls + reset, then w[i] = widthCurve(hz[i]) * m at the 64 points
    /// hz[i] = 20 * 500^(i / 63), m = 0 in Solid, else spread * (1 - kMotionExcursion * depth); the crossover, the
    /// two knees of the law and the wide / solid flags. UI thread only; a no-op before initialize.
    void refreshSpreadModel() noexcept;
    [[nodiscard]] const UI::SpreadCurveModel& spreadModelForTest() const noexcept { return spreadModel_; }

    /// Phase 12 FR-034 (plan 4.6.10, D-8; SC-024): wires the shared ADSRDisplay to the amp envelope 420 - 426 through
    /// the law adapter: base IDs 420 / 424, max times kMaxEnvelopeTimeMs, Bezier off; the parameter callback drops any
    /// ID outside 420 - 426 and otherwise calls setParamNormalized(id, v) and performEdit(id, v) with
    /// v = adsrToRegistered(id, n); begin / end edit go to beginEdit / endEdit. The display then shows the current
    /// 420 - 426. nullptr forgets the display (editor close); the view tree owns it, the pointer is non-owning.
    void wireAdsrDisplay(Krate::Plugins::ADSRDisplay* display);
    /// Plan 4.6.10 table: 420 / 421 / 423 -> normalizedFromPlain(law, the display's cubic ms of n); 422 and 424 - 426
    /// -> n (the display already emits the registered Linear law).
    [[nodiscard]] static double adsrToRegistered(Steinberg::Vst::ParamID id, float n) noexcept;

    [[nodiscard]] Krate::Plugins::PresetManager* presetManagerForTest() const noexcept {
        return presetManager_.get();
    }
    [[nodiscard]] const Krate::DSP::MorphState& mirrorForTest(
        Krate::DSP::BassMorphEngine::Endpoint e) const noexcept {
        return mirror_[static_cast<std::size_t>(e)];
    }
    [[nodiscard]] CompareMode compareModeForTest() const noexcept { return compare_; }
    [[nodiscard]] int redisplayCountForTest() const noexcept { return redisplayCount_; }

    // Phase 12 T021 test reads (SC-010 - SC-012 / SC-025 connected arms, SC-023, SC-026 step 3).
    [[nodiscard]] const ProfundumSpectrumFrame& cachedFrameForTest() const noexcept { return cachedFrame_; }
    [[nodiscard]] std::uint64_t framesReceivedForTest() const noexcept { return framesReceived_; }
    [[nodiscard]] std::uint32_t queueOpenCountForTest() const noexcept { return queueOpenCount_; }
    [[nodiscard]] std::uint32_t queueCloseCountForTest() const noexcept { return queueCloseCount_; }
    [[nodiscard]] std::uint32_t lastQueueBlockSizeForTest() const noexcept { return lastBlockSize_; }

    /// The frame the cache holds before any block arrives and after every queueOpened: silent, sequence 0.
    [[nodiscard]] static constexpr ProfundumSpectrumFrame silenceFrame() noexcept {
        ProfundumSpectrumFrame f{};
        f.flags = kFrameFlagSilent;
        return f;
    }

private:
    /// The current Edit Target index (0 A, 1 B), read from parameter 1001.
    [[nodiscard]] int editTargetIndex();
    /// 60 x EditControllerEx1::setParamNormalized from the selected mirror (no host notification).
    void redisplayEditTarget();
    /// FR-044 v1 load: setParamNormalized(id, the registered defaultNormalizedValue).
    void setToRegisteredDefault(Steinberg::Vst::ParamID id);
    /// FR-047: both names back to "A" / "B" (a v1 or v2 load).
    void resetNamesToDefaults();
    /// Phase 14 FR-024: 1003 - 1011 back to their registered defaults (a v1 or v2 load).
    void resetMorphModToRegisteredDefaults();
    /// Plan 4.6.4: true for 100 - 103 and every registered ID in kMacroTargetParamIds (the ring refresh hook).
    [[nodiscard]] static bool isMacroRingTrigger(Steinberg::Vst::ParamID tag) noexcept;
    /// Plan 4.6.4: true for 800 - 805 (Image Mode, Spread, Gravity, Crossover, Curve, Field Motion Depth).
    [[nodiscard]] static bool isSpreadTrigger(Steinberg::Vst::ParamID tag) noexcept;
    /// Plan 4.6.10: true for the amp envelope 420 - 426 (the ADSRDisplay's IDs).
    [[nodiscard]] static bool isAdsrParam(Steinberg::Vst::ParamID tag) noexcept;
    /// Plan 4.6.10 reverse path: the wired display's field for @p tag set to plainFromNormalized(law, displayed value).
    void syncAdsrDisplay(Steinberg::Vst::ParamID tag);
    /// syncAdsrDisplay for every ID in 420 - 426 (on wiring and on an Edit Target re-display).
    void refreshAdsrDisplay();
    /// Plan 4.6.7: skipped while a gesture is open (R-6) or no editor is open; otherwise one kMsgRequestSync.
    void onEditorSyncTick();
    /// Stops and releases the sync timer (willClose, terminate).
    void stopEditorSyncTimer() noexcept;
    /// Phase 13 FR-031 (Vorago controller.cpp:152-168): the processor's own getState bytes, through the host's
    /// IComponent (the component handler); nullptr when the handler offers none. The caller releases the stream.
    [[nodiscard]] Steinberg::IBStream* createComponentStateStream();

    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;
    std::unique_ptr<Krate::Plugins::UpdateChecker> updateChecker_;  // the live update check (2026-10-26)
    // Phase 13 FR-030: "All" + the six subcategories (makeProfundumPresetTabLabels), set in initialize.
    std::vector<std::string> presetTabLabels_;
    std::array<Krate::DSP::MorphState, 2> mirror_{};  // FR-021: [A, B]
    CompareMode compare_ = CompareMode::AB;           // FR-031, set only by the processor's message
    std::uint32_t compareChanges_ = 0;                // FR-046: accepted kMsgCompareMode messages
    bool inStateLoad_ = false;                        // suppresses the 1001 hook during setComponentState
    int redisplayCount_ = 0;
    // FR-025 Edit Target pull: a kMsgRequestSync is out and its reply (which ends with kMsgCompareMode) has not
    // arrived; pullRedisplayed_ once the switch has re-displayed, so a later Edit Target reply restarts again.
    bool pullAwaitingReply_ = false;
    bool pullRedisplayed_ = false;
    // Phase 12 FR-047: [A, B]. Set by setEndpointName and setComponentState (v1 / v2: the defaults; v3: the stream).
    std::array<EndpointName, 2> names_{defaultEndpointName(Krate::DSP::BassMorphEngine::Endpoint::A),
                                       defaultEndpointName(Krate::DSP::BassMorphEngine::Endpoint::B)};
    // Phase 12 FR-076 (plan 4.6.1): UI thread only (queueOpened asks for UI dispatch). The views read the cache
    // through a const ProfundumSpectrumFrame*; it is written even while no editor is open (FR-091).
    ProfundumSpectrumFrame cachedFrame_ = silenceFrame();
    Steinberg::Vst::DataExchangeReceiverHandler dataExchangeReceiver_{this};  // the IMessage fallback decoder
    std::uint64_t framesReceived_ = 0;
    std::uint32_t queueOpenCount_ = 0;
    std::uint32_t queueCloseCount_ = 0;
    std::uint32_t lastBlockSize_ = 0;
    // Phase 14 FR-035 (plan 4.9): a frame arrived since the last queueOpened (the indicator shows it; before one the
    // marker falls back to the slider value). UI thread only.
    bool frameSinceOpen_ = false;
    // Phase 12 FR-061 (P-5): the OffsetRingKnobs bound to each macro target, by ProfundumMacroTarget index; raw,
    // non-owning, non-null only while the editor is open (the view tree owns the knobs).
    static constexpr std::size_t kMaxRingKnobsPerTarget = 4;
    std::array<std::array<OffsetRingKnob*, kMaxRingKnobsPerTarget>, Krate::DSP::ProfundumMacroMatrix::kNumTargets>
        ringKnobs_{};
    // Phase 12 FR-080 (plan 4.6.8): created in initialize and never prepared, so reset() never prepares its
    // crossover (bass_stereo_field.h:205-207); heap-held because the class is non-copyable (:168-169).
    std::unique_ptr<Krate::DSP::BassStereoField> field_;
    UI::SpreadCurveModel spreadModel_{};  // read by the SpreadCurveView through a const pointer
    // Phase 12 FR-034 (plan 4.6.10): the wired ADSRDisplay; raw, non-owning, non-null only while it is wired.
    Krate::Plugins::ADSRDisplay* adsrDisplay_ = nullptr;

    // Phase 12 T026 (plan 4.6.2; FR-091): raw, non-owning view pointers, non-null only while the editor is open
    // (the view tree owns the views); all nulled in willClose.
    VSTGUI::VST3Editor* activeEditor_ = nullptr;
    std::array<UI::HarmonicBarView*, 2> barViews_{};  // [Main, Core] in creation order
    UI::SpreadCurveView* spreadView_ = nullptr;
    UI::ProfundumPanelSubController* panel_ = nullptr;
    // Phase 14 FR-035: the Main-page Morph Position slider, registered by the panel's verifyView, nulled in willClose.
    MorphIndicatorSlider* morphIndicator_ = nullptr;
    // Phase 13 FR-030: frame-owned, created in didOpen, nulled in willClose.
    Krate::Plugins::PresetBrowserView* presetBrowserView_ = nullptr;
    // Frame-owned, created by createCustomView("UpdateBanner"), nulled in willClose.
    Krate::Plugins::UpdateBannerView* updateBannerView_ = nullptr;
    // Plan 4.6.7 (D-10): created in didOpen, released in willClose.
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> editorSyncTimer_;
    int openGestures_ = 0;  // plan 4.6.4: beginEdit minus endEdit
    Steinberg::Vst::ParamID paramBeingSet_ = 0xFFFFFFFFu;  // the tag inside setParamNormalized (echo guard)
    // Session UI state (never persisted): the page, the Advanced tab and the zoom step.
    int activePage_ = 0;
    int activeTab_ = 0;
    int zoomIndex_ = 0;
};

}  // namespace Profundum
