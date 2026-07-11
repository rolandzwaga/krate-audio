// ==============================================================================
// arp_shared_prefix_golden.h — cross-plugin byte-identity check for the shared
// arpeggiator save prefix (Gradus + Ruinae), Wave 3 D3.
// ==============================================================================
// Both plugins serialize the shared arp param range (IDs 3000-3372) through
// Krate::Shared::saveArpParamsShared. This helper sets those shared fields to a
// fixed, deterministic configuration and FNV-1a-hashes the resulting 200-byte
// prefix. Each plugin's test asserts the hash equals kSharedArpPrefixGoldenFnv, so
// if the two plugins ever diverge in the shared save format (field order, count, or
// type) — or a refactor changes it — a test fails in the plugin that drifted.
//
// 200 bytes = 50 shared fields x 4 bytes (all writeInt32 / writeFloat).
// ==============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate::Test {

inline uint32_t fnv1a(const uint8_t* data, std::size_t n) {
    uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

constexpr std::size_t kSharedArpPrefixBytes = 200;  // 50 fields x 4 bytes

// Set the 50 shared fields to a fixed configuration (templated on either plugin's
// ArpeggiatorParams). Values are arbitrary but distinct so the hash is sensitive to
// field order/type. Plugin-specific fields are left untouched.
template <class P>
inline void setSharedArpFieldsDeterministic(P& p) {
    using std::memory_order_relaxed;
    p.operatingMode.store(1, memory_order_relaxed);
    p.mode.store(3, memory_order_relaxed);
    p.octaveRange.store(2, memory_order_relaxed);
    p.octaveMode.store(1, memory_order_relaxed);
    p.tempoSync.store(false, memory_order_relaxed);
    p.noteValue.store(14, memory_order_relaxed);
    p.freeRate.store(12.5f, memory_order_relaxed);
    p.gateLength.store(60.0f, memory_order_relaxed);
    p.swing.store(25.0f, memory_order_relaxed);
    p.latchMode.store(1, memory_order_relaxed);
    p.retrigger.store(2, memory_order_relaxed);

    p.velocityLaneLength.store(16, memory_order_relaxed);
    for (int i = 0; i < 32; ++i) p.velocityLaneSteps[i].store(0.25f + 0.01f * i, memory_order_relaxed);
    p.gateLaneLength.store(12, memory_order_relaxed);
    for (int i = 0; i < 32; ++i) p.gateLaneSteps[i].store(0.5f + 0.02f * i, memory_order_relaxed);
    p.pitchLaneLength.store(8, memory_order_relaxed);
    for (int i = 0; i < 32; ++i) p.pitchLaneSteps[i].store((i % 25) - 12, memory_order_relaxed);
    p.modifierLaneLength.store(20, memory_order_relaxed);
    for (int i = 0; i < 32; ++i) p.modifierLaneSteps[i].store(i % 16, memory_order_relaxed);
    p.accentVelocity.store(90, memory_order_relaxed);
    p.slideTime.store(40.0f, memory_order_relaxed);
    p.ratchetLaneLength.store(10, memory_order_relaxed);
    for (int i = 0; i < 32; ++i) p.ratchetLaneSteps[i].store(1 + (i % 4), memory_order_relaxed);

    p.euclideanEnabled.store(true, memory_order_relaxed);
    p.euclideanHits.store(5, memory_order_relaxed);
    p.euclideanSteps.store(16, memory_order_relaxed);
    p.euclideanRotation.store(3, memory_order_relaxed);

    p.conditionLaneLength.store(14, memory_order_relaxed);
    for (int i = 0; i < 32; ++i) p.conditionLaneSteps[i].store(i % 18, memory_order_relaxed);
    p.fillToggle.store(true, memory_order_relaxed);

    p.spice.store(0.3f, memory_order_relaxed);
    p.humanize.store(0.4f, memory_order_relaxed);
    p.ratchetSwing.store(55.0f, memory_order_relaxed);

    p.scaleType.store(4, memory_order_relaxed);
    p.rootNote.store(2, memory_order_relaxed);
    p.scaleQuantizeInput.store(true, memory_order_relaxed);
    p.midiOut.store(true, memory_order_relaxed);

    p.chordLaneLength.store(6, memory_order_relaxed);
    for (int i = 0; i < 32; ++i) p.chordLaneSteps[i].store(i % 5, memory_order_relaxed);
    p.inversionLaneLength.store(7, memory_order_relaxed);
    for (int i = 0; i < 32; ++i) p.inversionLaneSteps[i].store(i % 4, memory_order_relaxed);
    p.voicingMode.store(2, memory_order_relaxed);

    p.velocityLaneSpeed.store(1.5f, memory_order_relaxed);
    p.gateLaneSpeed.store(2.0f, memory_order_relaxed);
    p.pitchLaneSpeed.store(0.5f, memory_order_relaxed);
    p.modifierLaneSpeed.store(1.25f, memory_order_relaxed);
    p.ratchetLaneSpeed.store(0.75f, memory_order_relaxed);
    p.conditionLaneSpeed.store(1.75f, memory_order_relaxed);
    p.chordLaneSpeed.store(0.25f, memory_order_relaxed);
    p.inversionLaneSpeed.store(3.0f, memory_order_relaxed);
}

// FNV-1a of the shared 200-byte prefix produced by the deterministic config above.
// IDENTICAL for both plugins by construction (they call the same shared writer).
// Pinned from the first run; a change here means the shared save format moved.
constexpr uint32_t kSharedArpPrefixGoldenFnv = 0x2df4909fu;

}  // namespace Krate::Test
