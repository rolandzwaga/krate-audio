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

/// @brief Transition zone lower bound (40% weight).
constexpr float kTransitionZoneLow = 0.4f;

/// @brief Transition zone upper bound (60% weight).
constexpr float kTransitionZoneHigh = 0.6f;

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

void MorphEngine::prepare(double sampleRate, int maxBlockSize) noexcept {
    sampleRate_ = sampleRate;

    // Configure position smoothers
    smootherX_.configure(smoothingTimeMs_, static_cast<float>(sampleRate));
    smootherY_.configure(smoothingTimeMs_, static_cast<float>(sampleRate));
    smootherX_.snapTo(targetX_);
    smootherY_.snapTo(targetY_);

    // Prepare all distortion adapters
    for (auto& adapter : adapters_) {
        adapter.prepare(sampleRate, maxBlockSize);
    }
    blendedAdapter_.prepare(sampleRate, maxBlockSize);

    // Initialize adapter types from nodes
    for (int i = 0; i < kMaxMorphNodes; ++i) {
        adapters_[i].setType(nodes_[i].type);
        adapters_[i].setParams(nodes_[i].params);
        adapters_[i].setCommonParams(nodes_[i].commonParams);
    }

    prepared_ = true;
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

    // Reset weights to default (50/50 for 2-node)
    weights_ = {0.5f, 0.5f, 0.0f, 0.0f};
    transitionGains_ = {1.0f, 1.0f, 1.0f, 1.0f};

    // Reset modulation offsets
    driveModOffset_ = 0.0f;
    mixModOffset_ = 0.0f;
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
}

void MorphEngine::setSmoothingTime(float timeMs) noexcept {
    smoothingTimeMs_ = std::clamp(timeMs, 0.0f, 500.0f);
    smootherX_.configure(smoothingTimeMs_, static_cast<float>(sampleRate_));
    smootherY_.configure(smoothingTimeMs_, static_cast<float>(sampleRate_));
}

void MorphEngine::setNodes(const std::array<MorphNode, kMaxMorphNodes>& nodes, int activeCount) noexcept {
    nodes_ = nodes;
    activeNodeCount_ = std::clamp(activeCount, kMinActiveNodes, kMaxMorphNodes);

    // Update adapter configurations
    for (int i = 0; i < kMaxMorphNodes; ++i) {
        adapters_[i].setType(nodes_[i].type);
        adapters_[i].setParams(nodes_[i].params);
        adapters_[i].setCommonParams(nodes_[i].commonParams);
    }

    // Update family check
    allSameFamily_ = isSameFamily();
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
    // Initialize all weights to zero
    weights_.fill(0.0f);

    if (activeNodeCount_ <= 0) {
        return;
    }

    // Mode-specific weight calculation
    switch (mode_) {
        case MorphMode::Linear1D: {
            // 1D Linear: Use only X position along the node axis
            std::array<float, kMaxMorphNodes> distances{};

            for (int i = 0; i < activeNodeCount_; ++i) {
                distances[i] = calculate1DDistance(posX, nodes_[i].posX);

                // Check for "cursor on node" special case (100% weight)
                if (distances[i] < kOnNodeThreshold) {
                    weights_.fill(0.0f);
                    weights_[i] = 1.0f;
                    return;
                }
            }

            // Calculate inverse distance weights (p=2)
            float totalWeight = 0.0f;
            for (int i = 0; i < activeNodeCount_; ++i) {
                const float invDist = 1.0f / (distances[i] * distances[i] + kDistanceEpsilon);
                weights_[i] = invDist;
                totalWeight += invDist;
            }

            // Normalize weights
            if (totalWeight > 0.0f) {
                for (int i = 0; i < activeNodeCount_; ++i) {
                    weights_[i] /= totalWeight;
                }
            }
            break;
        }

        case MorphMode::Planar2D: {
            // 2D Planar: Use Euclidean distance in XY space
            std::array<float, kMaxMorphNodes> distances{};

            for (int i = 0; i < activeNodeCount_; ++i) {
                distances[i] = calculate2DDistance(posX, posY, nodes_[i].posX, nodes_[i].posY);

                // Check for "cursor on node" special case (100% weight)
                if (distances[i] < kOnNodeThreshold) {
                    weights_.fill(0.0f);
                    weights_[i] = 1.0f;
                    return;
                }
            }

            // Calculate inverse distance weights (p=2)
            float totalWeight = 0.0f;
            for (int i = 0; i < activeNodeCount_; ++i) {
                const float invDist = 1.0f / (distances[i] * distances[i] + kDistanceEpsilon);
                weights_[i] = invDist;
                totalWeight += invDist;
            }

            // Normalize weights
            if (totalWeight > 0.0f) {
                for (int i = 0; i < activeNodeCount_; ++i) {
                    weights_[i] /= totalWeight;
                }
            }
            break;
        }

        case MorphMode::Radial2D: {
            calculateRadialWeights(posX, posY);
            break;
        }
    }

    // Apply weight threshold (FR-015): skip nodes below kWeightThreshold
    float totalAfterThreshold = 0.0f;
    for (int i = 0; i < activeNodeCount_; ++i) {
        if (weights_[i] < kWeightThreshold) {
            weights_[i] = 0.0f;
        } else {
            totalAfterThreshold += weights_[i];
        }
    }

    // Renormalize remaining weights to sum to 1.0
    if (totalAfterThreshold > 0.0f && totalAfterThreshold < 1.0f - kDistanceEpsilon) {
        for (int i = 0; i < activeNodeCount_; ++i) {
            weights_[i] /= totalAfterThreshold;
        }
    }

    // Calculate transition zone gains for cross-family processing (FR-008)
    for (int i = 0; i < activeNodeCount_; ++i) {
        transitionGains_[i] = calculateTransitionGain(weights_[i]);
    }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
float MorphEngine::calculate1DDistance(float cursorX, float nodeX) const noexcept {
    return std::abs(cursorX - nodeX);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
float MorphEngine::calculate2DDistance(float cursorX, float cursorY,
                                        float nodeX, float nodeY) const noexcept {
    const float dx = cursorX - nodeX;
    const float dy = cursorY - nodeY;
    return std::sqrt(dx * dx + dy * dy);
}

void MorphEngine::calculateRadialWeights(float cursorX, float cursorY) noexcept {
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
        const float equalWeight = 1.0f / static_cast<float>(activeNodeCount_);
        for (int i = 0; i < activeNodeCount_; ++i) {
            weights_[i] = equalWeight;
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
    for (int i = 0; i < activeNodeCount_; ++i) {
        const float nodeDx = nodes_[i].posX - centerX;
        const float nodeDy = nodes_[i].posY - centerY;
        nodeAngles[i] = std::atan2(nodeDy, nodeDx);
    }

    // Calculate angular distances (wrap-around aware)
    float totalWeight = 0.0f;
    for (int i = 0; i < activeNodeCount_; ++i) {
        float angularDist = std::abs(angle - nodeAngles[i]);
        if (angularDist > kPi) {
            angularDist = kTwoPi - angularDist;  // Wrap around
        }

        // Inverse angular distance weighting
        const float invAngDist = 1.0f / (angularDist * angularDist + kDistanceEpsilon);
        weights_[i] = invAngDist;
        totalWeight += invAngDist;
    }

    // Normalize by angle
    if (totalWeight > 0.0f) {
        for (int i = 0; i < activeNodeCount_; ++i) {
            weights_[i] /= totalWeight;
        }
    }

    // Scale by distance: at center (dist=0) weights are equal,
    // at edge (dist=1) weights are fully determined by angle
    const float equalWeight = 1.0f / static_cast<float>(activeNodeCount_);
    for (int i = 0; i < activeNodeCount_; ++i) {
        weights_[i] = equalWeight * (1.0f - clampedDistance) + weights_[i] * clampedDistance;
    }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
float MorphEngine::calculateTransitionGain(float weight) const noexcept {
    // FR-008: Transition zone 40-60%
    // Below 40%: May deactivate (gain ramps down)
    // 40-60%: Equal-power ramp up
    // Above 60%: Fully active (gain = 1.0)

    if (weight >= kTransitionZoneHigh) {
        return 1.0f;  // Fully active
    }
    if (weight <= kWeightThreshold) {
        return 0.0f;  // Deactivated
    }
    if (weight < kTransitionZoneLow) {
        // Below transition zone: linear ramp from 0 to entry level
        const float normalizedPos = weight / kTransitionZoneLow;
        float fadeOut = 0.0f;
        float fadeIn = 0.0f;
        Krate::DSP::equalPowerGains(normalizedPos, fadeOut, fadeIn);
        return fadeIn;  // Use equal-power fade-in
    }

    // In transition zone (40-60%): equal-power ramp
    const float zonePos = (weight - kTransitionZoneLow) / (kTransitionZoneHigh - kTransitionZoneLow);
    float fadeOut = 0.0f;
    float fadeIn = 0.0f;
    Krate::DSP::equalPowerGains(zonePos, fadeOut, fadeIn);
    return fadeIn;
}

// =============================================================================
// Family Detection
// =============================================================================

bool MorphEngine::isSameFamily() const noexcept {
    if (activeNodeCount_ <= 1) {
        return true;
    }

    const DistortionFamily firstFamily = getFamily(nodes_[0].type);
    for (int i = 1; i < activeNodeCount_; ++i) {
        if (getFamily(nodes_[i].type) != firstFamily) {
            return false;
        }
    }
    return true;
}

// =============================================================================
// Parameter Interpolation (Same-Family)
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
    result.gate = 0.0f;
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
    result.resonance = 0.0f;
    result.bitwiseIntensity = 0.0f;
    result.bitwisePattern = 0.0f;
    result.bitwiseBits = 0.0f;

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
    result.stages = 0;
    result.modDepth = 0.0f;
    result.rsCurve = 0.0f;
    result.fbCurve = 0.0f;
    result.filterFreq = 0.0f;
    result.limThreshold = 0.0f;

    // Aliasing continuous
    result.freqShift = 0.0f;

    // Bitwise continuous
    result.rotateAmount = 0;

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
    result.fftSize = 0;
    result.magnitudeBits = 0;
    result.spectralCurve = 0.0f;
    result.spectralTilt = 0.0f;
    result.spectralThreshold = 0.0f;
    result.spectralFreq = 0.0f;

    // Fractal continuous
    result.iterations = 0;
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
    result.allpassCurve = 0.0f;
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
        result.gate += w * p.gate;
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
        result.resonance += w * p.resonance;
        result.bitwiseIntensity += w * p.bitwiseIntensity;
        result.bitwisePattern += w * p.bitwisePattern;
        result.bitwiseBits += w * p.bitwiseBits;

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
        result.stages += static_cast<int>(w * static_cast<float>(p.stages));
        result.modDepth += w * p.modDepth;
        result.rsCurve += w * p.rsCurve;
        result.fbCurve += w * p.fbCurve;
        result.filterFreq += w * p.filterFreq;
        result.limThreshold += w * p.limThreshold;

        // Aliasing
        result.freqShift += w * p.freqShift;

        // Bitwise
        result.rotateAmount += static_cast<int>(w * static_cast<float>(p.rotateAmount));

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
        result.fftSize += static_cast<int>(w * static_cast<float>(p.fftSize));
        result.magnitudeBits += static_cast<int>(w * static_cast<float>(p.magnitudeBits));
        result.spectralCurve += w * p.spectralCurve;
        result.spectralTilt += w * p.spectralTilt;
        result.spectralThreshold += w * p.spectralThreshold;
        result.spectralFreq += w * p.spectralFreq;

        // Fractal
        result.iterations += static_cast<int>(w * static_cast<float>(p.iterations));
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
        result.allpassCurve += w * p.allpassCurve;
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

    // Recalculate weights if position changed
    calculateMorphWeights(smoothedX, smoothedY);

    // Update family check
    allSameFamily_ = isSameFamily();

    // Choose processing path based on family
    if (allSameFamily_) {
        return processSameFamily(input);
    } else {
        return processCrossFamily(input);
    }
}

void MorphEngine::processBlock(const float* input, float* output, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i) {
        output[i] = process(input[i]);
    }
}

float MorphEngine::processSameFamily(float input) noexcept {
    // Same-family optimization: interpolate parameters, use single processor
    // Per spec FR-006, FR-018

    // Get the dominant type (use highest-weight node's type)
    int dominantNode = 0;
    float maxWeight = 0.0f;
    for (int i = 0; i < activeNodeCount_; ++i) {
        if (weights_[i] > maxWeight) {
            maxWeight = weights_[i];
            dominantNode = i;
        }
    }

    // Set blended adapter to dominant type with interpolated params
    blendedAdapter_.setType(nodes_[dominantNode].type);
    blendedAdapter_.setParams(interpolateParams());

    // Apply drive/mix modulation offsets after interpolation
    auto cp = interpolateCommonParams();
    cp.drive = std::clamp(cp.drive + driveModOffset_ * 10.0f, 0.0f, 10.0f);
    cp.mix = std::clamp(cp.mix + mixModOffset_, 0.0f, 1.0f);
    blendedAdapter_.setCommonParams(cp);

    return blendedAdapter_.process(input);
}

float MorphEngine::processCrossFamily(float input) noexcept {
    // Cross-family: parallel processing with equal-power crossfade
    // Per spec FR-007, FR-008

    // Apply drive/mix modulation offsets to per-node adapters (from base node params)
    if (driveModOffset_ != 0.0f || mixModOffset_ != 0.0f) {
        for (int i = 0; i < activeNodeCount_; ++i) {
            auto cp = nodes_[i].commonParams;
            cp.drive = std::clamp(cp.drive + driveModOffset_ * 10.0f, 0.0f, 10.0f);
            cp.mix = std::clamp(cp.mix + mixModOffset_, 0.0f, 1.0f);
            adapters_[i].setCommonParams(cp);
        }
    }

    float output = 0.0f;

    // Process each active node
    for (int i = 0; i < activeNodeCount_; ++i) {
        const float weight = weights_[i];
        if (weight < kWeightThreshold) continue;

        // Apply transition zone gain (FR-008)
        const float gain = transitionGains_[i];
        if (gain < kWeightThreshold) continue;

        // Process through this node's adapter
        const float nodeOutput = adapters_[i].process(input);

        // Equal-power weighted sum
        // Weight already normalized, apply transition gain for smooth activation
        output += nodeOutput * weight * gain;
    }

    return output;
}

} // namespace Disrumpo
