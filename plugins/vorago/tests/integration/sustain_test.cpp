// ==============================================================================
// Vorago Phase 12 - sustain latch (SC-012)
// ==============================================================================
// T032: "Vorago_SustainLatchUnit" - Vorago::SustainLatch in isolation.
// T043 adds the processor-level "Vorago_SustainLatch" case to this file.
//
// ALLOCATION READING FORM (see unit/lifecycle_test.cpp:9-12): the count is read
// from the LIVE detector singleton INSIDE the scope and REQUIREd after it closes.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "plugin_ids.h"
#include "processor/sustain_latch.h"
#include "vorago_test_fixture.h"

#include <allocation_detector.h>
#include <vst_event_list.h>

#include <krate/dsp/systems/voice_allocator.h>
#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_voice.h>

#include "public.sdk/source/common/memorystream.h"

#include <pluginterfaces/vst/ivstevents.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace {

/// Fixed-capacity release collector (no allocation).
struct ReleaseLog {
    std::array<std::uint8_t, 128> notes{};
    std::size_t count = 0;

    void clear() noexcept { count = 0; }
    void push(std::uint8_t n) noexcept {
        if (count < notes.size()) notes[count] = n;
        ++count;  // overflow still counted so an over-release is visible
    }
};

}  // namespace

TEST_CASE("Vorago_SustainLatchUnit", "[vorago][midi]")
{
    Vorago::SustainLatch latch;
    ReleaseLog rel;
    auto collect = [&rel](std::uint8_t n) noexcept { rel.push(n); };

    SECTION("PedalUpNoteOffIsImmediate")
    {
        REQUIRE_FALSE(latch.isDown());
        latch.noteOn(60);
        REQUIRE(latch.noteOff(60));
    }

    SECTION("PedalDownLatchesThenReleasesOnPedalUp")
    {
        latch.setPedal(true, collect);
        REQUIRE(latch.isDown());
        REQUIRE(rel.count == 0u);
        latch.noteOn(60);
        REQUIRE_FALSE(latch.noteOff(60));
        latch.setPedal(false, collect);
        REQUIRE_FALSE(latch.isDown());
        REQUIRE(rel.count == 1u);
        REQUIRE(rel.notes[0] == 60);
    }

    SECTION("ReStrikeOfLatchedNoteIsHeld")
    {
        latch.setPedal(true, collect);
        latch.noteOn(60);
        REQUIRE_FALSE(latch.noteOff(60));  // latched
        latch.noteOn(60);                  // re-strike: held again, latch mark cleared
        latch.setPedal(false, collect);
        REQUIRE(rel.count == 0u);
        REQUIRE(latch.noteOff(60));  // pedal is up: immediate
        REQUIRE(rel.count == 0u);
    }

    SECTION("ReleaseAllReleasesEveryLatchedNote")
    {
        latch.setPedal(true, collect);
        latch.noteOn(60);
        latch.noteOn(64);
        REQUIRE_FALSE(latch.noteOff(60));
        REQUIRE_FALSE(latch.noteOff(64));
        latch.releaseAll(collect);
        REQUIRE_FALSE(latch.isDown());
        REQUIRE(rel.count == 2u);
        REQUIRE(rel.notes[0] == 60);
        REQUIRE(rel.notes[1] == 64);

        // Nothing is left latched: a fresh pedal cycle releases nothing.
        rel.clear();
        latch.setPedal(true, collect);
        latch.setPedal(false, collect);
        REQUIRE(rel.count == 0u);
    }

    SECTION("ClearWithoutReleaseForgetsEverything")
    {
        latch.setPedal(true, collect);
        latch.noteOn(60);
        latch.noteOn(64);
        REQUIRE_FALSE(latch.noteOff(60));
        REQUIRE_FALSE(latch.noteOff(64));
        latch.clearWithoutRelease();
        REQUIRE(rel.count == 0u);
        REQUIRE_FALSE(latch.isDown());

        // A later pedal cycle releases nothing (the latched set was cleared).
        latch.setPedal(true, collect);
        latch.setPedal(false, collect);
        REQUIRE(rel.count == 0u);
    }

    SECTION("NoteNumbersAbove127AreIgnored")
    {
        latch.setPedal(true, collect);
        latch.noteOn(200);
        REQUIRE(latch.noteOff(200));  // never latched
        latch.setPedal(false, collect);
        REQUIRE(rel.count == 0u);
    }

    SECTION("RandomOperationsAreAllocationFree")
    {
        std::mt19937 rng(12012u);
        std::uniform_int_distribution<int> opDist(0, 5);
        std::uniform_int_distribution<int> noteDist(0, 127);

        std::size_t allocs = 0;
        std::size_t immediateOffs = 0;
        std::size_t latchedOffs = 0;
        std::size_t maxReleasesPerPass = 0;
        {
            TestHelpers::AllocationScope scope;
            for (int i = 0; i < 10000; ++i) {
                const auto note = static_cast<std::uint8_t>(noteDist(rng));
                switch (opDist(rng)) {
                    case 0:
                    case 1:
                        latch.noteOn(note);
                        break;
                    case 2:
                    case 3:
                        if (latch.noteOff(note)) {
                            ++immediateOffs;
                        } else {
                            ++latchedOffs;
                        }
                        break;
                    case 4:
                        rel.clear();
                        latch.setPedal(!latch.isDown(), collect);
                        maxReleasesPerPass = std::max(rel.count, maxReleasesPerPass);
                        break;
                    default:
                        rel.clear();
                        if ((i % 64) == 0) {
                            latch.releaseAll(collect);
                        } else if ((i % 128) == 1) {
                            latch.clearWithoutRelease();
                        }
                        maxReleasesPerPass = std::max(rel.count, maxReleasesPerPass);
                        break;
                }
            }
            allocs = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        REQUIRE(allocs == 0u);
        REQUIRE(immediateOffs > 0u);  // the run exercised pedal-up note-offs
        REQUIRE(latchedOffs > 0u);    // ... and pedal-down (latched) note-offs
        REQUIRE(maxReleasesPerPass <= 128u);  // no pass released a key twice
    }
}

// ==============================================================================
// T043: processor-level sustain wiring (SC-012, FR-030, plan 4.7)
// ==============================================================================
// Every latch / release assertion reads engineForTest()->getVoiceState(slot)
// AFTER the block that carries the event; `slot` is the one that left Idle on the
// note-on. A released voice still below kTailSilenceThreshold is retired at the
// next control step (see unit/midi_event_test.cpp holdUntilAudible), so every
// scenario holds its note until audible before any release is asserted.
// ==============================================================================

namespace {

using Krate::DSP::VoiceState;
using Krate::DSP::VoragoEngine;

constexpr double kRigSampleRate = 48000.0;
constexpr std::size_t kRigBlock = 512;
constexpr float kRigVelocity = 100.0f / 127.0f;
constexpr std::size_t kRigMaxHoldBlocks = 375;  // 4 s at 48 kHz / 512
constexpr std::size_t kTwoSecondBlocks = 188;   // >= 2 s at 48 kHz / 512

[[nodiscard]] bool releasingOrIdle(VoiceState s) noexcept {
    return s == VoiceState::Releasing || s == VoiceState::Idle;
}

[[nodiscard]] Steinberg::Vst::Event makeVelocityZeroNoteOn(Steinberg::int16 pitch,
                                                           Steinberg::int32 offset) {
    Steinberg::Vst::Event e{};
    e.type = Steinberg::Vst::Event::kNoteOnEvent;
    e.sampleOffset = offset;
    e.noteOn.channel = 0;
    e.noteOn.pitch = pitch;
    e.noteOn.velocity = 0.0f;
    e.noteOn.noteId = -1;
    return e;
}

/// A prepared processor (48 kHz / 512) plus one reusable event list and one
/// parameter-change set; block() sends whatever was queued since the last block.
class SustainRig {
public:
    SustainRig() {
        fx_.prepare(kRigSampleRate, static_cast<Steinberg::int32>(kRigBlock));
        fx_.reserveCapture(kRigBlock);
        pc_.reserve(4);
    }

    void noteOn(Steinberg::int16 pitch, Steinberg::int32 offset = 0) {
        ev_.addNoteOn(pitch, kRigVelocity, offset);
    }
    void noteOnVelocityZero(Steinberg::int16 pitch, Steinberg::int32 offset = 0) {
        Steinberg::Vst::Event e = makeVelocityZeroNoteOn(pitch, offset);
        ev_.addEvent(e);
    }
    void noteOff(Steinberg::int16 pitch, Steinberg::int32 offset = 0) {
        ev_.addNoteOff(pitch, offset);
    }
    void pedal(bool down, Steinberg::int32 offset = 0) {
        if (pedalQueue_ == nullptr) {
            pedalQueue_ = &pc_.addQueue(::Vorago::kSustainPedalId);
        }
        pedalQueue_->addTestPoint(offset, down ? 1.0 : 0.0);
    }

    void block() {
        fx_.capturedL.clear();  // keeps capacity
        fx_.capturedR.clear();
        REQUIRE(fx_.processBlock(kRigBlock, &ev_, &pc_) == Steinberg::kResultOk);
        ev_.clear();
        pc_.clear();
        pedalQueue_ = nullptr;
    }

    [[nodiscard]] const VoragoEngine& engine() const {
        const VoragoEngine* e = fx_.proc->engineForTest();
        REQUIRE(e != nullptr);
        return *e;
    }
    [[nodiscard]] VoiceState state(std::size_t slot) const { return engine().getVoiceState(slot); }

    [[nodiscard]] std::size_t countActive() const {
        std::size_t n = 0;
        for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
            if (engine().getVoiceState(i) == VoiceState::Active) {
                ++n;
            }
        }
        return n;
    }

    /// Note-on at offset 0 in its own block; returns the slot that left Idle.
    [[nodiscard]] std::size_t strike(Steinberg::int16 pitch) {
        std::array<VoiceState, VoragoEngine::kMaxVoices> before{};
        for (std::size_t i = 0; i < before.size(); ++i) {
            before[i] = engine().getVoiceState(i);
        }
        noteOn(pitch);
        block();
        std::size_t found = VoragoEngine::kMaxVoices;
        std::size_t changed = 0;
        for (std::size_t i = 0; i < before.size(); ++i) {
            if (before[i] == VoiceState::Idle && engine().getVoiceState(i) != VoiceState::Idle) {
                found = i;
                ++changed;
            }
        }
        REQUIRE(changed == 1u);
        REQUIRE(state(found) == VoiceState::Active);
        return found;
    }

    /// Holds until the slot's level clears kTailSilenceThreshold, so a release
    /// reads Releasing rather than an immediate retire to Idle.
    void holdUntilAudible(std::size_t slot) {
        std::size_t held = 0;
        while (engine().getVoiceLevel(slot) < Krate::DSP::VoragoVoice::kTailSilenceThreshold &&
               held < kRigMaxHoldBlocks) {
            block();
            ++held;
        }
        INFO("held blocks = " << held << ", level = " << engine().getVoiceLevel(slot));
        REQUIRE(engine().getVoiceLevel(slot) >= Krate::DSP::VoragoVoice::kTailSilenceThreshold);
        REQUIRE(state(slot) == VoiceState::Active);
    }

    [[nodiscard]] std::size_t strikeAudible(Steinberg::int16 pitch) {
        const std::size_t slot = strike(pitch);
        holdUntilAudible(slot);
        return slot;
    }

    /// Pedal down (own block), then the note-off (own block): the note is latched.
    void latch(Steinberg::int16 pitch, std::size_t slot) {
        pedal(true);
        block();
        REQUIRE(proc().latchForTest().isDown());
        noteOff(pitch);
        block();
        REQUIRE(state(slot) == VoiceState::Active);
    }

    [[nodiscard]] ::Vorago::Processor& proc() const { return *fx_.proc; }

private:
    VoragoTest::ProcessorFixture fx_;
    Krate::Test::EventList ev_;
    VoragoTest::MultiParamChanges pc_;
    VoragoTest::MultiPointParamValueQueue* pedalQueue_ = nullptr;
};

}  // namespace

TEST_CASE("Vorago_SustainLatch", "[vorago][midi]")
{
    constexpr Steinberg::int16 kPitch = 60;

    SECTION("PedalDownHoldsNoteOffUntilPedalUp")  // SC-012 (1)
    {
        SustainRig rig;
        const std::size_t slot = rig.strikeAudible(kPitch);
        rig.latch(kPitch, slot);

        for (std::size_t b = 0; b < kTwoSecondBlocks; ++b) {
            rig.block();
        }
        REQUIRE(rig.state(slot) == VoiceState::Active);  // still latched 2 s later

        rig.pedal(false);
        rig.block();
        REQUIRE_FALSE(rig.proc().latchForTest().isDown());
        REQUIRE(rig.state(slot) == VoiceState::Releasing);
    }

    SECTION("ControlPedalUpNoteOffReleases")  // SC-012 (1) control
    {
        SustainRig rig;
        const std::size_t slot = rig.strikeAudible(kPitch);
        rig.noteOff(kPitch);
        rig.block();
        REQUIRE(rig.state(slot) == VoiceState::Releasing);
    }

    SECTION("NoteOffAt100ThenPedalDownAt200Releases")  // SC-012 (2)
    {
        SustainRig rig;
        const std::size_t slot = rig.strikeAudible(kPitch);
        rig.noteOff(kPitch, 100);
        rig.pedal(true, 200);
        rig.block();
        REQUIRE(rig.proc().latchForTest().isDown());
        REQUIRE(rig.state(slot) == VoiceState::Releasing);
    }

    SECTION("PedalDownAt100ThenNoteOffAt200Latches")  // SC-012 (2)
    {
        SustainRig rig;
        const std::size_t slot = rig.strikeAudible(kPitch);
        rig.pedal(true, 100);
        rig.noteOff(kPitch, 200);
        rig.block();
        REQUIRE(rig.state(slot) == VoiceState::Active);
    }

    SECTION("SameOffsetNoteOffBeforePedalDown")  // SC-012 (2), D-P6 notes first
    {
        SustainRig rig;
        const std::size_t slot = rig.strikeAudible(kPitch);
        rig.pedal(true, 150);  // queued first on purpose: the rule, not queue order, decides
        rig.noteOff(kPitch, 150);
        rig.block();
        REQUIRE(rig.proc().latchForTest().isDown());
        REQUIRE(rig.state(slot) == VoiceState::Releasing);
    }

    SECTION("EveryPedalPointIsHonoured")  // plan 4.2: all points, not the last one
    {
        SustainRig rig;
        const std::size_t slot = rig.strikeAudible(kPitch);
        rig.latch(kPitch, slot);

        // Up @100 releases the latched note; down again @300 cannot un-release it.
        // A last-point-only reading would see "down" and leave the slot Active.
        rig.pedal(false, 100);
        rig.pedal(true, 300);
        rig.block();
        REQUIRE(rig.proc().latchForTest().isDown());
        REQUIRE(rig.state(slot) == VoiceState::Releasing);
    }

    SECTION("VelocityZeroNoteOnIsLatchedLikeANoteOff")  // plan 4.7
    {
        SustainRig rig;
        const std::size_t slot = rig.strikeAudible(kPitch);
        rig.pedal(true);
        rig.block();
        rig.noteOnVelocityZero(kPitch);
        rig.block();
        REQUIRE(rig.state(slot) == VoiceState::Active);  // latched, not released

        rig.pedal(false);
        rig.block();
        REQUIRE(rig.state(slot) == VoiceState::Releasing);
    }

    SECTION("ReStruckLatchedNoteSurvivesPedalUp")  // SC-012 (3)
    {
        SustainRig rig;
        const std::size_t slot = rig.strikeAudible(kPitch);
        rig.latch(kPitch, slot);

        rig.noteOn(kPitch);  // re-strike: retriggers the same slot, clears the latch mark
        rig.block();
        REQUIRE(rig.state(slot) == VoiceState::Active);
        REQUIRE(rig.countActive() == 1u);

        rig.pedal(false);  // the key is held: the pedal-up must skip it
        rig.block();
        REQUIRE(rig.state(slot) == VoiceState::Active);

        rig.noteOff(kPitch);
        rig.block();
        REQUIRE(rig.state(slot) == VoiceState::Releasing);
    }

    SECTION("SetActiveFalseReleasesImmediately")  // SC-012 (4)
    {
        SustainRig rig;
        const std::size_t slot = rig.strikeAudible(kPitch);
        rig.latch(kPitch, slot);

        REQUIRE(rig.proc().setActive(static_cast<Steinberg::TBool>(false)) == Steinberg::kResultOk);
        REQUIRE(releasingOrIdle(rig.state(slot)));
        REQUIRE_FALSE(rig.proc().latchForTest().isDown());
    }

    SECTION("SetStateReleasesOnTheNextProcessOnly")  // SC-012 (4), Q8 (a)
    {
        SustainRig rig;
        auto stream = Steinberg::owned(new Steinberg::MemoryStream());
        REQUIRE(rig.proc().getState(stream) == Steinberg::kResultOk);  // a v2 stream

        const std::size_t slot = rig.strikeAudible(kPitch);
        rig.latch(kPitch, slot);

        REQUIRE(stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);
        REQUIRE(rig.proc().setState(stream) == Steinberg::kResultOk);
        REQUIRE(rig.state(slot) == VoiceState::Active);  // setState makes no DSP call
        REQUIRE(rig.proc().latchForTest().isDown());     // latch untouched until process()

        rig.block();
        REQUIRE(releasingOrIdle(rig.state(slot)));
        REQUIRE_FALSE(rig.proc().latchForTest().isDown());
    }

    SECTION("RandomPedalAndNoteEventsAreAllocationFree")  // SC-012 (5)
    {
        constexpr std::size_t kFuzzEvents = 10000;
        constexpr std::size_t kFuzzBlock = 128;
        constexpr std::size_t kMaxPerBlock = 12;
        constexpr int kLowNote = 48;
        constexpr int kNumNotes = 12;

        VoragoTest::ProcessorFixture fx;
        fx.prepare(kRigSampleRate, static_cast<Steinberg::int32>(kRigBlock));
        fx.reserveCapture(kRigBlock);
        REQUIRE(fx.processBlock(kFuzzBlock) == Steinberg::kResultOk);  // warm-up

        // Pre-build every block's event list and pedal queue OUTSIDE the scope.
        std::mt19937 rng(12043u);
        std::uniform_int_distribution<std::size_t> countDist(1, kMaxPerBlock);
        std::uniform_int_distribution<int> offsetDist(0, static_cast<int>(kFuzzBlock) - 1);
        std::uniform_int_distribution<int> kindDist(0, 99);
        std::uniform_int_distribution<int> noteDist(0, kNumNotes - 1);

        std::vector<Krate::Test::EventList> events;
        std::vector<VoragoTest::MultiParamChanges> params;
        events.reserve(kFuzzEvents);
        params.reserve(kFuzzEvents);
        bool pedalDown = false;
        std::size_t generated = 0;
        std::size_t pedalPoints = 0;
        std::size_t velocityZero = 0;
        while (generated < kFuzzEvents) {
            const std::size_t n = std::min(countDist(rng), kFuzzEvents - generated);
            std::array<int, kMaxPerBlock> offsets{};
            for (std::size_t k = 0; k < n; ++k) {
                offsets[k] = offsetDist(rng);
            }
            std::sort(offsets.begin(), offsets.begin() + static_cast<std::ptrdiff_t>(n));

            Krate::Test::EventList& ev = events.emplace_back();
            VoragoTest::MultiParamChanges& pc = params.emplace_back();
            pc.reserve(1);
            VoragoTest::MultiPointParamValueQueue* pedalQueue = nullptr;
            for (std::size_t k = 0; k < n; ++k) {
                const auto off = static_cast<Steinberg::int32>(offsets[k]);
                const auto pitch = static_cast<Steinberg::int16>(kLowNote + noteDist(rng));
                const int kind = kindDist(rng);
                if (kind < 40) {
                    ev.addNoteOn(pitch, kRigVelocity, off);
                } else if (kind < 75) {
                    ev.addNoteOff(pitch, off);
                } else if (kind < 80) {
                    Steinberg::Vst::Event e = makeVelocityZeroNoteOn(pitch, off);
                    ev.addEvent(e);
                    ++velocityZero;
                } else {
                    if (pedalQueue == nullptr) {
                        pedalQueue = &pc.addQueue(::Vorago::kSustainPedalId);
                    }
                    pedalDown = !pedalDown;
                    pedalQueue->addTestPoint(off, pedalDown ? 1.0 : 0.0);
                    ++pedalPoints;
                }
                ++generated;
            }
        }
        REQUIRE(pedalPoints > 1000u);
        REQUIRE(velocityZero > 0u);

        std::size_t allocs = 0;
        std::size_t okBlocks = 0;
        std::size_t downBlocks = 0;  // blocks that ended with the pedal down
        {
            TestHelpers::AllocationScope scope;
            for (std::size_t b = 0; b < events.size(); ++b) {
                fx.capturedL.clear();  // keeps capacity
                fx.capturedR.clear();
                if (fx.processBlock(kFuzzBlock, &events[b], &params[b]) == Steinberg::kResultOk) {
                    ++okBlocks;
                }
                if (fx.proc->latchForTest().isDown()) {
                    ++downBlocks;
                }
            }
            allocs = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        REQUIRE(okBlocks == events.size());
        REQUIRE(allocs == 0u);
        REQUIRE(downBlocks > 0u);
        REQUIRE(fx.proc->latchForTest().isDown() == pedalDown);  // every point reached the latch

        // No stuck note: every key off, then pedal up -> nothing reads Active.
        Krate::Test::EventList offs;
        for (int p = 0; p < kNumNotes; ++p) {
            offs.addNoteOff(static_cast<Steinberg::int16>(kLowNote + p), 0);
        }
        VoragoTest::MultiParamChanges up;
        up.reserve(1);
        up.addQueue(::Vorago::kSustainPedalId).addTestPoint(0, 0.0);
        fx.capturedL.clear();
        fx.capturedR.clear();
        REQUIRE(fx.processBlock(kFuzzBlock, &offs, &up) == Steinberg::kResultOk);
        const VoragoEngine* engine = fx.proc->engineForTest();
        REQUIRE(engine != nullptr);
        for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
            INFO("slot " << i);
            REQUIRE(engine->getVoiceState(i) != VoiceState::Active);
        }
    }
}
