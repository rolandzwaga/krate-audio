#pragma once

// ==============================================================================
// RingGeometry — Pure Math for Concentric Ring Step Sequencer
// ==============================================================================
// Handles all coordinate math for the circular UI: ring radii, segment angles,
// hit testing (point → ring/subzone/step), and radial value conversion.
//
// Ring layout (outer to inner):
//   Ring 0: Velocity (outer) + Gate (inner)
//   Ring 1: Pitch (bipolar)
//   Ring 2: Modifier (outer) + Condition (inner)
//   Ring 3: Ratchet + Chord + Inversion (3 sub-zones)
//   Center: Euclidean dot ring
//
// All angles in radians. 0 = 12 o'clock (top), clockwise positive.
// ==============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Gradus {

// ==============================================================================
// Constants
// ==============================================================================

static constexpr int kRingCount = 4;
static constexpr int kMaxSteps = 32;
static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTwoPi = 2.0f * kPi;

/// Start angle: 12 o'clock = -PI/2 in standard math coordinates.
static constexpr float kStartAngle = -kPi / 2.0f;

// ==============================================================================
// Sub-Zone Identifiers
// ==============================================================================

/// Identifies which lane within a combined ring was hit.
enum class SubZone : uint8_t {
    kNone = 0,
    // Ring 0
    kVelocity,
    kGate,
    // Ring 1
    kPitch,
    // Ring 2
    kModifier,
    kCondition,
    // Ring 3
    kRatchet,
    kChord,
    kInversion,
    // Center
    kEuclidean
};

/// Maps a SubZone to its lane index (0-7) in the IArpLane array.
inline int subZoneToLaneIndex(SubZone zone)
{
    switch (zone) {
        case SubZone::kVelocity:  return 0;
        case SubZone::kGate:      return 1;
        case SubZone::kPitch:     return 2;
        case SubZone::kModifier:  return 3;
        case SubZone::kCondition: return 4;
        case SubZone::kRatchet:   return 5;
        case SubZone::kChord:     return 6;
        case SubZone::kInversion: return 7;
        default:                  return -1;
    }
}

// ==============================================================================
// RingConfig — Per-Ring Radius Configuration
// ==============================================================================

struct RingConfig {
    float innerRadius = 0.0f;
    float outerRadius = 0.0f;
    /// Split radius for combined rings (-1 = no split).
    float splitRadius = -1.0f;
    /// For Ring 3 (3-way split): boundaries between Ratchet/Chord/Inversion.
    float split2Radius = -1.0f;
};

// ==============================================================================
// HitResult — Result of a Point Hit Test
// ==============================================================================

struct HitResult {
    int ringIndex = -1;       ///< Which ring (0-3), or -1 for miss/center
    SubZone subZone = SubZone::kNone;
    int stepIndex = -1;       ///< Which step within the lane's step count
    bool isCenter = false;    ///< True if hit the Euclidean center area
};

// ==============================================================================
// SegmentArc — Angular Bounds for a Step Segment
// ==============================================================================

struct SegmentArc {
    float startAngle = 0.0f;  ///< Radians, measured from 12 o'clock clockwise
    float endAngle = 0.0f;
    float centerAngle = 0.0f;
};

// ==============================================================================
// RingGeometry
// ==============================================================================

class RingGeometry {
public:
    RingGeometry() { setDefaultLayout(); }

    // =========================================================================
    // Configuration
    // =========================================================================

    void setCenter(float cx, float cy)
    {
        centerX_ = cx;
        centerY_ = cy;
    }

    [[nodiscard]] float centerX() const { return centerX_; }
    [[nodiscard]] float centerY() const { return centerY_; }

    void setRingConfig(int ringIndex, const RingConfig& config)
    {
        if (ringIndex >= 0 && ringIndex < kRingCount)
            rings_[ringIndex] = config;
    }

    [[nodiscard]] const RingConfig& ringConfig(int ringIndex) const
    {
        return rings_[std::clamp(ringIndex, 0, kRingCount - 1)];
    }

    void setCenterRadius(float r) { centerRadius_ = r; }
    [[nodiscard]] float centerRadius() const { return centerRadius_; }

    /// Set the step count for a given lane (used for hit testing).
    void setLaneStepCount(int laneIndex, int steps)
    {
        if (laneIndex >= 0 && laneIndex < 8)
            laneStepCounts_[laneIndex] = std::clamp(steps, 1, kMaxSteps);
    }

    [[nodiscard]] int laneStepCount(int laneIndex) const
    {
        if (laneIndex >= 0 && laneIndex < 8)
            return laneStepCounts_[laneIndex];
        return 16;
    }

    // =========================================================================
    // Default Layout (660x660 display)
    // =========================================================================

    void setDefaultLayout()
    {
        centerX_ = 330.0f;
        centerY_ = 330.0f;
        centerRadius_ = 75.0f;

        // Ring 0 (outer): Velocity + Gate
        rings_[0] = {245.0f, 320.0f, 282.0f, -1.0f};
        // Ring 1: Pitch
        rings_[1] = {185.0f, 240.0f, -1.0f, -1.0f};
        // Ring 2: Modifier + Condition
        rings_[2] = {135.0f, 180.0f, 157.0f, -1.0f};
        // Ring 3: Ratchet + Chord + Inversion (3 sub-zones, ~17px each)
        // Ratchet: 113-130, Chord: 97-113, Inversion: 80-97
        rings_[3] = {80.0f, 130.0f, 113.0f, 97.0f};

        laneStepCounts_.fill(16);
    }

    // =========================================================================
    // Angle Calculations
    // =========================================================================

    /// Get the angular arc for a given step index within a total step count.
    [[nodiscard]] static SegmentArc stepArc(int stepIndex, int totalSteps)
    {
        if (totalSteps <= 0) totalSteps = 1;
        const float stepAngle = kTwoPi / static_cast<float>(totalSteps);
        SegmentArc arc;
        arc.startAngle = kStartAngle + static_cast<float>(stepIndex) * stepAngle;
        arc.endAngle = arc.startAngle + stepAngle;
        arc.centerAngle = arc.startAngle + stepAngle * 0.5f;
        return arc;
    }

    /// Convert a point (relative to view origin) to angle from center.
    /// Returns angle in radians, 12 o'clock = -PI/2, clockwise positive.
    [[nodiscard]] float pointToAngle(float x, float y) const
    {
        return std::atan2(y - centerY_, x - centerX_);
    }

    /// Convert a point to distance from center.
    [[nodiscard]] float pointToRadius(float x, float y) const
    {
        const float dx = x - centerX_;
        const float dy = y - centerY_;
        return std::sqrt(dx * dx + dy * dy);
    }

    /// Convert an angle to a step index within a given step count.
    [[nodiscard]] static int angleToStep(float angle, int totalSteps)
    {
        if (totalSteps <= 0) return 0;
        // Normalize angle to [0, 2*PI) relative to start angle
        float normalized = angle - kStartAngle;
        while (normalized < 0.0f) normalized += kTwoPi;
        while (normalized >= kTwoPi) normalized -= kTwoPi;

        const float stepAngle = kTwoPi / static_cast<float>(totalSteps);
        int step = static_cast<int>(normalized / stepAngle);
        return std::clamp(step, 0, totalSteps - 1);
    }

    // =========================================================================
    // Hit Testing
    // =========================================================================

    /// Hit test a point against all rings and center.
    [[nodiscard]] HitResult hitTest(float x, float y) const
    {
        const float radius = pointToRadius(x, y);
        const float angle = pointToAngle(x, y);

        // Check center
        if (radius <= centerRadius_) {
            return {-1, SubZone::kEuclidean, -1, true};
        }

        // Check each ring from inner to outer
        for (int r = kRingCount - 1; r >= 0; --r) {
            const auto& ring = rings_[r];
            if (radius >= ring.innerRadius && radius <= ring.outerRadius) {
                return hitTestRing(r, radius, angle);
            }
        }

        // Gap between rings or outside all rings
        return {};
    }

    // =========================================================================
    // Radial Value Conversion
    // =========================================================================

    /// Convert a radial position to a normalized 0-1 value for a bar-type lane.
    /// For unipolar lanes (velocity, gate, ratchet): 0 at inner edge, 1 at outer.
    /// For bipolar lanes (pitch): 0 at inner, 0.5 at midline, 1 at outer.
    [[nodiscard]] float radiusToNormalizedValue(
        int ringIndex, SubZone zone, float radius) const
    {
        float innerR = 0.0f;
        float outerR = 0.0f;
        getSubZoneRadii(ringIndex, zone, innerR, outerR);

        if (outerR <= innerR) return 0.0f;
        return std::clamp(
            (radius - innerR) / (outerR - innerR), 0.0f, 1.0f);
    }

    /// Convert a normalized 0-1 value to a radius within a sub-zone.
    [[nodiscard]] float normalizedValueToRadius(
        int ringIndex, SubZone zone, float value) const
    {
        float innerR = 0.0f;
        float outerR = 0.0f;
        getSubZoneRadii(ringIndex, zone, innerR, outerR);

        return innerR + std::clamp(value, 0.0f, 1.0f) * (outerR - innerR);
    }

    /// Get the inner and outer radii for a specific sub-zone.
    void getSubZoneRadii(int ringIndex, SubZone zone,
                         float& innerR, float& outerR) const
    {
        if (ringIndex < 0 || ringIndex >= kRingCount) {
            innerR = outerR = 0.0f;
            return;
        }

        const auto& ring = rings_[ringIndex];

        switch (zone) {
            // Ring 0: Velocity (outer half) + Gate (inner half)
            case SubZone::kVelocity:
                innerR = ring.splitRadius;
                outerR = ring.outerRadius;
                break;
            case SubZone::kGate:
                innerR = ring.innerRadius;
                outerR = ring.splitRadius;
                break;

            // Ring 1: Pitch (full ring)
            case SubZone::kPitch:
                innerR = ring.innerRadius;
                outerR = ring.outerRadius;
                break;

            // Ring 2: Modifier (outer half) + Condition (inner half)
            case SubZone::kModifier:
                innerR = ring.splitRadius;
                outerR = ring.outerRadius;
                break;
            case SubZone::kCondition:
                innerR = ring.innerRadius;
                outerR = ring.splitRadius;
                break;

            // Ring 3: Ratchet (outer) + Chord (middle) + Inversion (inner)
            case SubZone::kRatchet:
                innerR = ring.splitRadius;   // 113
                outerR = ring.outerRadius;   // 130
                break;
            case SubZone::kChord:
                innerR = ring.split2Radius;  // 97
                outerR = ring.splitRadius;   // 113
                break;
            case SubZone::kInversion:
                innerR = ring.innerRadius;   // 80
                outerR = ring.split2Radius;  // 97
                break;

            default:
                innerR = ring.innerRadius;
                outerR = ring.outerRadius;
                break;
        }
    }

    // =========================================================================
    // Cartesian Helpers
    // =========================================================================

    /// Convert polar coordinates (relative to center) to cartesian.
    void polarToCartesian(float angle, float radius,
                          float& outX, float& outY) const
    {
        outX = centerX_ + radius * std::cos(angle);
        outY = centerY_ + radius * std::sin(angle);
    }

private:
    // =========================================================================
    // Private Hit Test Helpers
    // =========================================================================

    [[nodiscard]] HitResult hitTestRing(
        int ringIndex, float radius, float angle) const
    {
        const auto& ring = rings_[ringIndex];
        HitResult result;
        result.ringIndex = ringIndex;

        switch (ringIndex) {
            case 0: // Velocity + Gate
                if (ring.splitRadius > 0.0f && radius >= ring.splitRadius) {
                    result.subZone = SubZone::kVelocity;
                    result.stepIndex = angleToStep(
                        angle, laneStepCounts_[0]); // velocity lane
                } else {
                    result.subZone = SubZone::kGate;
                    result.stepIndex = angleToStep(
                        angle, laneStepCounts_[1]); // gate lane
                }
                break;

            case 1: // Pitch
                result.subZone = SubZone::kPitch;
                result.stepIndex = angleToStep(angle, laneStepCounts_[2]);
                break;

            case 2: // Modifier + Condition
                if (ring.splitRadius > 0.0f && radius >= ring.splitRadius) {
                    result.subZone = SubZone::kModifier;
                    result.stepIndex = angleToStep(
                        angle, laneStepCounts_[3]);
                } else {
                    result.subZone = SubZone::kCondition;
                    result.stepIndex = angleToStep(
                        angle, laneStepCounts_[4]);
                }
                break;

            case 3: // Ratchet + Chord + Inversion (3-way)
                if (ring.splitRadius > 0.0f && radius >= ring.splitRadius) {
                    result.subZone = SubZone::kRatchet;
                    result.stepIndex = angleToStep(
                        angle, laneStepCounts_[5]);
                } else if (ring.split2Radius > 0.0f
                           && radius >= ring.split2Radius) {
                    result.subZone = SubZone::kChord;
                    result.stepIndex = angleToStep(
                        angle, laneStepCounts_[6]);
                } else {
                    result.subZone = SubZone::kInversion;
                    result.stepIndex = angleToStep(
                        angle, laneStepCounts_[7]);
                }
                break;

            default:
                break;
        }

        return result;
    }

    // =========================================================================
    // Members
    // =========================================================================

    float centerX_ = 330.0f;
    float centerY_ = 330.0f;
    float centerRadius_ = 75.0f;

    std::array<RingConfig, kRingCount> rings_{};
    std::array<int, 8> laneStepCounts_{};  // per-lane step counts for hit testing
};

} // namespace Gradus
