// ==============================================================================
// Vorago Phase 12 - DSP parameter-surface tests (SC-006, SC-019, SC-021, SC-022, SC-023 (1), FR-004)
// ==============================================================================
// Registered by T002 (specs/vorago-phase12-parameters/tasks.md) so no later task
// edits CMake. Filled by T003 (SC-006 / FR-001 / FR-002), T007 (spec B-1 / B-2:
// the ResonanceOctaveLock and OutputDriveDb macro targets), T008-T012.
//
// This TU is compiled with -fno-fast-math -fno-finite-math-only
// (dsp/tests/CMakeLists.txt) because it injects NaN/Inf. Every non-finite probe
// is still built from its BIT PATTERN through a volatile, never from
// std::numeric_limits, and nothing here calls std::isnan.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/modulation_curves.h>
#include <krate/dsp/processors/tape_saturator.h>
#include <krate/dsp/systems/resonance_drift_network.h>
#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>

#include <vorago_fixtures.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ios>
#include <memory>
#include <random>
#include <type_traits>
#include <vector>

using Krate::DSP::applyModCurve;
using Krate::DSP::ResonanceDriftNetwork;
using Krate::DSP::TapeSaturator;
using Krate::DSP::VoragoCavernTargets;
using Krate::DSP::VoragoEngine;
using Krate::DSP::VoragoEngineConfig;
using Krate::DSP::VoragoMacro;
using Krate::DSP::VoragoMacroMatrix;
using Krate::DSP::VoragoMacroTarget;
using Krate::DSP::VoragoMacroValues;
using Krate::DSP::VoragoVoice;
using Krate::DSP::TestUtils::Vorago::makeEngine;

namespace {

constexpr double kSurfaceSampleRate = 48000.0;

/// Build a non-finite float from its bit pattern through a volatile sink.
/// 0x7FC00000 = quiet NaN, 0x7F800000 = +Inf, 0xFF800000 = -Inf.
[[nodiscard]] float surfaceNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t b = bits;       // defeats constant folding
    const std::uint32_t materialized = b;  // the volatile READ is the sink
    float f = 0.0f;
    std::memcpy(&f, &materialized, sizeof(f));
    return f;
}

/// Macro -> VoragoMacroValues field, stated independently of the matrix.
[[nodiscard]] float surfaceMacroField(const VoragoMacroValues& v, VoragoMacro m) noexcept {
    switch (m) {
        case VoragoMacro::Darkness:
            return v.darkness;
        case VoragoMacro::Age:
            return v.age;
        case VoragoMacro::Density:
            return v.density;
        case VoragoMacro::Movement:
            return v.movement;
        case VoragoMacro::Gravity:
            return v.gravity;
        case VoragoMacro::Entropy:
            return v.entropy;
        case VoragoMacro::Pressure:
            return v.pressure;
        case VoragoMacro::Weight:
            return v.weight;
        case VoragoMacro::Fog:
            return v.fog;
        case VoragoMacro::Life:
            return v.life;
        case VoragoMacro::Depth:
            return v.depth;
        case VoragoMacro::Mass:
            return v.mass;
        case VoragoMacro::Count:
        default:
            return 0.0f;
    }
}

/// The Voice-owned targets read back through the voice's own getters.
[[nodiscard]] float surfaceReadVoiceTarget(const VoragoVoice& v, VoragoMacroTarget t) noexcept {
    switch (t) {
        case VoragoMacroTarget::CloudRichness:
            return v.getRichness();
        case VoragoMacroTarget::CloudSpectralTiltDb:
            return v.getSpectralTiltDb();
        case VoragoMacroTarget::CloudMutation:
            return v.getMutation();
        case VoragoMacroTarget::CloudInharmonicity:
            return v.getInharmonicity();
        case VoragoMacroTarget::CloudDriftDepthCents:
            return v.getDriftDepthCents();
        case VoragoMacroTarget::NoiseLevelDb:
            return v.getNoiseLevelDb();
        case VoragoMacroTarget::NoiseWakeBase:
            return v.getNoiseWakeBase();
        case VoragoMacroTarget::NoiseWanderRate:
            return v.getNoiseWanderRate();
        case VoragoMacroTarget::ResonanceGravity:
            return v.getResonanceGravity();
        case VoragoMacroTarget::ResonanceMix:
            return v.getResonanceMix();
        case VoragoMacroTarget::ResonanceWanderRate:
            return v.getResonanceWanderRate();
        case VoragoMacroTarget::EcologyMix:
            return v.getEcologyMix();
        case VoragoMacroTarget::EcologyLoopGain:
            return v.getEcologyLoopGain();
        case VoragoMacroTarget::BodyBlend:
            return v.getBodyBlend();
        case VoragoMacroTarget::BodyDamping:
            return v.getBodyDamping();
        case VoragoMacroTarget::BodyResonance:
            return v.getBodyResonance();
        case VoragoMacroTarget::BodyMix:
            return v.getBodyMix();
        case VoragoMacroTarget::EcosystemDepth:
            return v.getEcosystemDepth();
        case VoragoMacroTarget::EventRateScale:
            return v.getEventRateScale();
        case VoragoMacroTarget::BloomDepth:
            return v.getBloomDepth();
        case VoragoMacroTarget::BloomSpawnRateHz:
            return v.getBloomSpawnRateHz();
        case VoragoMacroTarget::BreathingDepth:
            return v.getBreathingDepth();
        case VoragoMacroTarget::BreathingIrregularity:
            return v.getBreathingIrregularity();
        case VoragoMacroTarget::TidalDepth:
            return v.getTidalDepth();
        case VoragoMacroTarget::ResonanceOctaveLock:
            return v.getResonanceOctaveLock();
        default:
            return 0.0f;
    }
}

/// The Engine-owned targets read back through the engine's getters.
[[nodiscard]] float surfaceReadEngineTarget(const VoragoEngine& e, VoragoMacroTarget t) noexcept {
    switch (t) {
        case VoragoMacroTarget::SubToneLevelOffsetDb:
            return e.getSubToneLevelOffsetDb();
        case VoragoMacroTarget::SubTrackingAmount:
            return e.getSubTrackingAmount();
        case VoragoMacroTarget::SmearAmount:
            return e.getSmearAmount();
        case VoragoMacroTarget::SmearDecoherence:
            return e.getSmearDecoherence();
        case VoragoMacroTarget::SmearTilt:
            return e.getSmearTilt();
        case VoragoMacroTarget::GhostPeakLevel:
            return e.getGhostPeakLevel();
        case VoragoMacroTarget::AtmosBlur:
            return e.getAtmosBlur();
        case VoragoMacroTarget::OutputSaturation:
            return e.getOutputSaturation();
        case VoragoMacroTarget::OutputDriveDb:
            return e.getOutputDriveDb();
        default:
            return 0.0f;
    }
}

/// The first kRows base on @p t (everyRowSharesOneBasePerTarget makes it THE base).
[[nodiscard]] float surfaceLiteralBase(VoragoMacroTarget t) noexcept {
    for (const auto& row : VoragoMacroMatrix::kRows) {
        if (row.target == t) {
            return row.base;
        }
    }
    return 0.0f;
}

/// clamp(override + sum of every kRows contribution on @p target), computed
/// INDEPENDENTLY of the matrix's evaluateAll(): seeded once, accumulated in
/// table order, Gravity in its bipolar amount * curve(|g|) * sign(g) form.
[[nodiscard]] float surfaceExpected(VoragoMacroTarget target, float overrideBase,
                                    const VoragoMacroValues& mv, float lo, float hi) noexcept {
    float acc = overrideBase;
    for (const auto& row : VoragoMacroMatrix::kRows) {
        if (row.target != target) {
            continue;
        }
        const float m = surfaceMacroField(mv, row.macro);
        if (row.macro == VoragoMacro::Gravity) {
            const float g = (m - 0.5f) * 2.0f;
            const float sign = (g < 0.0f) ? -1.0f : 1.0f;
            acc += row.amount * applyModCurve(row.curve, std::fabs(g)) * sign;
        } else {
            acc += row.amount * applyModCurve(row.curve, m);
        }
    }
    return std::clamp(acc, lo, hi);
}

void surfaceSetField(VoragoMacroValues& v, VoragoMacro m, float value) noexcept {
    switch (m) {
        case VoragoMacro::Darkness:
            v.darkness = value;
            break;
        case VoragoMacro::Age:
            v.age = value;
            break;
        case VoragoMacro::Density:
            v.density = value;
            break;
        case VoragoMacro::Movement:
            v.movement = value;
            break;
        case VoragoMacro::Gravity:
            v.gravity = value;
            break;
        case VoragoMacro::Entropy:
            v.entropy = value;
            break;
        case VoragoMacro::Pressure:
            v.pressure = value;
            break;
        case VoragoMacro::Weight:
            v.weight = value;
            break;
        case VoragoMacro::Fog:
            v.fog = value;
            break;
        case VoragoMacro::Life:
            v.life = value;
            break;
        case VoragoMacro::Depth:
            v.depth = value;
            break;
        case VoragoMacro::Mass:
            v.mass = value;
            break;
        case VoragoMacro::Count:
        default:
            break;
    }
}

[[nodiscard]] float surfaceCavernField(const VoragoCavernTargets& c, VoragoMacroTarget t) noexcept {
    switch (t) {
        case VoragoMacroTarget::CavernSize:
            return c.size;
        case VoragoMacroTarget::CavernDarkness:
            return c.darkness;
        case VoragoMacroTarget::CavernDecaySeconds:
            return c.decaySeconds;
        case VoragoMacroTarget::CavernFog:
            return c.fog;
        case VoragoMacroTarget::CavernDamperDepth:
            return c.damperDepth;
        case VoragoMacroTarget::CavernMix:
            return c.mix;
        case VoragoMacroTarget::CavernWidth:
            return c.width;
        default:
            return 0.0f;
    }
}

}  // namespace

// =============================================================================
// SC-006 / FR-001 / FR-002 - the per-target base override
// =============================================================================

TEST_CASE("VoragoMacro_TargetBaseOverride", "[systems][vorago]") {
    const VoragoEngineConfig cfg{};

    SECTION("NoOverrideIsBitIdentical") {
        std::mt19937 rng{12012u};
        std::uniform_real_distribution<float> uni(0.0f, 1.0f);

        VoragoMacroMatrix a;
        a.setTargetBase(VoragoMacroTarget::CloudRichness, 0.3f);
        a.resetTargetBases();
        VoragoMacroMatrix b;  // never touched by setTargetBase

        auto e1 = makeEngine(kSurfaceSampleRate, cfg);
        auto e2 = makeEngine(kSurfaceSampleRate, cfg);
        REQUIRE(e1->isPrepared());
        REQUIRE(e2->isPrepared());
        REQUIRE(e1->getPolyphony() == e2->getPolyphony());

        for (int vec = 0; vec < 8; ++vec) {
            VoragoMacroValues v{};
            v.darkness = uni(rng);
            v.age = uni(rng);
            v.density = uni(rng);
            v.movement = uni(rng);
            v.gravity = uni(rng);
            v.entropy = uni(rng);
            v.pressure = uni(rng);
            v.weight = uni(rng);
            v.fog = uni(rng);
            v.life = uni(rng);
            v.depth = uni(rng);
            v.mass = uni(rng);

            a.setMacros(v);
            a.apply(*e1);
            b.setMacros(v);
            b.apply(*e2);

            for (std::size_t t = 0; t < VoragoMacroMatrix::kFirstEngineTarget; ++t) {
                const auto target = static_cast<VoragoMacroTarget>(t);
                for (std::size_t i = 0; i < e1->getPolyphony(); ++i) {
                    INFO("vector " << vec << "  voice " << i << "  target " << t);
                    REQUIRE(surfaceReadVoiceTarget(e1->getVoice(i), target)
                            == surfaceReadVoiceTarget(e2->getVoice(i), target));
                }
            }
            for (std::size_t t = VoragoMacroMatrix::kFirstEngineTarget;
                 t < VoragoMacroMatrix::kFirstCavernTarget; ++t) {
                const auto target = static_cast<VoragoMacroTarget>(t);
                INFO("vector " << vec << "  engine target " << t);
                REQUIRE(surfaceReadEngineTarget(*e1, target) == surfaceReadEngineTarget(*e2, target));
            }

            const VoragoCavernTargets ca = a.computeCavernTargets();
            const VoragoCavernTargets cb = b.computeCavernTargets();
            INFO("vector " << vec);
            REQUIRE(ca.size == cb.size);
            REQUIRE(ca.darkness == cb.darkness);
            REQUIRE(ca.decaySeconds == cb.decaySeconds);
            REQUIRE(ca.fog == cb.fog);
            REQUIRE(ca.damperDepth == cb.damperDepth);
            REQUIRE(ca.mix == cb.mix);
            REQUIRE(ca.width == cb.width);
        }
    }

    SECTION("OverrideComposesWithMacro") {
        struct Case {
            VoragoMacroTarget target;
            float overrideBase;
            VoragoMacro macro;
            float lo;  ///< the owner's documented clamp range
            float hi;
        };
        // Clamp ranges: HarmonicCloud::setRichness [0, 1] (harmonic_cloud.h:416);
        // NoiseOrganism::setSourceLevel [-96, 12] dB (noise_organism.h:493);
        // VoragoEngine::setOutputSaturation [0, 1] (vorago_engine.h:854);
        // the cavern POD carries the RAW sum (vorago_macro_matrix.h computeCavernTargets).
        // VoragoEngine::setOutputDriveDb [TapeSaturator::kMinDriveDb, kMaxDriveDb]
        // (spec B-2, T007).
        const std::array<Case, 5> cases = {{
            {VoragoMacroTarget::CloudRichness, 0.45f, VoragoMacro::Density, 0.0f, 1.0f},
            {VoragoMacroTarget::NoiseLevelDb, -24.0f, VoragoMacro::Density, -96.0f, 12.0f},
            {VoragoMacroTarget::CavernSize, 0.30f, VoragoMacro::Depth, -1.0e30f, 1.0e30f},
            {VoragoMacroTarget::OutputSaturation, 0.05f, VoragoMacro::Pressure, 0.0f, 1.0f},
            {VoragoMacroTarget::OutputDriveDb, 2.0f, VoragoMacro::Pressure,
             TapeSaturator::kMinDriveDb, TapeSaturator::kMaxDriveDb},
        }};

        auto engine = makeEngine(kSurfaceSampleRate, cfg);
        REQUIRE(engine->isPrepared());

        for (const Case& c : cases) {
            VoragoMacroMatrix matrix;
            matrix.setTargetBase(c.target, c.overrideBase);
            REQUIRE(matrix.getTargetBase(c.target) == c.overrideBase);

            float previous = 0.0f;
            for (int k = 0; k <= 10; ++k) {
                const float m = static_cast<float>(k) / 10.0f;
                VoragoMacroValues mv{};  // every other macro at its neutral
                surfaceSetField(mv, c.macro, m);
                matrix.setMacros(mv);
                matrix.apply(*engine);

                float got = 0.0f;
                switch (VoragoMacroMatrix::ownerOfTarget(c.target)) {
                    case Krate::DSP::VoragoMacroTargetOwner::Voice:
                        got = surfaceReadVoiceTarget(engine->getVoice(0), c.target);
                        break;
                    case Krate::DSP::VoragoMacroTargetOwner::Engine:
                        got = surfaceReadEngineTarget(*engine, c.target);
                        break;
                    case Krate::DSP::VoragoMacroTargetOwner::Cavern:
                    default:
                        got = surfaceCavernField(matrix.computeCavernTargets(), c.target);
                        break;
                }
                const float expected = surfaceExpected(c.target, c.overrideBase, mv, c.lo, c.hi);
                INFO("target " << static_cast<int>(c.target) << "  macro "
                               << static_cast<int>(c.macro) << "  k " << k << "  got " << got
                               << "  expected " << expected);
                REQUIRE(got == Catch::Approx(expected).epsilon(1e-6));
                if (k > 0) {
                    REQUIRE(got > previous);
                }
                previous = got;
            }
        }
    }

    SECTION("OctaveLockOverrideComposesWithBipolarGravity") {
        // spec B-1 (T007). Gravity is bipolar, so the ResonanceOctaveLock row
        // contributes -1 at air, 0 at neutral, +1 at stone; the network setter
        // clamps the sum to [0, 1] (the air half is inert).
        auto engine = makeEngine(kSurfaceSampleRate, cfg);
        REQUIRE(engine->isPrepared());

        VoragoMacroMatrix matrix;
        constexpr float kLockBase = 0.25f;
        matrix.setTargetBase(VoragoMacroTarget::ResonanceOctaveLock, kLockBase);
        REQUIRE(matrix.getTargetBase(VoragoMacroTarget::ResonanceOctaveLock) == kLockBase);

        float previous = -1.0f;
        for (int k = 0; k <= 10; ++k) {
            VoragoMacroValues mv{};
            mv.gravity = static_cast<float>(k) / 10.0f;
            matrix.setMacros(mv);
            matrix.apply(*engine);
            for (std::size_t i = 0; i < engine->getPolyphony(); ++i) {
                const float got = engine->getVoice(i).getResonanceOctaveLock();
                const float expected = surfaceExpected(VoragoMacroTarget::ResonanceOctaveLock,
                                                       kLockBase, mv, 0.0f, 1.0f);
                INFO("k " << k << "  voice " << i << "  got " << got << "  expected "
                          << expected);
                REQUIRE(got == Catch::Approx(expected).margin(1e-6));
                REQUIRE(got >= previous);
            }
            previous = engine->getVoice(0).getResonanceOctaveLock();
        }
        REQUIRE(engine->getVoice(0).getResonanceOctaveLock() == 1.0f);  // stone end

        {
            VoragoMacroValues mv{};  // Gravity 0.5 -> contribution exactly 0
            matrix.setMacros(mv);
            matrix.apply(*engine);
            REQUIRE(engine->getVoice(0).getResonanceOctaveLock() == kLockBase);
            mv.gravity = 0.0f;  // air end: 0.25 - 1 clamps to 0
            matrix.setMacros(mv);
            matrix.apply(*engine);
            REQUIRE(engine->getVoice(0).getResonanceOctaveLock() == 0.0f);
        }
    }

    SECTION("OverrideAtTravelClampSaturates") {
        auto engine = makeEngine(kSurfaceSampleRate, cfg);
        REQUIRE(engine->isPrepared());

        VoragoMacroMatrix matrix;
        matrix.setTargetBase(VoragoMacroTarget::CloudRichness, 1.0f);
        REQUIRE(matrix.getTargetBase(VoragoMacroTarget::CloudRichness) == 1.0f);

        float previous = -1.0f;
        for (int k = 0; k <= 10; ++k) {
            matrix.setMacro(VoragoMacro::Density, static_cast<float>(k) / 10.0f);
            matrix.apply(*engine);
            const float got = engine->getVoice(0).getRichness();
            INFO("k " << k << "  richness " << got);
            REQUIRE(got >= previous);
            REQUIRE(got <= 1.0f);
            previous = got;
        }
        REQUIRE(matrix.getTargetBase(VoragoMacroTarget::CloudRichness) == 1.0f);
    }

    SECTION("RejectsNonFiniteAndOutOfRange") {
        VoragoMacroMatrix matrix;
        REQUIRE(matrix.getTargetBase(VoragoMacroTarget::CloudRichness) == 0.70f);

        const std::array<std::uint32_t, 3> badBits = {{0x7FC00000u, 0x7F800000u, 0xFF800000u}};
        for (const std::uint32_t bits : badBits) {
            matrix.setTargetBase(VoragoMacroTarget::CloudRichness, surfaceNonFinite(bits));
            INFO("bits 0x" << std::hex << bits);
            REQUIRE(matrix.getTargetBase(VoragoMacroTarget::CloudRichness) == 0.70f);
        }

        std::array<float, VoragoMacroMatrix::kNumTargets> before{};
        for (std::size_t t = 0; t < VoragoMacroMatrix::kNumTargets; ++t) {
            before[t] = matrix.getTargetBase(static_cast<VoragoMacroTarget>(t));
        }
        matrix.setTargetBase(VoragoMacroTarget::Count, 0.5f);
        matrix.setTargetBase(static_cast<VoragoMacroTarget>(255), 0.5f);
        for (std::size_t t = 0; t < VoragoMacroMatrix::kNumTargets; ++t) {
            INFO("target " << t);
            REQUIRE(matrix.getTargetBase(static_cast<VoragoMacroTarget>(t)) == before[t]);
        }

        for (std::size_t t = 0; t < VoragoMacroMatrix::kNumTargets; ++t) {
            matrix.setTargetBase(static_cast<VoragoMacroTarget>(t), 0.123f);
        }
        for (std::size_t t = 0; t < VoragoMacroMatrix::kNumTargets; ++t) {
            REQUIRE(matrix.getTargetBase(static_cast<VoragoMacroTarget>(t)) == 0.123f);
        }
        matrix.resetTargetBases();
        for (std::size_t t = 0; t < VoragoMacroMatrix::kNumTargets; ++t) {
            const auto target = static_cast<VoragoMacroTarget>(t);
            INFO("target " << t);
            REQUIRE(matrix.getTargetBase(target) == surfaceLiteralBase(target));
        }
    }
}

// =============================================================================
// spec B-1 (T007, FR-007 / FR-060 / SC-021) - ResonanceDriftNetwork octave lock
// =============================================================================

namespace {

constexpr float kLockNoteHz = 55.0f;

/// A prepared network in @p mode, keyed to kLockNoteHz, Hybrid pulled fully
/// onto the keyed anchors (setGravity(1)).
[[nodiscard]] std::unique_ptr<ResonanceDriftNetwork> makeLockNetwork(
    ResonanceDriftNetwork::AnchorMode mode) {
    auto net = std::make_unique<ResonanceDriftNetwork>();
    net->prepare(kSurfaceSampleRate, ResonanceDriftNetwork::PrepareConfig{});
    net->setAnchorMode(mode);
    net->setNoteFrequency(kLockNoteHz);
    net->setGravity(1.0f);
    return net;
}

/// The worst |log2(f / note) - nearest int| over the resolved peaks.
[[nodiscard]] double worstOctaveOffset(const ResonanceDriftNetwork& net) noexcept {
    double worst = 0.0;
    const std::size_t peaks = net.getNumPeaks();
    for (std::size_t p = 0; p < peaks; ++p) {
        const double f = static_cast<double>(net.getPeakCurrentFrequency(p));
        const double l2 = std::log2(f / static_cast<double>(kLockNoteHz));
        worst = std::max(worst, std::fabs(l2 - std::round(l2)));
    }
    return worst;
}

}  // namespace

TEST_CASE("ResonanceDriftNetwork_OctaveLock", "[systems][vorago]") {
    using Mode = ResonanceDriftNetwork::AnchorMode;

    SECTION("LockZeroIsBitEqualToShippedAnchors") {
        for (const Mode mode : {Mode::Hybrid, Mode::Keyed}) {
            auto shipped = makeLockNetwork(mode);  // never sees setOctaveLock
            auto locked = makeLockNetwork(mode);
            REQUIRE(locked->getOctaveLock() == 0.0f);  // default-inert
            locked->setOctaveLock(1.0f);
            locked->setOctaveLock(0.0f);
            REQUIRE(locked->getOctaveLock() == 0.0f);

            // reset() snaps the control state (anchors resolved, applied :=
            // target, no slew) and preserves configuration.
            (*shipped).reset();
            (*locked).reset();
            for (std::size_t p = 0; p < ResonanceDriftNetwork::kMaxPeaks; ++p) {
                INFO("mode " << static_cast<int>(mode) << "  peak " << p);
                REQUIRE(locked->getPeakCurrentFrequency(p) == shipped->getPeakCurrentFrequency(p));
            }

            // ...and still bit-equal after ~100 ms of rendering (wander live).
            std::array<float, 512> a{};
            std::array<float, 512> b{};
            for (int blk = 0; blk < 10; ++blk) {
                a.fill(0.0f);
                b.fill(0.0f);
                shipped->processBlock(a.data(), a.data(), a.data(), a.data(), a.size());
                locked->processBlock(b.data(), b.data(), b.data(), b.data(), b.size());
            }
            for (std::size_t p = 0; p < ResonanceDriftNetwork::kMaxPeaks; ++p) {
                INFO("mode " << static_cast<int>(mode) << "  peak " << p << " after render");
                REQUIRE(locked->getPeakCurrentFrequency(p) == shipped->getPeakCurrentFrequency(p));
            }
        }
    }

    SECTION("LockOnePutsEveryKeyedAnchorOnAnOctave") {
        for (const Mode mode : {Mode::Keyed, Mode::Hybrid}) {
            auto net = makeLockNetwork(mode);
            for (std::size_t p = 0; p < ResonanceDriftNetwork::kMaxPeaks; ++p) {
                net->setFreqWander(p, 0.0f);  // measure the anchors, not the wander
            }

            // Teeth: the shipped keyed ratios are harmonic, NOT octave-aligned.
            (*net).reset();
            const double unlockedWorst = worstOctaveOffset(*net);
            INFO("mode " << static_cast<int>(mode) << "  unlocked worst " << unlockedWorst);
            REQUIRE(unlockedWorst > 0.1);

            net->setOctaveLock(1.0f);
            (*net).reset();
            for (std::size_t p = 0; p < net->getNumPeaks(); ++p) {
                const double f = static_cast<double>(net->getPeakCurrentFrequency(p));
                const double l2 = std::log2(f / static_cast<double>(kLockNoteHz));
                INFO("mode " << static_cast<int>(mode) << "  peak " << p << "  f " << f
                             << "  log2(f/note) " << l2);
                REQUIRE(std::fabs(l2 - std::round(l2)) <= 1e-4);
            }
        }
    }

    SECTION("HalfLockInterpolatesInLog2") {
        auto net = makeLockNetwork(Mode::Keyed);
        for (std::size_t p = 0; p < ResonanceDriftNetwork::kMaxPeaks; ++p) {
            net->setFreqWander(p, 0.0f);
        }
        net->setOctaveLock(0.5f);
        (*net).reset();
        for (std::size_t p = 0; p < net->getNumPeaks(); ++p) {
            const double r = std::log2(static_cast<double>(net->getPeakRatio(p)));
            const double expected = r + 0.5 * (std::round(r) - r);
            const double f = static_cast<double>(net->getPeakCurrentFrequency(p));
            const double got = std::log2(f / static_cast<double>(kLockNoteHz));
            INFO("peak " << p << "  expected " << expected << "  got " << got);
            REQUIRE(got == Catch::Approx(expected).margin(1e-4));
        }
    }

    SECTION("RejectsNonFiniteAndClamps") {
        auto net = makeLockNetwork(Mode::Hybrid);
        net->setOctaveLock(0.4f);
        REQUIRE(net->getOctaveLock() == 0.4f);
        const std::array<std::uint32_t, 3> badBits = {{0x7FC00000u, 0x7F800000u, 0xFF800000u}};
        for (const std::uint32_t bits : badBits) {
            net->setOctaveLock(surfaceNonFinite(bits));
            INFO("bits 0x" << std::hex << bits);
            REQUIRE(net->getOctaveLock() == 0.4f);
        }
        net->setOctaveLock(2.0f);
        REQUIRE(net->getOctaveLock() == 1.0f);
        net->setOctaveLock(-3.0f);
        REQUIRE(net->getOctaveLock() == 0.0f);
    }

    SECTION("VoiceForwarder") {
        auto voicePtr = std::make_unique<VoragoVoice>();  // heap: a voice is large
        voicePtr->prepare(kSurfaceSampleRate, Krate::DSP::VoragoVoiceConfig{});
        REQUIRE(voicePtr->isPrepared());
        VoragoVoice& voice = *voicePtr;
        REQUIRE(voice.getResonanceOctaveLock() == 0.0f);  // prepared default
        voice.setResonanceOctaveLock(0.6f);
        REQUIRE(voice.getResonanceOctaveLock() == 0.6f);
        REQUIRE(voice.resonance().getOctaveLock() == 0.6f);
        voice.setResonanceOctaveLock(surfaceNonFinite(0x7FC00000u));
        REQUIRE(voice.getResonanceOctaveLock() == 0.6f);
    }
}

// =============================================================================
// spec B-2 (T007, FR-007 / FR-060 / SC-021) - VoragoEngine compensated drive
// =============================================================================

namespace {

constexpr std::size_t kDriveBlock = 512u;
constexpr int kDriveBlocks = 64;
constexpr int kDriveSettleBlocks = 16;  ///< the saturator's drive smoother ramps in

/// Fill one block of a -12 dBFS 110 Hz sine on both channels, phase-continuous.
void fillDriveTone(std::array<float, kDriveBlock>& l, std::array<float, kDriveBlock>& r,
                   std::size_t blockIndex) noexcept {
    const double amp = std::pow(10.0, -12.0 / 20.0);
    const double w = 2.0 * 3.14159265358979323846 * 110.0 / kSurfaceSampleRate;
    for (std::size_t i = 0; i < kDriveBlock; ++i) {
        const double n = static_cast<double>(blockIndex * kDriveBlock + i);
        const auto s = static_cast<float>(amp * std::sin(w * n));
        l[i] = s;
        r[i] = s;
    }
}

struct DriveToneStats {
    double rmsDb = 0.0;
    double crestDb = 0.0;
};

/// Render the tone through @p engine's output stage only; stats of the left
/// channel over blocks [kDriveSettleBlocks, kDriveBlocks).
[[nodiscard]] DriveToneStats renderDriveTone(VoragoEngine& engine) noexcept {
    std::array<float, kDriveBlock> l{};
    std::array<float, kDriveBlock> r{};
    double sumSq = 0.0;
    double peak = 0.0;
    std::size_t count = 0u;
    for (int blk = 0; blk < kDriveBlocks; ++blk) {
        fillDriveTone(l, r, static_cast<std::size_t>(blk));
        engine.processOutputStage(l.data(), r.data(), kDriveBlock);
        if (blk < kDriveSettleBlocks) {
            continue;
        }
        for (const float s : l) {
            const auto d = static_cast<double>(s);
            sumSq += d * d;
            peak = std::max(peak, std::fabs(d));
            ++count;
        }
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(count));
    return {20.0 * std::log10(rms), 20.0 * std::log10(peak / rms)};
}

}  // namespace

TEST_CASE("VoragoEngine_OutputDriveDb", "[systems][vorago]") {
    const VoragoEngineConfig cfg{};

    SECTION("ZeroDriveIsBitEqualToShippedOutputStage") {
        auto shipped = makeEngine(kSurfaceSampleRate, cfg);  // never sees setOutputDriveDb
        auto zero = makeEngine(kSurfaceSampleRate, cfg);
        REQUIRE(zero->getOutputDriveDb() == 0.0f);  // default-inert
        zero->setOutputDriveDb(0.0f);
        REQUIRE(zero->getOutputDriveDb() == 0.0f);

        std::array<float, kDriveBlock> l1{};
        std::array<float, kDriveBlock> r1{};
        std::array<float, kDriveBlock> l2{};
        std::array<float, kDriveBlock> r2{};
        for (int blk = 0; blk < kDriveBlocks; ++blk) {
            fillDriveTone(l1, r1, static_cast<std::size_t>(blk));
            fillDriveTone(l2, r2, static_cast<std::size_t>(blk));
            shipped->processOutputStage(l1.data(), r1.data(), kDriveBlock);
            zero->processOutputStage(l2.data(), r2.data(), kDriveBlock);
            for (std::size_t i = 0; i < kDriveBlock; ++i) {
                INFO("block " << blk << "  sample " << i);
                REQUIRE(l2[i] == l1[i]);
                REQUIRE(r2[i] == r1[i]);
            }
        }
    }

    SECTION("TwelveDbIsLoudnessCompensatedAndLowersCrest") {
        auto e0 = makeEngine(kSurfaceSampleRate, cfg);
        auto e12 = makeEngine(kSurfaceSampleRate, cfg);
        e12->setOutputDriveDb(12.0f);
        REQUIRE(e12->getOutputDriveDb() == 12.0f);

        const DriveToneStats s0 = renderDriveTone(*e0);
        const DriveToneStats s12 = renderDriveTone(*e12);
        INFO("0 dB: rms " << s0.rmsDb << " dB crest " << s0.crestDb << " dB;  12 dB: rms "
                          << s12.rmsDb << " dB crest " << s12.crestDb << " dB");
        REQUIRE(std::fabs(s12.rmsDb - s0.rmsDb) <= 1.0);
        REQUIRE(s12.crestDb < s0.crestDb);
    }

    SECTION("RejectsNonFiniteAndClampsToSaturatorRange") {
        auto engine = makeEngine(kSurfaceSampleRate, cfg);
        engine->setOutputDriveDb(6.0f);
        REQUIRE(engine->getOutputDriveDb() == 6.0f);
        const std::array<std::uint32_t, 3> badBits = {{0x7FC00000u, 0x7F800000u, 0xFF800000u}};
        for (const std::uint32_t bits : badBits) {
            engine->setOutputDriveDb(surfaceNonFinite(bits));
            INFO("bits 0x" << std::hex << bits);
            REQUIRE(engine->getOutputDriveDb() == 6.0f);
        }
        engine->setOutputDriveDb(100.0f);
        REQUIRE(engine->getOutputDriveDb() == TapeSaturator::kMaxDriveDb);
        engine->setOutputDriveDb(-100.0f);
        REQUIRE(engine->getOutputDriveDb() == TapeSaturator::kMinDriveDb);
    }
}

// =============================================================================
// T008 (FR-004) - the seven VoragoVoice Phase 12 forwarders
// =============================================================================

using Krate::DSP::FeedbackEcology;
using Krate::DSP::NoiseOrganism;
using Krate::DSP::NoiseOrganismModel;
using Krate::DSP::NoiseType;

namespace {

constexpr std::size_t kFwdBlock = 512u;
constexpr std::size_t kFwdSettleSamples = 4800u;  ///< 100 ms: a model duck completes

[[nodiscard]] std::unique_ptr<VoragoVoice> makeForwarderVoice() {
    auto v = std::make_unique<VoragoVoice>();  // heap: a voice is ~118 KB
    v->prepare(kSurfaceSampleRate, Krate::DSP::VoragoVoiceConfig{});
    return v;
}

/// Render @p n samples in kFwdBlock pieces and discard them.
void renderForwarderVoice(VoragoVoice& v, std::size_t n) noexcept {
    std::array<float, kFwdBlock> l{};
    std::array<float, kFwdBlock> r{};
    std::size_t done = 0u;
    while (done < n) {
        const std::size_t take = std::min(kFwdBlock, n - done);
        v.processStereoBlock(l.data(), r.data(), take);
        done += take;
    }
}

/// Every forwarder, called with the value a freshly prepared voice already
/// holds (vorago_voice.h prepare() step 5; NoiseOrganism's requested type
/// default is Brown, noise_organism.h:1138).
void broadcastHeldForwarderValues(VoragoVoice& v) noexcept {
    v.setCloudSpectralGravity(0.10f);
    constexpr std::array<NoiseOrganismModel, NoiseOrganism::kMaxSources> kModels = {
        {NoiseOrganismModel::FilteredWind, NoiseOrganismModel::GranularDust,
         NoiseOrganismModel::Direct, NoiseOrganismModel::MetallicHiss}};
    constexpr std::array<float, NoiseOrganism::kMaxSources> kFeedback = {
        {NoiseOrganism::kDefaultCombFeedback, NoiseOrganism::kDefaultCombFeedback,
         NoiseOrganism::kDefaultCombFeedback, NoiseOrganism::kMetallicCombFeedback}};
    for (std::size_t s = 0; s < NoiseOrganism::kMaxSources; ++s) {
        v.setNoiseSourceModel(s, kModels[s]);
        v.setNoiseSourceType(s, NoiseType::Brown);
        v.setNoiseCombTuning(s, 60.0f, 0.35f);
        v.setNoiseCombTuning(s, 60.0f, 0.35f);
        v.setNoiseCombFeedback(s, kFeedback[s]);
    }
    v.setResonanceAnchorMode(ResonanceDriftNetwork::AnchorMode::Hybrid);
    for (std::size_t l = 0; l < FeedbackEcology::kMaxLoops; ++l) {
        v.setEcologyLoopFilterMode(l, FeedbackEcology::FilterMode::Lowpass);
    }
}

}  // namespace

TEST_CASE("VoragoVoice_Phase12Forwarders", "[systems][vorago]") {
    const float qnan = surfaceNonFinite(0x7FC00000u);

    SECTION("SpectralGravity") {
        auto voice = makeForwarderVoice();
        voice->setCloudSpectralGravity(-0.5f);
        REQUIRE(voice->cloud().getSpectralGravity() == -0.5f);
        voice->setCloudSpectralGravity(qnan);
        REQUIRE(voice->cloud().getSpectralGravity() == -0.5f);
    }

    SECTION("SourceModel") {
        auto voice = makeForwarderVoice();
        voice->noteOn(110.0f, 1.0f);
        voice->setNoiseSourceModel(2, NoiseOrganismModel::MetallicHiss);
        renderForwarderVoice(*voice, kFwdSettleSamples);
        REQUIRE(voice->noise().getSourceModel(2) == NoiseOrganismModel::MetallicHiss);

        std::array<NoiseOrganismModel, NoiseOrganism::kMaxSources> before{};
        for (std::size_t s = 0; s < NoiseOrganism::kMaxSources; ++s) {
            before[s] = voice->noise().getSourceModel(s);
        }
        voice->setNoiseSourceModel(4, NoiseOrganismModel::Direct);  // out of range
        renderForwarderVoice(*voice, kFwdSettleSamples);
        for (std::size_t s = 0; s < NoiseOrganism::kMaxSources; ++s) {
            INFO("slot " << s);
            REQUIRE(voice->noise().getSourceModel(s) == before[s]);
        }
    }

    SECTION("SourceType") {
        auto voice = makeForwarderVoice();
        REQUIRE(voice->noise().getSourceModel(2) == NoiseOrganismModel::Direct);
        voice->noteOn(110.0f, 1.0f);
        voice->setNoiseSourceType(2, NoiseType::Pink);
        renderForwarderVoice(*voice, kFwdSettleSamples);
        REQUIRE(voice->noise().getSourceNoiseType(2) == NoiseType::Pink);
    }

    SECTION("CombTuningRejectsNonFinite") {
        auto voice = makeForwarderVoice();
        voice->setNoiseCombTuning(0, 440.0f, 0.6f);
        REQUIRE(voice->noise().getCombFundamental(0) == 440.0f);
        REQUIRE(voice->noise().getCombSpread(0) == 0.6f);
        // Unchanged - NOT the organism's 60 / 0.35 substitutes.
        voice->setNoiseCombTuning(0, qnan, 0.2f);
        REQUIRE(voice->noise().getCombFundamental(0) == 440.0f);
        REQUIRE(voice->noise().getCombSpread(0) == 0.6f);
        voice->setNoiseCombTuning(0, 200.0f, qnan);
        REQUIRE(voice->noise().getCombFundamental(0) == 440.0f);
        REQUIRE(voice->noise().getCombSpread(0) == 0.6f);
    }

    SECTION("CombFeedbackRejectsNonFinite") {
        auto voice = makeForwarderVoice();
        voice->setNoiseCombFeedback(1, 0.3f);
        REQUIRE(voice->noise().getCombFeedback(1) == 0.3f);
        voice->setNoiseCombFeedback(1, qnan);
        REQUIRE(voice->noise().getCombFeedback(1) == 0.3f);
    }

    SECTION("CombFeedbackLatchesAtRunningValue") {
        // C-4: the FIRST push at the value slot 3 already runs must still latch
        // the organism, or the model change below re-derives 0.55.
        auto voice = makeForwarderVoice();
        REQUIRE(voice->noise().getCombFeedback(3) == NoiseOrganism::kMetallicCombFeedback);
        voice->setNoiseCombFeedback(3, 0.75f);
        voice->noteOn(110.0f, 1.0f);
        voice->setNoiseSourceModel(3, NoiseOrganismModel::Direct);
        renderForwarderVoice(*voice, kFwdSettleSamples);
        REQUIRE(voice->noise().getSourceModel(3) == NoiseOrganismModel::Direct);
        REQUIRE(voice->noise().getCombFeedback(3) == 0.75f);
    }

    SECTION("AnchorMode") {
        auto voice = makeForwarderVoice();
        voice->setResonanceAnchorMode(ResonanceDriftNetwork::AnchorMode::Keyed);
        REQUIRE(voice->resonance().getAnchorMode() == ResonanceDriftNetwork::AnchorMode::Keyed);
    }

    SECTION("LoopFilterMode") {
        auto voice = makeForwarderVoice();
        voice->setEcologyLoopFilterMode(4, FeedbackEcology::FilterMode::Highpass);
        REQUIRE(voice->ecology().getLoopFilterMode(4) == FeedbackEcology::FilterMode::Highpass);

        std::array<FeedbackEcology::FilterMode, FeedbackEcology::kMaxLoops> before{};
        for (std::size_t l = 0; l < FeedbackEcology::kMaxLoops; ++l) {
            before[l] = voice->ecology().getLoopFilterMode(l);
        }
        voice->setEcologyLoopFilterMode(6, FeedbackEcology::FilterMode::Bandpass);  // no-op
        for (std::size_t l = 0; l < FeedbackEcology::kMaxLoops; ++l) {
            INFO("loop " << l);
            REQUIRE(voice->ecology().getLoopFilterMode(l) == before[l]);
        }
    }

    SECTION("EarlyOutRenderIdentity") {
        auto a = makeForwarderVoice();
        auto b = makeForwarderVoice();
        a->noteOn(110.0f, 1.0f);
        b->noteOn(110.0f, 1.0f);

        std::array<float, kFwdBlock> la{};
        std::array<float, kFwdBlock> ra{};
        std::array<float, kFwdBlock> lb{};
        std::array<float, kFwdBlock> rb{};
        const auto total = static_cast<std::size_t>(kSurfaceSampleRate);  // 1 s
        float maxL = 0.0f;
        float maxR = 0.0f;
        std::size_t done = 0u;
        while (done < total) {
            const std::size_t take = std::min(kFwdBlock, total - done);
            broadcastHeldForwarderValues(*b);  // every 512 samples
            a->processStereoBlock(la.data(), ra.data(), take);
            b->processStereoBlock(lb.data(), rb.data(), take);
            for (std::size_t i = 0; i < take; ++i) {
                maxL = std::max(maxL, std::fabs(la[i] - lb[i]));
                maxR = std::max(maxR, std::fabs(ra[i] - rb[i]));
            }
            done += take;
        }
        INFO("max-abs diff L " << maxL << "  R " << maxR);
        REQUIRE(maxL <= 1e-6f);
        REQUIRE(maxR <= 1e-6f);
    }
}

// =============================================================================
// T009 (FR-003, SC-019 part 1) - VoragoVoiceParams + VoragoEngine::applyVoiceParams
// =============================================================================

using Krate::DSP::ContinuousBody;
using Krate::DSP::VoragoVoiceParams;
using Krate::DSP::TestUtils::Vorago::renderEngine;

namespace {

constexpr std::size_t kVpBlock = 512u;

/// The EFFECTIVE noise type a slot reports for (model, requested type), stated
/// independently of the organism: FilteredWind pins Brown, GranularDust pins
/// Velvet, MetallicHiss pins Blue (setHissBright is never called by the voice),
/// Direct plays the request (noise_organism.h:1226-1240).
[[nodiscard]] NoiseType vpEffectiveType(NoiseOrganismModel m, NoiseType requested) noexcept {
    switch (m) {
        case NoiseOrganismModel::FilteredWind:
            return NoiseType::Brown;
        case NoiseOrganismModel::GranularDust:
            return NoiseType::Velvet;
        case NoiseOrganismModel::MetallicHiss:
            return NoiseType::Blue;
        case NoiseOrganismModel::Direct:
        default:
            return (requested == NoiseType::ModulationNoise) ? NoiseType::TapeHiss : requested;
    }
}

/// Every VoragoVoiceParams field read back from one voice and compared to @p p.
/// The noise model / type halves are optional because a model change is DUCKED
/// and only lands while the slot renders (noise_organism.h:455-471).
void requireVoiceMatchesParams(const VoragoVoice& v, const VoragoVoiceParams& p,
                               bool checkNoiseModels) {
    REQUIRE(v.getStereoSpread() == p.stereoSpread);
    REQUIRE(v.cloud().getSpectralGravity() == p.cloudSpectralGravity);
    REQUIRE(v.getBodyMaterialA() == p.bodyMaterialA);
    REQUIRE(v.getBodyMaterialB() == p.bodyMaterialB);
    for (std::size_t s = 0; s < VoragoVoiceParams::kNumNoiseSlots; ++s) {
        INFO("noise slot " << s);
        if (checkNoiseModels) {
            REQUIRE(v.noise().getSourceModel(s) == p.noiseModel[s]);
            REQUIRE(v.noise().getSourceNoiseType(s)
                    == vpEffectiveType(p.noiseModel[s], p.noiseType[s]));
        }
        REQUIRE(v.noise().getCombFundamental(s) == p.noiseCombFundamentalHz[s]);
        REQUIRE(v.noise().getCombSpread(s) == p.noiseCombSpread[s]);
        REQUIRE(v.noise().getCombFeedback(s) == p.noiseCombFeedback[s]);
    }
    REQUIRE(v.resonance().getAnchorMode() == p.resonanceAnchorMode);
    for (std::size_t l = 0; l < VoragoVoiceParams::kNumLoops; ++l) {
        INFO("loop " << l);
        REQUIRE(v.ecology().getLoopFilterMode(l) == p.ecologyLoopFilterMode[l]);
    }
}

/// Every field moved off its default. Comb values stay inside the organism's
/// clamps at 48 kHz, so the getters report them verbatim.
[[nodiscard]] VoragoVoiceParams vpNonDefaultParams() noexcept {
    VoragoVoiceParams p{};
    p.stereoSpread = 0.80f;
    p.cloudSpectralGravity = -0.30f;
    p.bodyMaterialA = ContinuousBody::BodyMaterial::CavernWall;
    p.bodyMaterialB = ContinuousBody::BodyMaterial::WoodenHull;
    p.noiseModel = {{NoiseOrganismModel::Direct, NoiseOrganismModel::MetallicHiss,
                     NoiseOrganismModel::GranularDust, NoiseOrganismModel::FilteredWind}};
    p.noiseType = {{NoiseType::Pink, NoiseType::White, NoiseType::Violet, NoiseType::Grey}};
    p.noiseCombFundamentalHz = {{110.0f, 220.0f, 330.0f, 440.0f}};
    p.noiseCombSpread = {{0.10f, 0.20f, 0.50f, 0.90f}};
    p.noiseCombFeedback = {{0.30f, 0.40f, 0.60f, 0.20f}};
    p.resonanceAnchorMode = ResonanceDriftNetwork::AnchorMode::Keyed;
    p.ecologyLoopFilterMode = {{FeedbackEcology::FilterMode::Bandpass,
                                FeedbackEcology::FilterMode::Highpass,
                                FeedbackEcology::FilterMode::Bandpass,
                                FeedbackEcology::FilterMode::Highpass,
                                FeedbackEcology::FilterMode::Bandpass,
                                FeedbackEcology::FilterMode::Highpass}};
    return p;
}

/// SC-019 part 1: engine A never sees applyVoiceParams, engine B gets a
/// default-constructed broadcast after prepare; @p seconds of a held C1 must
/// agree within 1e-6 per channel. Also checks every default against the
/// prepared voices of engine A, on every slot.
void runDefaultBroadcastNoOp(double seconds) {
    const VoragoEngineConfig cfg{};
    auto a = makeEngine(kSurfaceSampleRate, cfg);
    auto b = makeEngine(kSurfaceSampleRate, cfg);
    for (VoragoEngine* e : {a.get(), b.get()}) {
        e->setPolyphony(4u);
        e->setSeed(1u);
    }

    const VoragoVoiceParams defaults{};
    for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
        INFO("slot " << i);
        requireVoiceMatchesParams(a->getVoice(i), defaults, true);
    }

    b->applyVoiceParams(VoragoVoiceParams{});
    a->noteOn(36u, 100u);
    b->noteOn(36u, 100u);

    std::vector<float> la;
    std::vector<float> ra;
    std::vector<float> lb;
    std::vector<float> rb;
    const auto total = static_cast<std::size_t>(seconds * kSurfaceSampleRate);
    float maxL = 0.0f;
    float maxR = 0.0f;
    std::size_t done = 0u;
    while (done < total) {
        const std::size_t take = std::min(kVpBlock, total - done);
        renderEngine(*a, la, ra, take, kVpBlock);
        renderEngine(*b, lb, rb, take, kVpBlock);
        for (std::size_t i = 0; i < take; ++i) {
            maxL = std::max(maxL, std::fabs(la[i] - lb[i]));
            maxR = std::max(maxR, std::fabs(ra[i] - rb[i]));
        }
        done += take;
    }
    INFO("max-abs diff L " << maxL << "  R " << maxR);
    REQUIRE(maxL <= 1e-6f);
    REQUIRE(maxR <= 1e-6f);
}

}  // namespace

TEST_CASE("VoragoEngine_ApplyVoiceParamsDefaultIsNoOp_Short", "[systems][vorago]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<VoragoVoiceParams>);
    STATIC_REQUIRE(VoragoVoiceParams::kFieldCount == 31u);
    runDefaultBroadcastNoOp(4.0);
}

TEST_CASE("VoragoEngine_ApplyVoiceParamsDefaultIsNoOp", "[systems][vorago][long]") {
    runDefaultBroadcastNoOp(60.0);
}

TEST_CASE("VoragoEngine_ApplyVoiceParamsReachesAllSlots", "[systems][vorago]") {
    const VoragoEngineConfig cfg{};
    auto engine = makeEngine(kSurfaceSampleRate, cfg);
    engine->setPolyphony(2u);
    engine->noteOn(36u, 100u);
    engine->noteOn(43u, 100u);

    const VoragoVoiceParams p = vpNonDefaultParams();
    engine->applyVoiceParams(p);

    std::vector<float> l;
    std::vector<float> r;
    renderEngine(*engine, l, r, kFwdSettleSamples, kVpBlock);

    // Every field that lands on the write reaches ALL kMaxVoices slots, not
    // only the i < polyphony ones.
    for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
        INFO("slot " << i);
        requireVoiceMatchesParams(engine->getVoice(i), p, false);
    }

    // The ducked model / type change completes only on a RENDERING slot
    // (advanceLifeOnly never runs the organism), so the spare slots are
    // admitted - a polyphony increase, the case the kMaxVoices bound exists
    // for - and every slot must then play the broadcast models.
    engine->setPolyphony(VoragoEngine::kMaxVoices);
    constexpr std::array<std::uint8_t, 4> kMoreNotes = {{48, 55, 60, 67}};
    for (const std::uint8_t n : kMoreNotes) {
        engine->noteOn(n, 100u);
    }
    renderEngine(*engine, l, r, kFwdSettleSamples, kVpBlock);
    for (std::size_t i = 0; i < VoragoEngine::kMaxVoices; ++i) {
        INFO("slot " << i << " (after admission)");
        requireVoiceMatchesParams(engine->getVoice(i), p, true);
    }
}

// =============================================================================
// T010 (FR-005) - per-tone sub level base setSubToneLevelDb / getSubToneLevelDb
// =============================================================================

using Krate::DSP::SubharmonicEngine;

namespace {

constexpr std::size_t kSubChunk = 64u;

/// Max-abs difference of two equal-length renders, per channel, folded into
/// the running maxima.
void subFoldDiff(const std::vector<float>& la, const std::vector<float>& ra,
                 const std::vector<float>& lb, const std::vector<float>& rb, float& maxL,
                 float& maxR) noexcept {
    for (std::size_t i = 0; i < la.size(); ++i) {
        maxL = std::max(maxL, std::fabs(la[i] - lb[i]));
        maxR = std::max(maxR, std::fabs(ra[i] - rb[i]));
    }
}

/// SC-019 part 2 (first half, T010): engine A never sees the new setters,
/// engine B writes every tone's shipped default through setSubToneLevelDb
/// after prepare; @p seconds of a held C1 must agree within 1e-6 per channel.
/// T011 extends the B-side writes with the ghost setters.
void runNewSettersDefaultInert(double seconds) {
    const VoragoEngineConfig cfg{};
    auto a = makeEngine(kSurfaceSampleRate, cfg);
    auto b = makeEngine(kSurfaceSampleRate, cfg);
    for (VoragoEngine* e : {a.get(), b.get()}) {
        e->setPolyphony(4u);
        e->setSeed(1u);
    }
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        b->setSubToneLevelDb(t, SubharmonicEngine::kDefaultToneLevelDb[t]);
    }
    // T011: the ghost setters at the shipped config defaults.
    b->setGhostReverseProbability(0.0f);
    b->setGhostEventTriggers(false);
    a->noteOn(36u, 100u);
    b->noteOn(36u, 100u);

    std::vector<float> la;
    std::vector<float> ra;
    std::vector<float> lb;
    std::vector<float> rb;
    const auto total = static_cast<std::size_t>(seconds * kSurfaceSampleRate);
    float maxL = 0.0f;
    float maxR = 0.0f;
    std::size_t done = 0u;
    while (done < total) {
        const std::size_t take = std::min(kVpBlock, total - done);
        renderEngine(*a, la, ra, take, kVpBlock);
        renderEngine(*b, lb, rb, take, kVpBlock);
        subFoldDiff(la, ra, lb, rb, maxL, maxR);
        done += take;
    }
    INFO("max-abs diff L " << maxL << "  R " << maxR);
    REQUIRE(maxL <= 1e-6f);
    REQUIRE(maxR <= 1e-6f);
}

}  // namespace

TEST_CASE("VoragoEngine_SubToneLevelBase", "[systems][vorago]") {
    const VoragoEngineConfig cfg{};

    SECTION("BaseCombinesWithOffsetAndRejectsBadInput") {
        auto engine = makeEngine(kSurfaceSampleRate, cfg);
        const auto& sub = engine->subharmonic();

        // Fresh engine: the base IS the shipped default.
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t);
            REQUIRE(engine->getSubToneLevelDb(t) == SubharmonicEngine::kDefaultToneLevelDb[t]);
        }

        engine->setSubToneLevelDb(0u, -12.0f);
        REQUIRE(engine->getSubToneLevelDb(0u) == -12.0f);
        REQUIRE(sub.getToneLevelDb(0u) == -12.0f);  // offset 0
        REQUIRE(engine->getSubToneLevelDb(1u) == -24.0f);
        REQUIRE(engine->getSubToneLevelDb(2u) == -30.0f);
        REQUIRE(sub.getToneLevelDb(1u) == -24.0f);
        REQUIRE(sub.getToneLevelDb(2u) == -30.0f);

        // The macro offset rides on top of the base; the base getter is unmoved.
        engine->setSubToneLevelOffsetDb(3.0f);
        REQUIRE(sub.getToneLevelDb(0u) == -9.0f);
        REQUIRE(engine->getSubToneLevelDb(0u) == -12.0f);
        REQUIRE(sub.getToneLevelDb(1u) == -21.0f);
        REQUIRE(sub.getToneLevelDb(2u) == -27.0f);

        // Out-of-range tone: no-op, and the out-of-range getter reads 0.
        engine->setSubToneLevelDb(3u, -40.0f);
        REQUIRE(engine->getSubToneLevelDb(3u) == 0.0f);
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t << " after out-of-range write");
            REQUIRE(engine->getSubToneLevelDb(t)
                    == ((t == 0u) ? -12.0f : SubharmonicEngine::kDefaultToneLevelDb[t]));
        }

        // Non-finite (bit patterns): rejected, the previous value stands.
        constexpr std::array<std::uint32_t, 3> kBad = {{0x7FC00000u, 0x7F800000u, 0xFF800000u}};
        for (const std::uint32_t bits : kBad) {
            INFO("bits 0x" << std::hex << bits);
            engine->setSubToneLevelDb(0u, surfaceNonFinite(bits));
            REQUIRE(engine->getSubToneLevelDb(0u) == -12.0f);
            REQUIRE(sub.getToneLevelDb(0u) == -9.0f);
        }
    }

    SECTION("RepeatSetIsAnEarlyOut") {
        // Engine A repeats tone 0's value while tone 0 AND tone 1 have ramps in
        // flight; control engine B does not. A re-armed LinearRamp re-derives
        // its increment from the current value (the setSubToneLevelOffsetDb
        // early-out reason), so any write would bend the trajectory.
        auto a = makeEngine(kSurfaceSampleRate, cfg);
        auto b = makeEngine(kSurfaceSampleRate, cfg);
        for (VoragoEngine* e : {a.get(), b.get()}) {
            e->setPolyphony(4u);
            e->setSeed(1u);
            e->noteOn(36u, 100u);
        }

        std::vector<float> la;
        std::vector<float> ra;
        std::vector<float> lb;
        std::vector<float> rb;
        // Let the fundamental land and the tones sound.
        renderEngine(*a, la, ra, kFwdSettleSamples, kVpBlock);
        renderEngine(*b, lb, rb, kFwdSettleSamples, kVpBlock);

        for (VoragoEngine* e : {a.get(), b.get()}) {
            e->setSubToneLevelDb(0u, -12.0f);
            e->setSubToneLevelDb(1u, -20.0f);
        }
        float maxL = 0.0f;
        float maxR = 0.0f;
        renderEngine(*a, la, ra, kSubChunk, kSubChunk);
        renderEngine(*b, lb, rb, kSubChunk, kSubChunk);
        subFoldDiff(la, ra, lb, rb, maxL, maxR);

        const float tone1Before = a->subharmonic().getToneCurrentGain(1u);
        REQUIRE(tone1Before == b->subharmonic().getToneCurrentGain(1u));

        a->setSubToneLevelDb(0u, -12.0f);  // the repeat: must be an early-out
        REQUIRE(a->getSubToneLevelDb(0u) == -12.0f);

        // Follow both trajectories over several chunks after the repeat.
        for (int chunk = 0; chunk < 16; ++chunk) {
            renderEngine(*a, la, ra, kSubChunk, kSubChunk);
            renderEngine(*b, lb, rb, kSubChunk, kSubChunk);
            subFoldDiff(la, ra, lb, rb, maxL, maxR);
            INFO("chunk " << chunk);
            REQUIRE(a->subharmonic().getToneCurrentGain(0u)
                    == b->subharmonic().getToneCurrentGain(0u));
            REQUIRE(a->subharmonic().getToneCurrentGain(1u)
                    == b->subharmonic().getToneCurrentGain(1u));
        }
        INFO("max-abs diff L " << maxL << "  R " << maxR);
        REQUIRE(maxL <= 1e-6f);
        REQUIRE(maxR <= 1e-6f);
    }
}

TEST_CASE("VoragoEngine_NewSettersDefaultInert_Short", "[systems][vorago]") {
    runNewSettersDefaultInert(4.0);
}

TEST_CASE("VoragoEngine_NewSettersDefaultInert", "[systems][vorago][long]") {
    runNewSettersDefaultInert(60.0);
}

using Krate::DSP::TestUtils::Vorago::applyFastAttack;

TEST_CASE("VoragoEngine_NewSettersSurvivePrepare", "[systems][vorago]") {
    const VoragoEngineConfig cfg{};

    SECTION("SurvivesPrepare") {
        auto engine = makeEngine(kSurfaceSampleRate, cfg);
        constexpr std::array<float, SubharmonicEngine::kNumTones> kTones = {{-12.0f, -20.0f, -36.0f}};
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            engine->setSubToneLevelDb(t, kTones[t]);
        }
        engine->setGhostReverseProbability(0.4f);
        engine->setGhostEventTriggers(true);

        // Re-prepare with the SAME config: the config holds the defaults (0 / false),
        // so a config-driven prepare would erase both ghost values.
        engine->prepare(kSurfaceSampleRate, cfg);
        std::vector<float> l;
        std::vector<float> r;
        renderEngine(*engine, l, r, kVpBlock, kVpBlock);

        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            INFO("tone " << t);
            REQUIRE(engine->getSubToneLevelDb(t) == kTones[t]);
            REQUIRE(engine->subharmonic().getToneLevelDb(t) == kTones[t]);
        }
        REQUIRE(engine->getGhostReverseProbability() == 0.4f);
        REQUIRE(engine->atmosphere().getGrainReverseProbability() == 0.4f);
        REQUIRE(engine->getGhostEventTriggers());

        // NaN (bit pattern): rejected, the previous value stands.
        engine->setGhostReverseProbability(surfaceNonFinite(0x7FC00000u));
        REQUIRE(engine->getGhostReverseProbability() == 0.4f);
        REQUIRE(engine->atmosphere().getGrainReverseProbability() == 0.4f);

        // Out of range: clamped to 1.
        engine->setGhostReverseProbability(1.7f);
        REQUIRE(engine->getGhostReverseProbability() == 1.0f);
        REQUIRE(engine->atmosphere().getGrainReverseProbability() == 1.0f);
    }

    SECTION("DisarmAndRearm") {
        auto engine = makeEngine(kSurfaceSampleRate, cfg);
        engine->setGhostEventTriggers(true);
        engine->setSeed(1u);
        engine->setPolyphony(1u);
        applyFastAttack(*engine);
        engine->noteOn(36u, 100u);

        // Event rate raised through a matrix override (the matrix writes voices
        // below the polyphony, so it runs after setPolyphony). The matrix also
        // writes GhostPeakLevel, so the peak is set AFTER apply.
        VoragoMacroMatrix m;
        m.setTargetBase(VoragoMacroTarget::EventRateScale, 10.0f);
        m.apply(*engine);
        engine->setGhostPeakLevel(1.0f);
        REQUIRE(engine->getGhostPeakLevel() == 1.0f);
        REQUIRE_FALSE(engine->isGhostTriggerLatchHigh());

        std::vector<float> l;
        std::vector<float> r;
        const auto limit = static_cast<std::size_t>(120.0 * kSurfaceSampleRate);
        std::size_t done = 0u;
        while (done < limit && !engine->isGhostTriggerLatchHigh()) {
            renderEngine(*engine, l, r, kVpBlock, kVpBlock);
            done += kVpBlock;
        }
        INFO("samples rendered until the latch rose: " << done);
        REQUIRE(engine->isGhostTriggerLatchHigh());

        // on -> off disarms, and with the flag false the latch cannot re-arm.
        engine->setGhostEventTriggers(false);
        REQUIRE_FALSE(engine->getGhostEventTriggers());
        renderEngine(*engine, l, r, kVpBlock, kVpBlock);
        REQUIRE_FALSE(engine->isGhostTriggerLatchHigh());

        // off -> on with the request still high: the next control step re-arms.
        engine->setGhostEventTriggers(true);
        renderEngine(*engine, l, r, 64u, 64u);
        REQUIRE(engine->isGhostTriggerLatchHigh());
    }
}

// =============================================================================
// T012 (SC-023 (1)) - a repeated broadcast is inert
// =============================================================================

namespace {

/// SC-023 (1). Three engines, polyphony 6, six held notes, rendered in lockstep
/// in kVpBlock blocks for @p seconds:
///   A - applyVoiceParams(p) for a non-default p and setSubToneLevelDb(t, v_t)
///       called ONCE, before the first block;
///   B - the identical calls repeated before EVERY block;
///   C - positive control (spec B-3): A, plus bodyMaterialA written alternately
///       the fixture's material / Glass before every block, carried by the same
///       applyVoiceParams call. (The slot-2 model toggle the spec first named
///       measured 2.1e-6 RMS: the noise bed is inaudible at this fixture.)
/// B must match A within 1e-5 max-abs per channel (the T008 / T010 early-outs);
/// C must differ from A by > 1e-3 RMS (the comparison can see a per-block write).
void runRepeatedBroadcast(double seconds) {
    const VoragoEngineConfig cfg{};
    auto a = makeEngine(kSurfaceSampleRate, cfg);
    auto b = makeEngine(kSurfaceSampleRate, cfg);
    auto c = makeEngine(kSurfaceSampleRate, cfg);

    // Every field off its default; slot 3 carries the 440 Hz comb fundamental.
    const VoragoVoiceParams p = vpNonDefaultParams();
    REQUIRE(p.noiseCombFundamentalHz[3] == 440.0f);
    constexpr std::array<float, SubharmonicEngine::kNumTones> kTones = {{-12.0f, -20.0f, -36.0f}};
    constexpr std::array<std::uint8_t, 6> kNotes = {{36, 43, 48, 55, 60, 67}};

    for (VoragoEngine* e : {a.get(), b.get(), c.get()}) {
        e->setPolyphony(6u);
        e->setSeed(1u);
        for (const std::uint8_t n : kNotes) {
            e->noteOn(n, 100u);
        }
    }

    // Arm A: once.
    a->applyVoiceParams(p);
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        a->setSubToneLevelDb(t, kTones[t]);
    }
    // Arm C: the sub levels once; the voice params per block (below).
    for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
        c->setSubToneLevelDb(t, kTones[t]);
    }

    std::vector<float> la;
    std::vector<float> ra;
    std::vector<float> lb;
    std::vector<float> rb;
    std::vector<float> lc;
    std::vector<float> rc;
    const auto total = static_cast<std::size_t>(seconds * kSurfaceSampleRate);
    float maxL = 0.0f;
    float maxR = 0.0f;
    double sumSqAC = 0.0;
    std::size_t done = 0u;
    std::size_t block = 0u;
    while (done < total) {
        const std::size_t take = std::min(kVpBlock, total - done);

        // Arm B: the identical calls, every block.
        b->applyVoiceParams(p);
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            b->setSubToneLevelDb(t, kTones[t]);
        }

        // Arm C: body material A toggles every block (spec B-3).
        VoragoVoiceParams pc = p;
        pc.bodyMaterialA = ((block % 2u) == 0u) ? p.bodyMaterialA
                                                 : ContinuousBody::BodyMaterial::Glass;
        c->applyVoiceParams(pc);

        renderEngine(*a, la, ra, take, kVpBlock);
        renderEngine(*b, lb, rb, take, kVpBlock);
        renderEngine(*c, lc, rc, take, kVpBlock);
        subFoldDiff(la, ra, lb, rb, maxL, maxR);
        for (std::size_t i = 0; i < take; ++i) {
            const double dl = static_cast<double>(la[i]) - static_cast<double>(lc[i]);
            const double dr = static_cast<double>(ra[i]) - static_cast<double>(rc[i]);
            sumSqAC += dl * dl + dr * dr;
        }
        done += take;
        ++block;
    }

    const double rmsAC = std::sqrt(sumSqAC / (2.0 * static_cast<double>(total)));
    INFO("A vs B max-abs diff L " << maxL << "  R " << maxR << "   A vs C rms diff " << rmsAC);
    REQUIRE(maxL <= 1e-5f);
    REQUIRE(maxR <= 1e-5f);
    REQUIRE(rmsAC > 1e-3);
}

}  // namespace

TEST_CASE("VoragoEngine_RepeatedBroadcastIsInert_Short", "[systems][vorago]") {
    runRepeatedBroadcast(2.0);
}

TEST_CASE("VoragoEngine_RepeatedBroadcastIsInert", "[systems][vorago][long]") {
    runRepeatedBroadcast(10.0);
}

