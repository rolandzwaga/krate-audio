// ==============================================================================
// Profundum - MIDI event translation tests (Phase 10 T015, SC-009, FR-025, FR-026)
// ==============================================================================
// Profundum_MidiEventTranslation. Every render comparison is an explicit
// mismatch count (FR-065), never a bit-exact golden: the processor and the
// reference arm run the same engine from the same script, so 0 mismatches is
// the bar (plan 3.3). Each equality carries a non-vacuity precondition; if a
// precondition misses, LENGTHEN the script - never move a threshold.
//
//   PartitionInvariance  host blocks {1, 7, 32, 65, 512, 4096} vs 512
//   EventOrder           an unsorted list renders as the sorted one (FR-025)
//   Overflow             spec SC-009 / plan 4.3: events past 1024 are read in list
//                        order at non-decreasing effective offsets (FR-025)
//   OffsetClamp          offsets clamped to [0, numSamples - 1] (FR-025)
//   Translation          FR-026: tiny velocity, velocity-0 note-off, bad pitches,
//                        non-note events
//   SliceCount           one slice per distinct event offset (plan 2.5.6)
//   PartitionInvarianceMorphMod_<Envelope|Cycle|Velocity|ModWheel>
//                        Phase 14 T016 (SC-012 (a); FR-021): the PartitionInvariance
//                        script with Morph Modulation at Amount 1, host blocks
//                        {1, 7, 32, 65, 4096} vs 512
// ==============================================================================

#include "profundum_test_fixture.h"

#include "parameters/morph_mod_params.h"
#include "plugin_ids.h"

#include <vst_event_list.h>

#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>

#include <pluginterfaces/vst/ivstevents.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

using Krate::DSP::ProfundumEngine;
using ScriptedEvent = ProcessorFixture::ScriptedEvent;

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr float kVelocity100 = 100.0f / 127.0f;
constexpr std::uint8_t kMidiVelocity100 = 100;  // quantiseVelocity(100/127) = uint8(100.5)
constexpr float kAudibleFloor = 1.0e-4f;
constexpr float kSilenceBound = 1.0e-6f;  // -120 dBFS (SC-009 (a))

struct Stereo {
    std::vector<float> l;
    std::vector<float> r;
};

[[nodiscard]] Stereo takeCapture(ProcessorFixture& fx) {
    return Stereo{.l = std::move(fx.capturedL), .r = std::move(fx.capturedR)};
}

[[nodiscard]] float stereoPeak(const Stereo& s, std::size_t from, std::size_t to) {
    const std::span<const float> l(s.l);
    const std::span<const float> r(s.r);
    REQUIRE(from <= to);
    REQUIRE(to <= l.size());
    REQUIRE(to <= r.size());
    return std::max(peakOf(l.subspan(from, to - from)), peakOf(r.subspan(from, to - from)));
}

// A fresh fixture prepared at 48 kHz (maxBlock 4096), then `numBlocks` blocks of
// `block` samples. perBlock(b) returns the (events, changes) pair for block b
// (either may be nullptr); afterBlock(b, fx) runs after each block.
template <typename PerBlock, typename AfterBlock>
[[nodiscard]] Stereo renderBlocks(std::size_t numBlocks, std::size_t block, const PerBlock& perBlock,
                                  const AfterBlock& afterBlock) {
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, 4096) == Steinberg::kResultOk);
    fx.reserveCapture(numBlocks * block);
    for (std::size_t b = 0; b < numBlocks; ++b) {
        const auto [ev, pc] = perBlock(b);
        REQUIRE(fx.processBlock(block, ev, pc) == Steinberg::kResultOk);
        afterBlock(b, fx);
    }
    return takeCapture(fx);
}

template <typename PerBlock>
[[nodiscard]] Stereo renderBlocks(std::size_t numBlocks, std::size_t block, PerBlock&& perBlock) {
    return renderBlocks(numBlocks, block, std::forward<PerBlock>(perBlock),
                        [](std::size_t, ProcessorFixture&) {});
}

// Block 0 carries `first` (events) and `firstPc` (changes); every later block is
// event-free.
[[nodiscard]] Stereo renderFirstBlockEvents(Steinberg::Vst::IEventList* first,
                                            Steinberg::Vst::IParameterChanges* firstPc,
                                            std::size_t numBlocks) {
    return renderBlocks(numBlocks, kBlock, [&](std::size_t b) {
        return std::pair<Steinberg::Vst::IEventList*, Steinberg::Vst::IParameterChanges*>{
            b == 0 ? first : nullptr, b == 0 ? firstPc : nullptr};
    });
}

void requireNoMismatches(const Stereo& a, const Stereo& b) {
    REQUIRE(a.l.size() == b.l.size());
    REQUIRE(a.r.size() == b.r.size());
    const std::size_t mL = mismatches(std::span<const float>(a.l), std::span<const float>(b.l));
    const std::size_t mR = mismatches(std::span<const float>(a.r), std::span<const float>(b.r));
    INFO("mismatches L = " << mL << ", R = " << mR);
    REQUIRE(mL == 0u);
    REQUIRE(mR == 0u);
}

// POLY, polyphony 8: voice mode 1.0 and polyphony 1.0 (-> 8, global_params.h:86-88)
// as offset-0 points.
void addPoly8Points(Krate::Test::ParameterChanges& pc) {
    pc.addChange(::Profundum::kVoiceModeId, 1.0);
    pc.addChange(::Profundum::kPolyphonyId, 1.0);
}

// ---- Overflow script (spec SC-009, plan 4.3) ----
constexpr std::size_t kOverflowFillers = ::Profundum::Processor::kMaxEventsPerBlock;  // 1024
constexpr std::size_t kOverflowTail = 76;                                             // list 1024..1099
constexpr std::size_t kOneSecondBlocks = 94;        // 48 128 samples >= 1 s
constexpr std::size_t kOneAndHalfSecondBlocks = 141;  // 72 192 samples >= 1.5 s
constexpr std::size_t kBlockBIndex = 1 + kOneSecondBlocks;               // block A, then 1 s
constexpr std::size_t kBStart = kBlockBIndex * kBlock;                    // 48 640
constexpr std::size_t kBEnd = kBStart + kBlock;                           // 49 152
constexpr std::size_t kOverflowBlocks = kBlockBIndex + 1 + kOneAndHalfSecondBlocks;
constexpr std::size_t kOverflowWindowFrom = kBEnd + 48000;  // B_end + 1 s
constexpr std::size_t kOverflowWindowTo = kBEnd + 72000;    // + 0.5 s
constexpr std::size_t kOverflowCompareTail = 4800;           // 100 ms after block B
constexpr std::size_t kOverflowTailBaseOffset = 450;
static_assert(kOverflowWindowTo <= kOverflowBlocks * kBlock, "SC-009 (a) window inside the render");
static_assert(kOverflowTailBaseOffset >= 5u * (kOverflowTail - 1u),
              "the 76 overflow offsets stay non-negative");

[[nodiscard]] Steinberg::int16 fillerPitch(std::size_t i) {
    return static_cast<Steinberg::int16>(100 + static_cast<int>(i % 28u));  // 100..127, none sounding
}

[[nodiscard]] Steinberg::int16 tailPitch(std::size_t k) {
    return kChord8[k % kChord8.size()];
}

[[nodiscard]] Steinberg::int32 tailOffset(std::size_t k) {
    return static_cast<Steinberg::int32>(kOverflowTailBaseOffset - 5u * k);  // 450 - 5k, decreasing
}

struct OverflowResult {
    Stereo out;
    std::uint32_t slicesAfterB = 0;
    std::uint64_t dropped = 0;
};

[[nodiscard]] OverflowResult renderOverflow(bool withTail) {
    Krate::Test::EventList evA;
    for (const Steinberg::int16 p : kChord8) {
        evA.addNoteOn(p, kVelocity100, 0);
    }
    Krate::Test::ParameterChanges pcA;
    addPoly8Points(pcA);

    Krate::Test::EventList evB;
    for (std::size_t i = 0; i < kOverflowFillers; ++i) {
        evB.addNoteOff(fillerPitch(i), 0);
    }
    if (withTail) {
        for (std::size_t k = 0; k < kOverflowTail; ++k) {
            evB.addNoteOff(tailPitch(k), tailOffset(k));
        }
    }
    REQUIRE(evB.getEventCount() ==
            static_cast<Steinberg::int32>(kOverflowFillers + (withTail ? kOverflowTail : 0u)));

    OverflowResult res;
    res.out = renderBlocks(
        kOverflowBlocks, kBlock,
        [&](std::size_t b) {
            if (b == 0) {
                return std::pair<Steinberg::Vst::IEventList*, Steinberg::Vst::IParameterChanges*>{
                    &evA, &pcA};
            }
            if (b == kBlockBIndex) {
                return std::pair<Steinberg::Vst::IEventList*, Steinberg::Vst::IParameterChanges*>{
                    &evB, nullptr};
            }
            return std::pair<Steinberg::Vst::IEventList*, Steinberg::Vst::IParameterChanges*>{
                nullptr, nullptr};
        },
        [&](std::size_t b, ProcessorFixture& fx) {
            if (b == kBlockBIndex) {
                res.slicesAfterB = fx.proc->lastSliceCountForTest();
            }
            if (b + 1u == kOverflowBlocks) {
                const ProfundumEngine* engine = fx.proc->engineForTest();
                REQUIRE(engine != nullptr);
                res.dropped = engine->droppedEvents();
            }
        });
    return res;
}

// ---- PartitionInvariance script (shared by the Phase 14 morph-modulation SECTIONs) ----
// No offset is a multiple of 32 (the engine grid) nor of any partition.
constexpr std::size_t kPartitionTotal = 96000;  // 2 s
const std::array<ScriptedEvent, 5> kPartitionScript{{
    {.sample = 311, .kind = ScriptedEvent::Kind::NoteOn, .pitch = 28, .velocity = kVelocity100},
    {.sample = 4097, .kind = ScriptedEvent::Kind::NoteOn, .pitch = 33, .velocity = kVelocity100},
    {.sample = 20001, .kind = ScriptedEvent::Kind::NoteOff, .pitch = 28},
    {.sample = 47999, .kind = ScriptedEvent::Kind::NoteOn, .pitch = 40, .velocity = kVelocity100},
    {.sample = 70003, .kind = ScriptedEvent::Kind::NoteOff, .pitch = 33},
}};

// ---- Phase 14 T016 (SC-012 (a); FR-021): Morph Modulation partition invariance ----
constexpr std::size_t kMorphGridRows = kPartitionTotal / 32u + 2u;  // one row per engine grid point, + slack

/// Records the morph target of every grid point (profundum_engine_observer.h:52-65). Rows are reserved up front, so
/// onGrid never grows the vector.
class MorphGridRecorder final : public Krate::DSP::ProfundumEngineObserver {
public:
    explicit MorphGridRecorder(std::size_t capacity) { targets.reserve(capacity); }
    // NOLINTNEXTLINE(bugprone-exception-escape) -- push_back below the reserved capacity: no allocation, no throw
    void onGrid(const Krate::DSP::ProfundumGridRecord& r) noexcept override {
        if (targets.size() < targets.capacity()) {
            targets.push_back(r.morphTarget);
        }
    }
    std::vector<float> targets;
};

[[nodiscard]] ScriptedEvent paramAtZero(Steinberg::Vst::ParamID id, double value) {
    return ScriptedEvent{.sample = 0, .kind = ScriptedEvent::Kind::Param, .id = id, .value = value};
}

// The PartitionInvariance script with @p params (all at sample 0) in front, rendered in host blocks of @p hostBlock.
// The observer is attached on EVERY arm (before prepare, detached while inactive: the main_page_sanity_test.cpp
// pattern), so the reference and the partitions run the same engine path.
[[nodiscard]] Stereo renderMorphModPartition(std::span<const ScriptedEvent> params, std::size_t hostBlock,
                                             MorphGridRecorder& rec) {
    std::vector<ScriptedEvent> script(params.begin(), params.end());
    script.insert(script.end(), kPartitionScript.begin(), kPartitionScript.end());
    ProcessorFixture fx;
    fx.proc->setEngineObserverForTest(&rec);
    REQUIRE(fx.prepare(kSampleRate, 4096) == Steinberg::kResultOk);
    fx.renderScript(std::span<const ScriptedEvent>(script), kPartitionTotal, hostBlock);
    fx.proc->setActive(0u);
    fx.proc->setEngineObserverForTest(nullptr);
    return takeCapture(fx);
}

// Source (1003) + Amount 1 (1004 = 1.0) + Morph Position 0.5 (105) + the source's own @p extras, all at sample 0.
void requireMorphModPartitionInvariance(double sourceNorm, std::initializer_list<ScriptedEvent> extras) {
    std::vector<ScriptedEvent> params{paramAtZero(::Profundum::kMorphModSourceId, sourceNorm),
                                      paramAtZero(::Profundum::kMorphModAmountId, 1.0),
                                      paramAtZero(::Profundum::kMorphPositionId, 0.5)};
    params.insert(params.end(), extras.begin(), extras.end());
    const std::span<const ScriptedEvent> p(params);

    MorphGridRecorder refRec(kMorphGridRows);
    const Stereo ref = renderMorphModPartition(p, 512, refRec);

    // Non-vacuity: the modulation moved the target off b = 0.5 at least once.
    std::size_t moved = 0;
    for (const float t : refRec.targets) {
        if (std::bit_cast<std::uint32_t>(t) != std::bit_cast<std::uint32_t>(0.5f)) {
            ++moved;
        }
    }
    WARN("PartitionInvarianceMorphMod (source " << sourceNorm << "): 512 reference grid records = "
                                                << refRec.targets.size() << ", morphTarget != 0.5f in " << moved);
    REQUIRE_FALSE(refRec.targets.empty());
    REQUIRE(moved >= 1u);

    for (const std::size_t hostBlock : {std::size_t{1}, std::size_t{7}, std::size_t{32}, std::size_t{65},
                                        std::size_t{4096}}) {
        INFO("host block = " << hostBlock);
        MorphGridRecorder rec(kMorphGridRows);
        const Stereo got = renderMorphModPartition(p, hostBlock, rec);
        requireNoMismatches(got, ref);
    }
}

}  // namespace

TEST_CASE("Profundum_MidiEventTranslation", "[profundum][processor][midi]") {
    SECTION("PartitionInvariance") {
        // No offset is a multiple of 32 (the engine grid) nor of any partition.
        constexpr std::size_t kTotal = kPartitionTotal;  // 2 s
        const std::array<ScriptedEvent, 5>& script = kPartitionScript;
        for (const ScriptedEvent& s : script) {
            REQUIRE(s.sample % 32 != 0);
        }

        const auto render = [&](std::size_t hostBlock) {
            ProcessorFixture fx;
            REQUIRE(fx.prepare(kSampleRate, 4096) == Steinberg::kResultOk);
            fx.renderScript(std::span<const ScriptedEvent>(script), kTotal, hostBlock);
            return takeCapture(fx);
        };

        const Stereo ref = render(512);
        const double refDbL = rmsDb(std::span<const float>(ref.l), 24000, 96000);
        const double refDbR = rmsDb(std::span<const float>(ref.r), 24000, 96000);
        WARN("PartitionInvariance: 512 render rmsDb[24000, 96000) L = " << refDbL
                                                                      << " dB, R = " << refDbR << " dB");
        REQUIRE(refDbL >= -40.0);  // non-vacuity precondition
        REQUIRE(refDbR >= -40.0);

        for (const std::size_t hostBlock : {std::size_t{1}, std::size_t{7}, std::size_t{32},
                                            std::size_t{65}, std::size_t{4096}}) {
            INFO("host block = " << hostBlock);
            const Stereo got = render(hostBlock);
            requireNoMismatches(got, ref);
        }
    }

    SECTION("EventOrder") {
        constexpr std::size_t kBlocks = 1 + 47;  // block 0, then 0.5 s (24 064 samples)
        constexpr std::size_t kTotal = kBlocks * kBlock;
        constexpr std::size_t kLastQuarterSecond = 12000;

        Krate::Test::EventList unsorted;  // list order reversed against offset order
        unsorted.addNoteOn(40, kVelocity100, 300);
        unsorted.addNoteOff(40, 100);

        Krate::Test::EventList sorted;
        sorted.addNoteOff(40, 100);
        sorted.addNoteOn(40, kVelocity100, 300);

        const Stereo ref = renderFirstBlockEvents(&sorted, nullptr, kBlocks);
        const float refTailPeak = stereoPeak(ref, kTotal - kLastQuarterSecond, kTotal);
        WARN("EventOrder: reference peak over the last 0.25 s = " << refTailPeak);
        REQUIRE(refTailPeak > kAudibleFloor);  // the note is held: precondition

        const Stereo got = renderFirstBlockEvents(&unsorted, nullptr, kBlocks);
        requireNoMismatches(got, ref);
    }

    SECTION("Overflow") {
        const OverflowResult full = renderOverflow(true);
        const OverflowResult control = renderOverflow(false);

        WARN("Overflow: droppedEvents() = " << full.dropped << " (control " << control.dropped
                                            << "); slices in block B = " << full.slicesAfterB);

        // Slices [0, 450) and [450, 512): sorting or raw offsets would give 77,
        // deferral to the next block 1.
        REQUIRE(full.slicesAfterB == 2u);

        // (a) the 76 overflow note-offs released the chord.
        const float windowPeak = stereoPeak(full.out, kOverflowWindowFrom, kOverflowWindowTo);
        WARN("Overflow (a): peak over [B_end + 1 s, + 0.5 s) = " << windowPeak);
        REQUIRE(windowPeak < kSilenceBound);

        // (b) control: without them the chord still sounds.
        const double ctlDbL =
            rmsDb(std::span<const float>(control.out.l), kOverflowWindowFrom, kOverflowWindowTo);
        const double ctlDbR =
            rmsDb(std::span<const float>(control.out.r), kOverflowWindowFrom, kOverflowWindowTo);
        WARN("Overflow (b): control rmsDb L = " << ctlDbL << " dB, R = " << ctlDbR << " dB");
        REQUIRE(ctlDbL >= -60.0);
        REQUIRE(ctlDbR >= -60.0);

        // (c) ordering and offset rule: a DirectEngine replay with the 1024
        // fillers at block B's sample 0 and the 76 overflow note-offs at sample
        // 450 (the FR-025 non-decreasing clamp), each group in list order.
        std::vector<DirectEvent> script;
        script.reserve(2u + kChord8.size() + kOverflowFillers + kOverflowTail);
        script.push_back(DirectEvent{.sample = 0,
                                     .kind = DirectEvent::Kind::VoiceMode,
                                     .mode = ProfundumEngine::VoiceMode::Poly});
        script.push_back(DirectEvent{.sample = 0,
                                     .kind = DirectEvent::Kind::Polyphony,
                                     .polyphony = ProfundumEngine::kMaxVoices});
        for (const Steinberg::int16 p : kChord8) {
            script.push_back(DirectEvent{.sample = 0,
                                         .kind = DirectEvent::Kind::NoteOn,
                                         .note = static_cast<std::uint8_t>(p),
                                         .velocity = kMidiVelocity100});
        }
        for (std::size_t i = 0; i < kOverflowFillers; ++i) {
            script.push_back(DirectEvent{.sample = kBStart,
                                         .kind = DirectEvent::Kind::NoteOff,
                                         .note = static_cast<std::uint8_t>(fillerPitch(i))});
        }
        for (std::size_t k = 0; k < kOverflowTail; ++k) {
            script.push_back(DirectEvent{.sample = kBStart + kOverflowTailBaseOffset,
                                         .kind = DirectEvent::Kind::NoteOff,
                                         .note = static_cast<std::uint8_t>(tailPitch(k))});
        }

        constexpr std::size_t kCompareEnd = kBEnd + kOverflowCompareTail;
        DirectEngine direct(kSampleRate);
        direct.render(std::span<const DirectEvent>(script), kCompareEnd);

        const std::size_t len = kCompareEnd - kBStart;
        const auto procL = std::span<const float>(full.out.l).subspan(kBStart, len);
        const auto procR = std::span<const float>(full.out.r).subspan(kBStart, len);
        const auto refL = std::span<const float>(direct.outL).subspan(kBStart, len);
        const auto refR = std::span<const float>(direct.outR).subspan(kBStart, len);
        const float refPeak = std::max(peakOf(refL), peakOf(refR));
        const std::size_t mL = mismatches(procL, refL);
        const std::size_t mR = mismatches(procR, refR);
        WARN("Overflow (c): reference peak over block B + 100 ms = "
             << refPeak << "; mismatches L = " << mL << ", R = " << mR);
        REQUIRE(refPeak > kAudibleFloor);  // non-vacuity: the chord sounds in the window
        REQUIRE(mL == 0u);
        REQUIRE(mR == 0u);
    }

    SECTION("OffsetClamp") {
        constexpr std::size_t kBlocks = 1 + 47;  // block 0, then 0.5 s

        Krate::Test::ParameterChanges pcClamp;
        addPoly8Points(pcClamp);
        Krate::Test::EventList evClamp;
        evClamp.addNoteOn(28, kVelocity100, -5);
        evClamp.addNoteOn(40, kVelocity100, static_cast<Steinberg::int32>(kBlock) + 100);  // 612

        Krate::Test::ParameterChanges pcRef;
        addPoly8Points(pcRef);
        Krate::Test::EventList evRef;
        evRef.addNoteOn(28, kVelocity100, 0);
        evRef.addNoteOn(40, kVelocity100, static_cast<Steinberg::int32>(kBlock) - 1);  // 511

        // processBlock() REQUIREs the guard words intact after EVERY block
        // (profundum_test_fixture.h:310-311), so a slice written past the host
        // buffer fails inside the render.
        std::uint32_t slicesAfterFirst = 0;
        const Stereo got = renderBlocks(
            kBlocks, kBlock,
            [&](std::size_t b) {
                return std::pair<Steinberg::Vst::IEventList*, Steinberg::Vst::IParameterChanges*>{
                    b == 0 ? &evClamp : nullptr, b == 0 ? &pcClamp : nullptr};
            },
            [&](std::size_t b, ProcessorFixture& fx) {
                if (b == 0) {
                    slicesAfterFirst = fx.proc->lastSliceCountForTest();
                }
            });

        std::size_t nonFinite = 0;
        for (std::size_t i = 0; i < got.l.size(); ++i) {
            if (!isFiniteBits(got.l[i]) || !isFiniteBits(got.r[i])) {
                ++nonFinite;
            }
        }
        REQUIRE(nonFinite == 0u);

        // -5 -> 0 is dispatched at cursor 0 (no boundary); 612 -> 511.
        REQUIRE(slicesAfterFirst == 2u);

        const Stereo ref = renderFirstBlockEvents(&evRef, &pcRef, kBlocks);
        REQUIRE(stereoPeak(ref, 0, ref.l.size()) > kAudibleFloor);  // non-vacuity
        requireNoMismatches(got, ref);
    }

    SECTION("Translation") {
        SECTION("TinyVelocitySounds") {
            // 0.003 * 127 + 0.5 -> clamped to 1: a note-on, never the engine's
            // velocity-0 note-off (FR-026).
            constexpr std::size_t kBlocks = 94;  // 48 128 samples >= 1 s
            Krate::Test::EventList ev;
            ev.addNoteOn(kE1, 0.003f, 0);
            const Stereo out = renderFirstBlockEvents(&ev, nullptr, kBlocks);
            const double dbL = rmsDb(std::span<const float>(out.l), 24000, 48000);
            const double dbR = rmsDb(std::span<const float>(out.r), 24000, 48000);
            WARN("Translation: velocity 0.003 rmsDb[24000, 48000) L = " << dbL << " dB, R = " << dbR
                                                                       << " dB");
            REQUIRE(dbL >= -40.0);
            REQUIRE(dbR >= -40.0);
        }

        SECTION("VelocityZeroNoteOnReleases") {
            constexpr std::size_t kTotal = 96000;  // 2 s
            const std::array<ScriptedEvent, 2> script{{
                {.sample = 0, .kind = ScriptedEvent::Kind::NoteOn, .pitch = kE1, .velocity = kVelocity100},
                {.sample = 24000, .kind = ScriptedEvent::Kind::NoteOn, .pitch = kE1, .velocity = 0.0f},
            }};
            ProcessorFixture fx;
            REQUIRE(fx.prepare(kSampleRate, 4096) == Steinberg::kResultOk);
            fx.renderScript(std::span<const ScriptedEvent>(script), kTotal, kBlock);
            const Stereo out = takeCapture(fx);

            const float heldPeak = stereoPeak(out, 0, 24000);
            const float windowPeak = stereoPeak(out, 72000, 96000);
            WARN("Translation: velocity-0 note-on, held peak = " << heldPeak
                                                                 << ", peak over [72000, 96000) = "
                                                                 << windowPeak);
            REQUIRE(heldPeak > kAudibleFloor);  // non-vacuity: the note sounded
            REQUIRE(windowPeak < kSilenceBound);
        }

        SECTION("IgnoredEventsChangeNothing") {
            constexpr std::size_t kBlocks = 48;  // 24 576 samples
            const Stereo silent = renderFirstBlockEvents(nullptr, nullptr, kBlocks);

            for (const Steinberg::int16 pitch :
                 {static_cast<Steinberg::int16>(-1), static_cast<Steinberg::int16>(128),
                  static_cast<Steinberg::int16>(300)}) {
                INFO("kNoteOnEvent pitch = " << pitch);
                Krate::Test::EventList ev;
                ev.addNoteOn(pitch, kVelocity100, 0);
                const Stereo got = renderFirstBlockEvents(&ev, nullptr, kBlocks);
                requireNoMismatches(got, silent);
            }

            INFO("kDataEvent");
            Krate::Test::EventList ev;
            Steinberg::Vst::Event data{};
            data.type = Steinberg::Vst::Event::kDataEvent;
            data.sampleOffset = 0;
            data.data.size = 0;
            data.data.type = Steinberg::Vst::DataEvent::kMidiSysEx;
            data.data.bytes = nullptr;
            REQUIRE(ev.addEvent(data) == Steinberg::kResultTrue);
            const Stereo got = renderFirstBlockEvents(&ev, nullptr, kBlocks);
            requireNoMismatches(got, silent);
        }
    }

    SECTION("SliceCount") {
        ProcessorFixture fx;
        REQUIRE(fx.prepare(kSampleRate, 4096) == Steinberg::kResultOk);
        fx.reserveCapture(4096 + kBlock);

        REQUIRE(fx.processBlock(4096) == Steinberg::kResultOk);
        REQUIRE(fx.proc->lastSliceCountForTest() == 1u);

        Krate::Test::EventList ev;
        ev.addNoteOn(28, kVelocity100, 100);
        ev.addNoteOff(28, 200);
        ev.addNoteOn(33, kVelocity100, 300);
        REQUIRE(fx.processBlock(kBlock, &ev) == Steinberg::kResultOk);
        REQUIRE(fx.proc->lastSliceCountForTest() == 4u);
    }

    // ---- Phase 14 T016 (SC-012 (a); FR-021): 1003 normalised {0.25, 0.5, 0.75, 1.0} = Envelope, Cycle, Velocity,
    //      Mod Wheel (indexFromNormalized over 5 entries, morph_mod_params.h:226). ----
    SECTION("PartitionInvarianceMorphMod_Envelope") {
        requireMorphModPartitionInvariance(
            0.25, {paramAtZero(::Profundum::kMorphModAttackId,
                               ::Profundum::normalizedFromPlain(::Profundum::kMorphModTimeLaw, 400.0))});
    }

    SECTION("PartitionInvarianceMorphMod_Cycle") {
        requireMorphModPartitionInvariance(
            0.5, {paramAtZero(::Profundum::kMorphModCycleShapeId, 0.0),  // Sine (index 0)
                  paramAtZero(::Profundum::kMorphModCycleRateId,
                              ::Profundum::normalizedFromPlain(::Profundum::kMorphModRateLaw, 2.0))});
    }

    SECTION("PartitionInvarianceMorphMod_Velocity") {
        requireMorphModPartitionInvariance(0.75, {});
    }

    SECTION("PartitionInvarianceMorphMod_ModWheel") {
        // Mod Wheel (6) = 1.0, constant from sample 0.
        requireMorphModPartitionInvariance(1.0, {paramAtZero(::Profundum::kModWheelId, 1.0)});
    }
}
