// Profundum Phase 14 morph-modulation processor tests (specs/profundum-phase14-morph-modulation/tasks.md)
//
// Registered early by T004 (tasks Deviation 3). Cases written by T016 (SC-012 (d), SC-010 (f); FR-021, C-8):
//   - Profundum_MorphModCompareBypass ([profundum][morphmod][morph]): the Compare bypass holds the target at b while
//     the sources keep advancing.
//   - Profundum_MorphModPedalRestrike ([profundum][morphmod][midi]): a pedal-latched re-strike restarts the envelope.
//
// Test-only: a red here is a T007 (engine) or T011 (processor) defect, never a reason to edit this file's bars.
// Every comparison against a separately compiled computation is within 1e-6 (no cross-site bit-equality); the one
// bit-equality, morphTarget == +0.0f under the bypass, is a branch result (profundum_engine.h:958-960).

#include "profundum_test_fixture.h"

#include "parameters/morph_mod_params.h"
#include "parameters/morph_params.h"
#include "plugin_ids.h"

#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_morph_modulator.h>

#include <pluginterfaces/vst/ivstattributes.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <utility>
#include <vector>

using namespace ProfundumPluginTest;

namespace {

namespace P = ::Profundum;
using ScriptedEvent = ProcessorFixture::ScriptedEvent;

constexpr double kFs = 48000.0;
constexpr float kVelocity100 = 100.0f / 127.0f;
constexpr std::uint8_t kMidiVelocity100 = 100;  // quantiseVelocity(100/127) = uint8(100.5) (processor.cpp:82-84)
constexpr std::size_t kGrid = 32;               // ProfundumEngine::kControlInterval
constexpr float kSourceTolerance = 1.0e-6f;

/// One grid record's Morph Modulation fields (profundum_engine_observer.h:52-65).
struct MorphModGridRow {
    std::uint64_t sample;
    float source;
    float target;
};

/// Rows reserved up front, so onGrid never grows the vector.
class MorphModGridRecorder final : public Krate::DSP::ProfundumEngineObserver {
public:
    explicit MorphModGridRecorder(std::size_t capacity) { rows.reserve(capacity); }
    // NOLINTNEXTLINE(bugprone-exception-escape) -- push_back below the reserved capacity: no allocation, no throw
    void onGrid(const Krate::DSP::ProfundumGridRecord& r) noexcept override {
        if (rows.size() < rows.capacity()) {
            rows.push_back(MorphModGridRow{.sample = r.sample, .source = r.morphModSource, .target = r.morphTarget});
        }
    }
    std::vector<MorphModGridRow> rows;
};

[[nodiscard]] ScriptedEvent paramAt(Steinberg::int64 sample, Steinberg::Vst::ParamID id, double value) {
    return ScriptedEvent{.sample = sample, .kind = ScriptedEvent::Kind::Param, .id = id, .value = value};
}

[[nodiscard]] ScriptedEvent noteOnAt(Steinberg::int64 sample, Steinberg::int16 pitch) {
    return ScriptedEvent{.sample = sample, .kind = ScriptedEvent::Kind::NoteOn, .pitch = pitch, .velocity = kVelocity100};
}

[[nodiscard]] ScriptedEvent noteOffAt(Steinberg::int64 sample, Steinberg::int16 pitch) {
    return ScriptedEvent{.sample = sample, .kind = ScriptedEvent::Kind::NoteOff, .pitch = pitch};
}

/// A ctl -> proc kMsgCompare message carrying the int attribute "mode" (phase11_alloc_test.cpp:81-89).
[[nodiscard]] Steinberg::IPtr<Steinberg::Vst::HostMessage> makeCompareMessage(P::CompareMode mode) {
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID(P::kMsgCompare);
    Steinberg::Vst::IAttributeList* attrs = msg->getAttributes();
    REQUIRE(attrs != nullptr);
    REQUIRE(attrs->setInt("mode", static_cast<Steinberg::int64>(mode)) == Steinberg::kResultOk);
    return msg;
}

/// The row's sample relative to the first grid record (the engine clock of the render).
[[nodiscard]] std::uint64_t relSample(const std::vector<MorphModGridRow>& rows, std::size_t i) {
    return rows[i].sample - rows.front().sample;
}

// ---- Profundum_MorphModCompareBypass (SC-012 (d)) ----
constexpr std::size_t kCbBlock = 480;                // 15 grid intervals: every compare switch lands on a block edge
constexpr std::size_t kCbBypassFrom = 48000;         // Compare A at 1.0 s
constexpr std::size_t kCbBypassTo = 72000;           // back to A.B at 1.5 s
constexpr std::size_t kCbTotal = 96000;              // 2.0 s
static_assert(kCbBlock % kGrid == 0 && kCbBypassFrom % kCbBlock == 0 && kCbBypassTo % kCbBlock == 0 &&
                  kCbTotal % kCbBlock == 0,
              "the compare switches fall between process() calls, on grid points");
constexpr std::size_t kCbRows = kCbTotal / kGrid + 2u;

/// Held E1 from sample 0 with @p params at sample 0; with @p compare, Compare A arrives before the block at 1.0 s and
/// A.B before the block at 1.5 s (notify between process() calls).
[[nodiscard]] std::vector<MorphModGridRow> renderCompareArm(std::span<const ScriptedEvent> params, bool compare) {
    std::vector<ScriptedEvent> script(params.begin(), params.end());
    script.push_back(noteOnAt(0, kE1));

    MorphModGridRecorder rec(kCbRows);
    auto fx = std::make_unique<ProcessorFixture>();
    fx->proc->setEngineObserverForTest(&rec);  // attached before setupProcessing (main_page_sanity_test.cpp:353)
    REQUIRE(fx->prepare(kFs, static_cast<Steinberg::int32>(kCbBlock)) == Steinberg::kResultOk);

    fx->renderScript(std::span<const ScriptedEvent>(script), kCbBypassFrom, kCbBlock);
    if (compare) {
        auto msg = makeCompareMessage(P::CompareMode::A);
        REQUIRE(fx->proc->notify(msg) == Steinberg::kResultOk);
        REQUIRE(fx->proc->compareModeForTest() == P::CompareMode::A);
    }
    fx->renderScript(std::span<const ScriptedEvent>{}, kCbBypassTo - kCbBypassFrom, kCbBlock);
    if (compare) {
        auto msg = makeCompareMessage(P::CompareMode::AB);
        REQUIRE(fx->proc->notify(msg) == Steinberg::kResultOk);
        REQUIRE(fx->proc->compareModeForTest() == P::CompareMode::AB);
    }
    fx->renderScript(std::span<const ScriptedEvent>{}, kCbTotal - kCbBypassTo, kCbBlock);

    // Detach while inactive, before the recorder goes out of scope.
    fx->proc->setActive(0u);
    fx->proc->setEngineObserverForTest(nullptr);
    return std::move(rec.rows);
}

void requireCompareBypass(const char* arm, std::initializer_list<ScriptedEvent> sourceParams) {
    std::vector<ScriptedEvent> params{paramAt(0, P::kMorphModAmountId, 1.0),  // Amount 1
                                      paramAt(0, P::kMorphPositionId, 0.5)};  // b = 0.5
    params.insert(params.end(), sourceParams.begin(), sourceParams.end());
    const std::span<const ScriptedEvent> p(params);

    const std::vector<MorphModGridRow> ref = renderCompareArm(p, false);
    const std::vector<MorphModGridRow> got = renderCompareArm(p, true);
    REQUIRE_FALSE(ref.empty());
    REQUIRE(got.size() == ref.size());
    REQUIRE(ref.size() >= kCbTotal / kGrid);

    std::size_t bypassRows = 0;
    std::size_t refMovedInWindow = 0;  // non-vacuity: without the compare the target is not 0 in the window
    std::size_t targetNotB = 0;
    std::size_t sourceMismatches = 0;
    float worstSourceDiff = 0.0f;
    for (std::size_t i = 0; i < ref.size(); ++i) {
        REQUIRE(relSample(got, i) == relSample(ref, i));
        const std::uint64_t at = relSample(ref, i);
        if (at >= kCbBypassFrom && at < kCbBypassTo) {
            ++bypassRows;
            if (std::bit_cast<std::uint32_t>(ref[i].target) != std::bit_cast<std::uint32_t>(0.0f)) {
                ++refMovedInWindow;
            }
            // Compare A pins b = 0, and the bypass hands b through unmodulated.
            if (std::bit_cast<std::uint32_t>(got[i].target) != std::bit_cast<std::uint32_t>(0.0f)) {
                ++targetNotB;
            }
        }
        const float d = std::fabs(got[i].source - ref[i].source);
        worstSourceDiff = std::max(worstSourceDiff, d);
        if (!(d <= kSourceTolerance)) {
            ++sourceMismatches;
        }
    }
    WARN("CompareBypass (" << arm << "): rows = " << ref.size() << ", bypass rows = " << bypassRows
                           << ", reference target != 0 in the window = " << refMovedInWindow
                           << ", bypass target != +0.0f = " << targetNotB << ", source mismatches = "
                           << sourceMismatches << " (worst |diff| = " << worstSourceDiff << ")");
    REQUIRE(bypassRows == (kCbBypassTo - kCbBypassFrom) / kGrid);
    REQUIRE(refMovedInWindow >= 1u);
    REQUIRE(targetNotB == 0u);
    REQUIRE(sourceMismatches == 0u);
}

// ---- Profundum_MorphModPedalRestrike (SC-010 (f)) ----
constexpr std::size_t kPrBlock = 512;
constexpr Steinberg::int64 kPrNoteOff = 14400;    // 0.3 s: latched by the held pedal
constexpr Steinberg::int64 kPrRestrike = 28800;   // 0.6 s: Attack (400 ms) is over, the envelope sits at Sustain 1
constexpr std::size_t kPrTotal = 48000;           // 1.0 s
static_assert(kPrRestrike % static_cast<Steinberg::int64>(kGrid) == 0,
              "the re-strike lands on a grid point, so that grid step applies it");
constexpr std::size_t kPrRows = kPrTotal / kGrid + 2u;

}  // namespace

TEST_CASE("Profundum_MorphModCompareBypass", "[profundum][morphmod][morph]") {
    SECTION("Envelope") {
        // A 400 ms, S 1 (1003 = 0.25 -> Envelope, morph_mod_params.h:226).
        requireCompareBypass("Envelope",
                             {paramAt(0, P::kMorphModSourceId, 0.25),
                              paramAt(0, P::kMorphModAttackId, P::normalizedFromPlain(P::kMorphModTimeLaw, 400.0)),
                              paramAt(0, P::kMorphModSustainId, 1.0)});
    }
    SECTION("Cycle") {
        // 1 Hz Sine (1003 = 0.5 -> Cycle; 1008 index 0 = Sine).
        requireCompareBypass("Cycle",
                             {paramAt(0, P::kMorphModSourceId, 0.5), paramAt(0, P::kMorphModCycleShapeId, 0.0),
                              paramAt(0, P::kMorphModCycleRateId, P::normalizedFromPlain(P::kMorphModRateLaw, 1.0))});
    }
}

TEST_CASE("Profundum_MorphModPedalRestrike", "[profundum][morphmod][midi]") {
    const std::array<std::pair<Steinberg::Vst::ParamID, double>, 4> modParams{{
        {P::kMorphModSourceId, 0.25},  // Envelope
        {P::kMorphModAmountId, 1.0},   // Amount 1
        {P::kMorphModAttackId, P::normalizedFromPlain(P::kMorphModTimeLaw, 400.0)},
        {P::kMorphModSustainId, 1.0},  // S 1
    }};

    std::vector<ScriptedEvent> script;
    script.reserve(modParams.size() + 2u);
    for (const auto& [id, v] : modParams) {
        script.push_back(paramAt(0, id, v));
    }
    script.push_back(paramAt(0, P::kMorphPositionId, 0.5));
    script.push_back(paramAt(0, P::kSustainPedalId, 1.0));  // pedal down from sample 0
    script.push_back(noteOnAt(0, kE1));
    script.push_back(noteOffAt(kPrNoteOff, kE1));  // latched
    script.push_back(noteOnAt(kPrRestrike, kE1));  // the re-strike

    MorphModGridRecorder rec(kPrRows);
    auto fx = std::make_unique<ProcessorFixture>();
    fx->proc->setEngineObserverForTest(&rec);
    REQUIRE(fx->prepare(kFs, static_cast<Steinberg::int32>(kPrBlock)) == Steinberg::kResultOk);
    fx->renderScript(std::span<const ScriptedEvent>(script), kPrTotal, kPrBlock);
    const bool pedalDown = fx->proc->sustainDownForTest();
    fx->proc->setActive(0u);
    fx->proc->setEngineObserverForTest(nullptr);
    REQUIRE(pedalDown);  // precondition: the note-off at 0.3 s was latched, not dispatched

    // The fresh reference: the plugin's own parameter path into a new modulator, one note-on, one step.
    P::MorphModParams stored;
    for (const auto& [id, v] : modParams) {
        REQUIRE(P::handleMorphModParamChange(stored, id, v));
    }
    auto fresh = std::make_unique<Krate::DSP::ProfundumMorphModulator>();
    fresh->prepare(kFs);
    fresh->setConfig(P::morphModConfigOf(stored));
    fresh->noteOn(kMidiVelocity100);
    const float firstStep = fresh->advanceInterval();

    REQUIRE_FALSE(rec.rows.empty());
    std::size_t k = rec.rows.size();
    for (std::size_t i = 0; i < rec.rows.size(); ++i) {
        if (relSample(rec.rows, i) == static_cast<std::uint64_t>(kPrRestrike)) {
            k = i;
            break;
        }
    }
    REQUIRE(k < rec.rows.size());
    REQUIRE(k >= 1u);
    const float atRestrike = rec.rows[k].source;
    const float previous = rec.rows[k - 1u].source;
    WARN("PedalRestrike: previous interval = " << previous << ", re-strike grid point = " << atRestrike
                                               << ", fresh first step = " << firstStep);
    REQUIRE(previous > 0.5f);
    REQUIRE(std::fabs(atRestrike - firstStep) <= kSourceTolerance);
}
