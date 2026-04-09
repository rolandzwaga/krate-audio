// ==============================================================================
// Visibility Controllers Implementation
// ==============================================================================

#include "controller/visibility_controllers.h"
#include "controller/views/spectrum_display.h"
#include "controller/views/sweep_indicator.h"
#include "controller/morph_link.h"
#include "dsp/sweep_morph_link.h"
#include "plugin_ids.h"
#include "dsp/band_state.h"
#include "dsp/morph_node.h"

#include <cmath>
#include <algorithm>

namespace Disrumpo {

// ==============================================================================
// BandCountDisplayController
// ==============================================================================

BandCountDisplayController::BandCountDisplayController(
    SpectrumDisplay** displayPtr,
    Steinberg::Vst::Parameter* bandCountParam)
: displayPtr_(displayPtr)
, bandCountParam_(bandCountParam)
{
    if (bandCountParam_) {
        bandCountParam_->addRef();
        bandCountParam_->addDependent(this);
    }
}

BandCountDisplayController::~BandCountDisplayController() {
    deactivate();
    if (bandCountParam_) {
        bandCountParam_->release();
        bandCountParam_ = nullptr;
    }
}

void BandCountDisplayController::deactivate() {
    if (isActive_.exchange(false, std::memory_order_acq_rel)) {
        if (bandCountParam_) {
            bandCountParam_->removeDependent(this);
        }
    }
}

void PLUGIN_API BandCountDisplayController::update(
    Steinberg::FUnknown* /*changedUnknown*/, Steinberg::int32 message) {
    if (!isActive_.load(std::memory_order_acquire)) return;

    if (message == IDependent::kChanged && bandCountParam_
        && displayPtr_ && *displayPtr_) {
        float normalized = static_cast<float>(bandCountParam_->getNormalized());
        int bandCount = static_cast<int>(std::round(normalized * 3.0f)) + 1;
        (*displayPtr_)->setNumBands(bandCount);
    }
}

// ==============================================================================
// SpectrumModeController
// ==============================================================================

SpectrumModeController::SpectrumModeController(
    SpectrumDisplay** displayPtr,
    Steinberg::Vst::Parameter* modeParam)
: displayPtr_(displayPtr)
, modeParam_(modeParam)
{
    if (modeParam_) {
        modeParam_->addRef();
        modeParam_->addDependent(this);
    }
}

SpectrumModeController::~SpectrumModeController() {
    deactivate();
    if (modeParam_) {
        modeParam_->release();
        modeParam_ = nullptr;
    }
}

void SpectrumModeController::deactivate() {
    if (isActive_.exchange(false, std::memory_order_acq_rel)) {
        if (modeParam_) {
            modeParam_->removeDependent(this);
        }
    }
}

void SpectrumModeController::applyMode() {
    if (!modeParam_ || !displayPtr_ || !*displayPtr_) return;
    int index = static_cast<int>(std::round(modeParam_->getNormalized() * 2.0));
    switch (index) {
        case 0:  (*displayPtr_)->setViewMode(SpectrumViewMode::kWet);  break;
        case 1:  (*displayPtr_)->setViewMode(SpectrumViewMode::kDry);  break;
        case 2:  (*displayPtr_)->setViewMode(SpectrumViewMode::kBoth); break;
        default: (*displayPtr_)->setViewMode(SpectrumViewMode::kWet);  break;
    }
}

void PLUGIN_API SpectrumModeController::update(
    Steinberg::FUnknown* /*changedUnknown*/, Steinberg::int32 message) {
    if (!isActive_.load(std::memory_order_acquire)) return;

    if (message == IDependent::kChanged && modeParam_
        && displayPtr_ && *displayPtr_) {
        applyMode();
    }
}

// ==============================================================================
// MorphSweepLinkController
// ==============================================================================

MorphSweepLinkController::MorphSweepLinkController(
    Steinberg::Vst::EditControllerEx1* controller,
    Steinberg::Vst::Parameter* sweepFreqParam)
: controller_(controller)
, sweepFreqParam_(sweepFreqParam)
{
    if (sweepFreqParam_) {
        sweepFreqParam_->addRef();
        sweepFreqParam_->addDependent(this);
    }
}

MorphSweepLinkController::~MorphSweepLinkController() {
    deactivate();
    if (sweepFreqParam_) {
        sweepFreqParam_->release();
        sweepFreqParam_ = nullptr;
    }
}

void MorphSweepLinkController::deactivate() {
    if (isActive_.exchange(false, std::memory_order_acq_rel)) {
        if (sweepFreqParam_) {
            sweepFreqParam_->removeDependent(this);
        }
    }
}

void PLUGIN_API MorphSweepLinkController::update(
    Steinberg::FUnknown* /*changedUnknown*/, Steinberg::int32 message) {
    if (!isActive_.load(std::memory_order_acquire)) {
        return;
    }

    if (message == IDependent::kChanged && sweepFreqParam_ && controller_) {
        float sweepNorm = static_cast<float>(sweepFreqParam_->getNormalized());
        for (int band = 0; band < 8; ++band) {
            updateBandMorphFromSweep(static_cast<uint8_t>(band), sweepNorm);
        }
    }
}

void MorphSweepLinkController::updateBandMorphFromSweep(uint8_t band, float sweepNorm) {
    auto* morphXLinkParam = controller_->getParameterObject(
        makeBandParamId(band, BandParamType::kBandMorphXLink));
    auto* morphYLinkParam = controller_->getParameterObject(
        makeBandParamId(band, BandParamType::kBandMorphYLink));

    if (!morphXLinkParam || !morphYLinkParam) {
        return;
    }

    int xLinkIndex = static_cast<int>(std::round(morphXLinkParam->getNormalized() * 6.0));
    int yLinkIndex = static_cast<int>(std::round(morphYLinkParam->getNormalized() * 6.0));

    MorphLinkMode xLinkMode = static_cast<MorphLinkMode>(std::clamp(xLinkIndex, 0, 6));
    MorphLinkMode yLinkMode = static_cast<MorphLinkMode>(std::clamp(yLinkIndex, 0, 6));

    if (xLinkMode == MorphLinkMode::None && yLinkMode == MorphLinkMode::None) {
        return;
    }

    auto* morphXParam = controller_->getParameterObject(
        makeBandParamId(band, BandParamType::kBandMorphX));
    auto* morphYParam = controller_->getParameterObject(
        makeBandParamId(band, BandParamType::kBandMorphY));

    if (!morphXParam || !morphYParam) {
        return;
    }

    float currentX = static_cast<float>(morphXParam->getNormalized());
    float currentY = static_cast<float>(morphYParam->getNormalized());

    float newX = applyMorphLinkMode(xLinkMode, sweepNorm, currentX);
    float newY = applyMorphLinkMode(yLinkMode, sweepNorm, currentY);

    if (xLinkMode != MorphLinkMode::None && std::abs(newX - currentX) > 0.001f) {
        controller_->setParamNormalized(
            makeBandParamId(band, BandParamType::kBandMorphX), newX);
        controller_->performEdit(
            makeBandParamId(band, BandParamType::kBandMorphX), newX);
    }

    if (yLinkMode != MorphLinkMode::None && std::abs(newY - currentY) > 0.001f) {
        controller_->setParamNormalized(
            makeBandParamId(band, BandParamType::kBandMorphY), newY);
        controller_->performEdit(
            makeBandParamId(band, BandParamType::kBandMorphY), newY);
    }
}

// ==============================================================================
// NodeSelectionController
// ==============================================================================

NodeSelectionController::NodeSelectionController(
    Steinberg::Vst::EditControllerEx1* controller,
    uint8_t band,
    ShapeShadowStorage* shadowStorage)
: controller_(controller)
, band_(band)
, selectedNodeParam_(controller->getParameterObject(
      makeBandParamId(band, BandParamType::kBandSelectedNode)))
, shapeShadowPtr_(shadowStorage)
{
    for (int i = 0; i < kNumDisplayedProxyParams; ++i) {
        proxyParams_[i] = controller_->getParameterObject(
            makeBandParamId(band, kProxyIndexToBandParam[i]));
    }

    if (selectedNodeParam_) {
        selectedNodeParam_->addRef();
        selectedNodeParam_->addDependent(this);
    }

    for (int n = 0; n < 4; ++n) {
        auto paramId = makeNodeParamId(band, static_cast<uint8_t>(n), NodeParamType::kNodeType);
        nodeTypeParams_[n] = controller_->getParameterObject(paramId);

        if (nodeTypeParams_[n]) {
            nodeTypeParams_[n]->addRef();
            nodeTypeParams_[n]->addDependent(this);
        }
    }

    displayedTypeParam_ = controller_->getParameterObject(
        makeBandParamId(band, BandParamType::kBandDisplayedType));
    if (displayedTypeParam_) {
        displayedTypeParam_->addRef();
        displayedTypeParam_->addDependent(this);
    }

    for (auto* proxyParam : proxyParams_) {
        if (proxyParam) {
            proxyParam->addRef();
            proxyParam->addDependent(this);
        }
    }

    if (selectedNodeParam_) {
        selectedNodeParam_->deferUpdate();
    }
}

NodeSelectionController::~NodeSelectionController() {
    deactivate();
    if (selectedNodeParam_) {
        selectedNodeParam_->release();
        selectedNodeParam_ = nullptr;
    }
    for (auto*& param : nodeTypeParams_) {
        if (param) {
            param->release();
            param = nullptr;
        }
    }
    if (displayedTypeParam_) {
        displayedTypeParam_->release();
        displayedTypeParam_ = nullptr;
    }
    for (auto*& param : proxyParams_) {
        if (param) {
            param->release();
            param = nullptr;
        }
    }
}

void NodeSelectionController::deactivate() {
    if (isActive_.exchange(false, std::memory_order_acq_rel)) {
        if (selectedNodeParam_) {
            selectedNodeParam_->removeDependent(this);
        }
        for (auto* param : nodeTypeParams_) {
            if (param) {
                param->removeDependent(this);
            }
        }
        if (displayedTypeParam_) {
            displayedTypeParam_->removeDependent(this);
        }
        for (auto* param : proxyParams_) {
            if (param) {
                param->removeDependent(this);
            }
        }
    }
}

void PLUGIN_API NodeSelectionController::update(
    Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) {
    if (!isActive_.load(std::memory_order_acquire)) {
        return;
    }
    if (message != IDependent::kChanged || !controller_) {
        return;
    }
    if (isUpdating_) {
        return;
    }

    isUpdating_ = true;

    auto* changedParam = Steinberg::FCast<Steinberg::Vst::Parameter>(changedUnknown);

    if (changedParam == displayedTypeParam_) {
        copyDisplayedTypeToSelectedNode();
    } else if (changedParam == selectedNodeParam_) {
        copySelectedNodeToDisplayedType();
        copySelectedNodeToProxies();
    } else if (isProxyParam(changedParam)) {
        copyProxyToSelectedNode(changedParam);
    } else {
        copySelectedNodeToDisplayedType();
    }

    isUpdating_ = false;
}

int NodeSelectionController::getSelectedNode() const {
    if (!selectedNodeParam_) return 0;
    int sel = static_cast<int>(selectedNodeParam_->getNormalized() * 3.0 + 0.5);
    return std::clamp(sel, 0, 3);
}

bool NodeSelectionController::isProxyParam(Steinberg::Vst::Parameter* param) const {
    return std::ranges::any_of(proxyParams_, [param](const auto* p) { return p == param; });
}

void NodeSelectionController::editParam(Steinberg::Vst::ParamID id, double normValue) {
    controller_->beginEdit(id);
    controller_->setParamNormalized(id, normValue);
    controller_->performEdit(id, normValue);
    controller_->endEdit(id);
}

void NodeSelectionController::copySelectedNodeToDisplayedType() {
    if (!selectedNodeParam_ || !displayedTypeParam_) return;

    int selectedNode = getSelectedNode();
    auto* nodeTypeParam = nodeTypeParams_[selectedNode];
    if (!nodeTypeParam) return;

    float nodeTypeNorm = static_cast<float>(nodeTypeParam->getNormalized());
    float currentDisplayedType = static_cast<float>(displayedTypeParam_->getNormalized());

    if (std::abs(currentDisplayedType - nodeTypeNorm) < 0.001f) return;

    editParam(makeBandParamId(band_, BandParamType::kBandDisplayedType), nodeTypeNorm);
}

void NodeSelectionController::copySelectedNodeToProxies() {
    int selectedNode = getSelectedNode();
    auto nodeIdx = static_cast<uint8_t>(selectedNode);

    for (int i = 0; i < kNumDisplayedProxyParams; ++i) {
        if (!proxyParams_[i]) continue;

        auto* actualParam = controller_->getParameterObject(
            makeNodeParamId(band_, nodeIdx, kProxyIndexToNodeParam[i]));
        if (!actualParam) continue;

        float actualNorm = static_cast<float>(actualParam->getNormalized());
        float proxyNorm = static_cast<float>(proxyParams_[i]->getNormalized());

        if (std::abs(proxyNorm - actualNorm) < 0.0001f) continue;

        editParam(makeBandParamId(band_, kProxyIndexToBandParam[i]), actualNorm);
    }
}

void NodeSelectionController::copyProxyToSelectedNode(Steinberg::Vst::Parameter* changedProxy) {
    int selectedNode = getSelectedNode();
    auto nodeIdx = static_cast<uint8_t>(selectedNode);

    for (int i = 0; i < kNumDisplayedProxyParams; ++i) {
        if (proxyParams_[i] != changedProxy) continue;

        float proxyNorm = static_cast<float>(changedProxy->getNormalized());
        auto actualId = makeNodeParamId(band_, nodeIdx, kProxyIndexToNodeParam[i]);

        auto* actualParam = controller_->getParameterObject(actualId);
        if (actualParam) {
            float actualNorm = static_cast<float>(actualParam->getNormalized());
            if (std::abs(actualNorm - proxyNorm) < 0.0001f) return;
        }

        editParam(actualId, proxyNorm);
        return;
    }
}

void NodeSelectionController::copyDisplayedTypeToSelectedNode() {
    if (!selectedNodeParam_ || !displayedTypeParam_) return;

    int selectedNode = getSelectedNode();

    float displayedTypeNorm = static_cast<float>(displayedTypeParam_->getNormalized());
    auto* nodeTypeParam = nodeTypeParams_[selectedNode];
    if (!nodeTypeParam) return;

    float currentNodeType = static_cast<float>(nodeTypeParam->getNormalized());
    if (std::abs(currentNodeType - displayedTypeNorm) < 0.001f) return;

    int oldTypeIdx = static_cast<int>(currentNodeType * 25.0 + 0.5);
    int newTypeIdx = static_cast<int>(displayedTypeNorm * 25.0 + 0.5);
    oldTypeIdx = std::clamp(oldTypeIdx, 0, 25);
    newTypeIdx = std::clamp(newTypeIdx, 0, 25);

    if (shapeShadowPtr_) {
        float currentSlots[MorphNode::kShapeSlotCount];
        for (int s = 0; s < MorphNode::kShapeSlotCount; ++s) {
            if (proxyParams_[4 + s])
                currentSlots[s] = static_cast<float>(proxyParams_[4 + s]->getNormalized());
            else
                currentSlots[s] = 0.5f;
        }
        shapeShadowPtr_->save(oldTypeIdx, currentSlots);
    }

    auto nodeTypeId = makeNodeParamId(band_, static_cast<uint8_t>(selectedNode),
                                       NodeParamType::kNodeType);
    editParam(nodeTypeId, displayedTypeNorm);

    if (shapeShadowPtr_) {
        float newSlots[MorphNode::kShapeSlotCount];
        shapeShadowPtr_->load(newTypeIdx, newSlots);
        for (int s = 0; s < MorphNode::kShapeSlotCount; ++s) {
            if (proxyParams_[4 + s]) {
                editParam(makeBandParamId(band_, kProxyIndexToBandParam[4 + s]),
                          newSlots[s]);
            }
        }
    }
}

// ==============================================================================
// SweepVisualizationController
// ==============================================================================

SweepVisualizationController::SweepVisualizationController(
    Steinberg::Vst::EditControllerEx1* controller,
    SweepIndicator** sweepIndicator,
    SpectrumDisplay** spectrumDisplay)
: controller_(controller)
, sweepIndicatorPtr_(sweepIndicator)
, spectrumDisplayPtr_(spectrumDisplay)
, modFreqParam_(controller->getParameterObject(kSweepModulatedFrequencyOutputId))
{
    if (modFreqParam_) {
        modFreqParam_->addRef();
        modFreqParam_->addDependent(this);
    }

    sweepEnableParam_ = controller_->getParameterObject(
        makeSweepParamId(SweepParamType::kSweepEnable));
    if (sweepEnableParam_) {
        sweepEnableParam_->addRef();
        sweepEnableParam_->addDependent(this);
    }

    sweepWidthParam_ = controller_->getParameterObject(
        makeSweepParamId(SweepParamType::kSweepWidth));
    if (sweepWidthParam_) {
        sweepWidthParam_->addRef();
        sweepWidthParam_->addDependent(this);
    }

    sweepIntensityParam_ = controller_->getParameterObject(
        makeSweepParamId(SweepParamType::kSweepIntensity));
    if (sweepIntensityParam_) {
        sweepIntensityParam_->addRef();
        sweepIntensityParam_->addDependent(this);
    }

    sweepFalloffParam_ = controller_->getParameterObject(
        makeSweepParamId(SweepParamType::kSweepFalloff));
    if (sweepFalloffParam_) {
        sweepFalloffParam_->addRef();
        sweepFalloffParam_->addDependent(this);
    }
}

SweepVisualizationController::~SweepVisualizationController() {
    deactivate();
    releaseParam(modFreqParam_);
    releaseParam(sweepEnableParam_);
    releaseParam(sweepWidthParam_);
    releaseParam(sweepIntensityParam_);
    releaseParam(sweepFalloffParam_);
}

void SweepVisualizationController::deactivate() {
    if (isActive_.exchange(false, std::memory_order_acq_rel)) {
        removeDependent(modFreqParam_);
        removeDependent(sweepEnableParam_);
        removeDependent(sweepWidthParam_);
        removeDependent(sweepIntensityParam_);
        removeDependent(sweepFalloffParam_);
    }
}

void PLUGIN_API SweepVisualizationController::update(
    Steinberg::FUnknown* /*changedUnknown*/, Steinberg::int32 message) {
    if (!isActive_.load(std::memory_order_acquire)) {
        return;
    }
    if (message != IDependent::kChanged || !controller_) {
        return;
    }

    SweepIndicator* indicator = sweepIndicatorPtr_ ? *sweepIndicatorPtr_ : nullptr;
    if (!indicator) {
        return;
    }

    if (sweepEnableParam_) {
        indicator->setEnabled(sweepEnableParam_->getNormalized() >= 0.5);
    }

    if (modFreqParam_) {
        float normFreq = static_cast<float>(modFreqParam_->getNormalized());
        float freqHz = denormalizeSweepFrequency(normFreq);
        indicator->setCenterFrequency(freqHz);
    }

    if (sweepWidthParam_) {
        constexpr float kMinWidth = 0.5f;
        constexpr float kMaxWidth = 4.0f;
        float widthNorm = static_cast<float>(sweepWidthParam_->getNormalized());
        float widthOct = kMinWidth + widthNorm * (kMaxWidth - kMinWidth);
        indicator->setWidth(widthOct);
    }

    if (sweepIntensityParam_) {
        float intensityNorm = static_cast<float>(sweepIntensityParam_->getNormalized());
        indicator->setIntensity(intensityNorm * 2.0f);
    }

    if (sweepFalloffParam_) {
        indicator->setFalloffMode(
            sweepFalloffParam_->getNormalized() >= 0.5
                ? SweepFalloff::Smooth
                : SweepFalloff::Sharp);
    }

    updateSpectrumBandIntensities();
}

void SweepVisualizationController::updateSpectrumBandIntensities() {
    SpectrumDisplay* display = spectrumDisplayPtr_ ? *spectrumDisplayPtr_ : nullptr;
    if (!display || !modFreqParam_ || !sweepWidthParam_ || !sweepIntensityParam_ || !sweepEnableParam_) {
        return;
    }

    bool enabled = sweepEnableParam_->getNormalized() >= 0.5;
    if (!enabled) {
        display->setSweepEnabled(false);
        return;
    }

    display->setSweepEnabled(true);

    float normFreq = static_cast<float>(modFreqParam_->getNormalized());
    float sweepCenterHz = denormalizeSweepFrequency(normFreq);

    constexpr float kMinWidth = 0.5f;
    constexpr float kMaxWidth = 4.0f;
    float widthNorm = static_cast<float>(sweepWidthParam_->getNormalized());
    float widthOctaves = kMinWidth + widthNorm * (kMaxWidth - kMinWidth);

    float intensityNorm = static_cast<float>(sweepIntensityParam_->getNormalized());
    float intensity = intensityNorm * 2.0f;

    bool smoothFalloff = (sweepFalloffParam_ != nullptr) && sweepFalloffParam_->getNormalized() >= 0.5;

    int numBands = display->getNumBands();
    std::array<float, 4> intensities{};

    for (int i = 0; i < numBands && i < 4; ++i) {
        float lowFreq = (i == 0) ? 20.0f : display->getCrossoverFrequency(i - 1);
        float highFreq = (i == numBands - 1) ? 20000.0f : display->getCrossoverFrequency(i);
        float bandCenterHz = std::sqrt(lowFreq * highFreq);

        if (smoothFalloff) {
            intensities[static_cast<size_t>(i)] = calculateGaussianIntensity(
                bandCenterHz, sweepCenterHz, widthOctaves, intensity);
        } else {
            intensities[static_cast<size_t>(i)] = calculateLinearFalloff(
                bandCenterHz, sweepCenterHz, widthOctaves, intensity);
        }
    }

    display->setSweepBandIntensities(intensities, numBands);
}

void SweepVisualizationController::removeDependent(Steinberg::Vst::Parameter* param) {
    if (param) {
        param->removeDependent(this);
    }
}

void SweepVisualizationController::releaseParam(Steinberg::Vst::Parameter*& param) {
    if (param) {
        param->release();
        param = nullptr;
    }
}

// ==============================================================================
// CrossoverDragBridge
// ==============================================================================

CrossoverDragBridge::CrossoverDragBridge(Steinberg::Vst::EditControllerEx1* controller)
: controller_(controller) {}

void CrossoverDragBridge::onCrossoverChanged(int dividerIndex, float frequencyHz) {
    if (!controller_ || dividerIndex < 0 || dividerIndex >= kMaxBands - 1)
        return;

    auto paramId = makeCrossoverParamId(static_cast<uint8_t>(dividerIndex));

    const float logMin = std::log10(kMinCrossoverHz);
    const float logMax = std::log10(kMaxCrossoverHz);
    float clampedFreq = std::clamp(frequencyHz, kMinCrossoverHz, kMaxCrossoverHz);
    float logFreq = std::log10(clampedFreq);
    double normalized = static_cast<double>(logFreq - logMin) / static_cast<double>(logMax - logMin);
    normalized = std::clamp(normalized, 0.0, 1.0);

    controller_->beginEdit(paramId);
    controller_->setParamNormalized(paramId, normalized);
    controller_->performEdit(paramId, normalized);
    controller_->endEdit(paramId);
}

void CrossoverDragBridge::onBandSelected(int /*bandIndex*/) {
    // No-op: band selection is handled elsewhere
}

void CrossoverDragBridge::deactivate() {
    controller_ = nullptr;
}

} // namespace Disrumpo
