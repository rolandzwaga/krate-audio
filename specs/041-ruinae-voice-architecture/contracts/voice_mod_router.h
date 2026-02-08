// ==============================================================================
// API Contract: VoiceModRouter
// ==============================================================================
// Layer 3: System Component
// Location: dsp/include/krate/dsp/systems/voice_mod_router.h
//
// Lightweight per-voice modulation router with fixed-size storage.
// Computes modulated parameter offsets once per block.
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

enum class VoiceModSource : uint8_t;
enum class VoiceModDest : uint8_t;

struct VoiceModRoute {
    VoiceModSource source{};
    VoiceModDest destination{};
    float amount{0.0f};  // Bipolar: [-1.0, +1.0]
};

class VoiceModRouter {
public:
    static constexpr int kMaxRoutes = 16;

    VoiceModRouter() noexcept = default;

    // Route management
    void setRoute(int index, VoiceModRoute route) noexcept;
    void clearRoute(int index) noexcept;
    void clearAllRoutes() noexcept;
    [[nodiscard]] int getRouteCount() const noexcept;

    // Per-block modulation computation (FR-024)
    // Called once at start of processBlock with current source values.
    // All sources are in [0, 1] except LFO which is [-1, +1].
    void computeOffsets(float env1, float env2, float env3,
                        float lfo, float gate,
                        float velocity, float keyTrack) noexcept;

    // Get computed offset for a destination (FR-027)
    // Returns the summed modulation offset for the given destination.
    // For pitch/cutoff destinations: value is in semitones.
    // For normalized destinations: value is in linear space.
    [[nodiscard]] float getOffset(VoiceModDest dest) const noexcept;
};

} // namespace Krate::DSP
