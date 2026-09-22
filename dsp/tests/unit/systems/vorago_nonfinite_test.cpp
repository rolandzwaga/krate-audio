// ==============================================================================
// Layer 3: System Tests - the Vorago non-finite guard ladder
//                                    (specs/vorago-phase10-voice-engine)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase10-voice-engine/spec.md
//            specs/vorago-phase10-voice-engine/plan.md
//            specs/vorago-phase10-voice-engine/tasks.md  (T006 creates and wires
//                                                         this TU; T020 fills it)
//
// SCOPE OF THIS TU: SC-029 only, plus the definition of the injection probe.
//
// THE PROBE IS DEFINED HERE, NOT IN THE HEADER (ruling B-4 / Q-C). It is a
//   `detail` friend struct that vorago_voice.h / vorago_engine.h declare and
//   this TU DEFINES, so there is no KRATE_DSP_VORAGO_TEST_HOOKS macro and NO
//   target_compile_definitions line anywhere in dsp/tests/CMakeLists.txt. A
//   shipped header must not grow a test-only compilation mode.
//
// ODR NOTE. Krate::DSP::detail::VoragoEngineNonFiniteProbe is defined in THIS
//   TU AND NOWHERE ELSE - unlike the Seraphis twin, which is duplicated across
//   two TUs and therefore has to be kept byte-identical
//   (seraphis_nonfinite_test.cpp:60-67). If a second Phase 10 TU ever needs the
//   probe, either the two definitions become the same token sequence or one of
//   them goes.
//
// ALLOCATION DETECTION: this TU includes neither <allocation_detector.h> nor
//   <allocation_operator_overrides.h>. The single owner of the global
//   operator new/delete replacements in dsp_systems_tests is
//   unit/systems/selectable_oscillator_test.cpp:388; a second include of
//   <allocation_operator_overrides.h> is a duplicate-symbol link error.
//
// CONSTRUCTING NON-FINITE VALUES: never std::numeric_limits<float>::quiet_NaN()
//   or infinity(), and never std::isnan / std::isinf / std::isfinite. Build the
//   values from bit patterns through a VOLATILE sink - this TU compiles under
//   -ffast-math like every other.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_voice.h>

// The shared Phase 10 fixtures (T016): makeEngine(), applyFastAttack(),
// renderEngine(). No case re-implements them.
#include <vorago_fixtures.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

// -----------------------------------------------------------------------------
// FR-072's fault-injection probe (B-4 / Q-C).
//
// It stands in for a detection that has already happened, in the only place the
// engine can observe one: FR-072's scan runs at the ACCUMULATION POINT
// (vorago_engine.h:845-857), over what a voice actually served, so the fault has
// to arrive through the voice's carry FIFO rather than through a flag. Writing
// the bit pattern into carryL_/carryR_ and marking the carry servable
// (vorago_voice.h:1992-1996) makes the next slice the engine reads out of that
// voice genuinely non-finite, and the ENGINE'S OWN SCAN is what discovers it -
// which is the difference between exercising FR-072 and asserting a bookkeeping
// bit into place.
//
// No public call sequence can produce this state: every VoragoVoice setter
// sanitises through `detail::isFinite(x) ? x : default`, so the guard is
// unreachable from outside. That is a property of the composed components, not
// an oversight - and it is precisely why SC-029 exists at all.
// -----------------------------------------------------------------------------
namespace Krate::DSP::detail {
struct VoragoEngineNonFiniteProbe {
    /// @brief Write `bits` into voice `v`'s carry FIFO and mark it servable, so
    ///        the ENGINE's accumulation-point scan is what discovers it.
    ///
    /// `bits` is a bit pattern (0x7FC00000 qNaN / 0x7F800000 +Inf /
    /// 0xFF800000 -Inf), NEVER a std::numeric_limits value, which -ffast-math
    /// folds. The `volatile` read is the sink that stops the constant being
    /// folded back into the memcpy at compile time.
    ///
    /// The WHOLE carry is filled, both channels: the scan breaks out of the
    /// per-sample accumulation on the first non-finite sample it meets, so a
    /// single poisoned sample would leave the rest of the chunk's real audio
    /// silently unused and make the injection depend on where in the chunk it
    /// happened to land.
    static void poisonVoiceCarry(VoragoEngine& e, std::size_t v, std::uint32_t bits) noexcept {
        if (v >= VoragoEngine::kMaxVoices) {
            return;
        }
        volatile std::uint32_t sink = bits;
        const std::uint32_t materialized = sink;
        float poison = 0.0f;
        std::memcpy(&poison, &materialized, sizeof(poison));

        VoragoVoice& voice = e.voices_[v];
        voice.carryL_.fill(poison);
        voice.carryR_.fill(poison);
        voice.carryAvail_ = VoragoVoice::kControlChunkSamples;
        voice.carryRead_ = 0;
        // NOT life-only: this stands in for RENDERED material, and that flag is
        // what noteOn() reads to decide whether a pending carry may be dropped
        // (vorago_voice.h:868-874).
        voice.carryIsLifeOnly_ = false;
    }

    /// FR-072's deferred-reset bitmask (vorago_engine.h:1441), so the raise and
    /// the service can be observed as two separate control chunks rather than
    /// inferred from the counter alone.
    [[nodiscard]] static std::uint32_t pendingMask(const VoragoEngine& e) noexcept {
        return e.nonFinitePending_;
    }

    /// @brief The BIT PATTERNS of voice `v`'s carry FIFO - L then R.
    ///
    /// SC-029's "the other voices' contribution is bit-unchanged" clause needs
    /// the voices' AUDIO, and the mixed output cannot supply it: by the time a
    /// sample leaves processStereoBlock it has been through the sum gain, the
    /// atmosphere return, the subharmonic and the smear, and no subtraction
    /// recovers one voice's share of it. The carry FIFO IS that share, before
    /// any of them - so this is the one place the clause can be asserted at all.
    ///
    /// Read at a 64-sample boundary, carryL_/carryR_ hold exactly the chunk the
    /// voice rendered last (vorago_voice.h:1902-1927). Returned as bit patterns
    /// rather than floats so the comparison is bit-exact by construction and
    /// stays correct under -ffast-math.
    [[nodiscard]] static std::array<std::uint32_t, 2u * VoragoVoice::kControlChunkSamples>
    carryBits(const VoragoEngine& e, std::size_t v) noexcept {
        std::array<std::uint32_t, 2u * VoragoVoice::kControlChunkSamples> out{};
        if (v >= VoragoEngine::kMaxVoices) {
            return out;
        }
        const VoragoVoice& voice = e.voices_[v];
        for (std::size_t s = 0; s < VoragoVoice::kControlChunkSamples; ++s) {
            std::memcpy(&out[s], &voice.carryL_[s], sizeof(std::uint32_t));
            std::memcpy(&out[VoragoVoice::kControlChunkSamples + s], &voice.carryR_[s],
                        sizeof(std::uint32_t));
        }
        return out;
    }
};
}  // namespace Krate::DSP::detail

namespace {

using Krate::DSP::VoiceState;
using Krate::DSP::VoragoEngine;
using Krate::DSP::VoragoEngineConfig;
using Krate::DSP::VoragoVoice;
using Probe = Krate::DSP::detail::VoragoEngineNonFiniteProbe;

using Krate::DSP::TestUtils::Vorago::applyFastAttack;
using Krate::DSP::TestUtils::Vorago::makeEngine;
using Krate::DSP::TestUtils::Vorago::renderEngine;

// =============================================================================
// Constants
// =============================================================================

constexpr double kSampleRate48 = 48000.0;

/// The absolute control grid (vorago_engine.h:172). Every render below is a
/// whole number of these, so `sampleCounter_ % 64 == 0` at every checkpoint and
/// each voice's carry holds exactly the chunk it rendered last.
constexpr std::size_t kChunk = VoragoEngine::kControlChunkSamples;

/// The warm-up partition: 8 x 64, a whole number of control chunks.
constexpr std::size_t kBlock = 512;

/// SC-029 asks for `k >= 2` voices sounding AND an injection into EVERY voice
/// index, so the pool is opened to the full ceiling and every slot is given a
/// note - a slot that is not rendering is never read at the accumulation point,
/// so an injection into it could never be discovered.
constexpr std::size_t kVoices = VoragoEngine::kMaxVoices;
constexpr int kBaseNote = 36;
constexpr std::uint8_t kVel = 100u;

/// 0.256 s. Long enough that the kFastAttackEnvelopeConfig walk (50 ms stages)
/// has every voice well past its onset, so the reference levels the comparison
/// stands on are non-zero - asserted below, not assumed. Short enough that the
/// twenty-five engines this case renders stay inside the TU's seconds-scale
/// budget.
constexpr std::size_t kWarmupSamples = 12288;

/// FR-014a's fast attack reaches level 1.0 in 50 ms, so a re-gated voice is
/// emitting inside one chunk. 64 chunks (4096 samples, 85 ms) is the STATED
/// bound SC-029's "non-silent within a stated number of chunks" asks for; the
/// loop exits as soon as the voice sounds, so the slack costs nothing.
constexpr std::size_t kResumeChunkLimit = 64;

/// IEEE-754 binary32 exponent field, all ones == Inf or NaN.
constexpr std::uint32_t kExponentMask = 0x7F800000u;

struct Pattern {
    const char* name;
    std::uint32_t bits;
};

/// The three patterns SC-029 enumerates. A NaN-only sweep would not exercise the
/// Inf arms and vice versa: `detail::isFinite` is one exponent-field test, but
/// the arithmetic AROUND it is not - the two infinities survive a multiply as
/// infinities, while a NaN poisons every partial sum it reaches.
constexpr std::array<Pattern, 3> kPatterns{
    {{"quiet NaN", 0x7FC00000u}, {"+Inf", 0x7F800000u}, {"-Inf", 0xFF800000u}}};

// =============================================================================
// Helpers
// =============================================================================

/// @brief FR-008's finiteness test in its memcpy-bit form.
///
/// Never std::isnan / std::isinf / std::isfinite: this TU ships under the same
/// -ffast-math as the rest of the suite, which licenses the compiler to fold
/// those away and delete the assertion silently. Integer arithmetic on the bit
/// pattern reads correctly under any fast-math setting.
[[nodiscard]] bool sampleIsFinite(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & kExponentMask) != kExponentMask;
}

/// Accumulated rather than REQUIREd per sample: the case renders millions of
/// samples and one assertion each would be a different test's runtime.
[[nodiscard]] bool buffersAllFinite(const std::vector<float>& l, const std::vector<float>& r) {
    for (std::size_t i = 0; i < l.size(); ++i) {
        if (!sampleIsFinite(l[i]) || !sampleIsFinite(r[i])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint32_t bitsOf(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/// @brief Bit-exact equality of two renders, as BIT PATTERNS.
///
/// Two VoragoEngines built the same way, in the same process, from the same seed
/// and driven by the same note schedule ARE the same render - nothing in the
/// chain is address-dependent, and every SIMD path in KrateDSP loads unaligned.
/// That premise is what the per-voice comparison below stands on, so it is
/// asserted directly rather than assumed: a failure here says "the two arms
/// diverged before the injection", which is a different (and much more
/// interesting) finding than a carry mismatch after it.
[[nodiscard]] bool rendersAreBitIdentical(const std::vector<float>& al,
                                          const std::vector<float>& ar,
                                          const std::vector<float>& bl,
                                          const std::vector<float>& br) {
    if (al.size() != bl.size() || ar.size() != br.size()) {
        return false;
    }
    for (std::size_t i = 0; i < al.size(); ++i) {
        if (bitsOf(al[i]) != bitsOf(bl[i]) || bitsOf(ar[i]) != bitsOf(br[i])) {
            return false;
        }
    }
    return true;
}

/// `note` as a MIDI byte, explicitly, so no int -> uint8_t narrowing is left to
/// the compiler's discretion.
[[nodiscard]] constexpr std::uint8_t midi(int note) noexcept {
    return static_cast<std::uint8_t>(note);
}

/// @brief Everything about one slot that a wrong-voice reset would move.
///
/// `carry` is the voice's own last rendered chunk - the "contribution" clause.
/// The five scalars are run state VoragoVoice::clearRunState() zeroes
/// (vorago_voice.h:1341-1348), so a `resetForRecovery()` landing on the wrong
/// slot shows up in every one of them; they are compared as BIT PATTERNS for the
/// same reason the carry is.
struct VoiceSnapshot {
    std::array<std::uint32_t, 2u * VoragoVoice::kControlChunkSamples> carry{};
    std::uint32_t level = 0;
    std::uint32_t envOutput = 0;
    std::uint32_t ghostRequest = 0;
    std::uint32_t tidalFogDepth = 0;
    std::uint32_t breathGravityLane = 0;
    std::uint64_t serial = 0;
    VoiceState state = VoiceState::Idle;
};

[[nodiscard]] VoiceSnapshot snapshotVoice(const VoragoEngine& engine, std::size_t v) {
    const VoragoVoice& voice = engine.getVoice(v);
    VoiceSnapshot s{};
    s.carry = Probe::carryBits(engine, v);
    s.level = bitsOf(voice.getCurrentLevel());
    s.envOutput = bitsOf(voice.getEnvelopeOutput());
    s.ghostRequest = bitsOf(voice.getGhostRequest());
    s.tidalFogDepth = bitsOf(voice.getTidalFogDepth());
    s.breathGravityLane = bitsOf(voice.getBreathingGravityLane());
    s.serial = engine.getVoiceAllocationSerial(v);
    s.state = engine.getVoiceState(v);
    return s;
}

/// Compared through a bool rather than `REQUIRE(a.carry == b.carry)` so a
/// failure prints the case's own INFO instead of 128 stringified words.
[[nodiscard]] bool carryIsBitIdentical(const VoiceSnapshot& a, const VoiceSnapshot& b) {
    return a.carry == b.carry;
}

/// One engine plus the note it handed each slot.
struct Rig {
    std::unique_ptr<VoragoEngine> engine;
    std::array<std::uint8_t, kVoices> slotNote{};
};

/// @brief A prepared, warmed engine with every slot sounding.
///
/// THE NOTE -> SLOT MAP IS DISCOVERED, NOT ASSUMED. Placement is the allocator's
/// own business (voice_allocator.h:228-248), and SC-029's resume clause has to
/// re-gate the slot it poisoned, so each note is dispatched on its own and the
/// one slot that just left Idle is recorded.
[[nodiscard]] Rig makeWarmRig(std::vector<float>& l, std::vector<float>& r) {
    Rig rig;
    rig.engine = makeEngine(kSampleRate48, VoragoEngineConfig{});
    applyFastAttack(*rig.engine);
    rig.engine->setPolyphony(kVoices);

    for (std::size_t k = 0; k < kVoices; ++k) {
        const std::uint8_t note = midi(kBaseNote + static_cast<int>(k));
        rig.engine->noteOn(note, kVel);
        for (std::size_t v = 0; v < kVoices; ++v) {
            if (rig.engine->getVoiceState(v) != VoiceState::Idle && rig.slotNote[v] == 0u) {
                rig.slotNote[v] = note;
                break;
            }
        }
    }
    renderEngine(*rig.engine, l, r, kWarmupSamples, kBlock);
    return rig;
}

}  // namespace

// =============================================================================
// SC-029 - Non-finite containment actually runs (FR-072)
// =============================================================================
//
// UNTAGGED, DELIBERATELY. This is a NaN/Inf-guard case, i.e. one of the
// cross-platform sentinels FR-085 keeps in the per-push lane; CLAUDE.md names
// exactly this class of test as one that may never be tagged [long].
//
// WHY IT EXISTS (R-11). Every other criterion that mentions
// getNonFiniteRecoveryCount() asserts it is ZERO - so a containment branch that
// is dead, mis-indexed, or resets the wrong voice passes the entire suite. This
// case injects the fault on purpose, for EVERY voice index and EVERY bit
// pattern, and pins which slot was cleared.
TEST_CASE("VoragoEngine_NonFiniteContainment", "[systems][vorago]") {
    // --- the reference render: identical schedule, no injection ---------------
    std::vector<float> refL;
    std::vector<float> refR;
    Rig ref = makeWarmRig(refL, refR);
    REQUIRE(buffersAllFinite(refL, refR));
    REQUIRE(ref.engine->getRenderingVoiceCount() == kVoices);

    std::vector<float> refChunkL;
    std::vector<float> refChunkR;
    renderEngine(*ref.engine, refChunkL, refChunkR, 2u * kChunk, kChunk);
    REQUIRE(buffersAllFinite(refChunkL, refChunkR));
    REQUIRE(ref.engine->getNonFiniteRecoveryCount() == 0u);
    REQUIRE(Probe::pendingMask(*ref.engine) == 0u);

    std::array<VoiceSnapshot, kVoices> refSnap{};
    for (std::size_t v = 0; v < kVoices; ++v) {
        INFO("reference slot " << v);
        REQUIRE(ref.slotNote[v] != 0u);
        // TEETH: without this the "bit-unchanged" comparison below could be
        // comparing two slots' worth of zeros and pass on a dead engine.
        REQUIRE(ref.engine->getVoiceLevel(v) > 0.0f);
        refSnap[v] = snapshotVoice(*ref.engine, v);
    }

    // --- the injections: every bit pattern x every voice index ---------------
    for (const Pattern& pattern : kPatterns) {
        for (std::size_t i = 0; i < kVoices; ++i) {
            INFO("pattern " << pattern.name << ", poisoned voice index " << i);

            std::vector<float> warmL;
            std::vector<float> warmR;
            Rig inj = makeWarmRig(warmL, warmR);
            REQUIRE(buffersAllFinite(warmL, warmR));
            // Lockstep with the reference up to the injection point: the whole
            // comparison rests on the two engines being the same render.
            REQUIRE(rendersAreBitIdentical(warmL, warmR, refL, refR));
            for (std::size_t v = 0; v < kVoices; ++v) {
                REQUIRE(static_cast<int>(inj.slotNote[v]) == static_cast<int>(ref.slotNote[v]));
            }

            Probe::poisonVoiceCarry(*inj.engine, i, pattern.bits);
            // The engine has not looked yet - the raise below is ITS scan, not
            // the probe's bookkeeping.
            REQUIRE(Probe::pendingMask(*inj.engine) == 0u);

            const std::uint32_t expectedBit = static_cast<std::uint32_t>(1u)
                                              << static_cast<std::uint32_t>(i);

            // Chunk 1: the accumulation-point scan meets the poison, excludes
            // the voice from the sum and RAISES its bit. The reset is deferred.
            std::vector<float> c1L;
            std::vector<float> c1R;
            renderEngine(*inj.engine, c1L, c1R, kChunk, kChunk);
            REQUIRE(buffersAllFinite(c1L, c1R));
            REQUIRE(Probe::pendingMask(*inj.engine) == expectedBit);
            REQUIRE(inj.engine->getNonFiniteRecoveryCount() == 0u);

            // Chunk 2: the pre-render control step services exactly one slot.
            std::vector<float> c2L;
            std::vector<float> c2R;
            renderEngine(*inj.engine, c2L, c2R, kChunk, kChunk);
            REQUIRE(buffersAllFinite(c2L, c2R));
            REQUIRE(Probe::pendingMask(*inj.engine) == 0u);
            REQUIRE(inj.engine->getNonFiniteRecoveryCount() == 1u);

            // THE CLAUSE THAT CATCHES A WRONG-VOICE RESET: every other slot is
            // bit-identical to the reference, audio included.
            for (std::size_t j = 0; j < kVoices; ++j) {
                if (j == i) {
                    continue;
                }
                INFO("untouched slot " << j);
                const VoiceSnapshot s = snapshotVoice(*inj.engine, j);
                REQUIRE(carryIsBitIdentical(s, refSnap[j]));
                REQUIRE(s.level == refSnap[j].level);
                REQUIRE(s.envOutput == refSnap[j].envOutput);
                REQUIRE(s.ghostRequest == refSnap[j].ghostRequest);
                REQUIRE(s.tidalFogDepth == refSnap[j].tidalFogDepth);
                REQUIRE(s.breathGravityLane == refSnap[j].breathGravityLane);
                REQUIRE(s.serial == refSnap[j].serial);
                REQUIRE(s.state == refSnap[j].state);
            }

            // ...and the poisoned slot IS the one that was cleared. Stated as a
            // comparison against the reference rather than against a literal
            // floor, so it cannot be satisfied by a voice that was merely quiet.
            REQUIRE(inj.engine->getVoiceLevel(i) < ref.engine->getVoiceLevel(i));

            // RESUMES RENDERING. resetForRecovery() clears run state but leaves
            // the allocator untouched (vorago_engine.h:1145-1160), so the slot is
            // still Active and still takes the full audio path - it is the
            // ENVELOPE that was rewound, so a re-gate is what makes it sound
            // again. The same-note retrigger lands on the same slot by
            // construction (voice_allocator.h:238-243) and is NOT a steal.
            REQUIRE(inj.engine->getVoiceState(i) != VoiceState::Idle);
            REQUIRE(inj.engine->getRenderingVoiceCount() == kVoices);

            inj.engine->noteOn(inj.slotNote[i], kVel);
            std::size_t chunksToSound = kResumeChunkLimit + 1u;
            std::vector<float> resumeL;
            std::vector<float> resumeR;
            for (std::size_t c = 0; c < kResumeChunkLimit; ++c) {
                renderEngine(*inj.engine, resumeL, resumeR, kChunk, kChunk);
                REQUIRE(buffersAllFinite(resumeL, resumeR));
                if (inj.engine->getVoiceLevel(i) > 0.0f) {
                    chunksToSound = c + 1u;
                    break;
                }
            }
            INFO("chunks until the recovered voice sounded again: " << chunksToSound);
            REQUIRE(chunksToSound <= kResumeChunkLimit);

            // EXACTLY ONE recovery for the whole injection, counting the render
            // that followed it: a second increment would mean the cleared voice
            // went non-finite again, or that the service ran twice.
            REQUIRE(inj.engine->getNonFiniteRecoveryCount() == 1u);
        }
    }

    // The reference never saw a fault, and nothing above reached across to it.
    REQUIRE(ref.engine->getNonFiniteRecoveryCount() == 0u);
}
