// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "dsp/harmonic_snapshot_json.h"
#include "parameters/innexus_params.h"
#include "parameters/note_value_ui.h"
#include "plugin_ids.h"
#include "preset/innexus_preset_config.h"
#include "update/innexus_update_config.h"
#include "version.h"

#include "controller/views/harmonic_display_view.h"
#include "controller/views/confidence_indicator_view.h"
#include "controller/views/memory_slot_status_view.h"
#include "controller/views/evolution_position_view.h"
#include "controller/views/modulator_activity_view.h"
#include "controller/modulator_sub_controller.h"
#include "controller/adsr_expanded_overlay.h"
#include "controller/sample_drop_target.h"
#include "ui/adsr_display.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"
#include "ui/update_banner_view.h"
#include "display/shared_display_bridge.h"
#include "display/display_bridge_log.h"

#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cviewcontainer.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>

namespace Innexus {

// ==============================================================================
// Controller view sync: sample display + section visibility + display timer
// ==============================================================================


// ==============================================================================
// updateAdsrDisplayFromParams — push current ADSR parameter values to display
// ==============================================================================
void Controller::updateAdsrDisplayFromParams()
{
    if (!adsrDisplayView_)
        return;

    // Read normalized values and convert to plain ms using log mapping
    // This matches the processor's denormalization: ms = kMin * pow(kMax/kMin, norm)
    constexpr float kMin = 1.0f;
    constexpr float kMax = 5000.0f;
    auto normToMs = [&](Steinberg::Vst::ParamID id) -> float {
        auto norm = static_cast<float>(getParamNormalized(id));
        return kMin * std::pow(kMax / kMin, norm);
    };

    adsrDisplayView_->setAttackMs(normToMs(kAdsrAttackId));
    adsrDisplayView_->setDecayMs(normToMs(kAdsrDecayId));
    adsrDisplayView_->setSustainLevel(
        static_cast<float>(getParamNormalized(kAdsrSustainId)));
    adsrDisplayView_->setReleaseMs(normToMs(kAdsrReleaseId));

    // Curve amounts: normalized [0,1] -> plain [-1,+1]
    auto normToCurve = [&](Steinberg::Vst::ParamID id) -> float {
        return static_cast<float>(getParamNormalized(id)) * 2.0f - 1.0f;
    };

    adsrDisplayView_->setAttackCurve(normToCurve(kAdsrAttackCurveId));
    adsrDisplayView_->setDecayCurve(normToCurve(kAdsrDecayCurveId));
    adsrDisplayView_->setReleaseCurve(normToCurve(kAdsrReleaseCurveId));
}

// ==============================================================================
// onSampleFileSelected — called by SampleLoadSubController
// ==============================================================================
void Controller::onSampleFileSelected(const std::string& filePath)
{
    // Extract filename for display
    auto filename = std::filesystem::path(filePath).filename().string();
    loadedSampleFilename_ = filename;
    loadedSampleFullPath_ = filePath;
    setSampleFilenameDisplay(filename, filePath);

    // Send file path to processor via IMessage
    auto* msg = allocateMessage();
    if (!msg)
        return;
    msg->setMessageID("LoadSampleFile");
    auto* attrs = msg->getAttributes();
    attrs->setBinary("path", filePath.data(),
                     static_cast<Steinberg::uint32>(filePath.size()));
    sendMessage(msg);
    msg->release();
}

// ==============================================================================
// setSampleFilenameDisplay
// ==============================================================================
void Controller::setSampleFilenameDisplay(const std::string& filename,
                                          const std::string& fullPath)
{
    if (sampleFilenameLabel_)
    {
        sampleFilenameLabel_->setText(VSTGUI::UTF8String(filename));
        sampleFilenameLabel_->setTooltipText(VSTGUI::UTF8String(fullPath));
        sampleFilenameLabel_->invalid();
    }
}

// ==============================================================================
// updateSampleLoadVisibility
// ==============================================================================
void Controller::updateSampleLoadVisibility()
{
    if (!sampleLoadContainer_)
        return;

    // InputSource: 0 = Sample (show), 1 = Sidechain (hide)
    auto* param = getParameterObject(kInputSourceId);
    if (!param)
        return;

    bool isSampleMode = param->getNormalized() < 0.5;
    sampleLoadContainer_->setVisible(isSampleMode);
    if (sampleLoadContainer_->getParentView())
        sampleLoadContainer_->getParentView()->invalid();
}

// ==============================================================================
// updateImpactKnobVisibility
// ==============================================================================
void Controller::updateImpactKnobVisibility()
{
    // Lazy-find the impact knob container by locating a child with the
    // ImpactHardness tag (806) and taking its parent container
    if (!impactKnobContainer_ && activeEditor_) {
        if (auto* frame = activeEditor_->getFrame()) {
            std::function<void(VSTGUI::CViewContainer*)> search;
            search = [this, &search](VSTGUI::CViewContainer* container) {
                if (!container || impactKnobContainer_) return;
                VSTGUI::ViewIterator it(container);
                while (*it) {
                    if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                        if (control->getTag() == kImpactHardnessId) {
                            impactKnobContainer_ = container;
                            return;
                        }
                    }
                    if (auto* child = (*it)->asViewContainer())
                        search(child);
                    ++it;
                }
            };
            search(frame);
        }
    }

    if (!impactKnobContainer_)
        return;

    // ExciterType: StringListParameter with 3 values
    // Normalized: 0.0 = Residual, 0.5 = Impact, 1.0 = Bow
    // Show Impact knobs only when ExciterType == Impact (norm ~0.5)
    auto* param = getParameterObject(kExciterTypeId);
    if (!param)
        return;

    float norm = param->getNormalized();
    bool isImpact = (norm >= 0.25f && norm < 0.75f);
    impactKnobContainer_->setVisible(isImpact);
    if (impactKnobContainer_->getParentView())
        impactKnobContainer_->getParentView()->invalid();
}

// ==============================================================================
// updateBowVisibility
// ==============================================================================
void Controller::updateBowVisibility()
{
    // Lazy-find the bow knob container by locating a child with the
    // BowPressure tag (820) and taking its parent container
    if (!bowKnobContainer_ && activeEditor_) {
        if (auto* frame = activeEditor_->getFrame()) {
            std::function<void(VSTGUI::CViewContainer*)> search;
            search = [this, &search](VSTGUI::CViewContainer* container) {
                if (!container || bowKnobContainer_) return;
                VSTGUI::ViewIterator it(container);
                while (*it) {
                    if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                        if (control->getTag() == kBowPressureId) {
                            bowKnobContainer_ = container;
                            return;
                        }
                    }
                    if (auto* child = (*it)->asViewContainer())
                        search(child);
                    ++it;
                }
            };
            search(frame);
        }
    }

    if (!bowKnobContainer_)
        return;

    // ExciterType: StringListParameter with 3 values
    // Normalized: 0.0 = Residual, 0.5 = Impact, 1.0 = Bow
    // Show Bow knobs only when ExciterType == Bow (norm >= 0.75)
    auto* param = getParameterObject(kExciterTypeId);
    if (!param)
        return;

    float norm = param->getNormalized();
    bool isBow = (norm >= 0.75f);
    bowKnobContainer_->setVisible(isBow);
    if (bowKnobContainer_->getParentView())
        bowKnobContainer_->getParentView()->invalid();
}

// ==============================================================================
// updateResonatorVisibility
// ==============================================================================
void Controller::updateResonatorVisibility()
{
    // Lazy-find the two resonator knob containers by locating children with
    // unique tags: ResonanceStretch (803) for modal, WaveguideStiffness (811)
    // for waveguide. Take their parent containers.
    if ((!modalKnobContainer_ || !waveguideKnobContainer_) && activeEditor_) {
        if (auto* frame = activeEditor_->getFrame()) {
            std::function<void(VSTGUI::CViewContainer*)> search;
            search = [this, &search](VSTGUI::CViewContainer* container) {
                if (!container || (modalKnobContainer_ && waveguideKnobContainer_))
                    return;
                VSTGUI::ViewIterator it(container);
                while (*it) {
                    if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                        if (control->getTag() == kResonanceStretchId
                            && !modalKnobContainer_) {
                            modalKnobContainer_ = container;
                        }
                        if (control->getTag() == kWaveguideStiffnessId
                            && !waveguideKnobContainer_) {
                            waveguideKnobContainer_ = container;
                        }
                    }
                    if (auto* child = (*it)->asViewContainer())
                        search(child);
                    ++it;
                }
            };
            search(frame);
        }
    }

    if (!modalKnobContainer_ || !waveguideKnobContainer_)
        return;

    // ResonanceType: StringListParameter with 2 values
    // Normalized: 0.0 = Modal, 1.0 = Waveguide
    auto* param = getParameterObject(kResonanceTypeId);
    if (!param)
        return;

    float norm = param->getNormalized();
    bool isModal = (norm < 0.5f);      // Modal = 0.0
    bool isWaveguide = (norm >= 0.5f); // Waveguide = 1.0

    modalKnobContainer_->setVisible(isModal);
    waveguideKnobContainer_->setVisible(isWaveguide);

    if (modalKnobContainer_->getParentView())
        modalKnobContainer_->getParentView()->invalid();
}

// ==============================================================================
// updateFeedbackVisibility
// ==============================================================================
void Controller::updateFeedbackVisibility()
{
    // Lazy-find the feedback container by locating a child with the
    // FeedbackAmount tag (710) and taking its parent container.
    if (!feedbackContainer_ && activeEditor_) {
        if (auto* frame = activeEditor_->getFrame()) {
            std::function<void(VSTGUI::CViewContainer*)> search;
            search = [this, &search](VSTGUI::CViewContainer* container) {
                if (!container || feedbackContainer_) return;
                VSTGUI::ViewIterator it(container);
                while (*it) {
                    if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                        if (control->getTag() == kAnalysisFeedbackId) {
                            feedbackContainer_ = container;
                            return;
                        }
                    }
                    if (auto* child = (*it)->asViewContainer())
                        search(child);
                    ++it;
                }
            };
            search(frame);
        }
    }

    // Lazy-find the latency mode container by locating a child with the
    // LatencyMode tag (501) and taking its parent container.
    if (!latencyModeContainer_ && activeEditor_) {
        if (auto* frame = activeEditor_->getFrame()) {
            std::function<void(VSTGUI::CViewContainer*)> search;
            search = [this, &search](VSTGUI::CViewContainer* container) {
                if (!container || latencyModeContainer_) return;
                VSTGUI::ViewIterator it(container);
                while (*it) {
                    if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                        if (control->getTag() == kLatencyModeId) {
                            latencyModeContainer_ = container;
                            return;
                        }
                    }
                    if (auto* child = (*it)->asViewContainer())
                        search(child);
                    ++it;
                }
            };
            search(frame);
        }
    }

    // InputSource: 0 = Sample, 1 = Sidechain
    // Both feedback and latency mode only work in sidechain mode.
    auto* param = getParameterObject(kInputSourceId);
    if (!param)
        return;

    bool isSidechain = param->getNormalized() >= 0.5;

    if (feedbackContainer_) {
        feedbackContainer_->setVisible(isSidechain);
        if (feedbackContainer_->getParentView())
            feedbackContainer_->getParentView()->invalid();
    }

    if (latencyModeContainer_) {
        latencyModeContainer_->setVisible(isSidechain);
        if (latencyModeContainer_->getParentView())
            latencyModeContainer_->getParentView()->invalid();
    }
}

// ==============================================================================
// onDisplayTimerFired (T016: FR-049)
// ==============================================================================
void Controller::onDisplayTimerFired()
{
    // Update sample load panel visibility (cheap check every 30ms)
    updateSampleLoadVisibility();

    // Update impact exciter knob visibility based on exciter type
    updateImpactKnobVisibility();

    // Update bow exciter knob visibility based on exciter type
    updateBowVisibility();

    // Update resonator knob containers based on resonance type
    updateResonatorVisibility();

    // Hide feedback section when not in sidechain mode
    updateFeedbackVisibility();

    // Tier 3 fallback: if DataExchange hasn't delivered data after ~330ms,
    // read directly from the processor's shared display buffer
    if (!dataExchangeActive_ && sharedDisplay_) {
        ++fallbackTickCounter_;
        if (fallbackTickCounter_ > 10) { // ~330ms at 30ms timer
            if (fallbackTickCounter_ == 11)
                KRATE_BRIDGE_LOG("Innexus::Controller — Tier 3 fallback ACTIVATED (no DataExchange after ~330ms)");
            auto counter = sharedDisplay_->frameCounter.load(std::memory_order_acquire);
            if (counter != lastProcessedFrameCounter_) {
                std::memcpy(&cachedDisplayData_, &sharedDisplay_->buffer,
                    sizeof(DisplayData));
                cachedDisplayData_.frameCounter = counter;
            }
        }
    }

    // Check if we have new data (frame counter changed)
    if (cachedDisplayData_.frameCounter == lastProcessedFrameCounter_)
    {
        // No new frame — track staleness. When the host stops calling
        // process() (e.g. playback stopped), clear the display so it
        // doesn't show stale partial data indefinitely.
        if (staleTickCount_ < kStaleTickThreshold)
        {
            ++staleTickCount_;
            if (staleTickCount_ == kStaleTickThreshold)
            {
                // Clear cached data and push empty data to all views
                DisplayData empty{};
                cachedDisplayData_ = empty;
                if (harmonicDisplayView_)
                    harmonicDisplayView_->updateData(empty);
                if (confidenceIndicatorView_)
                    confidenceIndicatorView_->updateData(empty);
                if (memorySlotStatusView_)
                    memorySlotStatusView_->updateData(empty);
                if (evolutionPositionView_)
                    evolutionPositionView_->updateData(empty, false);
                if (modActivityView0_)
                    modActivityView0_->updateData(0.0f, false);
                if (modActivityView1_)
                    modActivityView1_->updateData(0.0f, false);
            }
        }
        return;
    }

    staleTickCount_ = 0;
    lastProcessedFrameCounter_ = cachedDisplayData_.frameCounter;

    // Update each custom view with cached display data
    if (harmonicDisplayView_)
        harmonicDisplayView_->updateData(cachedDisplayData_);

    if (confidenceIndicatorView_)
        confidenceIndicatorView_->updateData(cachedDisplayData_);

    if (memorySlotStatusView_)
        memorySlotStatusView_->updateData(cachedDisplayData_);

    if (evolutionPositionView_)
    {
        // Evolution is active when the enable parameter is on
        // We approximate by checking if the position is non-zero
        // or if the cached data shows it active
        bool evolutionActive = cachedDisplayData_.evolutionPosition > 0.001f ||
                               cachedDisplayData_.manualMorphPosition > 0.001f;
        evolutionPositionView_->updateData(cachedDisplayData_, evolutionActive);
    }

    if (modActivityView0_)
        modActivityView0_->updateData(
            cachedDisplayData_.mod1Phase,
            cachedDisplayData_.mod1Active);

    if (modActivityView1_)
        modActivityView1_->updateData(
            cachedDisplayData_.mod2Phase,
            cachedDisplayData_.mod2Active);

    // WI-9: ADSR playback state arrives as copied scalars on the display block,
    // not as pointers into processor-owned atomics.
    if (adsrDisplayView_)
        adsrDisplayView_->setPlaybackState(
            cachedDisplayData_.adsrEnvelopeOutput,
            static_cast<int>(cachedDisplayData_.adsrStage),
            cachedDisplayData_.adsrActive != 0);
}
} // namespace Innexus
