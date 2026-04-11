// ==============================================================================
// VoicePool -- Phase 3 polyphonic voice pool (scaffolding stubs)
// ==============================================================================
// Phase 3.0 intentionally provides empty / trivial bodies. Real implementations
// land in Phases 3.1 (allocator integration) and 3.2 (fast-release ramp).
// ==============================================================================

#include "voice_pool.h"

namespace Membrum {

void VoicePool::prepare(double /*sampleRate*/, int /*maxBlockSize*/) noexcept
{
    // Phase 3.0 scaffolding stub -- Phase 3.1 implements full setup.
}

void VoicePool::noteOn(std::uint8_t /*midiNote*/, float /*velocity*/) noexcept
{
    // Phase 3.0 scaffolding stub -- Phase 3.1 implements note-on routing.
}

void VoicePool::noteOff(std::uint8_t /*midiNote*/) noexcept
{
    // Phase 3.0 scaffolding stub -- Phase 3.1 implements bookkeeping.
}

void VoicePool::processBlock(float* outL, float* outR, int numSamples) noexcept
{
    // Phase 3.0 scaffolding stub -- Phase 3.1/3.2 implements full mixing and
    // fast-release ramp. Zero-fill for now so callers see silence.
    if (outL != nullptr) {
        for (int i = 0; i < numSamples; ++i) {
            outL[i] = 0.0f;
        }
    }
    if (outR != nullptr) {
        for (int i = 0; i < numSamples; ++i) {
            outR[i] = 0.0f;
        }
    }
}

void VoicePool::setMaxPolyphony(int /*n*/) noexcept
{
    // Phase 3.0 scaffolding stub -- Phase 3.1 wires this to the allocator.
}

void VoicePool::setVoiceStealingPolicy(VoiceStealingPolicy /*p*/) noexcept
{
    // Phase 3.0 scaffolding stub -- Phase 3.1 stores and uses the policy.
}

void VoicePool::setChokeGroup(std::uint8_t /*group*/) noexcept
{
    // Phase 3.0 scaffolding stub -- Phase 3.3 wires this to the choke table.
}

void VoicePool::setSharedVoiceParams(float /*material*/,
                                     float /*size*/,
                                     float /*decay*/,
                                     float /*strikePos*/,
                                     float /*level*/) noexcept
{
    // Phase 3.0 scaffolding stub -- Phase 3.1 stores the Phase 1 snapshot.
}

std::array<std::uint8_t, ChokeGroupTable::kSize>
VoicePool::getChokeGroupAssignments() const noexcept
{
    // Phase 3.0 scaffolding stub -- returns the current table (all zeros
    // until Phase 3.3 wires up the setter).
    return chokeGroups_.raw();
}

void VoicePool::loadChokeGroupAssignments(
    const std::array<std::uint8_t, ChokeGroupTable::kSize>& in) noexcept
{
    // Phase 3.0 scaffolding stub -- delegates to the inline stub.
    chokeGroups_.loadFromRaw(in);
}

bool VoicePool::isAnyVoiceActive() const noexcept
{
    // Phase 3.0 scaffolding stub -- no voices exist yet.
    return false;
}

} // namespace Membrum
