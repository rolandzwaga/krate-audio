// ==============================================================================
// FMImpulse strike normalisation follows the note's velocity (T1.18)
// ==============================================================================
// MB-260: the strike-normalisation probe drove the exciter at velocity 1.0, but
//   FMImpulse's velocity moves its spectrum (carrier 400 x 6.25^v Hz, sidebands,
//   modulation index), not only its level. On sparse high-Q Bell bodies a gain
//   measured with the 2500 Hz carrier was wrong at every other velocity: Glass
//   Bell Garden 46 read 1.846 x the body budget at v 100/127 and its level
//   jumped up and down with velocity.
// MB-261 (VERIFY-FIRST): FM Ratio steps on the default-kit hat with FMImpulse
//   forced swung the ring level (rms500) by up to 13.5 dB between adjacent
//   1/32 steps.
//
// Harness (the audit limiter probe's, specs/membrum-audit/probes/
// audit_limiter_probe.cpp.txt): one heap DrumVoice per render, 48 kHz, blocks of
// 256, the pad's PadConfig from the factory .vstpreset through State::readKitBlob,
// State::applyPadSnapshot and applyPadConfigToVoice.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/default_kit.h"
#include "dsp/drum_voice.h"
#include "dsp/pad_config.h"
#include "dsp/pad_config_apply.h"
#include "state/state_codec.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "pluginterfaces/base/ibstream.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr double kSR = 48000.0;
constexpr int    kBS = 256;

bool loadKit(const char* category, const char* name, Membrum::State::KitSnapshot& snap)
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
    return Membrum::State::readKitBlob(&ms, snap) == kResultOk;
}

struct Meas
{
    float peak   = 0.0f;
    float rms500 = 0.0f;
};

Meas hit(Membrum::DrumVoice& v, const Membrum::PadConfig& cfg, float vel, float seconds)
{
    Membrum::applyPadConfigToVoice(v, cfg);
    v.noteOn(vel);
    const int total = static_cast<int>(seconds * kSR);
    std::vector<float> y(static_cast<std::size_t>(total), 0.0f);
    for (int off = 0; off < total; off += kBS)
        v.processBlock(y.data() + off, std::min(kBS, total - off));
    Meas m;
    const int n500 = std::min(total, static_cast<int>(0.5 * kSR));
    double acc = 0.0;
    for (int i = 0; i < total; ++i)
    {
        const float a = std::fabs(y[static_cast<std::size_t>(i)]);
        m.peak = std::max(m.peak, a);
        if (i < n500)
            acc += static_cast<double>(a) * a;
    }
    m.rms500 = static_cast<float>(std::sqrt(acc / std::max(1, n500)));
    return m;
}

// Every post-bank stage neutral (the audit limiter probe's neutralAll()).
Membrum::PadConfig neutralAll(Membrum::PadConfig c)
{
    c.tsDriveAmount     = 0.0f;
    c.tsFoldAmount      = 0.0f;
    c.tsFilterCutoff    = 1.0f;
    c.tsFilterEnvAmount = 0.5f;
    c.modeInjectAmount  = 0.0f;
    c.nonlinearCoupling = 0.0f;
    c.noiseLayerMix     = 0.0f;
    c.clickLayerMix     = 0.0f;
    c.secondaryEnabled  = 0.0f;
    c.tsPitchEnvTime    = 0.0f;
    c.tensionModAmt     = 0.0f;
    c.morphEnabled      = 0.0f;
    return c;
}

std::unique_ptr<Membrum::DrumVoice> freshVoice()
{
    auto v = std::make_unique<Membrum::DrumVoice>();
    v->prepare(kSR, 0u);
    return v;
}

float db(float x)
{
    return 20.0f * std::log10(std::max(x, 1.0e-9f));
}

} // namespace

// ------------------------------------------------------------------------------
// MB-260
// ------------------------------------------------------------------------------
TEST_CASE("FMImpulse strike: voice peak follows velocity on bell pads",
          "[fm_impulse][gain_staging][velocity]")
{
    struct PadRef
    {
        const char* cat;
        const char* kit;
        int         note;
    };
    const std::array<PadRef, 3> pads{{
        {"Unnatural", "Glass Bell Garden", 46},
        {"Percussive", "Latin Perc", 46},
        {"Electronic", "LinnDrum CR-78", 40},
    }};

    std::vector<int> velocities;
    for (int k = 32; k <= 124; k += 4)
        velocities.push_back(k);
    velocities.push_back(127);

    for (const auto& pr : pads)
    {
        Membrum::State::KitSnapshot snap;
        REQUIRE(loadKit(pr.cat, pr.kit, snap));
        Membrum::PadConfig cfg;
        Membrum::State::applyPadSnapshot(snap.pads[static_cast<std::size_t>(pr.note - 36)], cfg);
        REQUIRE(cfg.exciterType == Membrum::ExciterType::FMImpulse);
        cfg = neutralAll(cfg);
        const float level = std::max(cfg.level, 1.0e-6f);

        float prevPeak = -1.0f;
        int   prevK    = 0;
        for (const int k : velocities)
        {
            const float v = static_cast<float>(k) / 127.0f;
            auto voice = freshVoice();
            const Meas m = hit(*voice, cfg, v, 0.6f);
            const float norm = m.peak / (level * v);
            INFO(pr.kit << " note " << pr.note << " velocity " << k << "/127: peak "
                        << db(m.peak) << " dBFS, normalised " << norm);
            CHECK(norm <= 0.56f);
            if (prevPeak >= 0.0f)
            {
                INFO("previous velocity " << prevK << "/127 peaked at " << db(prevPeak) << " dBFS");
                CHECK(m.peak >= prevPeak * std::pow(10.0f, -1.0f / 20.0f));
            }
            prevPeak = m.peak;
            prevK    = k;
        }
    }
}

// ------------------------------------------------------------------------------
// MB-261 (VERIFY-FIRST)
// ------------------------------------------------------------------------------
TEST_CASE("FMImpulse: FM Ratio steps do not jump the ring level",
          "[fm_impulse][velocity]")
{
    std::array<Membrum::PadConfig, Membrum::kNumPads> kit{};
    Membrum::DefaultKit::apply(kit);
    Membrum::PadConfig base = kit[6];  // MIDI 42
    base.exciterType = Membrum::ExciterType::FMImpulse;
    constexpr float kVel = 100.0f / 127.0f;

    float prevDb = 0.0f;
    for (int s = 0; s <= 32; ++s)
    {
        Membrum::PadConfig c = base;
        c.fmRatio = static_cast<float>(s) / 32.0f;
        auto voice = freshVoice();
        const float rmsDb = db(hit(*voice, c, kVel, 0.5f).rms500);
        if (s > 0)
        {
            INFO("FM Ratio " << (s - 1) << "/32 -> " << s << "/32: rms500 " << prevDb
                             << " -> " << rmsDb << " dB");
            CHECK(std::abs(rmsDb - prevDb) < 6.0f);
        }
        prevDb = rmsDb;
    }
}
