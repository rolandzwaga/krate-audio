#pragma once

// ==============================================================================
// IRingDataSource / RingDataBridge — Read-Only Adapter over IArpLane
// ==============================================================================
// Lets the RingRenderer query lane data without coupling to IArpLane directly.
// RingDataBridge is the concrete adapter that reads from IArpLane pointers.
// ==============================================================================

#include "lane_constants.h"
#include "ui/arp_lane.h"

#include <array>
#include <cstdint>

namespace Gradus {

// ==============================================================================
// IRingDataSource — Abstract Interface for Ring Renderer
// ==============================================================================

class IRingDataSource {
public:
    virtual ~IRingDataSource() = default;

    /// Number of active steps for a lane (1-32).
    [[nodiscard]] virtual int getStepCount(int laneIndex) const = 0;

    /// Normalized step value (0.0-1.0) for a lane at a given step.
    [[nodiscard]] virtual float getNormalizedValue(
        int laneIndex, int step) const = 0;

    /// Current playhead step index (-1 = no playhead).
    [[nodiscard]] virtual int getPlayheadStep(int laneIndex) const = 0;

    /// Trail state for playhead rendering.
    [[nodiscard]] virtual const Krate::Plugins::PlayheadTrailState&
    getTrailState(int laneIndex) const = 0;

    /// Euclidean rhythm state.
    [[nodiscard]] virtual bool isEuclideanEnabled() const = 0;
    [[nodiscard]] virtual int getEuclideanHits() const = 0;
    [[nodiscard]] virtual int getEuclideanSteps() const = 0;
    [[nodiscard]] virtual int getEuclideanRotation() const = 0;
};

// ==============================================================================
// RingDataBridge — Concrete Adapter over IArpLane Pointers
// ==============================================================================

class RingDataBridge : public IRingDataSource {
public:
    RingDataBridge() = default;

    // =========================================================================
    // Lane Binding
    // =========================================================================

    void setLane(int laneIndex, Krate::Plugins::IArpLane* lane)
    {
        if (laneIndex >= 0 && laneIndex < kLaneCount)
            lanes_[laneIndex] = lane;
    }

    void clearLanes()
    {
        lanes_.fill(nullptr);
    }

    // =========================================================================
    // Euclidean State (cached from controller parameter sync)
    // =========================================================================

    void setEuclideanState(bool enabled, int hits, int steps, int rotation)
    {
        euclideanEnabled_ = enabled;
        euclideanHits_ = hits;
        euclideanSteps_ = steps;
        euclideanRotation_ = rotation;
    }

    // =========================================================================
    // Playhead Cache (written by controller on parameter update)
    // =========================================================================

    void setPlayheadStep(int laneIndex, int step)
    {
        if (laneIndex >= 0 && laneIndex < kLaneCount)
            playheadSteps_[laneIndex] = step;
    }

    void setTrailState(int laneIndex,
                       const Krate::Plugins::PlayheadTrailState& state)
    {
        if (laneIndex >= 0 && laneIndex < kLaneCount)
            trailStates_[laneIndex] = state;
    }

    // =========================================================================
    // IRingDataSource Implementation
    // =========================================================================

    [[nodiscard]] int getStepCount(int laneIndex) const override
    {
        if (auto* lane = getLane(laneIndex))
            return lane->getActiveLength();
        return 16;
    }

    [[nodiscard]] float getNormalizedValue(
        int laneIndex, int step) const override
    {
        if (auto* lane = getLane(laneIndex))
            return lane->getNormalizedStepValue(step);
        return 0.0f;
    }

    [[nodiscard]] int getPlayheadStep(int laneIndex) const override
    {
        if (laneIndex >= 0 && laneIndex < kLaneCount)
            return playheadSteps_[laneIndex];
        return -1;
    }

    [[nodiscard]] const Krate::Plugins::PlayheadTrailState&
    getTrailState(int laneIndex) const override
    {
        if (laneIndex >= 0 && laneIndex < kLaneCount)
            return trailStates_[laneIndex];
        static const Krate::Plugins::PlayheadTrailState empty{};
        return empty;
    }

    [[nodiscard]] bool isEuclideanEnabled() const override
    {
        return euclideanEnabled_;
    }

    [[nodiscard]] int getEuclideanHits() const override
    {
        return euclideanHits_;
    }

    [[nodiscard]] int getEuclideanSteps() const override
    {
        return euclideanSteps_;
    }

    [[nodiscard]] int getEuclideanRotation() const override
    {
        return euclideanRotation_;
    }

private:
    [[nodiscard]] Krate::Plugins::IArpLane* getLane(int index) const
    {
        if (index >= 0 && index < kLaneCount)
            return lanes_[index];
        return nullptr;
    }

    std::array<Krate::Plugins::IArpLane*, kLaneCount> lanes_{};
    std::array<int, kLaneCount> playheadSteps_{};
    std::array<Krate::Plugins::PlayheadTrailState, kLaneCount> trailStates_{};

    bool euclideanEnabled_ = false;
    int euclideanHits_ = 0;
    int euclideanSteps_ = 16;
    int euclideanRotation_ = 0;
};

} // namespace Gradus
