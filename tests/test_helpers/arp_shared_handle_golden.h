// ==============================================================================
// arp_shared_handle_golden.h — cross-plugin identity check for the shared
// arpeggiator parameter HANDLER (Gradus + Ruinae), audit R-098.
// ==============================================================================
// Both plugins denormalize the shared arp IDs (3000-3387) in handleArpParamChange.
// This helper starts from the deterministic field set of arp_shared_prefix_golden.h,
// sends every ID in the shared span a fixed sweep of normalized values through the
// plugin's own handleArpParamChange, and folds the 50 shared fields (plus
// diceTrigger) into an FNV-1a fingerprint after every call. Each plugin's test
// asserts the fingerprint equals kSharedArpHandleGoldenFnv, so a plugin that
// denormalizes a shared ID differently fails its own test.
//
// The caller passes the IDs to leave out: the deliberate per-plugin differences
// in the shared span, which are product decisions, not drift:
//   kArpModeId (3001):         Ruinae lists 10 modes, Gradus 12.
//   kArpRatchetSwingId (3293): Ruinae's Ratchet Swing is continuous 50-75 %,
//                              Gradus's Ratchet Shuffle snaps to 3 positions.
//
// Float fields are folded in as llround(value * 4096): the fingerprint pins the
// denormalized values to 1/4096, never their float bits, so it holds on every
// toolchain (tools/lint-float-bit-goldens.js).
// ==============================================================================
#pragma once

#include "test_helpers/arp_shared_prefix_golden.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <type_traits>

namespace Krate::Test {

inline void foldSharedArpValue(uint32_t& h, int64_t v) {
    const auto u = static_cast<uint64_t>(v);
    for (int b = 0; b < 8; ++b) {
        h ^= static_cast<uint8_t>((u >> (8 * b)) & 0xFFu);
        h *= 16777619u;
    }
}

inline void foldSharedArpValue(uint32_t& h, float v) {
    foldSharedArpValue(h, static_cast<int64_t>(std::llround(static_cast<double>(v) * 4096.0)));
}

template <class T>
inline void foldSharedArpAtomic(uint32_t& h, const std::atomic<T>& a) {
    const T v = a.load(std::memory_order_relaxed);
    if constexpr (std::is_same_v<T, float>) {
        foldSharedArpValue(h, v);
    } else {
        foldSharedArpValue(h, static_cast<int64_t>(v));
    }
}

template <class T, std::size_t N>
inline void foldSharedArpArray(uint32_t& h, const std::array<std::atomic<T>, N>& arr) {
    for (const auto& a : arr) foldSharedArpAtomic(h, a);
}

// The 50 fields of the shared save prefix plus diceTrigger (the one shared-span
// atomic the prefix does not carry).
template <class P>
inline void foldSharedArpFields(uint32_t& h, const P& p) {
    foldSharedArpAtomic(h, p.operatingMode);
    foldSharedArpAtomic(h, p.mode);
    foldSharedArpAtomic(h, p.octaveRange);
    foldSharedArpAtomic(h, p.octaveMode);
    foldSharedArpAtomic(h, p.tempoSync);
    foldSharedArpAtomic(h, p.noteValue);
    foldSharedArpAtomic(h, p.freeRate);
    foldSharedArpAtomic(h, p.gateLength);
    foldSharedArpAtomic(h, p.swing);
    foldSharedArpAtomic(h, p.latchMode);
    foldSharedArpAtomic(h, p.retrigger);
    foldSharedArpAtomic(h, p.velocityLaneLength);
    foldSharedArpArray(h, p.velocityLaneSteps);
    foldSharedArpAtomic(h, p.gateLaneLength);
    foldSharedArpArray(h, p.gateLaneSteps);
    foldSharedArpAtomic(h, p.pitchLaneLength);
    foldSharedArpArray(h, p.pitchLaneSteps);
    foldSharedArpAtomic(h, p.modifierLaneLength);
    foldSharedArpArray(h, p.modifierLaneSteps);
    foldSharedArpAtomic(h, p.accentVelocity);
    foldSharedArpAtomic(h, p.slideTime);
    foldSharedArpAtomic(h, p.ratchetLaneLength);
    foldSharedArpArray(h, p.ratchetLaneSteps);
    foldSharedArpAtomic(h, p.euclideanEnabled);
    foldSharedArpAtomic(h, p.euclideanHits);
    foldSharedArpAtomic(h, p.euclideanSteps);
    foldSharedArpAtomic(h, p.euclideanRotation);
    foldSharedArpAtomic(h, p.conditionLaneLength);
    foldSharedArpArray(h, p.conditionLaneSteps);
    foldSharedArpAtomic(h, p.fillToggle);
    foldSharedArpAtomic(h, p.spice);
    foldSharedArpAtomic(h, p.diceTrigger);
    foldSharedArpAtomic(h, p.humanize);
    foldSharedArpAtomic(h, p.ratchetSwing);
    foldSharedArpAtomic(h, p.scaleType);
    foldSharedArpAtomic(h, p.rootNote);
    foldSharedArpAtomic(h, p.scaleQuantizeInput);
    foldSharedArpAtomic(h, p.midiOut);
    foldSharedArpAtomic(h, p.chordLaneLength);
    foldSharedArpArray(h, p.chordLaneSteps);
    foldSharedArpAtomic(h, p.inversionLaneLength);
    foldSharedArpArray(h, p.inversionLaneSteps);
    foldSharedArpAtomic(h, p.voicingMode);
    foldSharedArpAtomic(h, p.velocityLaneSpeed);
    foldSharedArpAtomic(h, p.gateLaneSpeed);
    foldSharedArpAtomic(h, p.pitchLaneSpeed);
    foldSharedArpAtomic(h, p.modifierLaneSpeed);
    foldSharedArpAtomic(h, p.ratchetLaneSpeed);
    foldSharedArpAtomic(h, p.conditionLaneSpeed);
    foldSharedArpAtomic(h, p.chordLaneSpeed);
    foldSharedArpAtomic(h, p.inversionLaneSpeed);
}

// Normalized values sent to every shared ID: exact binary fractions, so each
// plugin's double arithmetic lands on the same value with or without FMA.
inline constexpr std::array<double, 7> kSharedArpHandleSweep = {
    0.0, 0.125, 0.375, 0.5, 0.625, 0.875, 1.0};

// Fingerprint of handle(params, id, value) over IDs [firstId, lastId] minus
// `excluded`, every sweep value, from the deterministic starting state.
template <class P, class Handle>
inline uint32_t sharedArpHandleFingerprint(Handle handle, uint32_t firstId, uint32_t lastId,
                                           std::initializer_list<uint32_t> excluded) {
    auto p = std::make_unique<P>();
    setSharedArpFieldsDeterministic(*p);
    uint32_t h = 2166136261u;
    for (uint32_t id = firstId; id <= lastId; ++id) {
        bool skip = false;
        for (const uint32_t x : excluded) skip = skip || (x == id);
        if (skip) continue;
        for (const double v : kSharedArpHandleSweep) {
            handle(*p, id, v);
            foldSharedArpFields(h, *p);
        }
    }
    return h;
}

// Pinned 2026-11-22 from the two plugins' separate handler copies, before they
// were merged into Krate::Shared::handleArpParamChangeShared: both read 0x98178296.
constexpr uint32_t kSharedArpHandleGoldenFnv = 0x98178296u;

}  // namespace Krate::Test
