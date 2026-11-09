// ==============================================================================
// MorphEngine DSP System Implementation
// ==============================================================================
// Reference: specs/005-morph-system/spec.md
// ==============================================================================

#include "morph_engine.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Disrumpo {

// =============================================================================
// Constants
// =============================================================================

/// @brief Epsilon for distance calculations to avoid division by zero.
constexpr float kDistanceEpsilon = 1e-6f;

/// @brief Threshold for "cursor on node" detection (100% weight).
constexpr float kOnNodeThreshold = 0.001f;

/// @brief Inverse distance weighting exponent (p=2 per spec).
// Note: Not currently used directly but documents the algorithm
// constexpr float kIDWExponent = 2.0f;

// =============================================================================
// Lifecycle
// =============================================================================

MorphEngine::MorphEngine() noexcept {
    // Initialize nodes with default positions for 2-node A-B morphing
    nodes_[0] = MorphNode(0, 0.0f, 0.0f, DistortionType::SoftClip);
    nodes_[1] = MorphNode(1, 1.0f, 0.0f, DistortionType::Tube);
    nodes_[2] = MorphNode(2, 0.0f, 1.0f, DistortionType::Fuzz);
    nodes_[3] = MorphNode(3, 1.0f, 1.0f, DistortionType::SineFold);
}

void MorphEngine::prepare(double sampleRate, int maxBlockSize, int maxOversampleFactor) noexcept {
    sampleRate_ = sampleRate;

    // Configure position smoothers
    smootherX_.configure(smoothingTimeMs_, static_cast<float>(sampleRate));
    smootherY_.configure(smoothingTimeMs_, static_cast<float>(sampleRate));
    smootherX_.snapTo(targetX_);
    smootherY_.snapTo(targetY_);

    // Prepare all distortion adapters
    for (auto& adapter : adapters_) {
        adapter.prepare(sampleRate, maxBlockSize, maxOversampleFactor);
    }
    blendedAdapter_.prepare(sampleRate, maxBlockSize, maxOversampleFactor);
    blendedParamsDirty_ = true;

    // Initialize adapter types from nodes
    for (int i = 0; i < kMaxMorphNodes; ++i) {
        adapters_[i].setType(nodes_[i].type);
        adapters_[i].setParams(nodes_[i].params);
        adapters_[i].setCommonParams(nodes_[i].commonParams);
    }
    updateLatencyAlignment();
    clearSwitchFade();

    prepared_ = true;
}

void MorphEngine::updateLatencyAlignment() noexcept {
    int latency = 0;
    for (int i = 0; i < activeNodeCount_; ++i) {
        latency = std::max(latency, adapters_[i].getProcessingLatency());
    }
    latency_ = latency;
    for (auto& adapter : adapters_) {
        adapter.setLatencyAlignment(latency);
    }
    blendedAdapter_.setLatencyAlignment(latency);
}

void MorphEngine::reset() noexcept {
    // Reset smoothers
    smootherX_.reset();
    smootherY_.reset();
    smootherX_.snapTo(targetX_);
    smootherY_.snapTo(targetY_);

    // Reset all adapters
    for (auto& adapter : adapters_) {
        adapter.reset();
    }
    blendedAdapter_.reset();
    blendedParamsDirty_ = true;

    // Reset weights to default (50/50 for 2-node)
    weights_ = {0.5f, 0.5f, 0.0f, 0.0f};
    weightsValid_ = false;

    // Reset modulation offsets
    driveModOffset_ = 0.0f;
    mixModOffset_ = 0.0f;
    toneModOffset_ = 0.0f;
    biasModOffset_ = 0.0f;
    sweepDriveScale_ = 1.0f;
    appliedSweepDriveScale_ = 1.0f;

    clearSwitchFade();
}

void MorphEngine::resetState() noexcept {
    for (auto& adapter : adapters_) {
        adapter.reset();
    }
    blendedAdapter_.reset();
    blendedParamsDirty_ = true;
    clearSwitchFade();
}

void MorphEngine::clearSwitchFade() noexcept {
    // Every adapter is clear: no path is stale, nothing to fade from
    processedSameType_ = isSameType();
    processedNodeCount_ = activeNodeCount_;
    switchFadeRemaining_ = 0;
    lastOutput_ = 0.0f;
    hasOutput_ = false;
}

void MorphEngine::setProcessingRate(double rate) noexcept {
    sampleRate_ = rate;
    smootherX_.configure(smoothingTimeMs_, static_cast<float>(rate));
    smootherY_.configure(smoothingTimeMs_, static_cast<float>(rate));
    for (auto& adapter : adapters_) {
        adapter.setProcessingRate(rate);
    }
    blendedAdapter_.setProcessingRate(rate);
    blendedParamsDirty_ = true;
}

// =============================================================================
// Configuration
// =============================================================================

void MorphEngine::setMorphPosition(float x, float y) noexcept {
    targetX_ = std::clamp(x, 0.0f, 1.0f);
    targetY_ = std::clamp(y, 0.0f, 1.0f);
    smootherX_.setTarget(targetX_);
    smootherY_.setTarget(targetY_);
}

void MorphEngine::setMode(MorphMode mode) noexcept {
    mode_ = mode;
    // Recalculate weights with current position
    const float posX = smootherX_.getCurrentValue();
    const float posY = smootherY_.getCurrentValue();
    calculateMorphWeights(posX, posY);
}

void MorphEngine::setDriveMixModOffset(float driveOffset, float mixOffset) noexcept {
    driveModOffset_ = driveOffset;
    mixModOffset_ = mixOffset;
    blendedParamsDirty_ = true;
}

void MorphEngine::setToneBiasModOffset(float toneOffset, float biasOffset) noexcept {
    toneModOffset_ = toneOffset;
    biasModOffset_ = biasOffset;
    blendedParamsDirty_ = true;
}

void MorphEngine::setSmoothingTime(float timeMs) noexcept {
    smoothingTimeMs_ = std::clamp(timeMs, 0.0f, 500.0f);
    smootherX_.configure(smoothingTimeMs_, static_cast<float>(sampleRate_));
    smootherY_.configure(smoothingTimeMs_, static_cast<float>(sampleRate_));
}

void MorphEngine::setNodes(const std::array<MorphNode, kMaxMorphNodes>& nodes, int activeCount) noexcept {
    debugSetNodesCalls.fetch_add(1, std::memory_order_relaxed);
    nodes_ = nodes;
    activeNodeCount_ = std::clamp(activeCount, kMinActiveNodes, kMaxMorphNodes);

    // Update adapter configurations
    for (int i = 0; i < kMaxMorphNodes; ++i) {
        adapters_[i].setType(nodes_[i].type);
        adapters_[i].setParams(nodes_[i].params);
        adapters_[i].setCommonParams(nodes_[i].commonParams);
    }
    // The adapters now hold the unscaled drive: the next cross-family sample re-applies the sweep
    appliedSweepDriveScale_ = 1.0f;
    // ... and the unmodulated base params: the next sample re-applies any active offsets
    offsetsWereActive_ = false;
    // ... and the identical-type path routes the new nodes at the next sample (D-005)
    blendedParamsDirty_ = true;
    // Every node is padded to the slowest active node's latency (D-081)
    updateLatencyAlignment();

    // Update path check
    allSameType_ = isSameType();
    if (activeNodeCount_ > 0) {
        dominantFamily_ = getFamily(nodes_[0].type);
    }

    // Recalculate weights
    const float posX = smootherX_.getCurrentValue();
    const float posY = smootherY_.getCurrentValue();
    calculateMorphWeights(posX, posY);
}

const std::array<float, kMaxMorphNodes>& MorphEngine::getWeights() const noexcept {
    return weights_;
}

float MorphEngine::getSmoothedX() const noexcept {
    return smootherX_.getCurrentValue();
}

float MorphEngine::getSmoothedY() const noexcept {
    return smootherY_.getCurrentValue();
}

// =============================================================================
// Weight Computation (FR-001, FR-014, FR-015)
// =============================================================================

void MorphEngine::calculateMorphWeights(float posX, float posY) noexcept {
    weights_ = computeWeights(mode_, nodes_, activeNodeCount_, posX, posY);
    weightsX_ = posX;
    weightsY_ = posY;
    weightsValid_ = true;
}

namespace {

/// @brief Radial2D weights (no threshold step): angular IDW around the pad centre,
/// blended towards equal weights as the cursor nears the centre.
void calculateRadialWeights(std::array<float, kMaxMorphNodes>& weights,
                            const std::array<MorphNode, kMaxMorphNodes>& nodes, int count, float cursorX,
                            float cursorY) noexcept {
    // Convert XY to polar coordinates (center at 0.5, 0.5)
    const float centerX = 0.5f;
    const float centerY = 0.5f;
    const float dx = cursorX - centerX;
    const float dy = cursorY - centerY;

    // Distance from center (0 = center, ~0.707 = corner)
    const float distance = std::sqrt(dx * dx + dy * dy) * 2.0f;  // Scale to [0, ~1.41]
    const float clampedDistance = std::clamp(distance, 0.0f, 1.0f);

    // At center (distance = 0), all nodes get equal weight
    if (clampedDistance < kOnNodeThreshold) {
        const float equalWeight = 1.0f / static_cast<float>(count);
        for (int i = 0; i < count; ++i) {
            weights[i] = equalWeight;
        }
        return;
    }

    // Angle in radians (0 to 2*PI)
    const float angle = std::atan2(dy, dx);

    // Map nodes to angles based on their positions around the center
    // For 4 nodes at corners: A=0deg(east), B=90deg(north), C=180deg(west), D=270deg(south)
    // We use inverse angular distance weighting
    constexpr float kTwoPi = 6.283185307f;  // NOLINT(modernize-use-std-numbers)
    constexpr float kPi = 3.141592654f;     // NOLINT(modernize-use-std-numbers)

    std::array<float, kMaxMorphNodes> nodeAngles{};
    for (int i = 0; i < count; ++i) {
        const float nodeDx = nodes[i].posX - centerX;
        const float nodeDy = nodes[i].posY - centerY;
        nodeAngles[i] = std::atan2(nodeDy, nodeDx);
    }

    // Calculate angular distances (wrap-around aware)
    float totalWeight = 0.0f;
    for (int i = 0; i < count; ++i) {
        float angularDist = std::abs(angle - nodeAngles[i]);
        if (angularDist > kPi) {
            angularDist = kTwoPi - angularDist;  // Wrap around
        }

        // Inverse angular distance weighting
        const float invAngDist = 1.0f / (angularDist * angularDist + kDistanceEpsilon);
        weights[i] = invAngDist;
        totalWeight += invAngDist;
    }

    // Normalize by angle
    if (totalWeight > 0.0f) {
        for (int i = 0; i < count; ++i) {
            weights[i] /= totalWeight;
        }
    }

    // Scale by distance: at center (dist=0) weights are equal,
    // at edge (dist=1) weights are fully determined by angle
    const float equalWeight = 1.0f / static_cast<float>(count);
    for (int i = 0; i < count; ++i) {
        weights[i] = equalWeight * (1.0f - clampedDistance) + weights[i] * clampedDistance;
    }
}

}  // namespace

std::array<float, kMaxMorphNodes> MorphEngine::computeWeights(
    MorphMode mode, const std::array<MorphNode, kMaxMorphNodes>& nodes, int count, float x,
    float y) noexcept {
    std::array<float, kMaxMorphNodes> weights{};
    count = std::min(count, kMaxMorphNodes);
    if (count <= 0) {
        return weights;
    }

    // Mode-specific weight calculation
    switch (mode) {
        case MorphMode::Linear1D: {
            // 1D Linear: Use only X position along the node axis. The active nodes sit
            // evenly on the axis in order A, B, C, D (i / (N - 1)), not at their 2-D posX:
            // the corner layout gives C the X of A and D the X of B (D-029, D-100).
            std::array<float, kMaxMorphNodes> distances{};
            const float lastIndex = static_cast<float>(count - 1);

            for (int i = 0; i < count; ++i) {
                const float nodeX = count > 1 ? static_cast<float>(i) / lastIndex : 0.0f;
                distances[i] = std::abs(x - nodeX);

                // Check for "cursor on node" special case (100% weight)
                if (distances[i] < kOnNodeThreshold) {
                    weights.fill(0.0f);
                    weights[i] = 1.0f;
                    return weights;
                }
            }

            // Calculate inverse distance weights (p=2)
            float totalWeight = 0.0f;
            for (int i = 0; i < count; ++i) {
                const float invDist = 1.0f / (distances[i] * distances[i] + kDistanceEpsilon);
                weights[i] = invDist;
                totalWeight += invDist;
            }

            // Normalize weights
            if (totalWeight > 0.0f) {
                for (int i = 0; i < count; ++i) {
                    weights[i] /= totalWeight;
                }
            }
            break;
        }

        case MorphMode::Planar2D: {
            // 2D Planar: Use Euclidean distance in XY space
            std::array<float, kMaxMorphNodes> distances{};

            for (int i = 0; i < count; ++i) {
                const float dx = x - nodes[i].posX;
                const float dy = y - nodes[i].posY;
                distances[i] = std::sqrt(dx * dx + dy * dy);

                // Check for "cursor on node" special case (100% weight)
                if (distances[i] < kOnNodeThreshold) {
                    weights.fill(0.0f);
                    weights[i] = 1.0f;
                    return weights;
                }
            }

            // Calculate inverse distance weights (p=2)
            float totalWeight = 0.0f;
            for (int i = 0; i < count; ++i) {
                const float invDist = 1.0f / (distances[i] * distances[i] + kDistanceEpsilon);
                weights[i] = invDist;
                totalWeight += invDist;
            }

            // Normalize weights
            if (totalWeight > 0.0f) {
                for (int i = 0; i < count; ++i) {
                    weights[i] /= totalWeight;
                }
            }
            break;
        }

        case MorphMode::Radial2D: {
            calculateRadialWeights(weights, nodes, count, x, y);
            break;
        }
    }

    // Apply weight threshold (FR-015): skip nodes below kWeightThreshold
    float totalAfterThreshold = 0.0f;
    for (int i = 0; i < count; ++i) {
        if (weights[i] < kWeightThreshold) {
            weights[i] = 0.0f;
        } else {
            totalAfterThreshold += weights[i];
        }
    }

    // Renormalize remaining weights to sum to 1.0
    if (totalAfterThreshold > 0.0f && totalAfterThreshold < 1.0f - kDistanceEpsilon) {
        for (int i = 0; i < count; ++i) {
            weights[i] /= totalAfterThreshold;
        }
    }
    return weights;
}

float MorphEngine::transitionGainForWeight(float weight) noexcept {
    // Constant-voltage law (T1.13): the gain is the normalized weight itself, so the
    // gains of the active nodes sum to 1. Cross-family node outputs of the same input are
    // coherent (correlation 0.987-0.9987 measured), so this is level-exact for identical
    // nodes; an equal-power sqrt(w) law adds up to +3 dB at the midpoint. The endpoints
    // are exact by branch.
    if (weight <= 0.0f) {
        return 0.0f;
    }
    if (weight >= 1.0f) {
        return 1.0f;
    }
    return weight;
}

// =============================================================================
// Path Detection (D-028)
// =============================================================================

bool MorphEngine::isSameType() const noexcept {
    // Only identical types share one processor with interpolated parameters; two
    // different types, of one family or not, run in parallel and are blended (FR-MORPH-003:
    // a single processor would switch its transfer function at the dominance boundary).
    if (activeNodeCount_ <= 1) {
        return true;
    }

    const DistortionType firstType = nodes_[0].type;
    for (int i = 1; i < activeNodeCount_; ++i) {
        if (nodes_[i].type != firstType) {
            return false;
        }
    }
    return true;
}

// =============================================================================
// Parameter Interpolation (Same Type)
// =============================================================================

DistortionParams MorphEngine::interpolateParams() const noexcept {
    // Find dominant node (highest weight) for discrete parameters
    // (mode selects, toggles, bit patterns — can't be interpolated)
    int dominantNode = 0;
    float maxWeight = 0.0f;
    for (int i = 0; i < activeNodeCount_; ++i) {
        if (weights_[i] > maxWeight) {
            maxWeight = weights_[i];
            dominantNode = i;
        }
    }

    // Start with dominant node's params for discrete values,
    // then zero all continuous fields before weighted accumulation.
    // BUG FIX: Previously used default-constructed DistortionParams (non-zero defaults)
    // with += accumulation, which doubled/corrupted parameter values.
    DistortionParams result = nodes_[dominantNode].params;

    // --- Zero all continuous fields that will be accumulated via += ---
    // Discrete fields (mode selects, toggles, bit patterns) are kept from dominant node.
    // So are the integer/structural fields (stages, fftSize, magnitudeBits,
    // iterations): a weighted sum of floored terms lands below the shared value
    // (D-011/D-030), and an fftSize that is not the node's own value re-sized the
    // spectral processor every sample (D-001).

    // Saturation continuous
    result.bias = 0.0f;
    result.sag = 0.0f;
    result.curve = 0.0f;
    result.knee = 0.0f;
    result.threshold = 0.0f;
    result.ceiling = 0.0f;
    result.speed = 0.0f;
    result.hfRoll = 0.0f;
    result.flutter = 0.0f;
    result.octave = 0.0f;
    result.sustain = 0.0f;
    result.asymmetry = 0.0f;
    result.body = 0.0f;

    // Wavefold continuous
    result.folds = 0.0f;
    result.shape = 0.0f;
    result.symmetry = 0.0f;
    result.angle = 0.0f;

    // Digital continuous
    result.bitDepth = 0.0f;
    result.sampleRateRatio = 0.0f;
    result.smoothness = 0.0f;
    result.dither = 0.0f;
    result.jitter = 0.0f;
    result.quantLevels = 0.0f;
    result.quantOffset = 0.0f;
    result.bitwiseIntensity = 0.0f;
    result.bitwisePattern = 0.0f;

    // Dynamic continuous
    result.sensitivity = 0.0f;
    result.attackMs = 0.0f;
    result.releaseMs = 0.0f;
    result.dynamicCurve = 0.0f;
    result.dynamicDepth = 0.0f;
    result.hold = 0.0f;

    // Hybrid continuous
    result.feedback = 0.0f;
    result.delayMs = 0.0f;
    result.modDepth = 0.0f;
    result.rsCurve = 0.0f;
    result.fbCurve = 0.0f;
    result.filterFreq = 0.0f;
    result.limThreshold = 0.0f;

    // Aliasing continuous
    result.freqShift = 0.0f;

    // Experimental continuous
    result.chaosAmount = 0.0f;
    result.attractorSpeed = 0.0f;
    result.chaosCoupling = 0.0f;
    result.chaosXDrive = 0.0f;
    result.chaosYDrive = 0.0f;
    result.formantShift = 0.0f;
    result.formantCurve = 0.0f;
    result.formantReso = 0.0f;
    result.formantBW = 0.0f;
    result.formantGender = 0.0f;
    result.formantBlend = 0.0f;
    result.grainSizeMs = 0.0f;
    result.grainDensity = 0.0f;
    result.grainPVar = 0.0f;
    result.grainDVar = 0.0f;
    result.grainPos = 0.0f;
    result.grainCurve = 0.0f;

    // Spectral continuous
    result.spectralCurve = 0.0f;
    result.spectralTilt = 0.0f;
    result.spectralThreshold = 0.0f;
    result.spectralFreq = 0.0f;

    // Fractal continuous
    result.scaleFactor = 0.0f;
    result.frequencyDecay = 0.0f;
    result.fractalCurve = 0.0f;
    result.fractalFB = 0.0f;
    result.fractalDepth = 0.0f;

    // Stochastic continuous
    result.jitterAmount = 0.0f;
    result.jitterRate = 0.0f;
    result.coefficientNoise = 0.0f;
    result.stochasticDrift = 0.0f;
    result.stochasticSmooth = 0.0f;

    // Allpass continuous
    result.resonantFreq = 0.0f;
    result.allpassFeedback = 0.0f;
    result.decayTimeS = 0.0f;
    result.allpassDrive = 0.0f;
    result.allpassDamp = 0.0f;

    // --- Weighted accumulation of all continuous parameters ---
    for (int i = 0; i < activeNodeCount_; ++i) {
        const float w = weights_[i];
        if (w < kWeightThreshold) continue;

        const auto& p = nodes_[i].params;

        // Saturation
        result.bias += w * p.bias;
        result.sag += w * p.sag;
        result.curve += w * p.curve;
        result.knee += w * p.knee;
        result.threshold += w * p.threshold;
        result.ceiling += w * p.ceiling;
        result.speed += w * p.speed;
        result.hfRoll += w * p.hfRoll;
        result.flutter += w * p.flutter;
        result.octave += w * p.octave;
        result.sustain += w * p.sustain;
        result.asymmetry += w * p.asymmetry;
        result.body += w * p.body;

        // Wavefold
        result.folds += w * p.folds;
        result.shape += w * p.shape;
        result.symmetry += w * p.symmetry;
        result.angle += w * p.angle;

        // Digital
        result.bitDepth += w * p.bitDepth;
        result.sampleRateRatio += w * p.sampleRateRatio;
        result.smoothness += w * p.smoothness;
        result.dither += w * p.dither;
        result.jitter += w * p.jitter;
        result.quantLevels += w * p.quantLevels;
        result.quantOffset += w * p.quantOffset;
        result.bitwiseIntensity += w * p.bitwiseIntensity;
        result.bitwisePattern += w * p.bitwisePattern;

        // Dynamic
        result.sensitivity += w * p.sensitivity;
        result.attackMs += w * p.attackMs;
        result.releaseMs += w * p.releaseMs;
        result.dynamicCurve += w * p.dynamicCurve;
        result.dynamicDepth += w * p.dynamicDepth;
        result.hold += w * p.hold;

        // Hybrid
        result.feedback += w * p.feedback;
        result.delayMs += w * p.delayMs;
        result.modDepth += w * p.modDepth;
        result.rsCurve += w * p.rsCurve;
        result.fbCurve += w * p.fbCurve;
        result.filterFreq += w * p.filterFreq;
        result.limThreshold += w * p.limThreshold;

        // Aliasing
        result.freqShift += w * p.freqShift;

        // Experimental
        result.chaosAmount += w * p.chaosAmount;
        result.attractorSpeed += w * p.attractorSpeed;
        result.chaosCoupling += w * p.chaosCoupling;
        result.chaosXDrive += w * p.chaosXDrive;
        result.chaosYDrive += w * p.chaosYDrive;
        result.formantShift += w * p.formantShift;
        result.formantCurve += w * p.formantCurve;
        result.formantReso += w * p.formantReso;
        result.formantBW += w * p.formantBW;
        result.formantGender += w * p.formantGender;
        result.formantBlend += w * p.formantBlend;
        result.grainSizeMs += w * p.grainSizeMs;
        result.grainDensity += w * p.grainDensity;
        result.grainPVar += w * p.grainPVar;
        result.grainDVar += w * p.grainDVar;
        result.grainPos += w * p.grainPos;
        result.grainCurve += w * p.grainCurve;

        // Spectral
        result.spectralCurve += w * p.spectralCurve;
        result.spectralTilt += w * p.spectralTilt;
        result.spectralThreshold += w * p.spectralThreshold;
        result.spectralFreq += w * p.spectralFreq;

        // Fractal
        result.scaleFactor += w * p.scaleFactor;
        result.frequencyDecay += w * p.frequencyDecay;
        result.fractalCurve += w * p.fractalCurve;
        result.fractalFB += w * p.fractalFB;
        result.fractalDepth += w * p.fractalDepth;

        // Stochastic
        result.jitterAmount += w * p.jitterAmount;
        result.jitterRate += w * p.jitterRate;
        result.coefficientNoise += w * p.coefficientNoise;
        result.stochasticDrift += w * p.stochasticDrift;
        result.stochasticSmooth += w * p.stochasticSmooth;

        // Allpass
        result.resonantFreq += w * p.resonantFreq;
        result.allpassFeedback += w * p.allpassFeedback;
        result.decayTimeS += w * p.decayTimeS;
        result.allpassDrive += w * p.allpassDrive;
        result.allpassDamp += w * p.allpassDamp;
    }

    return result;
}

DistortionCommonParams MorphEngine::interpolateCommonParams() const noexcept {
    DistortionCommonParams result;
    result.drive = 0.0f;
    result.mix = 0.0f;
    result.toneHz = 0.0f;

    for (int i = 0; i < activeNodeCount_; ++i) {
        const float w = weights_[i];
        if (w < kWeightThreshold) continue;

        const auto& cp = nodes_[i].commonParams;
        result.drive += w * cp.drive;
        result.mix += w * cp.mix;
        result.toneHz += w * cp.toneHz;
    }

    return result;
}

// =============================================================================
// Processing
// =============================================================================

float MorphEngine::process(float input) noexcept {
    if (!prepared_) {
        return input;
    }

    // Advance smoothers
    const float smoothedX = smootherX_.process();
    const float smoothedY = smootherY_.process();

    // Recalculate weights if position changed (they are a function of the mode, the
    // nodes and the cursor; setMode/setNodes recompute them themselves). allSameType_
    // only changes in setNodes, which updates it.
    if (!weightsValid_ || smoothedX != weightsX_ || smoothedY != weightsY_) {
        calculateMorphWeights(smoothedX, smoothedY);
    }

    // A node type change can switch between the interpolated processor and the parallel
    // adapters, and a node count increase adds parallel adapters: the incoming ones were
    // idle, so clear them and fade from the last output over 5 ms (D-032). Before the first
    // processed sample (after prepare() / reset()) every adapter is clear already.
    const bool pathSwitch = allSameType_ != processedSameType_;
    const bool nodesAdded = !allSameType_ && activeNodeCount_ > processedNodeCount_;
    if ((pathSwitch || nodesAdded) && hasOutput_) {
        if (allSameType_) {
            blendedAdapter_.reset();
            blendedParamsDirty_ = true;
        } else {
            const int first = pathSwitch ? 0 : processedNodeCount_;
            for (int i = first; i < activeNodeCount_; ++i) {
                adapters_[i].reset();
            }
        }
        switchFadeOrigin_ = lastOutput_;
        switchFadeLength_ = std::max(1, static_cast<int>(kSwitchFadeSeconds * sampleRate_));
        switchFadeRemaining_ = switchFadeLength_;
    }
    processedSameType_ = allSameType_;
    processedNodeCount_ = activeNodeCount_;

    // One interpolated processor for identical types, parallel blend otherwise
    float out = allSameType_ ? processSameFamily(input) : processCrossFamily(input);
    if (switchFadeRemaining_ > 0) {
        // Linear crossfade; its last sample is the new path's output exactly (branch)
        --switchFadeRemaining_;
        if (switchFadeRemaining_ > 0) {
            const float g =
                1.0f - static_cast<float>(switchFadeRemaining_) / static_cast<float>(switchFadeLength_);
            out = switchFadeOrigin_ + g * (out - switchFadeOrigin_);
        }
    }
    lastOutput_ = out;
    hasOutput_ = true;
    return out;
}

void MorphEngine::processBlock(const float* input, float* output, int numSamples) noexcept {
    // Per sample until the engine is steady; from there every sample takes the same path
    // with nothing to update, so the rest runs as a block with the same arithmetic
    int i = 0;
    while (i < numSamples && !isSteady()) {
        output[i] = process(input[i]);
        ++i;
    }
    if (i < numSamples) {
        processSteadySpan(input + i, output + i, numSamples - i);
    }
}

bool MorphEngine::isSteady() const noexcept {
    // process() would advance the cursor smoothers (a settled smoother returns its target
    // unchanged only when it already sits on it) and recompute the weights
    if (!prepared_ || !weightsValid_) return false;
    if (smootherX_.getCurrentValue() != smootherX_.getTarget() ||
        smootherY_.getCurrentValue() != smootherY_.getTarget()) {
        return false;
    }
    if (smootherX_.getCurrentValue() != weightsX_ || smootherY_.getCurrentValue() != weightsY_) {
        return false;
    }
    // ... or start or continue a path switch / node activation fade
    if (allSameType_ != processedSameType_ || activeNodeCount_ != processedNodeCount_ ||
        switchFadeRemaining_ > 0) {
        return false;
    }
    if (allSameType_) {
        // ... or route the identical-type parameters (processSameFamily)
        float weightDelta = 0.0f;
        for (int n = 0; n < kMaxMorphNodes; ++n) {
            weightDelta = std::max(weightDelta, std::abs(weights_[n] - blendedWeights_[n]));
        }
        return !blendedParamsDirty_ && !(weightDelta > kBlendedWeightEpsilon) &&
               sweepDriveScale_ == blendedSweepDriveScale_;
    }
    // ... or re-apply the cross-family offsets / sweep multiplier (processCrossFamily)
    const bool offsetsActive = driveModOffset_ != 0.0f || mixModOffset_ != 0.0f ||
                               toneModOffset_ != 0.0f || biasModOffset_ != 0.0f;
    return !offsetsActive && !offsetsWereActive_ && sweepDriveScale_ == appliedSweepDriveScale_;
}

void MorphEngine::processSteadySpan(const float* input, float* output, int numSamples) noexcept {
    if (numSamples <= 0) return;
    if (allSameType_) {
        blendedAdapter_.processBlock(input, output, numSamples);
    } else {
        // processCrossFamily: output = 0, then += node output * gain, node by node in order
        std::array<float, kMaxMorphNodes> gains{};
        for (int n = 0; n < activeNodeCount_; ++n) {
            gains[static_cast<size_t>(n)] = transitionGainForWeight(weights_[n]);
        }
        for (int start = 0; start < numSamples; start += kSpanChunk) {
            const int len = std::min(kSpanChunk, numSamples - start);
            std::copy_n(input + start, len, spanInput_.begin());
            float* out = output + start;
            std::fill_n(out, len, 0.0f);
            for (int n = 0; n < activeNodeCount_; ++n) {
                adapters_[n].processBlock(spanInput_.data(), spanNode_.data(), len);
                const float gain = gains[static_cast<size_t>(n)];
                for (int k = 0; k < len; ++k) {
                    out[k] += spanNode_[static_cast<size_t>(k)] * gain;
                }
            }
        }
    }
    lastOutput_ = output[numSamples - 1];
    hasOutput_ = true;
}

float MorphEngine::processSameFamily(float input) noexcept {
    // Identical types (isSameType): interpolate parameters, use single processor
    // Per spec FR-006, FR-018

    // Control rate (D-005): route only when the weights moved, a node / offset setter
    // ran, the adapter was reset, or the sweep drive multiplier changed
    float weightDelta = 0.0f;
    for (int i = 0; i < kMaxMorphNodes; ++i) {
        weightDelta = std::max(weightDelta, std::abs(weights_[i] - blendedWeights_[i]));
    }
    if (blendedParamsDirty_ || weightDelta > kBlendedWeightEpsilon
        || sweepDriveScale_ != blendedSweepDriveScale_) {
        blendedParamsDirty_ = false;
        blendedWeights_ = weights_;
        blendedSweepDriveScale_ = sweepDriveScale_;
        ++blendedRouteCount_;

        // Every active node has this type, so the processor never switches
        blendedAdapter_.setType(nodes_[0].type);

        auto bp = interpolateParams();
        lastBlendedParams_ = bp;
        bp.bias = std::clamp(bp.bias + biasModOffset_ * 2.0f, -1.0f, 1.0f);
        blendedAdapter_.setParams(bp);

        auto cp = interpolateCommonParams();
        cp.drive = modulatedDrive(cp.drive);
        cp.mix = std::clamp(cp.mix + mixModOffset_, 0.0f, 1.0f);
        cp.toneHz = std::clamp(cp.toneHz + toneModOffset_ * 7800.0f, 200.0f, 8000.0f);
        blendedAdapter_.setCommonParams(cp);
    }

    return blendedAdapter_.process(input);
}

float MorphEngine::processCrossFamily(float input) noexcept {
    // Different types (of one family or not): parallel processing, outputs summed with
    // constant-voltage gains (transitionGainForWeight). Per spec FR-007, FR-008, SC-002

    // Apply modulation offsets and the sweep drive multiplier to per-node adapters (from
    // base node params). A sweep multiplier change alone re-applies once, so its return
    // to 1 restores the base drive; so does the offsets' return to all 0 (D-031).
    const bool offsetsActive = driveModOffset_ != 0.0f || mixModOffset_ != 0.0f
                               || toneModOffset_ != 0.0f || biasModOffset_ != 0.0f;
    if (offsetsActive || offsetsWereActive_ || sweepDriveScale_ != appliedSweepDriveScale_) {
        appliedSweepDriveScale_ = sweepDriveScale_;
        offsetsWereActive_ = offsetsActive;
        for (int i = 0; i < activeNodeCount_; ++i) {
            auto cp = nodes_[i].commonParams;
            cp.drive = modulatedDrive(cp.drive);
            cp.mix = std::clamp(cp.mix + mixModOffset_, 0.0f, 1.0f);
            cp.toneHz = std::clamp(cp.toneHz + toneModOffset_ * 7800.0f, 200.0f, 8000.0f);
            adapters_[i].setCommonParams(cp);
            auto p = nodes_[i].params;
            p.bias = std::clamp(p.bias + biasModOffset_ * 2.0f, -1.0f, 1.0f);
            adapters_[i].setParams(p);
        }
    }

    float output = 0.0f;

    // Every active node runs every sample, also below kWeightThreshold (its gain is then
    // 0), so its filter and delay state never goes stale before its weight rises again.
    for (int i = 0; i < activeNodeCount_; ++i) {
        const float nodeOutput = adapters_[i].process(input);
        output += nodeOutput * transitionGainForWeight(weights_[i]);
    }

    return output;
}

} // namespace Disrumpo
