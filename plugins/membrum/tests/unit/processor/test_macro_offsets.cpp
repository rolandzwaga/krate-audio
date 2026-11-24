// ==============================================================================
// Macros are offsets on a stored base value (Membrum audit fix plan T1.22:
// MB-073, MB-081, MB-173, STATE-COMPAT)
// ==============================================================================
// A pad's stored fields are base values; its five macros are offsets applied
// when the engine reads the pad (dsp/macro_offsets.h). So a macro sweep that
// returns to neutral restores the pad, Punch below neutral never switches on a
// pitch envelope, a direct edit after a macro move keeps the macro's
// contribution, and a version-5 state (whose stored fields are what it played)
// loads with its macro positions and its sound kept.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "dsp/default_kit.h"
#include "dsp/macro_offsets.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"
#include "processor/processor.h"
#include "state/state_codec.h"

#include "low_frequency_metrics.h"
#include "render_fingerprint.h"
#include "vst_event_list.h"
#include "vst_param_changes.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;
using Krate::DSP::TestUtils::compareFingerprints;
using Krate::DSP::TestUtils::fingerprintRender;
using Krate::DSP::TestUtils::RenderFingerprint;

namespace {

constexpr int32  kMoBlock      = 256;
constexpr double kMoSampleRate = 48000.0;

ParamID padId(int pad, int offset)
{
    return static_cast<ParamID>(Membrum::padParamId(pad, offset));
}

std::unique_ptr<Membrum::Processor> makeProcessor()
{
    auto proc = std::make_unique<Membrum::Processor>();
    REQUIRE(proc->initialize(nullptr) == kResultOk);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kMoBlock;
    setup.sampleRate = kMoSampleRate;
    REQUIRE(proc->setupProcessing(setup) == kResultOk);
    REQUIRE(proc->setActive(true) == kResultOk);
    return proc;
}

void shutdown(Membrum::Processor& proc)
{
    proc.setActive(false);
    proc.terminate();
}

// One block; the left channel is appended to `out` when given.
void processBlock(Membrum::Processor& proc, IParameterChanges* changes, IEventList* events,
                  std::vector<float>* out = nullptr)
{
    std::array<float, kMoBlock> outL{};
    std::array<float, kMoBlock> outR{};
    std::array<float*, 2> chans{outL.data(), outR.data()};
    AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.channelBuffers32 = chans.data();
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kMoBlock;
    data.numOutputs = 1;
    data.outputs = &bus;
    data.inputParameterChanges = changes;
    data.inputEvents = events;
    REQUIRE(proc.process(data) == kResultOk);
    if (out != nullptr)
        out->insert(out->end(), outL.begin(), outL.end());
}

void processChanges(Membrum::Processor& proc,
                    std::initializer_list<std::pair<ParamID, double>> changes)
{
    Krate::Test::ParameterChanges pc;
    for (const auto& [id, value] : changes)
        pc.addChange(id, value);
    processBlock(proc, &pc, nullptr);
}

// A hit of `note` at `velocity`, `samples` long (left channel).
std::vector<float> renderHit(Membrum::Processor& proc, int16 note, float velocity, int samples)
{
    Krate::Test::EventList events;
    events.addNoteOn(note, velocity);
    std::vector<float> out;
    while (std::cmp_less(out.size(), samples))
    {
        processBlock(proc, nullptr, &events, &out);
        events.clear();
    }
    out.resize(static_cast<std::size_t>(samples));
    return out;
}

std::vector<char> stateBytes(Membrum::Processor& proc)
{
    MemoryStream stream;
    REQUIRE(proc.getState(&stream) == kResultOk);
    const char* data = stream.getData();
    return {data, data + stream.getSize()};
}

tresult setStateBytes(Membrum::Processor& proc, const std::vector<char>& bytes)
{
    MemoryStream stream;
    stream.write(const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), nullptr);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    return proc.setState(&stream);
}

void checkSameSound(const std::vector<float>& got, const std::vector<float>& want)
{
    const auto cmp = compareFingerprints(fingerprintRender(got), fingerprintRender(want));
    INFO("worst metric error " << cmp.worstMetricRelativeError << ", worst checkpoint error "
         << cmp.worstSampleError << " (" << cmp.detail << ")");
    CHECK(cmp.withinTolerance());
}

float fieldAt(const Membrum::PadConfig& cfg, int offset)
{
    switch (offset)
    {
    case Membrum::kPadMaterial:           return cfg.material;
    case Membrum::kPadSize:               return cfg.size;
    case Membrum::kPadDecay:              return cfg.decay;
    case Membrum::kPadDecaySkew:          return cfg.decaySkew;
    case Membrum::kPadTSFilterCutoff:     return cfg.tsFilterCutoff;
    case Membrum::kPadModeInjectAmount:   return cfg.modeInjectAmount;
    case Membrum::kPadModeStretch:        return cfg.modeStretch;
    case Membrum::kPadTSPitchEnvStart:    return cfg.tsPitchEnvStart;
    case Membrum::kPadTSPitchEnvTime:     return cfg.tsPitchEnvTime;
    case Membrum::kPadNoiseBurstDuration: return cfg.noiseBurstDuration;
    case Membrum::kPadCouplingAmount:     return cfg.couplingAmount;
    case Membrum::kPadNonlinearCoupling:  return cfg.nonlinearCoupling;
    default:                              return -1.0f;
    }
}

struct MacroTargets
{
    int macroOffset;
    const char* name;
    std::vector<int> fields;
};

const std::array<MacroTargets, 5>& macroTargets()
{
    static const std::array<MacroTargets, 5> t{{
        {Membrum::kPadMacroTightness, "Tightness",
         {Membrum::kPadMaterial, Membrum::kPadDecay, Membrum::kPadDecaySkew}},
        {Membrum::kPadMacroBrightness, "Brightness",
         {Membrum::kPadTSFilterCutoff, Membrum::kPadModeInjectAmount}},
        {Membrum::kPadMacroBodySize, "Body Size",
         {Membrum::kPadSize, Membrum::kPadModeStretch, Membrum::kPadDecay}},
        {Membrum::kPadMacroPunch, "Punch",
         {Membrum::kPadTSPitchEnvStart, Membrum::kPadTSPitchEnvTime,
          Membrum::kPadNoiseBurstDuration}},
        {Membrum::kPadMacroComplexity, "Complexity",
         {Membrum::kPadCouplingAmount, Membrum::kPadNonlinearCoupling,
          Membrum::kPadModeInjectAmount}},
    }};
    return t;
}

constexpr int kHitSamples = 24000; // 0.5 s

} // namespace

// ==============================================================================
// MB-173, MB-081: a sweep that returns to neutral restores the pad.
// ==============================================================================
TEST_CASE("Macro offsets: a macro sweep back to neutral restores the pad",
          "[macro_mapper][macro_offsets][roundtrip]")
{
    for (const auto& macro : macroTargets())
    {
        for (const float start : {0.0f, 0.1f, 0.9f, 1.0f})
        {
            CAPTURE(macro.name, start);

            // Reference: the fields set, idle blocks where the sweep runs.
            auto ref = makeProcessor();
            auto swept = makeProcessor();
            for (auto* proc : {ref.get(), swept.get()})
            {
                Krate::Test::ParameterChanges pc;
                for (const int f : macro.fields)
                    pc.addChange(padId(0, f), start);
                processBlock(*proc, &pc, nullptr);
            }

            const ParamID macroId = padId(0, macro.macroOffset);
            for (const double m : {1.0, 0.0, 0.5})
            {
                processChanges(*swept, {{macroId, m}});
                processBlock(*ref, nullptr, nullptr);
            }
            processBlock(*ref, nullptr, nullptr);
            processBlock(*swept, nullptr, nullptr);

            const auto& cfg = swept->voicePoolForTest().padConfig(0);
            for (const int f : macro.fields)
            {
                CAPTURE(f);
                CHECK(fieldAt(cfg, f) == start);
            }

            const auto want = renderHit(*ref, 36, 0.8f, kHitSamples);
            const auto got = renderHit(*swept, 36, 0.8f, kHitSamples);
            checkSameSound(got, want);

            shutdown(*ref);
            shutdown(*swept);
        }
    }
}

// ==============================================================================
// MB-081: Punch below neutral on a pad without a pitch envelope.
// ==============================================================================
TEST_CASE("Macro offsets: Punch below neutral leaves an un-enveloped pad at its pitch",
          "[macro_mapper][macro_offsets][pitch]")
{
    constexpr int kPad = 2; // default-kit snare, note 38: no pitch envelope
    {
        std::array<Membrum::PadConfig, Membrum::kNumPads> pads;
        Membrum::DefaultKit::apply(pads);
        REQUIRE(pads[kPad].tsPitchEnvTime == 0.0f);
    }

    const auto peakHz = [](double punch) {
        auto proc = makeProcessor();
        processChanges(*proc, {{padId(kPad, Membrum::kPadNoiseLayerMix), 0.0},
                               {padId(kPad, Membrum::kPadClickLayerMix), 0.0},
                               {padId(kPad, Membrum::kPadMacroPunch), punch}});
        const auto hit = renderHit(*proc, 38, 0.8f, 48000);
        shutdown(*proc);
        constexpr std::size_t kFrame = 16384;
        const auto from = static_cast<std::size_t>(0.030 * kMoSampleRate);
        REQUIRE(from + kFrame <= hit.size());
        return Krate::DSP::TestUtils::estimatePeakFrequencyHz(
            hit.data() + from, kFrame, static_cast<float>(kMoSampleRate));
    };

    const float neutral = peakHz(0.5);
    const float less = peakHz(0.45);
    INFO("peak frequency at Punch 0.5: " << neutral << " Hz, at Punch 0.45: " << less << " Hz");
    REQUIRE(neutral > 0.0f);
    REQUIRE(less > 0.0f);
    const double ratio = static_cast<double>(less) / static_cast<double>(neutral);
    CHECK(ratio >= std::exp2(-1.0 / 12.0));
    CHECK(ratio <= std::exp2(1.0 / 12.0));
}

// ==============================================================================
// MB-073 (a): a direct edit after a macro move keeps the macro's contribution.
// ==============================================================================
TEST_CASE("Macro offsets: a direct edit after a macro move keeps the macro's contribution",
          "[macro_mapper][macro_offsets][param_plumbing]")
{
    auto first = makeProcessor();
    auto second = makeProcessor();
    const double material =
        static_cast<double>(first->voicePoolForTest().padConfig(0).material) + 0.01;
    const ParamID tightId = padId(0, Membrum::kPadMacroTightness);
    const ParamID materialId = padId(0, Membrum::kPadMaterial);

    // Tightness first, then Material; against Material first, then Tightness.
    processChanges(*first, {{tightId, 1.0}});
    processChanges(*first, {{materialId, material}});
    processChanges(*second, {{materialId, material}});
    processChanges(*second, {{tightId, 1.0}});

    const auto a = renderHit(*first, 36, 0.8f, 48000);
    const auto b = renderHit(*second, 36, 0.8f, 48000);
    checkSameSound(a, b);

    shutdown(*first);
    shutdown(*second);
}

// ==============================================================================
// STATE-COMPAT: a version-5 state keeps its macro positions and its sound.
// ==============================================================================
namespace {

std::vector<char> readGolden(const char* name)
{
    const auto path =
        std::filesystem::path(MEMBRUM_RESOURCES_DIR) / ".." / "tests" / "golden" / name;
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void setVersion(std::vector<char>& bytes, int32 version)
{
    REQUIRE(bytes.size() >= sizeof(version));
    std::memcpy(bytes.data(), &version, sizeof(version));
}

int32 versionOf(const std::vector<char>& bytes)
{
    int32 v = 0;
    std::memcpy(&v, bytes.data(), sizeof(v));
    return v;
}

// The fixture's own fields, read without a migration (its version patched to
// the current one).
Membrum::State::KitSnapshot rawSnapshot(std::vector<char> bytes)
{
    setVersion(bytes, Membrum::State::kBlobVersion);
    MemoryStream stream;
    stream.write(bytes.data(), static_cast<int32>(bytes.size()), nullptr);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    Membrum::State::KitSnapshot kit;
    REQUIRE(Membrum::State::readKitBlob(&stream, kit) == kResultOk);
    return kit;
}

// Fingerprints of notes 36 and 38 (1 s at velocity 0.8, a fresh processor
// loaded with state_v5_macros.bin), captured on the code before T1.22.
RenderFingerprint fixtureFingerprint36()
{
    RenderFingerprint fp;
    fp.rms = 0.0391502087;
    fp.peak = 0.271602988;
    fp.meanAbs = 0.0192590071;
    fp.totalVariation = 7.56436017;
    fp.checkpoints = {-8.00471633e-10f, 0.0907932296f, -0.0800138786f, -0.10773246f,
                      -0.00389413675f, 0.0353759974f, -0.0326498412f, 0.0149046723f,
                      -0.000246857031f, -0.0102336872f, 0.0152278142f, -0.0146536501f,
                      0.0115573127f, -0.00657388195f, 0.00114142871f, 0.00285632117f,
                      -0.00534008769f, 0.00660762144f, -0.0064645349f, 0.00534646213f,
                      -0.0039113313f, 0.00234944583f, -0.000882462715f, -0.000193318963f,
                      0.000856285333f, -0.00119885709f, 0.00123786309f, -0.00105721015f,
                      0.000794986147f, -0.000509569771f, 0.000239032626f, -3.57553436e-05f};
    return fp;
}

RenderFingerprint fixtureFingerprint38()
{
    RenderFingerprint fp;
    fp.rms = 0.0119471537;
    fp.peak = 0.220545009;
    fp.meanAbs = 0.00214164303;
    fp.totalVariation = 26.7017971;
    fp.checkpoints = {-1.33540198e-05f, -0.0244827475f, 0.00859569665f, 0.000523929775f,
                      -0.000770995917f, -0.000251841673f, 2.08035362e-05f, -1.11809493e-06f,
                      2.39934241e-08f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    return fp;
}

std::vector<float> renderLoaded(const std::vector<char>& state, int16 note)
{
    auto proc = makeProcessor();
    REQUIRE(setStateBytes(*proc, state) == kResultOk);
    processBlock(*proc, nullptr, nullptr);
    auto out = renderHit(*proc, note, 0.8f, 48000);
    shutdown(*proc);
    return out;
}

} // namespace

TEST_CASE("Macro offsets: a version-5 state loads with its macro positions and its sound kept",
          "[macro_mapper][macro_offsets][state]")
{
    const auto v5 = readGolden("state_v5_macros.bin");
    REQUIRE(versionOf(v5) == 5);

    auto proc = makeProcessor();
    REQUIRE(setStateBytes(*proc, v5) == kResultOk);
    processBlock(*proc, nullptr, nullptr);

    // Macro positions kept.
    const auto& pool = proc->voicePoolForTest();
    for (int p = 0; p < Membrum::kNumPads; ++p)
    {
        CAPTURE(p);
        const auto& cfg = pool.padConfig(p);
        CHECK(cfg.macroTightness == Approx(p == 0 ? 0.8 : 0.5).margin(1e-7));
        CHECK(cfg.macroBrightness == Approx(p == 2 ? 0.2 : 0.5).margin(1e-7));
        CHECK(cfg.macroBodySize == 0.5f);
        CHECK(cfg.macroPunch == 0.5f);
        CHECK(cfg.macroComplexity == 0.5f);
    }

    // Every stored field is base = clamp01(field - delta) of the fixture's
    // field, and the fixture's macros keep every base inside [0, 1].
    const auto raw = rawSnapshot(v5);
    for (int p = 0; p < Membrum::kNumPads; ++p)
    {
        CAPTURE(p);
        Membrum::PadConfig field;
        Membrum::State::applyPadSnapshot(raw.pads[static_cast<std::size_t>(p)], field);
        int clamped = -1;
        const auto want = Membrum::baseFromEffective(field, &clamped);
        CHECK(clamped == 0);
        const auto& got = pool.padConfig(p);
        for (const int f : {Membrum::kPadMaterial, Membrum::kPadSize, Membrum::kPadDecay,
                            Membrum::kPadDecaySkew, Membrum::kPadTSFilterCutoff,
                            Membrum::kPadModeInjectAmount, Membrum::kPadModeStretch,
                            Membrum::kPadTSPitchEnvStart, Membrum::kPadTSPitchEnvTime,
                            Membrum::kPadNoiseBurstDuration, Membrum::kPadCouplingAmount,
                            Membrum::kPadNonlinearCoupling})
        {
            CAPTURE(f);
            CHECK(fieldAt(got, f) == fieldAt(want, f));
        }
    }

    // The captured sound.
    for (const int16 note : {int16{36}, int16{38}})
    {
        CAPTURE(note);
        const auto cmp = compareFingerprints(
            fingerprintRender(renderLoaded(v5, note)),
            note == 36 ? fixtureFingerprint36() : fixtureFingerprint38());
        INFO("worst metric error " << cmp.worstMetricRelativeError << ", worst checkpoint error "
             << cmp.worstSampleError << " (" << cmp.detail << ")");
        CHECK(cmp.withinTolerance());
    }

    // Written as version 6, and byte-idempotent from there.
    const auto saved = stateBytes(*proc);
    CHECK(versionOf(saved) == 6);
    auto again = makeProcessor();
    REQUIRE(setStateBytes(*again, saved) == kResultOk);
    processBlock(*again, nullptr, nullptr);
    CHECK(stateBytes(*again) == saved);

    // Versions no release wrote are rejected and leave the state untouched.
    for (const int32 bad : {3, 4, 7})
    {
        CAPTURE(bad);
        auto blob = saved;
        setVersion(blob, bad);
        CHECK(setStateBytes(*again, blob) == kResultFalse);
        processBlock(*again, nullptr, nullptr);
        CHECK(stateBytes(*again) == saved);
    }

    shutdown(*again);
    shutdown(*proc);
}

TEST_CASE("Macro offsets: a version-5 pad preset loads unchanged but for its body damping",
          "[macro_mapper][macro_offsets][state]")
{
    std::array<Membrum::PadConfig, Membrum::kNumPads> pads;
    Membrum::DefaultKit::apply(pads);
    auto src = Membrum::State::toPadPresetSnapshot(pads[2]);
    src.sound[0] = 0.37;

    MemoryStream written;
    REQUIRE(Membrum::State::writePadPresetBlob(&written, src) == kResultOk);
    std::vector<char> bytes(written.getData(), written.getData() + written.getSize());
    CHECK(versionOf(bytes) == 6);

    for (const int32 v : {5, 6})
    {
        CAPTURE(v);
        setVersion(bytes, v);
        MemoryStream in;
        in.write(bytes.data(), static_cast<int32>(bytes.size()), nullptr);
        in.seek(0, IBStream::kIBSeekSet, nullptr);
        Membrum::State::PadPresetSnapshot got;
        REQUIRE(Membrum::State::readPadPresetBlob(&in, got) == kResultOk);
        CHECK(got.exciterType == src.exciterType);
        CHECK(got.bodyModel == src.bodyModel);
        if (v == 6)
        {
            CHECK(got.sound == src.sound);
            continue;
        }
        // Fix plan T1.23: version 5 stored body damping as absolute values
        // (b1 = 0.2 + n * 49.8 s^-1, b3 = n * 1e-3 s); the read path
        // re-encodes slots 42 / 43 relative to the pad's derived law. Every
        // other slot reads unchanged.
        auto rest = got.sound;
        rest[42] = src.sound[42];
        rest[43] = src.sound[43];
        CHECK(rest == src.sound);
        Membrum::PadConfig played;
        Membrum::State::applyPadPresetSnapshot(src, played);
        const auto want = Membrum::Bodies::encodeLegacyBodyDamping(played, src.sound[42],
                                                                    src.sound[43]);
        CHECK(got.sound[42] == static_cast<double>(want.b1));
        CHECK(got.sound[43] == static_cast<double>(want.b3));
    }
    for (const int32 v : {3, 4, 7})
    {
        CAPTURE(v);
        setVersion(bytes, v);
        MemoryStream in;
        in.write(bytes.data(), static_cast<int32>(bytes.size()), nullptr);
        in.seek(0, IBStream::kIBSeekSet, nullptr);
        Membrum::State::PadPresetSnapshot got;
        CHECK(Membrum::State::readPadPresetBlob(&in, got) == kResultFalse);
    }
}
