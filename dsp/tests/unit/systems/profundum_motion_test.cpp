// ==============================================================================
// Profundum Phase 9 (specs/profundum-phase9-voice-engine) - profundum_motion_test.cpp
// ==============================================================================
// tasks.md T003 (FR-040 - FR-049, SC-026 unit arm; plan S3, S9.1, C-13, C-18). fs 48 kHz, prepare(48000.0), one
// advanceInterval() per 32 samples.
//   ProfundumMotion_Sources            each source alone moves sourceValue() in range; Off reads +0; the clocks run
//                                      whichever source is selected (bitwise vs a standalone LFO; Drift to kDriftMargin)
//   ProfundumMotion_PolarityAndScale   offset == v_pol * amount * depth * span bitwise, exactly one field non-zero;
//                                      the polarity maps vs standalone components; outputValue() == clamp(m)
//   ProfundumMotion_DepthZeroIsZero    depth 0: every offset field is +-0 for every source x target x polarity
//   ProfundumMotion_ConfigChangeOnly   setConfig every interval == setConfig once, bitwise over 1000 intervals
//   ProfundumMotion_ModulationSource   output() is a ModulationSource, range {-1, 1}, value == outputValue()
//   ProfundumMotion_TempoAndSeed       invalid tempi keep the previous; seeds differ / match; setSeed after prepare
//                                      waits for reset()
// Includes every Phase 9 header so each is compiled by every gate from Group 1 on (T001).
// ==============================================================================

#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>
#include <krate/dsp/systems/profundum_motion.h>
#include <krate/dsp/systems/profundum_voice.h>

#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/envelope_utils.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/processors/brownian_drift.h>
#include <krate/dsp/systems/morph_state.h>

#include <catch2/catch_test_macros.hpp>

#include <enable_ftz_daz.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <vector>

namespace {

using Krate::DSP::ProfundumMotion;
using Config = ProfundumMotion::Config;
using Source = Config::Source;
using Target = Config::Target;
using Polarity = Config::Polarity;

constexpr double kFs = 48000.0;
constexpr double kControlRate = kFs / 32.0;   // 1500 Hz

constexpr std::array<Source, 4> kAllSources{Source::Off, Source::Env, Source::Cycle, Source::Drift};
constexpr std::array<Source, 3> kActiveSources{Source::Env, Source::Cycle, Source::Drift};
constexpr std::array<Target, 5> kAllTargets{Target::Mass, Target::Depth, Target::Body, Target::Edge, Target::Shift};
constexpr std::array<Polarity, 2> kAllPolarities{Polarity::Bipolar, Polarity::Unipolar};

[[nodiscard]] std::uint32_t bits(float x) noexcept { return std::bit_cast<std::uint32_t>(x); }
[[nodiscard]] bool isPlusZero(float x) noexcept { return bits(x) == 0u; }
[[nodiscard]] bool isZero(float x) noexcept { return (bits(x) & 0x7FFFFFFFu) == 0u; }
/// Drift vs a standalone BrownianDrift: the two instances are separately inlined copies of the same float code
/// (OnePoleSmoother::advanceSamples, smoother.h:247-249: std::pow + a contractible multiply-add), which /fp:fast and
/// -ffast-math may round differently by an ulp (measured: 1 ulp at interval 19, 0.000833279). The map is checked to
/// this margin, not bitwise; the offset chain is still checked bitwise against the motion's own sourceValue().
constexpr float kDriftMargin = 1.0e-6f;
[[nodiscard]] bool driftClose(float a, float b) noexcept { return std::abs(a - b) <= kDriftMargin; }
/// Forces one float rounding: under /fp:fast + -ffast-math the test-side chain could otherwise be reassociated
/// with the constant span into a single multiply that the header never performs.
[[nodiscard]] float launder(float x) noexcept {
    volatile float v = x;
    return v;
}

[[nodiscard]] float spanOf(Target t) noexcept {
    switch (t) {
        case Target::Mass: return 50.0f;
        case Target::Depth:
        case Target::Body:
        case Target::Edge: return 0.5f;
        case Target::Shift: return 1.0f;
    }
    return 0.0f;
}

[[nodiscard]] std::array<float, 5> fieldsOf(const ProfundumMotion::Offsets& o) noexcept {
    return {o.mass, o.depth, o.body, o.edge, o.shift};
}

[[nodiscard]] std::unique_ptr<ProfundumMotion> makeMotion(std::uint32_t seed = ProfundumMotion::kDefaultSeed) {
    auto m = std::make_unique<ProfundumMotion>();
    m->setSeed(seed);
    m->prepare(kFs);
    return m;
}

/// A standalone LFO configured as ProfundumMotion configures CYCLE (C-13, C-18).
void configureLfo(Krate::DSP::LFO& lfo, const Config& c) {
    lfo.prepare(kControlRate);
    lfo.setWaveform(c.cycleShape);
    lfo.setFrequency(c.cycleRateHz);
    lfo.setTempoSync(c.cycleSync);
    lfo.setNoteValue(c.cycleNote, c.cycleModifier);
    lfo.setPhaseOffset(c.cyclePhaseDeg);
    lfo.setRetriggerEnabled(c.cycleRetrigger);
    lfo.setFadeInTime(c.cycleFadeInMs);
}

/// A standalone ADSREnvelope configured as ProfundumMotion configures ENV (C-13).
void configureEnv(Krate::DSP::ADSREnvelope& env, const Config& c) {
    env.prepare(static_cast<float>(kControlRate));
    env.setSustain(0.0f);
    env.setRetriggerMode(Krate::DSP::RetriggerMode::Hard);
    env.setAttack(c.envAttackMs);
    env.setDecay(c.envDecayMs);
    env.setRelease(c.envDecayMs);
}

/// A standalone BrownianDrift configured as ProfundumMotion configures DRIFT (plan S3.3).
void configureDrift(Krate::DSP::BrownianDrift& drift, const Config& c) {
    drift.prepare(kFs);
    drift.setDepth(1.0f);
    drift.setMean(0.0f);
    drift.setSeed(ProfundumMotion::kDefaultSeed);
    drift.reset();
    drift.setSmoothness(c.driftSmoothness);
}

[[nodiscard]] Config baseConfig(Source s, Target t, Polarity p, float amount, float depth) {
    Config c{};
    c.source = s;
    c.target = t;
    c.polarity = p;
    c.amount = amount;
    c.depth = depth;
    c.cycleRateHz = 2.0f;
    c.cycleShape = Krate::DSP::Waveform::Sine;
    c.cyclePhaseDeg = 90.0f;   // the first process() reads the sine's peak: a known v
    c.driftSmoothness = 0.5f;
    c.envAttackMs = 10.0f;
    c.envDecayMs = 200.0f;
    return c;
}

} // namespace

TEST_CASE("ProfundumMotion_Sources", "[systems][profundum][voice-engine][fr-040][fr-046]") {
    enableFTZDAZ();
    // 4000 intervals (2.67 s): a 0.5 Hz sine needs 1.5 s (2250 intervals) to reach its minimum, so the 2000 of
    // tasks.md would cap the Cycle minimum at -0.866 (run-log T003 deviation 1).
    constexpr int kIntervals = 4000;

    for (const Source s : kAllSources) {
        INFO("source " << static_cast<int>(s));
        Config c = baseConfig(s, Target::Mass, Polarity::Bipolar, 1.0f, 1.0f);
        c.cycleRateHz = 0.5f;
        c.cyclePhaseDeg = 0.0f;
        auto m = makeMotion();
        m->setConfig(c);
        if (s == Source::Env) {
            m->noteRestart();
        }
        float mx = -2.0f;
        float mn = 2.0f;
        float last = 0.0f;
        bool allPlusZero = true;
        bool inRange = true;
        for (int i = 0; i < kIntervals; ++i) {
            m->advanceInterval();
            const float v = m->sourceValue();
            allPlusZero = allPlusZero && isPlusZero(v);
            inRange = inRange && v >= -1.0f && v <= 1.0f;
            mx = std::max(mx, v);
            mn = std::min(mn, v);
            last = v;
        }
        CHECK(inRange);
        switch (s) {
            case Source::Off:
                CHECK(allPlusZero);
                break;
            case Source::Env:
                CHECK(mx >= 0.99f);
                CHECK(last == -1.0f);   // rests at e = 0, Bipolar 2e - 1 (plan S9.1 step 3)
                break;
            case Source::Cycle:
                CHECK(mx >= 0.99f);
                CHECK(mn <= -0.99f);
                break;
            case Source::Drift:
                CHECK(mx > mn);   // not constant
                break;
        }
    }

    SECTION("Clock independence (C-13): Off for 500 intervals, then Cycle / Drift read the standalone clock") {
        for (const Source s : {Source::Cycle, Source::Drift}) {
            INFO("switched-to source " << static_cast<int>(s));
            Config c = baseConfig(Source::Off, Target::Mass, Polarity::Bipolar, 1.0f, 1.0f);
            c.cycleRateHz = 0.5f;
            c.cyclePhaseDeg = 0.0f;
            auto m = makeMotion();
            m->setConfig(c);
            for (int i = 0; i < 500; ++i) {
                m->advanceInterval();
            }
            c.source = s;
            m->setConfig(c);
            m->advanceInterval();

            float expected = 0.0f;
            if (s == Source::Cycle) {
                Krate::DSP::LFO lfo;
                configureLfo(lfo, c);
                for (int i = 0; i < 501; ++i) {
                    expected = lfo.process();
                }
            } else {
                Krate::DSP::BrownianDrift drift;
                configureDrift(drift, c);
                for (int i = 0; i < 501; ++i) {
                    drift.processBlock(32);
                }
                expected = std::clamp(drift.getCurrentValue(), -1.0f, 1.0f);
            }
            if (s == Source::Cycle) {
                CHECK(bits(m->sourceValue()) == bits(expected));
            } else {
                CHECK(driftClose(m->sourceValue(), expected));
            }
        }
    }
}

TEST_CASE("ProfundumMotion_PolarityAndScale", "[systems][profundum][voice-engine][fr-042]") {
    enableFTZDAZ();
    constexpr int kIntervals = 600;
    constexpr float kAmount = -0.75f;
    constexpr float kDepth = 0.5f;

    for (const Source s : kActiveSources) {
        for (const Polarity p : kAllPolarities) {
            for (const Target t : kAllTargets) {
                INFO("source " << static_cast<int>(s) << " polarity " << static_cast<int>(p) << " target "
                               << static_cast<int>(t));
                const Config c = baseConfig(s, t, p, kAmount, kDepth);
                auto m = makeMotion();
                m->setConfig(c);
                m->noteRestart();

                Krate::DSP::LFO lfo;
                configureLfo(lfo, c);
                lfo.retrigger();
                Krate::DSP::ADSREnvelope env;
                configureEnv(env, c);
                env.gate(true);
                Krate::DSP::BrownianDrift drift;
                configureDrift(drift, c);

                const auto tIdx = static_cast<std::size_t>(t);
                const float span = spanOf(t);
                bool mapOk = true;
                bool offsetOk = true;
                bool othersOk = true;
                bool outputOk = true;
                bool sawNonZero = false;
                for (int i = 0; i < kIntervals; ++i) {
                    const auto& o = m->advanceInterval();
                    const float e = env.process();
                    const float l = lfo.process();
                    drift.processBlock(32);
                    const float d = std::clamp(drift.getCurrentValue(), -1.0f, 1.0f);
                    float raw = d;
                    if (s == Source::Env)
                        raw = e;
                    else if (s == Source::Cycle)
                        raw = l;
                    float vExpected = 0.0f;
                    if (s == Source::Env) {
                        vExpected = p == Polarity::Bipolar ? (2.0f * raw) - 1.0f : raw;
                    } else {
                        vExpected = p == Polarity::Unipolar ? (raw + 1.0f) / 2.0f : raw;
                    }
                    const float v = m->sourceValue();
                    mapOk = mapOk && (s == Source::Drift ? driftClose(v, vExpected) : bits(v) == bits(vExpected));

                    const float mVal = (v * kAmount) * kDepth;
                    const float expected = launder(mVal) * span;   // ((v * amount) * depth) * span, left to right
                    const auto f = fieldsOf(o);
                    offsetOk = offsetOk && bits(f[tIdx]) == bits(expected);
                    for (std::size_t k = 0; k < f.size(); ++k) {
                        if (k != tIdx) {
                            othersOk = othersOk && isPlusZero(f[k]);
                        }
                    }
                    sawNonZero = sawNonZero || !isZero(f[tIdx]);
                    outputOk = outputOk && bits(m->outputValue()) == bits(std::clamp(mVal, -1.0f, 1.0f));
                }
                CHECK(mapOk);
                CHECK(offsetOk);
                CHECK(othersOk);
                CHECK(outputOk);
                CHECK(sawNonZero);
            }
        }
    }

    SECTION("Known first value: Bipolar Cycle at 90 degrees reads the peak, offset = peak * amount * depth * span") {
        const Config c = baseConfig(Source::Cycle, Target::Mass, Polarity::Bipolar, 1.0f, 1.0f);
        auto m = makeMotion();
        m->setConfig(c);
        const auto& o = m->advanceInterval();
        CHECK(m->sourceValue() >= 0.999f);
        CHECK(bits(o.mass) == bits(((m->sourceValue() * 1.0f) * 1.0f) * 50.0f));
        CHECK(o.mass >= 49.95f);
    }
}

TEST_CASE("ProfundumMotion_DepthZeroIsZero", "[systems][profundum][voice-engine][sc-026][fr-042]") {
    enableFTZDAZ();
    for (const Source s : kAllSources) {
        for (const Target t : kAllTargets) {
            for (const Polarity p : kAllPolarities) {
                INFO("source " << static_cast<int>(s) << " target " << static_cast<int>(t) << " polarity "
                               << static_cast<int>(p));
                const Config c = baseConfig(s, t, p, 1.0f, 0.0f);
                auto m = makeMotion();
                m->setConfig(c);
                m->noteRestart();
                bool allZero = true;
                for (int i = 0; i < 500; ++i) {
                    const auto& o = m->advanceInterval();
                    for (const float f : fieldsOf(o)) {
                        allZero = allZero && isZero(f);
                    }
                    allZero = allZero && isZero(m->outputValue());
                }
                CHECK(allZero);
            }
        }
    }
}

TEST_CASE("ProfundumMotion_ConfigChangeOnly", "[systems][profundum][voice-engine][fr-046][fr-054]") {
    enableFTZDAZ();
    for (const Source s : kActiveSources) {
        INFO("source " << static_cast<int>(s));
        Config c = baseConfig(s, Target::Depth, Polarity::Bipolar, 0.8f, 0.9f);
        c.cycleRetrigger = true;
        c.cycleFadeInMs = 200.0f;
        auto a = makeMotion();
        auto b = makeMotion();
        b->setConfig(c);
        bool equal = true;
        for (int i = 0; i < 1000; ++i) {
            a->setConfig(c);
            if (i == 0 || i == 400) {
                a->noteRestart();
                b->noteRestart();
            }
            const auto& oa = a->advanceInterval();
            const auto& ob = b->advanceInterval();
            equal = equal && bits(a->sourceValue()) == bits(b->sourceValue());
            equal = equal && bits(a->outputValue()) == bits(b->outputValue());
            const auto fa = fieldsOf(oa);
            const auto fb = fieldsOf(ob);
            for (std::size_t k = 0; k < fa.size(); ++k) {
                equal = equal && bits(fa[k]) == bits(fb[k]);
            }
        }
        CHECK(equal);
    }
}

TEST_CASE("ProfundumMotion_ModulationSource", "[systems][profundum][voice-engine][fr-048]") {
    enableFTZDAZ();
    static_assert(std::is_base_of_v<Krate::DSP::ModulationSource, ProfundumMotion::Output>);
    const Config c = baseConfig(Source::Cycle, Target::Edge, Polarity::Bipolar, 1.0f, 1.0f);
    auto motion = makeMotion();
    motion->setConfig(c);
    const Krate::DSP::ModulationSource& src = motion->output();
    const auto range = src.getSourceRange();
    CHECK(range.first == -1.0f);
    CHECK(range.second == 1.0f);
    bool equal = true;
    for (int i = 0; i < 100; ++i) {
        motion->advanceInterval();
        equal = equal && bits(src.getCurrentValue()) == bits(motion->outputValue());
    }
    CHECK(equal);
}

TEST_CASE("ProfundumMotion_TempoAndSeed", "[systems][profundum][voice-engine][fr-028][fr-032]") {
    enableFTZDAZ();

    SECTION("Non-finite / zero / negative tempo keeps the previous: a synced Cycle is unchanged") {
        Config c = baseConfig(Source::Cycle, Target::Mass, Polarity::Bipolar, 1.0f, 1.0f);
        c.cycleShape = Krate::DSP::Waveform::Sawtooth;
        c.cycleSync = true;
        c.cycleNote = Krate::DSP::NoteValue::Quarter;
        c.cyclePhaseDeg = 0.0f;
        auto ref = makeMotion();
        auto bad = makeMotion();
        auto slow = makeMotion();
        ref->setConfig(c);
        bad->setConfig(c);
        slow->setConfig(c);
        bad->setTempoBpm(std::numeric_limits<float>::quiet_NaN());
        bad->setTempoBpm(0.0f);
        bad->setTempoBpm(-10.0f);
        bad->setTempoBpm(std::numeric_limits<float>::infinity());
        slow->setTempoBpm(60.0f);   // positive control: a valid tempo moves the period
        bool same = true;
        bool slowDiffers = false;
        for (int i = 0; i < 2000; ++i) {
            ref->advanceInterval();
            bad->advanceInterval();
            slow->advanceInterval();
            same = same && bits(ref->sourceValue()) == bits(bad->sourceValue());
            slowDiffers = slowDiffers || bits(ref->sourceValue()) != bits(slow->sourceValue());
        }
        CHECK(same);
        CHECK(slowDiffers);
    }

    SECTION("Seeds: different seeds differ, equal seeds match, setSeed after prepare waits for reset()") {
        const Config c = baseConfig(Source::Drift, Target::Shift, Polarity::Bipolar, 1.0f, 1.0f);
        auto run = [&c](ProfundumMotion& m, int n) {
            std::vector<std::uint32_t> out;
            out.reserve(static_cast<std::size_t>(n));
            m.setConfig(c);
            for (int i = 0; i < n; ++i) {
                m.advanceInterval();
                out.push_back(bits(m.sourceValue()));
            }
            return out;
        };
        auto s1 = makeMotion(1u);
        auto s1b = makeMotion(1u);
        auto s2 = makeMotion(2u);
        const auto v1 = run(*s1, 500);
        const auto v1b = run(*s1b, 500);
        const auto v2 = run(*s2, 500);
        CHECK(v1 == v1b);
        CHECK(v1 != v2);

        auto late = makeMotion(1u);
        late->setSeed(2u);   // after prepare: nothing changes until reset()
        const auto vLate = run(*late, 500);
        CHECK(vLate == v1);

        (*late).reset();
        (*s2).reset();
        const auto vLateReset = run(*late, 500);
        const auto v2Reset = run(*s2, 500);
        CHECK(vLateReset == v2Reset);
        CHECK(vLateReset == v2);
        CHECK(vLateReset != v1);
    }
}
