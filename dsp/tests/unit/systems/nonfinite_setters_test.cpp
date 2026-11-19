// ==============================================================================
// Non-finite setter arguments never reach DSP state (Ruinae audit R-170..R-174)
// ==============================================================================
// A NaN float in a host state blob used to reach these setters raw. Each one
// clamped with std::clamp, which passes NaN, and the NaN then latched in DSP
// state that later finite values could not clear: the LFO phase accumulator, the
// global SVF integrators (via an unsanitised modulation offset), the FDN tank,
// the envelope curve tables, the trance-gate smoothers and the arp step timing.
// Every setter here now ignores a non-finite argument (a struct setter keeps the
// previous value of that field), and ModulationEngine maps a non-finite source,
// contribution or offset to 0.
//
// Built only into dsp_ieee_tests (-fno-fast-math): it injects NaN/+-Inf built
// from bit patterns through a volatile. Finiteness is read only through
// detail::isFinite.
// ==============================================================================

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/modulation_types.h>
#include <krate/dsp/effects/fdn_reverb.h>
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/processors/arpeggiator_core.h>
#include <krate/dsp/processors/trance_gate.h>
#include <krate/dsp/systems/modulation_engine.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

using namespace Krate::DSP;

namespace {

float nfsFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    return std::bit_cast<float>(static_cast<std::uint32_t>(sink));
}

constexpr std::array<std::pair<std::uint32_t, const char*>, 3> kNfsBad{
    {{0x7FC00000u, "NaN"}, {0x7F800000u, "+Inf"}, {0xFF800000u, "-Inf"}}};

constexpr double kNfsRate = 44100.0;
constexpr size_t kNfsBlock = 512;

BlockContext nfsContext() {
    BlockContext ctx;
    ctx.sampleRate = kNfsRate;
    ctx.blockSize = kNfsBlock;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    return ctx;
}

/// Runs `engine` for `blocks` blocks and returns how many end-of-block offsets
/// for destination 0 were non-finite.
int nfsCountNonFiniteOffsets(ModulationEngine& engine, int blocks) {
    const BlockContext ctx = nfsContext();
    int bad = 0;
    for (int b = 0; b < blocks; ++b) {
        engine.process(ctx, nullptr, nullptr, kNfsBlock);
        if (!detail::isFinite(engine.getModulationOffset(0))) {
            ++bad;
        }
        float start = 0.0f;
        float end = 0.0f;
        engine.getModulationOffsetInterp(0, start, end);
        if (!detail::isFinite(start) || !detail::isFinite(end)) {
            ++bad;
        }
    }
    return bad;
}

std::unique_ptr<ModulationEngine> nfsEngineRouting(ModSource source) {
    auto engine = std::make_unique<ModulationEngine>();
    engine->prepare(kNfsRate, kNfsBlock);
    ModRouting r;
    r.source = source;
    r.destParamId = 0;
    r.amount = 1.0f;
    r.active = true;
    engine->setRouting(0, r);
    return engine;
}

} // namespace

TEST_CASE("DSP setters ignore non-finite input", "[ieee][robustness]") {
    for (const auto& [bits, name] : kNfsBad) {
        const float bad = nfsFromBits(bits);
        INFO("injected " << name);

        DYNAMIC_SECTION(name << ": ModulationEngine::setLFO1Rate keeps the LFO and its routed offset finite") {
            auto engine = nfsEngineRouting(ModSource::LFO1);
            engine->setLFO1TempoSync(false);
            engine->setLFO1Rate(2.0f);
            engine->setLFO1Rate(bad);
            REQUIRE(nfsCountNonFiniteOffsets(*engine, 20) == 0);
            // A later finite rate is still honoured (the LFO phase did not latch).
            engine->setLFO1Rate(5.0f);
            REQUIRE(nfsCountNonFiniteOffsets(*engine, 20) == 0);
        }

        DYNAMIC_SECTION(name << ": ModulationEngine::setMacroValue keeps the routed offset finite") {
            auto engine = nfsEngineRouting(ModSource::Macro1);
            engine->setMacroValue(0, 0.75f);
            engine->setMacroValue(0, bad);
            REQUIRE(nfsCountNonFiniteOffsets(*engine, 4) == 0);
            // The previous value is kept.
            REQUIRE(engine->getModulationOffset(0) > 0.5f);
        }

        DYNAMIC_SECTION(name << ": ModulationEngine::evaluateRoutings maps a non-finite source value to 0") {
            auto engine = nfsEngineRouting(ModSource::External0);
            engine->setExternalSourceValue(0, bad);
            REQUIRE(nfsCountNonFiniteOffsets(*engine, 4) == 0);
            // Once the source is finite again the route works normally.
            engine->setExternalSourceValue(0, 0.5f);
            REQUIRE(nfsCountNonFiniteOffsets(*engine, 40) == 0);
            REQUIRE(engine->getModulationOffset(0) > 0.25f);
        }

        DYNAMIC_SECTION(name << ": LFO::setSymmetry keeps the previous symmetry") {
            LFO lfo;
            lfo.prepare(kNfsRate);
            lfo.setFrequency(3.0f);
            lfo.setSymmetry(0.3f);
            lfo.setSymmetry(bad);
            REQUIRE(lfo.symmetry() == 0.3f);
            int nonFinite = 0;
            for (int i = 0; i < 4410; ++i) {
                if (!detail::isFinite(lfo.process())) ++nonFinite;
            }
            REQUIRE(nonFinite == 0);
        }

        DYNAMIC_SECTION(name << ": FDNReverb::setParams with roomSize/damping non-finite does not poison the tank") {
            FDNReverb reverb;
            reverb.prepare(kNfsRate);
            ReverbParams p;
            p.mix = 0.5f;
            reverb.setParams(p);

            ReverbParams poisoned = p;
            poisoned.roomSize = bad;
            poisoned.damping = bad;
            poisoned.width = bad;
            poisoned.mix = bad;
            poisoned.preDelayMs = bad;
            poisoned.modRate = bad;
            poisoned.modDepth = bad;
            reverb.setParams(poisoned);

            std::vector<float> left(kNfsBlock, 0.0f);
            std::vector<float> right(kNfsBlock, 0.0f);
            left[0] = 1.0f;
            right[0] = 1.0f;
            reverb.processBlock(left.data(), right.data(), kNfsBlock);

            // Valid params again, then an impulse: the wet tail must be finite and audible.
            reverb.setParams(p);
            int nonFinite = 0;
            float peak = 0.0f;
            for (int b = 0; b < 16; ++b) {
                std::fill(left.begin(), left.end(), 0.0f);
                std::fill(right.begin(), right.end(), 0.0f);
                if (b == 0) {
                    left[0] = 1.0f;
                    right[0] = 1.0f;
                }
                reverb.processBlock(left.data(), right.data(), kNfsBlock);
                for (size_t i = 0; i < kNfsBlock; ++i) {
                    for (const float s : {left[i], right[i]}) {
                        if (!detail::isFinite(s)) {
                            ++nonFinite;
                        } else if (b > 0) {
                            peak = std::max(peak, std::fabs(s));
                        }
                    }
                }
            }
            REQUIRE(nonFinite == 0);
            REQUIRE(peak > 1e-4f);
        }

        DYNAMIC_SECTION(name << ": ADSREnvelope::setAttackBezierCurve keeps the previous curve table") {
            ADSREnvelope env;
            env.prepare(static_cast<float>(kNfsRate));
            env.setAttack(50.0f);
            env.setDecay(100.0f);
            env.setSustain(0.5f);
            env.setRelease(100.0f);
            env.setAttackBezierCurve(0.3f, bad, 0.7f, 0.7f);
            env.setDecayBezierCurve(bad, 0.6f, 0.7f, 0.3f);
            env.setReleaseBezierCurve(0.3f, 0.6f, 0.7f, bad);
            env.gate(true);
            int nonFinite = 0;
            float peak = 0.0f;
            for (int i = 0; i < 8820; ++i) {
                const float v = env.process();
                if (!detail::isFinite(v)) {
                    ++nonFinite;
                } else {
                    peak = std::max(peak, v);
                }
            }
            env.gate(false);
            for (int i = 0; i < 8820; ++i) {
                if (!detail::isFinite(env.process())) ++nonFinite;
            }
            REQUIRE(nonFinite == 0);
            REQUIRE(peak > 0.9f);
        }

        DYNAMIC_SECTION(name << ": TranceGate::setParams keeps the previous value of each non-finite field") {
            TranceGate gate;
            gate.prepare(kNfsRate);
            TranceGateParams p;
            p.tempoSync = false;
            gate.setParams(p);

            TranceGateParams poisoned = p;
            poisoned.depth = bad;
            poisoned.attackMs = bad;
            poisoned.releaseMs = bad;
            poisoned.rateHz = bad;
            poisoned.phaseOffset = bad;
            poisoned.retriggerDepth = bad;
            gate.setParams(poisoned);
            gate.setDepth(bad);
            gate.setRate(bad);
            gate.setStep(0, bad);

            int nonFinite = 0;
            float peak = 0.0f;
            for (int i = 0; i < 44100; ++i) {
                const float v = gate.process(1.0f);
                if (!detail::isFinite(v)) {
                    ++nonFinite;
                } else {
                    peak = std::max(peak, std::fabs(v));
                }
            }
            REQUIRE(nonFinite == 0);
            REQUIRE(peak > 0.5f);
        }

        DYNAMIC_SECTION(name << ": ArpeggiatorCore::setFreeRate / setGateLength keep the previous value") {
            ArpeggiatorCore arp;
            arp.prepare(kNfsRate, kNfsBlock);
            arp.setEnabled(true);
            arp.setMode(ArpMode::Up);
            arp.setTempoSync(false);
            arp.setFreeRate(10.0f);
            arp.setGateLength(50.0f);
            arp.setFreeRate(bad);
            arp.setGateLength(bad);
            arp.noteOn(48, 100);
            arp.noteOn(52, 100);

            // One second at 10 Hz: ten steps, each a NoteOn followed by its NoteOff.
            const BlockContext ctx = nfsContext();
            std::array<ArpEvent, ArpeggiatorCore::kMaxEvents> events{};
            int noteOns = 0;
            int noteOffs = 0;
            for (size_t done = 0; done < static_cast<size_t>(kNfsRate); done += kNfsBlock) {
                const size_t n = arp.processBlock(ctx, events);
                for (size_t i = 0; i < n; ++i) {
                    if (events[i].type == ArpEvent::Type::NoteOn) ++noteOns;
                    if (events[i].type == ArpEvent::Type::NoteOff) ++noteOffs;
                }
            }
            CHECK(noteOns >= 9);
            CHECK(noteOns <= 11);
            CHECK(noteOffs >= 9);
            CHECK(noteOffs <= 11);
        }

        DYNAMIC_SECTION(name << ": ArpeggiatorCore::setRateScale keeps the previous scale") {
            ArpeggiatorCore arp;
            arp.prepare(kNfsRate, kNfsBlock);
            arp.setEnabled(true);
            arp.setMode(ArpMode::Up);
            arp.setTempoSync(true);
            arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
            arp.setGateLength(50.0f);
            arp.setRateScale(2.0f);
            arp.setRateScale(bad);
            arp.noteOn(48, 100);

            // One second of 1/8 at 120 BPM (4 steps) at scale 2: eight steps.
            const BlockContext ctx = nfsContext();
            std::array<ArpEvent, ArpeggiatorCore::kMaxEvents> events{};
            int noteOns = 0;
            for (size_t done = 0; done < static_cast<size_t>(kNfsRate); done += kNfsBlock) {
                const size_t n = arp.processBlock(ctx, events);
                for (size_t i = 0; i < n; ++i) {
                    if (events[i].type == ArpEvent::Type::NoteOn) ++noteOns;
                }
            }
            CHECK(noteOns >= 7);
            CHECK(noteOns <= 9);
        }

        DYNAMIC_SECTION(name << ": ArpeggiatorCore::setLaneSpeed keeps the previous speed") {
            ArpeggiatorCore arp;
            arp.prepare(kNfsRate, kNfsBlock);
            arp.setLaneSpeed(0, 2.0f);
            arp.setLaneSpeed(0, bad);
            REQUIRE(arp.laneSpeed(0) == 2.0f);
        }
    }
}
