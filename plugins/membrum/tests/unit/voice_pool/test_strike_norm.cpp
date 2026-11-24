// ==============================================================================
// Strike normalisation: per-pad cached gain with a complete cache key (T1.6)
// ==============================================================================
// MB-003 / MB-036: the measured-strike probe (a 30 ms scalar render) ran on the
//   audio thread for almost every hit, because the cache lived in the voice slot
//   and slots rotate across pads.
// MB-021 / MB-176: the cache key omitted FM Ratio and Friction Pressure.
// MB-032: the cache survived a sample-rate change.
// MB-263: the 30 ms window missed peaks that build up after 30 ms.
//
// Cases (a)-(f) use only the pre-fix API and fail on the pre-fix code on their
// values; (g)-(j) pin the StrikeNormTable protocol (VoicePool owns one gain per
// pad, measured on a probe voice in prepare(), on a worker thread after an edit,
// and inline only for a hit that arrives before the worker has delivered);
// (k) pins an FMImpulse pad's per-velocity table (T1.18, MB-260).
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "dsp/default_kit.h"
#include "dsp/drum_voice.h"
#include "dsp/macro_offsets.h"
#include "dsp/pad_config.h"
#include "dsp/pad_config_apply.h"
#include "state/state_codec.h"
#include "voice_pool/voice_pool.h"

#include <krate/dsp/core/db_utils.h>

#include <allocation_detector.h>

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "pluginterfaces/base/ibstream.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace Membrum;

namespace {

float bodyGain(const DrumVoice& v)
{
    return v.bodyBank().getSharedBank().getOutputGain();
}

std::unique_ptr<DrumVoice> preparedVoice(double sr)
{
    auto v = std::make_unique<DrumVoice>();
    v->prepare(sr, 0u);
    return v;
}

void renderVoice(DrumVoice& v, int samples)
{
    std::array<float, 256> buf{};
    for (int done = 0; done < samples; done += 256)
        v.processBlock(buf.data(), std::min(256, samples - done));
}

void renderPool(VoicePool& pool, double sr, float seconds, int block,
                float* peakL = nullptr)
{
    std::vector<float> l(static_cast<std::size_t>(block));
    std::vector<float> r(static_cast<std::size_t>(block));
    const int total = static_cast<int>(seconds * sr);
    for (int done = 0; done < total; done += block)
    {
        const int n = std::min(block, total - done);
        pool.processBlock(l.data(), r.data(), n);
        if (peakL != nullptr)
            for (int i = 0; i < n; ++i)
                *peakL = std::max(*peakL, std::fabs(l[static_cast<std::size_t>(i)]));
    }
}

int activeSlotFor(const VoicePool& pool, std::uint8_t note)
{
    for (int s = 0; s < kMaxVoices; ++s)
    {
        const auto& m = pool.voiceMeta(s);
        if (m.state == VoiceSlotState::Active && m.originatingNote == note)
            return s;
    }
    return -1;
}

// (a) / (b): edit one exciter field on a voice that already played, hit again,
// compare with a fresh voice configured with the edited value from the start.
float editedGainDb(PadConfig cfg, float PadConfig::*field, float from, float to)
{
    cfg.*field = from;
    auto v = preparedVoice(48000.0);
    applyPadConfigToVoice(*v, cfg);
    v->noteOn(1.0f);
    renderVoice(*v, 4800);
    cfg.*field = to;
    applyPadConfigToVoice(*v, cfg);
    v->noteOn(1.0f);
    const float gain = bodyGain(*v);

    auto ref = preparedVoice(48000.0);
    applyPadConfigToVoice(*ref, cfg);
    ref->noteOn(1.0f);
    const float refGain = bodyGain(*ref);
    return 20.0f * std::log10(gain / refGain);
}

// (d): factory kit loading, the pattern of the audit limiter probe.
bool loadFactoryKit(const char* category, const char* name, State::KitSnapshot& snap)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    const auto path = std::filesystem::path(MEMBRUM_RESOURCES_DIR) / "presets" /
                      "Kit Presets" / category / (std::string(name) + ".vstpreset");
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    auto ps = owned(new MemoryStream(bytes.data(), static_cast<TSize>(bytes.size())));
    ps->seek(0, IBStream::kIBSeekSet, nullptr);
    PresetFile pf(ps);
    if (!pf.readChunkList())
        return false;
    const auto* e = pf.getEntry(kComponentState);
    if (e == nullptr)
        return false;
    std::vector<std::uint8_t> comp(static_cast<std::size_t>(e->size));
    ps->seek(e->offset, IBStream::kIBSeekSet, nullptr);
    int32 got = 0;
    ps->read(comp.data(), static_cast<int32>(comp.size()), &got);
    MemoryStream ms;
    int32 w = 0;
    ms.write(comp.data(), static_cast<int32>(comp.size()), &w);
    ms.seek(0, IBStream::kIBSeekSet, nullptr);
    return State::readKitBlob(&ms, snap) == kResultOk;
}

// The audit limiter probe's neutralAll(): every post-bank stage neutral. Since
// fix plan T1.22 a pad's macros offset its stored fields, so the pad as it
// plays (macro offsets folded in, macros neutral) is neutralised.
PadConfig neutralAll(PadConfig c)
{
    c = applyMacroOffsets(c);
    c.macroTightness = 0.5f;
    c.macroBrightness = 0.5f;
    c.macroBodySize = 0.5f;
    c.macroPunch = 0.5f;
    c.macroComplexity = 0.5f;
    c.tsDriveAmount = 0.0f;
    c.tsFoldAmount = 0.0f;
    c.tsFilterCutoff = 1.0f;
    c.tsFilterEnvAmount = 0.5f;
    c.modeInjectAmount = 0.0f;
    c.nonlinearCoupling = 0.0f;
    c.noiseLayerMix = 0.0f;
    c.clickLayerMix = 0.0f;
    c.secondaryEnabled = 0.0f;
    c.tsPitchEnvTime = 0.0f;
    c.tensionModAmt = 0.0f;
    c.morphEnabled = 0.0f;
    return c;
}

} // namespace

// ------------------------------------------------------------------------------
// (a) MB-021 / MB-176
// ------------------------------------------------------------------------------
TEST_CASE("Strike norm: an FM Ratio edit re-normalises the next hit on the same voice",
          "[drum_voice][strike_norm]")
{
    PadConfig cfg;
    cfg.exciterType = ExciterType::FMImpulse;
    cfg.clickLayerMix = 0.0f;

    SECTION("Membrane")
    {
        cfg.bodyModel = BodyModelType::Membrane;
        const float db = editedGainDb(cfg, &PadConfig::fmRatio, 0.133333f, 1.0f);
        INFO("gain off a fresh voice by " << db << " dB");
        CHECK(std::abs(db) <= 0.5f);
    }
    SECTION("Plate")
    {
        cfg.bodyModel = BodyModelType::Plate;
        const float db = editedGainDb(cfg, &PadConfig::fmRatio, 0.133333f, 1.0f);
        INFO("gain off a fresh voice by " << db << " dB");
        CHECK(std::abs(db) <= 0.5f);
    }
    SECTION("Bell")
    {
        cfg.bodyModel = BodyModelType::Bell;
        const float db = editedGainDb(cfg, &PadConfig::fmRatio, 0.133333f, 1.0f);
        INFO("gain off a fresh voice by " << db << " dB");
        CHECK(std::abs(db) <= 0.5f);
    }
}

// ------------------------------------------------------------------------------
// (b) MB-021
// ------------------------------------------------------------------------------
TEST_CASE("Strike norm: a Friction Pressure edit re-normalises the next hit on the same voice",
          "[drum_voice][strike_norm]")
{
    PadConfig cfg;
    cfg.exciterType = ExciterType::Friction;
    cfg.bodyModel = BodyModelType::Membrane;
    cfg.clickLayerMix = 0.0f;
    const float db = editedGainDb(cfg, &PadConfig::frictionPressure, 0.0f, 1.0f);
    INFO("gain off a fresh voice by " << db << " dB");
    CHECK(std::abs(db) <= 0.5f);
}

// ------------------------------------------------------------------------------
// (c) MB-032
// ------------------------------------------------------------------------------
TEST_CASE("Strike norm: the body gain follows a sample-rate change",
          "[voice_pool][strike_norm]")
{
    auto configure = [](VoicePool& p) {
        auto& c = p.padConfigMut(0);
        c.exciterType = ExciterType::Mallet;
        c.bodyModel = BodyModelType::Membrane;
        c.size = 0.5f;
    };

    auto pool = std::make_unique<VoicePool>();
    pool->prepare(44100.0, 256);
    configure(*pool);
    pool->noteOn(36, 1.0f);
    renderPool(*pool, 44100.0, 3.0f, 256);
    REQUIRE(activeSlotFor(*pool, 36) < 0);

    pool->prepare(96000.0, 256);
    pool->noteOn(36, 1.0f);
    const int slot = activeSlotFor(*pool, 36);
    REQUIRE(slot >= 0);
    const float gain = bodyGain(pool->mainVoiceRef(slot));

    auto ref = std::make_unique<VoicePool>();
    ref->prepare(96000.0, 256);
    configure(*ref);
    ref->noteOn(36, 1.0f);
    const int refSlot = activeSlotFor(*ref, 36);
    REQUIRE(refSlot >= 0);
    const float refGain = bodyGain(ref->mainVoiceRef(refSlot));

    INFO("gain " << gain << " vs fresh 96 kHz pool " << refGain);
    CHECK(gain == Catch::Approx(refGain).epsilon(0.01));
}

// ------------------------------------------------------------------------------
// (d) MB-263
// ------------------------------------------------------------------------------
TEST_CASE("Strike norm: a pad whose strike peaks after 30 ms stays inside the body budget",
          "[voice_pool][strike_norm]")
{
    State::KitSnapshot snap;
    REQUIRE(loadFactoryKit("Acoustic", "Orchestral", snap));

    auto check = [&snap](int pad, ExciterType exc, BodyModelType body) {
        PadConfig cfg;
        State::applyPadSnapshot(snap.pads[static_cast<std::size_t>(pad)], cfg);
        REQUIRE(cfg.exciterType == exc);
        REQUIRE(cfg.bodyModel == body);
        cfg = neutralAll(cfg);
        cfg.pan = 0.5f;

        auto pool = std::make_unique<VoicePool>();
        pool->padConfigMut(pad) = cfg;
        pool->prepare(48000.0, 256);
        pool->noteOn(static_cast<std::uint8_t>(36 + pad), 1.0f);
        float peak = 0.0f;
        renderPool(*pool, 48000.0, 1.0f, 256, &peak);
        const float norm = peak / cfg.level;
        INFO("note " << 36 + pad << ": peak / level = " << norm);
        CHECK(norm <= 0.5f * 1.06f);
    };

    SECTION("Orchestral 49 (Mallet / Plate)")
    {
        check(13, ExciterType::Mallet, BodyModelType::Plate);
    }
    SECTION("Orchestral 52 (Mallet / Bell)")
    {
        check(16, ExciterType::Mallet, BodyModelType::Bell);
    }
}

// ------------------------------------------------------------------------------
// (e) MB-036 (slot dependence)
// ------------------------------------------------------------------------------
TEST_CASE("Strike norm: a pad's gain does not depend on which voice slot plays it",
          "[voice_pool][strike_norm]")
{
    auto pool = std::make_unique<VoicePool>();
    pool->prepare(48000.0, 256);
    pool->setMaxPolyphony(8);
    pool->setVoiceStealingPolicy(VoiceStealingPolicy::Oldest);
    auto& c = pool->padConfigMut(2);
    c.exciterType = ExciterType::NoiseBurst;
    c.bodyModel = BodyModelType::Membrane;

    std::set<int> slots;
    float first = 0.0f;
    for (int hit = 0; hit < 8; ++hit)
    {
        renderPool(*pool, 48000.0, 2.0f, 256);
        REQUIRE(activeSlotFor(*pool, 38) < 0);
        pool->noteOn(38, 1.0f);
        const int slot = activeSlotFor(*pool, 38);
        REQUIRE(slot >= 0);
        slots.insert(slot);
        const float g = bodyGain(pool->mainVoiceRef(slot));
        if (hit == 0)
            first = g;
        INFO("hit " << hit << " slot " << slot << " gain " << g << " first " << first);
        CHECK(g == Catch::Approx(first).epsilon(1e-6));
        // The MIDI note-off releases the allocator's voice, so the next hit of
        // the same note is allocated afresh (Oldest idle slot) instead of
        // retriggering this one.
        pool->noteOff(38);
    }
    CHECK(slots.size() >= 4);
}

// ------------------------------------------------------------------------------
// (f) MB-003 (note-on cost)
// ------------------------------------------------------------------------------
TEST_CASE("Strike norm: note-on cost on rotating pads stays under 10 % of a 64-sample block at 96 kHz",
          "[voice_pool][strike_norm][wallclock]")
{
    constexpr double kSr = 96000.0;
    constexpr int kBlock = 64;
    auto pool = std::make_unique<VoicePool>();
    DefaultKit::apply(pool->padConfigsArray());
    pool->prepare(kSr, kBlock);
    pool->setMaxPolyphony(8);

    constexpr std::array<std::uint8_t, 6> kNotes{36, 38, 42, 45, 49, 51};
    std::vector<float> l(kBlock);
    std::vector<float> r(kBlock);
    std::vector<double> timesUs;
    timesUs.reserve(1024);
    int next = 0;
    int noteOns = 0;
    auto hit = [&]() {
        const auto note = kNotes[static_cast<std::size_t>(next)];
        next = (next + 1) % static_cast<int>(kNotes.size());
        const auto t0 = std::chrono::steady_clock::now();
        pool->noteOn(note, 1.0f);
        const auto t1 = std::chrono::steady_clock::now();
        if (noteOns++ >= 32)
            timesUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    };
    for (int b = 0; b < 600; ++b)
    {
        if (b % 8 == 0)
        {
            hit();
            hit();
            hit();
        }
        else if (b % 2 == 0)
        {
            hit();
        }
        pool->processBlock(l.data(), r.data(), kBlock);
    }
    REQUIRE(timesUs.size() > 100);
    std::sort(timesUs.begin(), timesUs.end());
    const double p99 = timesUs[static_cast<std::size_t>(0.99 * static_cast<double>(timesUs.size() - 1))];
    std::printf("Strike norm note-on cost at 96 kHz / 64: %zu timed note-ons, p99 %.2f us, max %.2f us\n",
                timesUs.size(), p99, timesUs.back());
    INFO("p99 note-on " << p99 << " us, max " << timesUs.back() << " us");
    CHECK(p99 <= 66.7);
}

// ------------------------------------------------------------------------------
// (g) MB-036: unchanged pads are never measured at note-on
// ------------------------------------------------------------------------------
TEST_CASE("StrikeNormTable: unchanged pads are never measured at note-on",
          "[voice_pool][strike_norm]")
{
    auto pool = std::make_unique<VoicePool>();
    pool->prepare(48000.0, 64);
    std::vector<float> l(64);
    std::vector<float> r(64);

    for (int i = 0; i < 64; ++i)
    {
        const auto note = static_cast<std::uint8_t>(36 + (i % 4));
        pool->noteOn(note, 1.0f);
        pool->processBlock(l.data(), r.data(), 64);
        pool->noteOff(note);
    }
    CHECK(pool->strikeNorm().inlineMeasureCount() == 0u);

    pool->padConfigMut(0).material = 0.8f;
    pool->noteOn(36, 1.0f);
    pool->processBlock(l.data(), r.data(), 64);
    pool->noteOn(36, 1.0f);
    pool->processBlock(l.data(), r.data(), 64);
    CHECK(pool->strikeNorm().inlineMeasureCount() == 1u);
}

// ------------------------------------------------------------------------------
// (h) THREADING: deterministic interleaving (no thread runs)
// ------------------------------------------------------------------------------
namespace {

// The gain a freshly prepared pool measured for `cfg` on pad 0 in prepare().
StrikeNormGains preparedReference(const PadConfig& cfg, double sr, int block)
{
    auto ref = std::make_unique<VoicePool>();
    ref->padConfigMut(0) = cfg;
    ref->prepare(sr, block);
    const auto before = ref->strikeNorm().inlineMeasureCount();
    const auto g = ref->strikeNorm().gainsForNoteOn(0, cfg);
    REQUIRE(ref->strikeNorm().inlineMeasureCount() == before);
    return g;
}

float hitGain(VoicePool& pool, std::uint8_t note, float velocity = 1.0f)
{
    pool.noteOn(note, velocity);
    const int slot = activeSlotFor(pool, note);
    REQUIRE(slot >= 0);
    return pool.mainVoiceRef(slot).lastStrikeNormGains().body;
}

} // namespace

TEST_CASE("StrikeNormTable: edits, worker drains and note-ons in a fixed order give the same gains",
          "[voice_pool][strike_norm][threading]")
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 64;
    auto pool = std::make_unique<VoicePool>();
    pool->prepare(kSr, kBlock);
    auto& table = pool->strikeNorm();
    table.setManualConsumer(true);
    std::vector<float> l(kBlock);
    std::vector<float> r(kBlock);
    auto block = [&]() { pool->processBlock(l.data(), r.data(), kBlock); };

    SECTION("edit between two blocks, drain, hit")
    {
        block();
        pool->padConfigMut(0).material = 0.8f;
        const auto ref = preparedReference(pool->padConfig(0), kSr, kBlock);
        block();
        const auto inline0 = table.inlineMeasureCount();
        const auto worker0 = table.workerMeasureCount();
        CHECK(table.drainOnce());
        const float g = hitGain(*pool, 36);
        CHECK(g == Catch::Approx(ref.body).epsilon(1e-6));
        CHECK(table.inlineMeasureCount() == inline0);
        CHECK(table.workerMeasureCount() == worker0 + 1u);
    }

    SECTION("edit, hit before the drain, then drain and hit again")
    {
        pool->padConfigMut(0).material = 0.8f;
        const auto ref = preparedReference(pool->padConfig(0), kSr, kBlock);
        const auto inline0 = table.inlineMeasureCount();
        const float g1 = hitGain(*pool, 36);
        CHECK(table.inlineMeasureCount() == inline0 + 1u);
        CHECK(g1 == Catch::Approx(ref.body).epsilon(1e-6));
        block();
        (void)table.drainOnce();
        block();
        const float g2 = hitGain(*pool, 36);
        CHECK(g2 == Catch::Approx(g1).epsilon(1e-6));
        CHECK(table.inlineMeasureCount() == inline0 + 1u);
    }

    SECTION("two edits before one drain: the stale request is skipped")
    {
        block();
        pool->padConfigMut(0).material = 0.6f;
        block();
        pool->padConfigMut(0).material = 0.8f;
        block();
        const auto ref = preparedReference(pool->padConfig(0), kSr, kBlock);
        const auto worker0 = table.workerMeasureCount();
        const auto inline0 = table.inlineMeasureCount();
        CHECK(table.drainOnce());
        CHECK(table.workerMeasureCount() == worker0 + 1u);
        const float g = hitGain(*pool, 36);
        CHECK(g == Catch::Approx(ref.body).epsilon(1e-6));
        CHECK(table.inlineMeasureCount() == inline0);
    }

    SECTION("an edit while voices ring leaves the ringing voices' gains alone")
    {
        constexpr std::array<std::uint8_t, 4> kRinging{36, 37, 38, 39};
        std::array<int, 4> slots{};
        std::array<float, 4> gains{};
        for (std::size_t k = 0; k < kRinging.size(); ++k)
        {
            pool->noteOn(kRinging[k], 1.0f);
            slots[k] = activeSlotFor(*pool, kRinging[k]);
            REQUIRE(slots[k] >= 0);
            gains[k] = bodyGain(pool->mainVoiceRef(slots[k]));
        }
        block();
        pool->padConfigMut(0).material = 0.8f;
        const auto ref = preparedReference(pool->padConfig(0), kSr, kBlock);
        block();
        (void)table.drainOnce();
        block();
        for (std::size_t k = 0; k < kRinging.size(); ++k)
        {
            INFO("note " << int{kRinging[k]});
            CHECK(bodyGain(pool->mainVoiceRef(slots[k])) == gains[k]);
        }
        const float g = hitGain(*pool, 36);
        CHECK(g == Catch::Approx(ref.body).epsilon(1e-6));
    }
}

// ------------------------------------------------------------------------------
// (i) THREADING: the worker thread against the audio thread
// ------------------------------------------------------------------------------
TEST_CASE("StrikeNormTable: worker thread against the audio thread under random edits",
          "[voice_pool][strike_norm][threading]")
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 64;
    auto pool = std::make_unique<VoicePool>();
    pool->prepare(kSr, kBlock);
    pool->strikeNorm().startWorker();

    struct Record
    {
        PadConfig cfg;
        StrikeNormGains gains;
        float velocity = 1.0f;
    };
    std::vector<Record> records;
    records.reserve(6000);

    std::mt19937 rng(0x51A7E5u);
    std::uniform_int_distribution<int> editCount(0, 2);
    std::uniform_int_distribution<int> hitCount(0, 3);
    std::uniform_int_distribution<int> padDist(0, 3);
    std::uniform_int_distribution<int> fieldDist(0, 6);
    std::uniform_int_distribution<int> excDist(0, static_cast<int>(ExciterType::kCount) - 1);
    std::uniform_int_distribution<int> bodyDist(0, static_cast<int>(BodyModelType::kCount) - 1);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> velDist(0.3f, 1.0f);

    std::vector<float> l(kBlock);
    std::vector<float> r(kBlock);
    bool allFinite = true;
    for (int b = 0; b < 3000; ++b)
    {
        for (int e = editCount(rng); e > 0; --e)
        {
            auto& c = pool->padConfigMut(padDist(rng));
            switch (fieldDist(rng))
            {
            case 0: c.size = unit(rng); break;
            case 1: c.material = unit(rng); break;
            case 2: c.fmRatio = unit(rng); break;
            case 3: c.frictionPressure = unit(rng); break;
            case 4: c.exciterType = static_cast<ExciterType>(excDist(rng)); break;
            case 5: c.bodyModel = static_cast<BodyModelType>(bodyDist(rng)); break;
            default:
                c.secondaryEnabled = (unit(rng) < 0.5f) ? 0.0f : 1.0f;
                c.couplingStrength = unit(rng);
                break;
            }
        }
        for (int h = hitCount(rng); h > 0; --h)
        {
            const int pad = padDist(rng);
            const auto note = static_cast<std::uint8_t>(36 + pad);
            const float vel = velDist(rng);
            pool->noteOn(note, vel);
            const int slot = activeSlotFor(*pool, note);
            REQUIRE(slot >= 0);
            records.push_back({pool->padConfig(pad),
                               pool->mainVoiceRef(slot).lastStrikeNormGains(), vel});
        }
        pool->processBlock(l.data(), r.data(), kBlock);
        for (int i = 0; i < kBlock; ++i)
            allFinite = allFinite && Krate::DSP::detail::isFinite(l[static_cast<std::size_t>(i)]) &&
                        Krate::DSP::detail::isFinite(r[static_cast<std::size_t>(i)]);
    }
    pool->strikeNorm().stopWorker();
    std::printf("StrikeNormTable stress: %zu note-ons, worker measured %u, inline measured %u\n",
                records.size(), pool->strikeNorm().workerMeasureCount(),
                pool->strikeNorm().inlineMeasureCount());
    CHECK(allFinite);
    REQUIRE(!records.empty());

    // Reference: the same measurement on a fresh probe voice, at velocity 1.0,
    // or at the note's velocity step for an FMImpulse pad (T1.18).
    auto probe = preparedVoice(kSr);
    struct Ref
    {
        PadConfig       cfg;
        float           velocity;
        StrikeNormGains gains;
    };
    std::vector<Ref> refs;
    int mismatches = 0;
    for (const auto& rec : records)
    {
        const float refVel =
            DrumVoice::strikeNormFollowsVelocity(rec.cfg.exciterType)
                ? static_cast<float>(DrumVoice::strikeNormVelocityStep(rec.velocity)) / 127.0f
                : 1.0f;
        auto it = std::find_if(refs.begin(), refs.end(), [&rec, refVel](const Ref& e) {
            return e.cfg == rec.cfg && e.velocity == refVel;
        });
        if (it == refs.end())
        {
            refs.push_back({rec.cfg, refVel, StrikeNormTable::measure(*probe, rec.cfg, refVel)});
            it = std::prev(refs.end());
        }
        // A String body runs a waveguide, not the modal bank: its bank gain
        // never reaches the audio and is not a strike-norm measurement (it is
        // the impulse bound of whatever modes the bank held before), so only
        // the shell gain is compared for it.
        const bool bodyIsMeasured = rec.cfg.bodyModel != BodyModelType::String;
        const bool bodyOk = !bodyIsMeasured ||
                            rec.gains.body == Catch::Approx(it->gains.body).epsilon(1e-6);
        const bool shellOk = rec.gains.shell == Catch::Approx(it->gains.shell).epsilon(1e-6);
        if (!bodyOk || !shellOk)
        {
            if (++mismatches <= 5)
                std::printf("mismatch: exc %d body %d gains %g/%g ref %g/%g\n",
                            static_cast<int>(rec.cfg.exciterType),
                            static_cast<int>(rec.cfg.bodyModel), rec.gains.body,
                            rec.gains.shell, it->gains.body, it->gains.shell);
        }
    }
    INFO(mismatches << " of " << records.size() << " note-ons differ from the reference");
    CHECK(mismatches == 0);
}

// ------------------------------------------------------------------------------
// (j) the inline fallback measurement is allocation-free
// ------------------------------------------------------------------------------
TEST_CASE("StrikeNormTable: note-on with a changed pad is allocation-free",
          "[voice_pool][strike_norm][allocation]")
{
    auto pool = std::make_unique<VoicePool>();
    pool->prepare(48000.0, 64);
    std::vector<float> l(64);
    std::vector<float> r(64);
    pool->processBlock(l.data(), r.data(), 64);

    auto& c = pool->padConfigMut(0);
    REQUIRE(c.exciterType == ExciterType::Impulse);
    REQUIRE(c.bodyModel == BodyModelType::Membrane);
    c.exciterType = ExciterType::FMImpulse;
    c.bodyModel = BodyModelType::Bell;

    const auto inline0 = pool->strikeNorm().inlineMeasureCount();
    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();
    pool->noteOn(36, 1.0f);
    const std::size_t allocations = detector.stopTracking();

    CHECK(pool->strikeNorm().inlineMeasureCount() == inline0 + 1u);
    INFO("allocations during the note-on: " << allocations);
    CHECK(allocations == 0u);
}

// ------------------------------------------------------------------------------
// (k) T1.18 (MB-260): an FMImpulse pad's gain is per MIDI velocity step
// ------------------------------------------------------------------------------
TEST_CASE("StrikeNormTable: an FMImpulse pad serves every MIDI velocity from its table",
          "[voice_pool][strike_norm][velocity]")
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 64;
    PadConfig fm;
    fm.exciterType = ExciterType::FMImpulse;
    fm.bodyModel = BodyModelType::Bell;
    fm.size = 0.22f;
    std::vector<float> l(kBlock);
    std::vector<float> r(kBlock);

    // Reference: velocity step k on a fresh probe voice.
    auto probe = preparedVoice(kSr);
    auto refGain = [&](int k) {
        return StrikeNormTable::measure(*probe, fm, static_cast<float>(k) / 127.0f).body;
    };

    SECTION("prepared table: 127 velocities, no inline measurement")
    {
        auto pool = std::make_unique<VoicePool>();
        pool->padConfigMut(0) = fm;
        const auto t0 = std::chrono::steady_clock::now();
        pool->prepare(kSr, kBlock);
        const auto t1 = std::chrono::steady_clock::now();
        std::printf("StrikeNormTable: prepare with one FMImpulse pad took %.1f ms\n",
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
        const auto inline0 = pool->strikeNorm().inlineMeasureCount();
        double worstUs = 0.0;
        for (int k = 1; k <= 127; ++k)
        {
            const auto a = std::chrono::steady_clock::now();
            const float g = hitGain(*pool, 36, static_cast<float>(k) / 127.0f);
            const auto b = std::chrono::steady_clock::now();
            worstUs = std::max(worstUs, std::chrono::duration<double, std::micro>(b - a).count());
            if (k % 18 == 1 || k == 127)
            {
                INFO("velocity step " << k);
                CHECK(g == Catch::Approx(refGain(k)).epsilon(1e-6));
            }
            pool->processBlock(l.data(), r.data(), kBlock);
            pool->noteOff(36);
        }
        std::printf("StrikeNormTable: 127 FMImpulse velocities, slowest note-on %.2f us\n", worstUs);
        CHECK(pool->strikeNorm().inlineMeasureCount() == inline0);
    }

    SECTION("an edit before the worker delivers measures only the steps played")
    {
        auto pool = std::make_unique<VoicePool>();
        pool->prepare(kSr, kBlock);
        auto& table = pool->strikeNorm();
        table.setManualConsumer(true);
        pool->processBlock(l.data(), r.data(), kBlock);
        pool->padConfigMut(0) = fm;
        const auto inline0 = table.inlineMeasureCount();

        CHECK(hitGain(*pool, 36, 100.0f / 127.0f) == Catch::Approx(refGain(100)).epsilon(1e-6));
        CHECK(hitGain(*pool, 36, 100.0f / 127.0f) == Catch::Approx(refGain(100)).epsilon(1e-6));
        CHECK(table.inlineMeasureCount() == inline0 + 1u);
        CHECK(hitGain(*pool, 36, 64.0f / 127.0f) == Catch::Approx(refGain(64)).epsilon(1e-6));
        CHECK(table.inlineMeasureCount() == inline0 + 2u);

        pool->processBlock(l.data(), r.data(), kBlock);
        CHECK(table.drainOnce());
        pool->processBlock(l.data(), r.data(), kBlock);
        CHECK(hitGain(*pool, 36, 37.0f / 127.0f) == Catch::Approx(refGain(37)).epsilon(1e-6));
        CHECK(hitGain(*pool, 36, 1.0f) == Catch::Approx(refGain(127)).epsilon(1e-6));
        CHECK(table.inlineMeasureCount() == inline0 + 2u);
    }
}
