// ==============================================================================
// Profundum Phase 14 (specs/profundum-phase14-morph-modulation) - profundum_morph_modulator_test.cpp
// ==============================================================================
// tasks.md T006 (FR-001 - FR-008; SC-001 (a), SC-002, SC-003 (a)(b)(c)(e) and the modulator half of (d); plan 4.1,
// 5.2, 5.3). All [systems][profundum][morphmod]. Control rate fc = fs / 32 (1500 / 1378.125 / 3000 Hz at 48 / 44.1 /
// 96 kHz); one advanceInterval() per interval; "interval 1" is the first advanceInterval() after the note (the C-4
// restart observable: it already holds one Attack step from 0).
//   ProfundumMorphModulator_ApiAndRt   SC-001 (a): exactly 10 Config fields, Output is a ModulationSource {-1, 1},
//                                      noexcept surface; FR-007 value contract; FR-006 wheel clamp; setConfig
//                                      and setTempoBpm sanitising (finite arms); 0 allocations per source.
//                                      The NaN arms live in profundum_morph_mod_nonfinite_test.cpp (dsp_ieee_tests).
//   ProfundumMorphModulator_Envelope   SC-002 (a)-(f) at 48 / 44.1 / 96 kHz.
//   ProfundumMorphModulator_Cycle      SC-003 (a)-(e), modulator part.
// No bit-equality is claimed between separately compiled computations: the modulator against a reference
// ADSREnvelope / LFO is checked to 1e-6. Bitwise claims are same-object (output() vs offsetValue()), values produced
// by a branch (+0 from the flush, S at the Decay end, the clamp endpoints) or two runs of one function in this TU.
// ==============================================================================

#include <krate/dsp/systems/profundum_morph_modulator.h>

#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/envelope_utils.h>
#include <krate/dsp/primitives/lfo.h>

#include <catch2/catch_test_macros.hpp>

#include <allocation_detector.h>
#include <enable_ftz_daz.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using Krate::DSP::ADSREnvelope;
using Krate::DSP::LFO;
using Krate::DSP::ModulationSource;
using Krate::DSP::NoteModifier;
using Krate::DSP::NoteValue;
using Krate::DSP::ProfundumMorphModulator;
using Krate::DSP::RetriggerMode;
using Krate::DSP::Waveform;
using MConfig = ProfundumMorphModulator::Config;
using Source = ProfundumMorphModulator::Source;

constexpr std::array<double, 3> kRates{48000.0, 44100.0, 96000.0};
constexpr std::array<Waveform, 6> kAllShapes{Waveform::Sine,   Waveform::Triangle,   Waveform::Sawtooth,
                                             Waveform::Square, Waveform::SampleHold, Waveform::SmoothRandom};
constexpr std::array<Source, 5> kAllSources{Source::Off, Source::Envelope, Source::Cycle, Source::Velocity,
                                            Source::ModWheel};
constexpr float kTraceTolerance = 1.0e-6f;

[[nodiscard]] std::uint32_t bits(float x) noexcept { return std::bit_cast<std::uint32_t>(x); }
[[nodiscard]] double controlRateOf(double fs) noexcept { return fs / 32.0; }

/// round(T * fc / 1000): a stage time in whole control intervals.
[[nodiscard]] long nominalIntervals(float ms, double fs) noexcept {
    return std::lround(static_cast<double>(ms) * controlRateOf(fs) / 1000.0);
}

[[nodiscard]] MConfig envConfig(float attackMs, float decayMs, float sustain) noexcept {
    MConfig c;
    c.source = Source::Envelope;
    c.amount = 1.0f;
    c.envAttackMs = attackMs;
    c.envDecayMs = decayMs;
    c.envSustain = sustain;
    return c;
}

[[nodiscard]] MConfig cycleConfig(Waveform shape, float rateHz) noexcept {
    MConfig c;
    c.source = Source::Cycle;
    c.amount = 1.0f;
    c.cycleShape = shape;
    c.cycleRateHz = rateHz;
    return c;
}

/// Synced Cycle at a Sync Rate dropdown index (kNoteValueDropdownMapping, core/note_value.h:136).
[[nodiscard]] MConfig syncedCycleConfig(Waveform shape, std::size_t dropdownIndex) noexcept {
    MConfig c = cycleConfig(shape, 1.0f);
    c.cycleSync = true;
    c.cycleNote = Krate::DSP::kNoteValueDropdownMapping[dropdownIndex].note;
    c.cycleModifier = Krate::DSP::kNoteValueDropdownMapping[dropdownIndex].modifier;
    return c;
}

/// A reference ADSREnvelope set up as plan 4.1 sets up the modulator's: prepared at fc, Hard retrigger, A / D / S.
void prepareReferenceEnvelope(ADSREnvelope& env, double fs, const MConfig& c) noexcept {
    env.prepare(static_cast<float>(controlRateOf(fs)));
    env.setRetriggerMode(RetriggerMode::Hard);
    env.setAttack(c.envAttackMs);
    env.setDecay(c.envDecayMs);
    env.setSustain(c.envSustain);
    env.reset();
}

/// The first process() of a freshly reset() + gate(true) reference envelope: the C-4 restart observable.
[[nodiscard]] float restartObservable(double fs, const MConfig& c) noexcept {
    ADSREnvelope ref;
    prepareReferenceEnvelope(ref, fs, c);
    ref.reset();
    ref.gate(true);
    return ref.process();
}

/// A reference LFO at fc with the modulator's setter calls in the modulator's order (plan 4.1): the tempo first
/// (LFO::setTempo only stores it while sync is off, lfo.h:246-252), then setWaveform, setFrequency, setTempoSync,
/// setNoteValue. `retriggerAt` (0-based intervals) calls retrigger() before that interval's process().
[[nodiscard]] std::vector<float> referenceLfoTrace(double fs, const MConfig& c, std::size_t n, float tempoBpm,
                                                   const std::vector<std::size_t>& retriggerAt = {}) {
    LFO ref;
    ref.prepare(controlRateOf(fs));
    ref.setTempo(tempoBpm);
    ref.setWaveform(c.cycleShape);
    ref.setFrequency(c.cycleRateHz);
    ref.setTempoSync(c.cycleSync);
    ref.setNoteValue(c.cycleNote, c.cycleModifier);
    std::vector<float> out(n);
    std::size_t next = 0;
    for (std::size_t k = 0; k < n; ++k) {
        while (next < retriggerAt.size() && retriggerAt[next] == k) {
            ref.retrigger();
            ++next;
        }
        out[k] = ref.process();
    }
    return out;
}

/// The modulator's s trace over n intervals: setTempoBpm(each of `tempi`, in order), setConfig, then
/// noteOn(velocities[i]) before the 0-based interval noteAt[i].
[[nodiscard]] std::vector<float> modulatorTrace(double fs, const MConfig& c, std::size_t n,
                                                std::initializer_list<float> tempi,
                                                const std::vector<std::size_t>& noteAt = {},
                                                const std::vector<std::uint8_t>& velocities = {}) {
    ProfundumMorphModulator m;
    m.prepare(fs);
    for (const float t : tempi) {
        m.setTempoBpm(t);
    }
    m.setConfig(c);
    std::vector<float> out(n);
    std::size_t next = 0;
    for (std::size_t k = 0; k < n; ++k) {
        while (next < noteAt.size() && noteAt[next] == k) {
            m.noteOn(velocities[next]);
            ++next;
        }
        out[k] = m.advanceInterval();
    }
    return out;
}

[[nodiscard]] float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    float worst = 0.0f;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t k = 0; k < n; ++k) {
        worst = std::max(worst, std::abs(a[k] - b[k]));
    }
    return worst;
}

[[nodiscard]] bool allInUnitRange(const std::vector<float>& t) noexcept {
    return std::all_of(t.begin(), t.end(), [](float v) { return v >= -1.0f && v <= 1.0f; });
}

/// Mean period between upward zero crossings, linear interpolation between grid points (plan 5.3).
struct PeriodReading {
    double seconds = 0.0;
    std::size_t cycles = 0;
};

[[nodiscard]] PeriodReading upwardCrossingPeriod(const std::vector<float>& trace, double fc) noexcept {
    double first = 0.0;
    double last = 0.0;
    std::size_t crossings = 0;
    for (std::size_t k = 1; k < trace.size(); ++k) {
        const double a = static_cast<double>(trace[k - 1]);
        const double b = static_cast<double>(trace[k]);
        if (a < 0.0 && b >= 0.0) {
            const double t = static_cast<double>(k - 1) + (-a) / (b - a);
            if (crossings == 0) {
                first = t;
            }
            last = t;
            ++crossings;
        }
    }
    PeriodReading r;
    if (crossings >= 2) {
        r.cycles = crossings - 1;
        r.seconds = (last - first) / static_cast<double>(r.cycles) / fc;
    }
    return r;
}

/// Intervals for `cycles` whole periods after the first upward crossing, plus margin.
[[nodiscard]] std::size_t intervalsFor(double periodSeconds, std::size_t cycles, double fc) noexcept {
    return static_cast<std::size_t>(std::ceil((static_cast<double>(cycles) + 1.5) * periodSeconds * fc));
}

/// SC-002 (e): restart at the first interval whose s exceeds 0.5 (A = 400 ms, Sustain 1); `noteOnCalls` noteOn(90)
/// calls before that one advanceInterval(). One function for both arms, so the two-call / one-call comparison runs
/// the same compiled code.
struct RestartReading {
    float before = 0.0f;
    float after = 0.0f;
    bool reached = false;
};

[[nodiscard]] RestartReading restartRun(double fs, int noteOnCalls) noexcept {
    ProfundumMorphModulator m;
    m.prepare(fs);
    m.setConfig(envConfig(400.0f, 300.0f, 1.0f));
    m.noteOn(100);
    RestartReading r;
    float s = 0.0f;
    for (long k = 0; k < 100000 && s <= 0.5f; ++k) {
        s = m.advanceInterval();
    }
    r.reached = s > 0.5f;
    r.before = s;
    for (int i = 0; i < noteOnCalls; ++i) {
        m.noteOn(90);
    }
    r.after = m.advanceInterval();
    return r;
}

#ifdef KRATE_TEST_FTZ_DAZ_X86
/// SC-002 (f): clears FTZ (MXCSR bit 15) and DAZ (bit 6) so the Sustain tail really goes subnormal; restores on exit.
class DenormalsOnGuard {
public:
    DenormalsOnGuard() noexcept : saved_(_mm_getcsr()) { _mm_setcsr(saved_ & ~(kFtzBit | kDazBit)); }
    ~DenormalsOnGuard() { _mm_setcsr(saved_); }
    DenormalsOnGuard(const DenormalsOnGuard&) = delete;
    DenormalsOnGuard& operator=(const DenormalsOnGuard&) = delete;

private:
    static constexpr unsigned int kFtzBit = 1u << 15;
    static constexpr unsigned int kDazBit = 1u << 6;
    unsigned int saved_;
};
#endif

} // namespace

// =============================================================================
// SC-001 (a), FR-006, FR-007, FR-008
// =============================================================================
TEST_CASE("ProfundumMorphModulator_ApiAndRt", "[systems][profundum][morphmod]") {
    SECTION("Config holds exactly the ten C-12 fields") {
        [[maybe_unused]] auto [cSource, cAmount, cAttack, cDecay, cSustain, cShape, cRate, cSync, cNote, cModifier] =
            ProfundumMorphModulator::Config{};
        STATIC_REQUIRE(std::is_same_v<decltype(cSource), Source>);
        STATIC_REQUIRE(std::is_same_v<decltype(cAmount), float>);
        STATIC_REQUIRE(std::is_same_v<decltype(cAttack), float>);
        STATIC_REQUIRE(std::is_same_v<decltype(cDecay), float>);
        STATIC_REQUIRE(std::is_same_v<decltype(cSustain), float>);
        STATIC_REQUIRE(std::is_same_v<decltype(cShape), Waveform>);
        STATIC_REQUIRE(std::is_same_v<decltype(cRate), float>);
        STATIC_REQUIRE(std::is_same_v<decltype(cSync), bool>);
        STATIC_REQUIRE(std::is_same_v<decltype(cNote), NoteValue>);
        STATIC_REQUIRE(std::is_same_v<decltype(cModifier), NoteModifier>);
        REQUIRE(cSource == Source::Off);
    }

    SECTION("Output is a ModulationSource with range {-1, 1}; the surface is noexcept") {
        STATIC_REQUIRE(std::is_base_of_v<ModulationSource, ProfundumMorphModulator::Output>);
        ProfundumMorphModulator m;
        m.prepare(48000.0);
        const auto range = m.output().getSourceRange();
        REQUIRE(range.first == -1.0f);
        REQUIRE(range.second == 1.0f);
        const MConfig cfg{};
        STATIC_REQUIRE(noexcept(m.reset()));
        STATIC_REQUIRE(noexcept(m.setConfig(cfg)));
        STATIC_REQUIRE(noexcept(m.setTempoBpm(120.0f)));
        STATIC_REQUIRE(noexcept(m.noteOn(std::uint8_t{64})));
        STATIC_REQUIRE(noexcept(m.setModWheel(0.5f)));
        STATIC_REQUIRE(noexcept(m.advanceInterval()));
        STATIC_REQUIRE(noexcept(m.sourceValue()));
        STATIC_REQUIRE(noexcept(m.offsetValue()));
        STATIC_REQUIRE(noexcept(m.output()));
        // Before the first note after prepare every source reads 0 (C-4, C-6).
        REQUIRE(bits(m.sourceValue()) == 0u);
        REQUIRE(bits(m.offsetValue()) == 0u);
        REQUIRE(bits(m.output().getCurrentValue()) == 0u);
    }

    SECTION("FR-007: getCurrentValue() is the offset, clamp(amount * s, -1, 1), for every source") {
        struct Arm {
            const char* name;
            MConfig cfg;
        };
        MConfig velocityCfg;
        velocityCfg.source = Source::Velocity;
        MConfig wheelCfg;
        wheelCfg.source = Source::ModWheel;
        const std::array<Arm, 4> arms{Arm{"Envelope A 400 ms (inside Attack)", envConfig(400.0f, 300.0f, 0.0f)},
                                      Arm{"Cycle Sine 1 Hz", cycleConfig(Waveform::Sine, 1.0f)},
                                      Arm{"Velocity after noteOn(64)", velocityCfg},
                                      Arm{"Mod Wheel 0.3", wheelCfg}};
        for (const auto& arm : arms) {
            for (const float amount : {-1.0f, 0.5f, 1.0f}) {
                CAPTURE(arm.name, amount);
                ProfundumMorphModulator m;
                m.prepare(48000.0);
                MConfig cfg = arm.cfg;
                cfg.amount = amount;
                m.setConfig(cfg);
                m.setModWheel(0.3f);
                m.noteOn(64);
                bool sameBits = true;
                float worst = 0.0f;
                float largestSource = 0.0f;
                // 300 intervals: 0.2 s, inside the 400 ms Attack (600 intervals at 1500 Hz).
                for (int k = 0; k < 300; ++k) {
                    const float returned = m.advanceInterval();
                    sameBits = sameBits && bits(returned) == bits(m.sourceValue());
                    sameBits = sameBits && bits(m.output().getCurrentValue()) == bits(m.offsetValue());
                    const float expected = std::clamp(amount * m.sourceValue(), -1.0f, 1.0f);
                    worst = std::max(worst, std::abs(m.output().getCurrentValue() - expected));
                    largestSource = std::max(largestSource, std::abs(m.sourceValue()));
                }
                REQUIRE(sameBits);
                REQUIRE(worst <= 1.0e-7f);
                REQUIRE(largestSource > 0.1f);   // non-vacuity: every source moved s
                if (cfg.source == Source::Velocity) {
                    REQUIRE(std::abs(m.sourceValue() - 64.0f / 127.0f) <= kTraceTolerance);
                }
                if (cfg.source == Source::ModWheel) {
                    REQUIRE(m.sourceValue() == 0.3f);
                }
            }
        }
    }

    SECTION("FR-005: velocity 127 reads exactly 1; velocity 0 is no note") {
        ProfundumMorphModulator m;
        m.prepare(48000.0);
        MConfig cfg;
        cfg.source = Source::Velocity;
        cfg.amount = 1.0f;
        m.setConfig(cfg);
        REQUIRE(bits(m.advanceInterval()) == 0u);   // 0 before the first note
        m.noteOn(127);
        REQUIRE(m.advanceInterval() == 1.0f);
        m.noteOn(0);
        REQUIRE(m.advanceInterval() == 1.0f);
        m.noteOn(1);
        REQUIRE(std::abs(m.advanceInterval() - 1.0f / 127.0f) <= kTraceTolerance);
    }

    SECTION("FR-006: the Mod Wheel is clamped to [0, 1]") {   // NaN arm: ProfundumMorphModulator_NonFinite
        ProfundumMorphModulator m;
        m.prepare(48000.0);
        MConfig cfg;
        cfg.source = Source::ModWheel;
        cfg.amount = 1.0f;
        m.setConfig(cfg);
        m.setModWheel(1.5f);
        REQUIRE(m.advanceInterval() == 1.0f);
        m.setModWheel(-0.2f);
        REQUIRE(m.advanceInterval() == 0.0f);
        m.setModWheel(0.4f);
        REQUIRE(m.advanceInterval() == 0.4f);
    }

    SECTION("setConfig sanitises against the applied config") {
        ProfundumMorphModulator m;
        m.prepare(48000.0);
        MConfig c = cycleConfig(Waveform::Triangle, 2.0f);
        c.amount = 0.5f;
        m.setConfig(c);
        REQUIRE(m.config().amount == 0.5f);
        REQUIRE(m.config().source == Source::Cycle);

        c.amount = 3.0f;
        m.setConfig(c);
        REQUIRE(m.config().amount == 1.0f);
        c.amount = -3.0f;
        m.setConfig(c);
        REQUIRE(m.config().amount == -1.0f);

        c.envAttackMs = 0.0f;
        m.setConfig(c);
        REQUIRE(m.config().envAttackMs == Krate::DSP::kMinEnvelopeTimeMs);
        c.envSustain = 2.0f;
        m.setConfig(c);
        REQUIRE(m.config().envSustain == 1.0f);

        c.cycleRateHz = 50.0f;
        m.setConfig(c);
        REQUIRE(m.config().cycleRateHz == Krate::DSP::kMaxFrequency);

        c.source = static_cast<Source>(7);
        m.setConfig(c);
        REQUIRE(m.config().source == Source::Cycle);
        c.source = Source::Cycle;
        c.cycleShape = static_cast<Waveform>(9);
        m.setConfig(c);
        REQUIRE(m.config().cycleShape == Waveform::Triangle);
        c.cycleShape = Waveform::Triangle;
        c.cycleNote = static_cast<NoteValue>(12);
        c.cycleModifier = static_cast<NoteModifier>(5);
        m.setConfig(c);
        REQUIRE(m.config().cycleNote == NoteValue::Quarter);
        REQUIRE(m.config().cycleModifier == NoteModifier::None);
    }

    SECTION("setTempoBpm: 0 and -1 keep the tempo; a valid tempo moves a synced Cycle") {   // NaN: _NonFinite
        constexpr double kFs = 48000.0;
        const double fc = controlRateOf(kFs);
        const MConfig synced = syncedCycleConfig(Waveform::Sine, 13);   // 1/4: one beat per cycle
        const std::size_t n = intervalsFor(60.0 / 90.0, 10, fc);
        const auto at90 = modulatorTrace(kFs, synced, n, {90.0f, 0.0f, -1.0f});
        const auto atZero = modulatorTrace(kFs, synced, n, {0.0f});
        const PeriodReading p90 = upwardCrossingPeriod(at90, fc);
        const PeriodReading p0 = upwardCrossingPeriod(atZero, fc);
        CAPTURE(p90.seconds, p0.seconds);
        REQUIRE(p90.cycles >= 10);
        REQUIRE(p0.cycles >= 10);
        REQUIRE(std::abs(p90.seconds - 60.0 / 90.0) <= 0.005 * (60.0 / 90.0));   // 90 kept through 0 / -1
        REQUIRE(std::abs(p0.seconds - 60.0 / 120.0) <= 0.005 * (60.0 / 120.0));  // 0 kept the default 120
        REQUIRE(std::abs(p90.seconds - p0.seconds) > 0.1);                       // non-vacuity
    }

    SECTION("RT: no allocation in setConfig, noteOn, setModWheel or advanceInterval, per source") {
        ProfundumMorphModulator m;
        m.prepare(48000.0);
        for (const Source src : kAllSources) {
            CAPTURE(static_cast<int>(src));
            float sink = 0.0f;
            std::size_t count = 0;
            {
                // AllocationScope latches its count in its destructor; read the live count inside the scope.
                [[maybe_unused]] const TestHelpers::AllocationScope scope;
                for (int i = 0; i < 10000; ++i) {
                    MConfig c;
                    c.source = src;
                    c.amount = (i % 2 == 0) ? 1.0f : -1.0f;
                    c.envAttackMs = 1.0f + static_cast<float>(i % 50);
                    c.envDecayMs = 10.0f + static_cast<float>(i % 40);
                    c.envSustain = static_cast<float>(i % 3) * 0.5f;
                    c.cycleShape = kAllShapes[static_cast<std::size_t>(i % 6)];
                    c.cycleRateHz = 0.5f + static_cast<float>(i % 7);
                    c.cycleSync = (i % 3) == 0;
                    m.setConfig(c);
                    m.noteOn(static_cast<std::uint8_t>(1 + (i % 127)));
                    m.setModWheel(static_cast<float>(i % 100) / 100.0f);
                    sink += m.advanceInterval();
                }
                count = TestHelpers::AllocationDetector::instance().getAllocationCount();
            }
            CAPTURE(sink);
            REQUIRE(count == 0);
        }
    }
}

// =============================================================================
// SC-002 Envelope at fc = fs / 32
// =============================================================================
TEST_CASE("ProfundumMorphModulator_Envelope", "[systems][profundum][morphmod]") {
    for (const double fs : kRates) {
        const double fc = controlRateOf(fs);
        CAPTURE(fs, fc);

        DYNAMIC_SECTION("(a) the trace matches a reference ADSREnvelope at fc within 1e-6, fs " << fs) {
            for (const MConfig& cfg : {envConfig(10.0f, 300.0f, 0.5f), envConfig(400.0f, 400.0f, 0.0f),
                                       envConfig(0.1f, 10.0f, 1.0f)}) {
                CAPTURE(cfg.envAttackMs, cfg.envDecayMs, cfg.envSustain);
                ProfundumMorphModulator m;
                m.prepare(fs);
                m.setConfig(cfg);
                m.noteOn(100);
                ADSREnvelope ref;
                prepareReferenceEnvelope(ref, fs, cfg);
                ref.reset();
                ref.gate(true);
                float worst = 0.0f;
                float largest = 0.0f;
                for (int k = 0; k < 3000; ++k) {
                    const float s = m.advanceInterval();
                    worst = std::max(worst, std::abs(s - ref.process()));
                    largest = std::max(largest, s);
                }
                REQUIRE(worst <= kTraceTolerance);
                REQUIRE(largest == 1.0f);   // non-vacuity: the contour reached its peak
            }
        }

        DYNAMIC_SECTION("(b) Attack: the peak lies within round(A * fc / 1000) +- 1 intervals of the note, fs " << fs) {
            for (const float attackMs : {0.1f, 10.0f, 400.0f, 10000.0f}) {
                const long nominal = nominalIntervals(attackMs, fs);
                ProfundumMorphModulator m;
                m.prepare(fs);
                m.setConfig(envConfig(attackMs, 300.0f, 0.0f));
                m.noteOn(100);
                long peakAt = 0;
                for (long k = 1; k <= nominal + 10; ++k) {
                    if (m.advanceInterval() == 1.0f) {
                        peakAt = k;
                        break;
                    }
                }
                CAPTURE(attackMs, nominal, peakAt);
                REQUIRE(peakAt > 0);
                REQUIRE(std::abs(peakAt - nominal) <= 1);
                if (attackMs == 0.1f) {
                    REQUIRE(peakAt == 1);   // the rate clamps to one interval: the first interval reads 1.0
                }
            }
        }

        DYNAMIC_SECTION("(b) Attack at the one-interval boundary: the envelope's own first step decides, fs " << fs) {
            // +-64 ulps of attackMs around one interval (1000 / fc ms). The modulator's interval 1 reads the peak
            // exactly when the restarted envelope's first step from 0 (a reference ADSREnvelope prepared the same
            // way) lands at or above kOneStepPeakFloor; otherwise it reads that first step. No recomputation of the
            // envelope's rate clamp is involved, so the two cannot disagree at the boundary under -ffast-math.
            const float boundaryMs = static_cast<float>(1000.0 / fc);
            float attackMs = boundaryMs;
            for (int i = 0; i < 64; ++i) {
                attackMs = std::nextafter(attackMs, 0.0f);
            }
            std::size_t peaked = 0;
            std::size_t stepped = 0;
            for (int i = 0; i <= 128; ++i, attackMs = std::nextafter(attackMs, 1.0e6f)) {
                const MConfig cfg = envConfig(attackMs, 300.0f, 0.0f);
                const float first = restartObservable(fs, cfg);
                ProfundumMorphModulator m;
                m.prepare(fs);
                m.setConfig(cfg);
                m.noteOn(100);
                const float s1 = m.advanceInterval();
                CAPTURE(i, attackMs, first, s1);
                if (first >= ProfundumMorphModulator::kOneStepPeakFloor) {
                    REQUIRE(s1 == 1.0f);
                    ++peaked;
                } else {
                    REQUIRE(std::abs(s1 - first) <= kTraceTolerance);
                    REQUIRE(s1 < 1.0f);
                    ++stepped;
                }
            }
            CAPTURE(peaked, stepped);
            REQUIRE(peaked >= 64);   // every attack below the boundary (rate clamped to one step) peaks in interval 1
            REQUIRE(stepped >= 1);   // non-vacuity: the sweep crosses the boundary
        }

        DYNAMIC_SECTION("(b') Decay, Sustain 0: peak to first s == 0 in round(D * fc / 1000) +- 1, fs " << fs) {
            for (const float decayMs : {10.0f, 400.0f, 10000.0f}) {
                const long nominal = nominalIntervals(decayMs, fs);
                ProfundumMorphModulator m;
                m.prepare(fs);
                m.setConfig(envConfig(0.1f, decayMs, 0.0f));
                m.noteOn(100);
                REQUIRE(m.advanceInterval() == 1.0f);   // peak at interval 1
                long zeroAfter = 0;
                for (long k = 1; k <= nominal + 10; ++k) {
                    if (m.advanceInterval() == 0.0f) {
                        zeroAfter = k;
                        break;
                    }
                }
                CAPTURE(decayMs, nominal, zeroAfter);
                REQUIRE(zeroAfter > 0);
                REQUIRE(std::abs(zeroAfter - nominal) <= 1);
            }
        }

        DYNAMIC_SECTION("(c) Sustain: the Decay end reads S exactly and s stays at S, fs " << fs) {
            for (const float sustain : {0.5f, 1.0f}) {
                CAPTURE(sustain);
                ProfundumMorphModulator m;
                m.prepare(fs);
                m.setConfig(envConfig(10.0f, 100.0f, sustain));
                m.noteOn(100);
                bool peaked = false;
                bool decayEnded = false;
                float worst = 0.0f;
                for (int k = 0; k < 4000; ++k) {
                    const float s = m.advanceInterval();
                    if (decayEnded) {
                        worst = std::max(worst, std::abs(s - sustain));
                    } else if (peaked && s <= sustain) {
                        // Decay ends where the output first reaches S; it is set to S (adsr_envelope.h:342-344).
                        REQUIRE(bits(s) == bits(sustain));
                        decayEnded = true;
                    } else if (s == 1.0f) {
                        peaked = true;
                    }
                }
                REQUIRE(decayEnded);
                REQUIRE(worst <= kTraceTolerance);
            }
            {
                const long from = nominalIntervals(10.0f, fs) + nominalIntervals(100.0f, fs) + 1;
                ProfundumMorphModulator m;
                m.prepare(fs);
                m.setConfig(envConfig(10.0f, 100.0f, 0.0f));
                m.noteOn(100);
                float worst = 0.0f;
                for (long k = 1; k <= from + 3000; ++k) {
                    const float s = m.advanceInterval();
                    if (k >= from) {
                        worst = std::max(worst, std::abs(s));
                    }
                }
                CAPTURE(from, worst);
                REQUIRE(worst <= 1.0e-3f);
            }
        }

        DYNAMIC_SECTION("(d) a Sustain step 0 -> 1 in the Sustain stage follows the 1 ms one-pole, fs " << fs) {
            long bound = 21;
            if (fs == 48000.0)
                bound = 11;
            else if (fs == 44100.0)
                bound = 10;
            const double cs = std::exp(-5000.0 / (5.0 * fc));
            ProfundumMorphModulator m;
            m.prepare(fs);
            m.setConfig(envConfig(0.1f, 10.0f, 0.0f));
            m.noteOn(100);
            float s = 1.0f;
            for (int k = 0; k < 200; ++k) {
                s = m.advanceInterval();
            }
            REQUIRE(s == 0.0f);   // resting in Sustain at 0
            m.setConfig(envConfig(0.1f, 10.0f, 1.0f));
            double expected = static_cast<double>(s);
            double worst = 0.0;
            long settledAt = 0;
            for (long k = 1; k <= 200; ++k) {
                s = m.advanceInterval();
                expected = 1.0 + cs * (expected - 1.0);
                worst = std::max(worst, std::abs(static_cast<double>(s) - expected));
                if (settledAt == 0 && std::abs(s - 1.0f) <= 1.0e-3f) {
                    settledAt = k;
                }
            }
            WARN("SC-002 (d) fs " << fs << ": Sustain step settles to 1e-3 in " << settledAt << " intervals (bound "
                                  << bound << ")");
            CAPTURE(settledAt, bound, worst);
            REQUIRE(settledAt > 0);
            REQUIRE(settledAt <= bound);
            REQUIRE(worst <= 1.0e-6);
        }

        DYNAMIC_SECTION("(e) a note-on restarts from 0; two note-ons in one interval are one restart, fs " << fs) {
            const RestartReading once = restartRun(fs, 1);
            const RestartReading twice = restartRun(fs, 2);
            const float observable = restartObservable(fs, envConfig(400.0f, 300.0f, 1.0f));
            CAPTURE(once.before, once.after, twice.after, observable);
            REQUIRE(once.reached);
            REQUIRE(std::abs(once.after - observable) <= kTraceTolerance);
            REQUIRE(once.after < once.before);
            REQUIRE(bits(twice.after) == bits(once.after));
        }

        DYNAMIC_SECTION("(f) a Sustain move to 0 while held reads +0 within the flush bound and stays +0, fs " << fs) {
#ifdef KRATE_TEST_FTZ_DAZ_X86
            const DenormalsOnGuard denormalsOn;
#endif
            const double cs = std::exp(-5000.0 / (5.0 * fc));
            const long bound = static_cast<long>(std::ceil(std::log(1.0e-15) / std::log(cs))) + 1;
            ProfundumMorphModulator m;
            m.prepare(fs);
            m.setConfig(envConfig(0.1f, 10.0f, 1.0f));
            m.noteOn(100);
            float s = 0.0f;
            for (int k = 0; k < 50; ++k) {
                s = m.advanceInterval();
            }
            REQUIRE(s == 1.0f);   // held in Sustain at 1
            m.setConfig(envConfig(0.1f, 10.0f, 0.0f));
            long zeroAt = 0;
            bool stayedZero = true;
            for (long k = 1; k <= bound + 1000; ++k) {
                s = m.advanceInterval();
                const bool plusZero = bits(s) == 0u && bits(m.offsetValue()) == 0u;
                if (zeroAt == 0) {
                    if (plusZero) {
                        zeroAt = k;
                    }
                } else {
                    stayedZero = stayedZero && plusZero;
                }
            }
            WARN("SC-002 (f) fs " << fs << ": Sustain tail reads +0 after " << zeroAt << " intervals (bound " << bound
                                  << ")");
            CAPTURE(zeroAt, bound);
            REQUIRE(zeroAt > 0);
            REQUIRE(zeroAt <= bound);
            REQUIRE(stayedZero);   // >= 1000 further intervals at +0 (zeroAt <= bound)
        }
    }
}

// =============================================================================
// SC-003 Cycle (modulator part)
// =============================================================================
TEST_CASE("ProfundumMorphModulator_Cycle", "[systems][profundum][morphmod]") {
    SECTION("(a)(c) every shape matches a reference LFO at fc within 1e-6 and stays in [-1, 1]") {
        constexpr std::size_t kN = 3000;
        for (const double fs : kRates) {
            for (const Waveform shape : kAllShapes) {
                for (const MConfig& cfg : {cycleConfig(shape, 2.0f), syncedCycleConfig(shape, 9)}) {
                    CAPTURE(fs, static_cast<int>(shape), cfg.cycleSync);
                    const auto trace = modulatorTrace(fs, cfg, kN, {120.0f});
                    const auto ref = referenceLfoTrace(fs, cfg, kN, 120.0f);
                    REQUIRE(maxAbsDiff(trace, ref) <= kTraceTolerance);
                    REQUIRE(allInUnitRange(trace));
                }
            }
        }
    }

    SECTION("(b) free rate: the zero-crossing period is within 0.5 % of 1 / f") {
        constexpr double kFs = 48000.0;
        const double fc = controlRateOf(kFs);
        for (const float hz : {0.01f, 0.1f, 1.0f, 10.0f, 20.0f}) {
            const std::size_t cycles = (hz == 0.01f) ? std::size_t{2} : std::size_t{10};
            const double expected = 1.0 / static_cast<double>(hz);
            const auto trace = modulatorTrace(kFs, cycleConfig(Waveform::Sine, hz), intervalsFor(expected, cycles, fc),
                                              {120.0f});
            const PeriodReading p = upwardCrossingPeriod(trace, fc);
            CAPTURE(hz, p.seconds, p.cycles, expected);
            REQUIRE(p.cycles >= cycles);
            REQUIRE(std::abs(p.seconds - expected) <= 0.005 * expected);
            REQUIRE(allInUnitRange(trace));
        }
    }

    SECTION("(b) synced: 1/4, 1/8T and 1/1 at 90 and 120 BPM within 0.5 % of beats * 60 / bpm") {
        constexpr double kFs = 48000.0;
        const double fc = controlRateOf(kFs);
        for (const float bpm : {90.0f, 120.0f}) {
            for (const std::size_t index : {std::size_t{13}, std::size_t{9}, std::size_t{19}}) {
                const auto mapping = Krate::DSP::kNoteValueDropdownMapping[index];
                const double beats = static_cast<double>(Krate::DSP::getBeatsForNote(mapping.note, mapping.modifier));
                const double expected = beats * 60.0 / static_cast<double>(bpm);
                const auto trace = modulatorTrace(kFs, syncedCycleConfig(Waveform::Sine, index),
                                                  intervalsFor(expected, 10, fc), {bpm});
                const PeriodReading p = upwardCrossingPeriod(trace, fc);
                CAPTURE(bpm, index, beats, p.seconds, p.cycles, expected);
                REQUIRE(p.cycles >= 10);
                REQUIRE(std::abs(p.seconds - expected) <= 0.005 * expected);
            }
        }
    }

    SECTION("(d) S&H and Smooth Random: two identical runs are bit-identical") {
        for (const Waveform shape : {Waveform::SampleHold, Waveform::SmoothRandom}) {
            CAPTURE(static_cast<int>(shape));
            const MConfig cfg = cycleConfig(shape, 7.0f);
            const auto first = modulatorTrace(48000.0, cfg, 6000, {120.0f});
            const auto second = modulatorTrace(48000.0, cfg, 6000, {120.0f});
            REQUIRE(first.size() == second.size());
            REQUIRE(std::memcmp(first.data(), second.data(), first.size() * sizeof(float)) == 0);
            // non-vacuity: the random shapes really move
            const auto [lo, hi] = std::minmax_element(first.begin(), first.end());
            REQUIRE(*hi - *lo > 0.1f);
        }
    }

    SECTION("(e) free-running: note-ons never re-phase the Cycle") {
        constexpr double kFs = 48000.0;
        constexpr std::size_t kN = 3000;
        const std::vector<std::size_t> noteAt{37, 41, 160, 161, 509, 777, 1200, 2047};
        const std::vector<std::uint8_t> velocities{1, 30, 64, 90, 100, 110, 120, 127};
        for (const Waveform shape : kAllShapes) {
            for (const MConfig& cfg : {cycleConfig(shape, 1.0f), syncedCycleConfig(shape, 13)}) {
                CAPTURE(static_cast<int>(shape), cfg.cycleSync);
                const auto trace = modulatorTrace(kFs, cfg, kN, {120.0f}, noteAt, velocities);
                const auto ref = referenceLfoTrace(kFs, cfg, kN, 120.0f);
                REQUIRE(maxAbsDiff(trace, ref) <= kTraceTolerance);
                REQUIRE(allInUnitRange(trace));
            }
        }
        // Non-vacuity: a retriggered reference at the same intervals differs by > 0.1 somewhere (Sine, 1 Hz).
        const MConfig sine = cycleConfig(Waveform::Sine, 1.0f);
        const auto freeRef = referenceLfoTrace(kFs, sine, kN, 120.0f);
        const auto retriggered = referenceLfoTrace(kFs, sine, kN, 120.0f, noteAt);
        REQUIRE(maxAbsDiff(freeRef, retriggered) > 0.1f);
    }
}
