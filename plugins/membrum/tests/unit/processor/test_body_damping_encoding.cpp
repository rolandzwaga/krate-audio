// ==============================================================================
// Body damping B1 / B3: one representable encoding, Decay stays live (T1.23)
// ==============================================================================
// Fix plan T1.23 (MB-074, MB-029, MB-109, MB-216).
//
// MB-074: the body-damping "derive" sentinel (-1) had no parameter value. The
//   controller showed 0.5 for a pad the DSP derived, and writing that displayed
//   value back (or nudging it by 1 %) replaced the derived law with an absolute
//   b1 = 25.1 s^-1, cutting the default kick and toms to a fraction of their ring.
// MB-029 / MB-109: with an explicit override (every factory pad) the override
//   REPLACED the derived law, so per-pad Decay, Material's damping role and the
//   Tightness / Body Size macro decay deltas did not move a modal body's ring.
//
// Harness: a real Membrum::Processor at 48 kHz, block 256, main bus only; per-pad
// parameters through Krate::Test::ParameterChanges, notes through
// Krate::Test::EventList. decay60 is measured on the main-bus envelope (10 ms
// RMS frames of (L + R) / 2): the time from the loudest frame to where the
// envelope falls 60 dB below it for good (crossing interpolated in dB between
// frames). The noise layer may be seeded per voice, so the cases compare decay60
// and RMS, never raw samples.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "dsp/bodies/body_damping_encoding.h"
#include "dsp/default_kit.h"
#include "dsp/macro_offsets.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"
#include "processor/processor.h"
#include "state/state_codec.h"

#include "vst_event_list.h"
#include "vst_param_changes.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr double kSR = 48000.0;
constexpr int    kBS = 256;
constexpr float  kVel100 = 100.0f / 127.0f;
constexpr int    kFrame = 480;  // 10 ms

// ------------------------------------------------------------------------------
// Rig: one Processor, main stereo bus only.
// ------------------------------------------------------------------------------
struct Rig
{
    std::unique_ptr<Membrum::Processor> proc = std::make_unique<Membrum::Processor>();
    std::array<float, kBS> left{};
    std::array<float, kBS> right{};
    std::array<float*, 2> chans{left.data(), right.data()};
    AudioBusBuffers bus{};
    ProcessData data{};
    Krate::Test::EventList events;
    Krate::Test::ParameterChanges params;

    Rig()
    {
        proc->initialize(nullptr);
        ProcessSetup s{};
        s.processMode = kRealtime;
        s.symbolicSampleSize = kSample32;
        s.maxSamplesPerBlock = kBS;
        s.sampleRate = kSR;
        proc->setupProcessing(s);
        proc->setActive(true);

        bus.numChannels = 2;
        bus.channelBuffers32 = chans.data();
        bus.silenceFlags = 0;
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kBS;
        data.numOutputs = 1;
        data.outputs = &bus;
        data.numInputs = 0;
        data.inputs = nullptr;
        data.inputParameterChanges = &params;
        data.outputParameterChanges = nullptr;
        data.inputEvents = &events;
        data.outputEvents = nullptr;
        data.processContext = nullptr;
    }

    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;

    ~Rig()
    {
        proc->setActive(false);
        proc->terminate();
    }

    void block()
    {
        left.fill(0.0f);
        right.fill(0.0f);
        proc->process(data);
        events.clear();
        params.clear();
    }

    void setParams(const std::vector<std::pair<ParamID, double>>& values)
    {
        for (const auto& [id, v] : values)
            params.add(id, v);
        block();
    }

    /// One hit at block start, `seconds` of mono (L + R) / 2.
    std::vector<float> hit(int note, float velocity, double seconds)
    {
        events.addNoteOn(static_cast<int16>(note), velocity, 0);
        const int blocks = static_cast<int>(std::ceil(seconds * kSR / kBS));
        std::vector<float> mono;
        mono.reserve(static_cast<std::size_t>(blocks * kBS));
        for (int b = 0; b < blocks; ++b)
        {
            block();
            for (int i = 0; i < kBS; ++i)
                mono.push_back(0.5f * (left[static_cast<std::size_t>(i)] +
                                       right[static_cast<std::size_t>(i)]));
        }
        return mono;
    }
};

struct Decay
{
    double seconds = 0.0;
    bool   capped  = false;
};

/// decay60 of a hit: 10 ms RMS frames; from the loudest frame to where the
/// envelope falls 60 dB below it and stays there, interpolated in dB between the
/// last frame above and the first frame below.
Decay decay60(const std::vector<float>& y)
{
    const std::size_t frames = y.size() / kFrame;
    std::vector<double> db(frames, -300.0);
    for (std::size_t f = 0; f < frames; ++f)
    {
        double acc = 0.0;
        for (int i = 0; i < kFrame; ++i)
        {
            const double s = y[f * kFrame + static_cast<std::size_t>(i)];
            acc += s * s;
        }
        const double rms = std::sqrt(acc / kFrame);
        db[f] = rms > 1.0e-15 ? 20.0 * std::log10(rms) : -300.0;
    }
    std::size_t peak = 0;
    for (std::size_t f = 1; f < frames; ++f)
        if (db[f] > db[peak])
            peak = f;
    const double floorDb = db[peak] - 60.0;
    std::size_t last = peak;
    for (std::size_t f = peak; f < frames; ++f)
        if (db[f] > floorDb)
            last = f;
    Decay d;
    const double frameS = static_cast<double>(kFrame) / kSR;
    if (last + 1 >= frames)
    {
        d.seconds = static_cast<double>(frames - peak) * frameS;
        d.capped = true;
        return d;
    }
    const double a = db[last];
    const double b = std::max(db[last + 1], floorDb - 60.0);
    const double t = (a - floorDb) / std::max(1.0e-9, a - b);
    d.seconds = (static_cast<double>(last - peak) + std::clamp(t, 0.0, 1.0)) * frameS;
    return d;
}

double rmsDb(const std::vector<float>& y, double seconds)
{
    const std::size_t n = std::min(y.size(), static_cast<std::size_t>(seconds * kSR));
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        acc += static_cast<double>(y[i]) * y[i];
    const double r = std::sqrt(acc / static_cast<double>(std::max<std::size_t>(1, n)));
    return r > 1.0e-15 ? 20.0 * std::log10(r) : -300.0;
}

std::vector<std::uint8_t> processorState(Membrum::Processor& p)
{
    MemoryStream s;
    REQUIRE(p.getState(&s) == kResultOk);
    const auto size = static_cast<std::size_t>(s.getSize());
    std::vector<std::uint8_t> bytes(size);
    s.seek(0, IBStream::kIBSeekSet, nullptr);
    int32 got = 0;
    s.read(bytes.data(), static_cast<int32>(size), &got);
    return bytes;
}

void loadControllerState(Membrum::Controller& c, const std::vector<std::uint8_t>& bytes)
{
    MemoryStream s;
    s.write(const_cast<std::uint8_t*>(bytes.data()), static_cast<int32>(bytes.size()), nullptr);
    s.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(c.setComponentState(&s) == kResultOk);
}

ParamID padId(int pad, int offset)
{
    return static_cast<ParamID>(Membrum::padParamId(pad, offset));
}

/// Load a factory kit into the processor through the VST3 preset file, the way a
/// host restores a component.
bool loadFactoryKit(Membrum::Processor& proc, const std::string& category, const std::string& name)
{
    const auto path = std::filesystem::path(MEMBRUM_RESOURCES_DIR) / "presets" / "Kit Presets" /
                      category / (name + ".vstpreset");
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
    return pf.restoreComponentState(static_cast<IComponent*>(&proc));
}

/// decay60 of one pad's modal body at per-pad Decay `decay`. The rulings of
/// 2026-11-24 measure the Decay bar on the modal body: the pad's noise- and
/// click-layer mixes and its tone-shaper Drive and Fold are set to 0 in the same
/// block (the shaper's DC-blocker recovery tail is T4.27, not the ring). Each
/// Decay setting renders on a FRESH processor (third round, option (A)), so both
/// settings get the identical hit: exciters vary per hit on one processor.
/// `category` / `kit` name a factory kit; nullptr means the default kit.
Decay padDecayAt(const char* category, const char* kit, int pad, double decay, double seconds)
{
    Rig rig;
    if (category != nullptr)
    {
        REQUIRE(loadFactoryKit(*rig.proc, category, kit));
        rig.block();
    }
    rig.setParams({{padId(pad, Membrum::kPadDecay), decay},
                   {padId(pad, Membrum::kPadNoiseLayerMix), 0.0},
                   {padId(pad, Membrum::kPadClickLayerMix), 0.0},
                   {padId(pad, Membrum::kPadTSDriveAmount), 0.0},
                   {padId(pad, Membrum::kPadTSFoldAmount), 0.0}});
    const auto y = rig.hit(36 + pad, kVel100, seconds);
    return decay60(y);
}

/// The pads whose tail is not the modal body (ruling 2026-11-24): NoiseBody
/// with the NoiseBurst exciter (the NoiseBody noise path sets the tail),
/// Feedback-exciter pads (the loop sets it) and Clap pads (the burst train and
/// its noise tail set it). The Decay bar does not apply to them.
bool tailIsNotTheModalBody(const Membrum::PadConfig& cfg)
{
    using Membrum::BodyModelType;
    using Membrum::ExciterType;
    return (cfg.bodyModel == BodyModelType::NoiseBody &&
            cfg.exciterType == ExciterType::NoiseBurst) ||
           cfg.exciterType == ExciterType::Feedback || cfg.exciterType == ExciterType::Clap;
}

struct KitPad
{
    const char* category;
    const char* kit;
    int         note;
    const char* what;
};

constexpr std::array<const char*, 20> kKitNames{{
    "Acoustic/Acoustic Studio Kit", "Acoustic/Jazz Brushes", "Acoustic/Orchestral",
    "Acoustic/Rock Big Room", "Acoustic/Vintage Wood", "Electronic/808 Electronic Kit",
    "Electronic/909 Drum Machine", "Electronic/LinnDrum CR-78", "Electronic/Modular West Coast",
    "Electronic/Trap Modern", "Percussive/Cajon and Frames", "Percussive/Hand Drums",
    "Percussive/Latin Perc", "Percussive/Tabla", "Percussive/World Metal",
    "Unnatural/Chaos Engine", "Unnatural/Drone and Sustain", "Unnatural/Experimental FX Kit",
    "Unnatural/Ghost Bones", "Unnatural/Glass Bell Garden",
}};

} // namespace

// ------------------------------------------------------------------------------
// MB-074: the knob shows the value the DSP uses.
// ------------------------------------------------------------------------------
TEST_CASE("Body damping: writing the controller's displayed B1/B3 value back to the processor "
          "leaves every pad's decay unchanged",
          "[body_damping][param_plumbing]")
{
    std::vector<std::uint8_t> state;
    {
        Rig source;
        state = processorState(*source.proc);
    }
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);
    loadControllerState(controller, state);

    int checked = 0;
    for (int pad = 0; pad < Membrum::kNumPads; ++pad)
    {
        Rig untouched;
        if (untouched.proc->voicePoolForTest().padConfig(pad).enabled < 0.5f)
            continue;
        const auto ref = untouched.hit(36 + pad, kVel100, 2.0);

        Rig written;
        const double b1 = controller.getParamNormalized(padId(pad, Membrum::kPadBodyDampingB1));
        const double b3 = controller.getParamNormalized(padId(pad, Membrum::kPadBodyDampingB3));
        written.setParams({{padId(pad, Membrum::kPadBodyDampingB1), b1},
                           {padId(pad, Membrum::kPadBodyDampingB3), b3}});
        const auto got = written.hit(36 + pad, kVel100, 2.0);

        const Decay dRef = decay60(ref);
        const Decay dGot = decay60(got);
        const double rRef = rmsDb(ref, 1.0);
        const double rGot = rmsDb(got, 1.0);
        INFO("note " << 36 + pad << ": displayed b1 " << b1 << " b3 " << b3 << "; decay60 "
                     << dRef.seconds << " s -> " << dGot.seconds << " s; rms(1 s) " << rRef
                     << " -> " << rGot << " dB");
        CHECK(dGot.seconds == Catch::Approx(dRef.seconds).epsilon(0.05));
        CHECK(std::abs(rGot - rRef) <= 0.1);
        ++checked;
    }
    CHECK(checked > 0);
    controller.terminate();
}

// ------------------------------------------------------------------------------
// MB-074: a small knob move makes a small change.
// ------------------------------------------------------------------------------
TEST_CASE("Body damping: a 0.01 nudge of Body Damping b1 or b3 from the displayed value changes "
          "decay60 by less than 10 %",
          "[body_damping][param_plumbing]")
{
    std::vector<std::uint8_t> state;
    {
        Rig source;
        state = processorState(*source.proc);
    }
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);
    loadControllerState(controller, state);

    // Default kit: kick 36 and low floor tom 41 derive their b1; snare 38 has an
    // explicit b1 / b3 (default_kit.h).
    for (const int note : {36, 41, 38})
    {
        const int pad = note - 36;
        Rig untouched;
        const Decay ref = decay60(untouched.hit(note, kVel100, 3.0));
        for (const int offset : {static_cast<int>(Membrum::kPadBodyDampingB1),
                                 static_cast<int>(Membrum::kPadBodyDampingB3)})
        {
            const double shown = controller.getParamNormalized(padId(pad, offset));
            const double nudged = shown > 0.99 ? shown - 0.01 : shown + 0.01;
            Rig rig;
            rig.setParams({{padId(pad, offset), nudged}});
            const Decay got = decay60(rig.hit(note, kVel100, 3.0));
            const double ratio = got.seconds / ref.seconds;
            INFO("note " << note << (offset == Membrum::kPadBodyDampingB1 ? " b1 " : " b3 ")
                         << shown << " -> " << nudged << ": decay60 " << ref.seconds << " s -> "
                         << got.seconds << " s, ratio " << ratio);
            CHECK(ratio >= 0.90);
            CHECK(ratio <= 1.10);
        }
    }
    controller.terminate();
}

// ------------------------------------------------------------------------------
// MB-029 / MB-109 (VERIFY-FIRST): per-pad Decay moves the modal ring of factory
// pads. Pads chosen from specs/membrum-audit/probes/kit-pads.csv: one kit per
// category, a Membrane, a Plate or Shell and a Bell pad each. Measured on the
// modal body (noise and click layers at 0, padDecayAt), per the user's ruling of
// 2026-11-24, under which Decay also scales an overridden b3 (ruling (a')).
// ------------------------------------------------------------------------------
TEST_CASE("Body damping: per-pad Decay changes the modal ring of factory-kit pads",
          "[body_damping][param_plumbing][preset]")
{
    SECTION("default kit snare 38 (explicit b1 0.60 / b3 0.40)")
    {
        const Decay lo = padDecayAt(nullptr, nullptr, 2, 0.2, 6.0);
        const Decay hi = padDecayAt(nullptr, nullptr, 2, 0.8, 6.0);
        INFO("decay60 " << lo.seconds << " s -> " << hi.seconds << " s");
        CHECK(hi.seconds / lo.seconds >= 2.0);
    }

    static constexpr std::array<KitPad, 12> kPads{{
        {"Acoustic", "Acoustic Studio Kit", 41, "Membrane tom"},
        {"Acoustic", "Acoustic Studio Kit", 37, "Shell"},
        {"Acoustic", "Acoustic Studio Kit", 51, "Bell"},
        {"Electronic", "Modular West Coast", 38, "Plate"},
        {"Electronic", "LinnDrum CR-78", 41, "Membrane tom"},
        {"Electronic", "LinnDrum CR-78", 51, "Bell"},
        {"Percussive", "Latin Perc", 60, "Membrane tom"},
        {"Percussive", "Cajon and Frames", 36, "Plate"},
        {"Percussive", "Latin Perc", 44, "Bell"},
        {"Unnatural", "Experimental FX Kit", 55, "Membrane tom"},
        {"Unnatural", "Experimental FX Kit", 41, "Plate"},
        {"Unnatural", "Experimental FX Kit", 49, "Bell"},
    }};
    for (const auto& kp : kPads)
    {
        CAPTURE(kp.kit, kp.note, kp.what);
        const int pad = kp.note - 36;
        {
            Rig rig;
            REQUIRE(loadFactoryKit(*rig.proc, kp.category, kp.kit));
            rig.block();
            REQUIRE_FALSE(tailIsNotTheModalBody(rig.proc->voicePoolForTest().padConfig(pad)));
        }
        const Decay lo = padDecayAt(kp.category, kp.kit, pad, 0.2, 6.0);
        const Decay hi = padDecayAt(kp.category, kp.kit, pad, 0.8, 6.0);
        INFO("decay60 " << lo.seconds << " s -> " << hi.seconds << " s"
                        << (hi.capped ? " (capped)" : ""));
        CHECK(hi.seconds / lo.seconds >= 2.0);
    }
}

TEST_CASE("Body damping: per-pad Decay changes the modal ring of every factory-kit modal pad",
          "[body_damping][param_plumbing][preset][long]")
{
    int swept = 0;
    int failed = 0;
    double lowest = 1.0e9;
    std::string lowestPad;
    std::string report;
    std::string excluded;
    for (const char* entry : kKitNames)
    {
        const std::string full(entry);
        const auto slash = full.find('/');
        const std::string category = full.substr(0, slash);
        const std::string kit = full.substr(slash + 1);
        Rig rig;
        REQUIRE(loadFactoryKit(*rig.proc, category, kit));
        rig.block();
        for (int pad = 0; pad < Membrum::kNumPads; ++pad)
        {
            const auto& cfg = rig.proc->voicePoolForTest().padConfig(pad);
            if (cfg.enabled < 0.5f || cfg.bodyModel == Membrum::BodyModelType::String)
                continue;
            if (tailIsNotTheModalBody(cfg))
            {
                excluded += kit + " " + std::to_string(36 + pad) + "\n";
                continue;
            }
            const Decay lo = padDecayAt(category.c_str(), kit.c_str(), pad, 0.2, 6.0);
            const Decay hi = padDecayAt(category.c_str(), kit.c_str(), pad, 0.8, 6.0);
            const double ratio = hi.seconds / lo.seconds;
            ++swept;
            if (ratio < lowest)
            {
                lowest = ratio;
                lowestPad = kit + " " + std::to_string(36 + pad);
            }
            if (!(ratio >= 2.0))
            {
                ++failed;
                report += kit + " note " + std::to_string(36 + pad) + " body " +
                          std::to_string(static_cast<int>(cfg.bodyModel)) + " exciter " +
                          std::to_string(static_cast<int>(cfg.exciterType)) + ": decay60 " +
                          std::to_string(lo.seconds) + " s -> " + std::to_string(hi.seconds) +
                          " s, ratio " + std::to_string(ratio) + "\n";
            }
        }
    }
    WARN("excluded (tail not the modal body):\n" << excluded);
    WARN("swept " << swept << " pads, " << failed << " below a 2x Decay ratio; lowest "
                  << lowest << " (" << lowestPad << ")");
    INFO(report);
    INFO("swept " << swept << " pads, " << failed << " below a 2x Decay ratio");
    CHECK(swept > 0);
    CHECK(failed == 0);
}

// ------------------------------------------------------------------------------
// The encoding itself (dsp/bodies/membrane_mapper.h).
// ------------------------------------------------------------------------------
TEST_CASE("Body damping: the encoding's derived value reproduces dampingLawFromLegacy exactly",
          "[body_damping]")
{
    using Krate::DSP::ModalResonatorBank;
    std::array<Membrum::PadConfig, Membrum::kNumPads> kit{};
    Membrum::DefaultKit::apply(kit);

    struct Case
    {
        const char*       what;
        Membrum::PadConfig cfg;
    };
    const std::array<Case, 4> cases{{
        {"default PadConfig", Membrum::PadConfig{}},
        {"default-kit kick 36", kit[0]},
        {"default-kit floor tom 41", kit[5]},
        {"default-kit hat 42", kit[6]},
    }};
    for (const auto& c : cases)
    {
        CAPTURE(c.what);
        auto params = Membrum::Bodies::bodyParamsFromPad(c.cfg);
        params.bodyDampingB1 = Membrum::kBodyDampingDerived;
        params.bodyDampingB3 = Membrum::kBodyDampingDerived;
        for (const auto body : {Membrum::BodyModelType::Membrane, Membrum::BodyModelType::Plate,
                                Membrum::BodyModelType::Shell, Membrum::BodyModelType::Bell,
                                Membrum::BodyModelType::NoiseBody})
        {
            CAPTURE(static_cast<int>(body));
            Membrum::Bodies::MapperResult r;
            switch (body)
            {
            case Membrum::BodyModelType::Plate: r = Membrum::Bodies::PlateMapper::map(params, 0.0f); break;
            case Membrum::BodyModelType::Shell: r = Membrum::Bodies::ShellMapper::map(params, 0.0f); break;
            case Membrum::BodyModelType::Bell:  r = Membrum::Bodies::BellMapper::map(params, 0.0f); break;
            case Membrum::BodyModelType::NoiseBody:
                r = Membrum::Bodies::NoiseBodyMapper::map(params, 0.0f).modal;
                break;
            default: r = Membrum::Bodies::MembraneMapper::map(params, 0.0f); break;
            }
            const auto legacy = ModalResonatorBank::dampingLawFromLegacy(r.decayTime, r.brightness);
            CHECK(r.damping.b1 == legacy.b1);
            CHECK(r.damping.b3 == legacy.b3);
        }
    }
    // The default kit's derived pads store exactly the derived value.
    CHECK(kit[0].bodyDampingB1 == Membrum::kBodyDampingDerived);
    CHECK(kit[0].bodyDampingB3 == Membrum::kBodyDampingDerived);
    CHECK(kit[5].bodyDampingB1 == Membrum::kBodyDampingDerived);
    CHECK(Membrum::PadConfig{}.bodyDampingB1 == Membrum::kBodyDampingDerived);
    CHECK(Membrum::PadConfig{}.bodyDampingB3 == Membrum::kBodyDampingDerived);
}

TEST_CASE("Body damping: dampingLawFromParams pins the documented ranges", "[body_damping]")
{
    // Typed from the doc comment in membrane_mapper.h (MB-216), not computed by
    // the code under test:
    //   derived law at decayTime 0.5 s, brightness 0.5: b1 = 2 s^-1, b3 = 2e-5 s
    //   derived law at decayTime 0.05 s, brightness 1:  b1 = 20 s^-1, b3 = 0
    //   b1 = max(0.2, b1d * 2^(12 (n - 0.5))),  b3 = max(0, b3d + (n - 0.5) 5e-3) * D
    //   D = 10^(0.5 - Decay) for an overridden b3, each decayTime through the
    //   [0.01, 5] s clamp (ruling (a'), 2026-11-24): D = b1d(Decay) / b1d(0.5)
    struct Row
    {
        float decayTime, brightness, decay, b1Norm, b3Norm, wantB1, wantB3;
    };
    const std::array<Row, 15> rows{{
        {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 2.0f, 2.0e-5f},
        {0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.2f, 2.0e-5f},       // 2/64 = 0.03125, floored
        {0.5f, 0.5f, 0.5f, 1.0f, 0.5f, 128.0f, 2.0e-5f},     // x64
        {0.5f, 0.5f, 0.5f, 0.75f, 0.5f, 16.0f, 2.0e-5f},     // x8
        {0.5f, 0.5f, 0.5f, 0.25f, 0.5f, 0.25f, 2.0e-5f},     // x1/8
        {0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 2.0f, 0.0f},          // 2e-5 - 2.5e-3, floored
        {0.5f, 0.5f, 0.5f, 0.5f, 1.0f, 2.0f, 2.52e-3f},      // 2e-5 + 2.5e-3
        {0.5f, 0.5f, 0.5f, 0.5f, 0.75f, 2.0f, 1.27e-3f},     // 2e-5 + 1.25e-3
        {0.05f, 1.0f, 0.5f, 0.0f, 1.0f, 0.3125f, 2.5e-3f},   // 20/64, 0 + 2.5e-3
        {0.05f, 1.0f, 0.5f, 1.0f, 0.0f, 1280.0f, 0.0f},      // 20 x 64, floored
        // Decay scales an overridden b3 (D), never a derived one.
        {0.5f, 0.5f, 0.0f, 0.5f, 1.0f, 2.0f, 7.968940e-3f},  // 2.52e-3 x 3.162278
        {0.5f, 0.5f, 1.0f, 0.5f, 1.0f, 2.0f, 7.968940e-4f},  // 2.52e-3 x 0.3162278
        {0.5f, 0.5f, 0.0f, 0.5f, 0.75f, 2.0f, 4.016093e-3f}, // 1.27e-3 x 3.162278
        {0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 2.0f, 2.0e-5f},       // derived: unscaled
        {4.0f, 0.5f, 0.0f, 0.5f, 1.0f, 0.25f, 3.15e-3f},     // ref 12.6 s clamps to 5: D 1.25
    }};
    for (const auto& row : rows)
    {
        CAPTURE(row.decayTime, row.brightness, row.decay, row.b1Norm, row.b3Norm);
        Membrum::VoiceCommonParams p{};
        p.decay = row.decay;
        p.bodyDampingB1 = row.b1Norm;
        p.bodyDampingB3 = row.b3Norm;
        const auto law =
            Membrum::Bodies::dampingLawFromParams(p, row.decayTime, row.brightness);
        CHECK(law.b1 == Catch::Approx(row.wantB1).epsilon(1e-5));
        CHECK(law.b3 == Catch::Approx(row.wantB3).epsilon(1e-5).margin(1e-10));
    }
    // An explicit 0.5 is the derived law, never a fixed absolute value: two
    // pads with different Decay get different b1 at the same stored value.
    Membrum::VoiceCommonParams p{};
    p.bodyDampingB1 = 0.5f;
    p.bodyDampingB3 = 0.5f;
    CHECK(Membrum::Bodies::dampingLawFromParams(p, 0.25f, 0.5f).b1 == Catch::Approx(4.0f));
    CHECK(Membrum::Bodies::dampingLawFromParams(p, 1.0f, 0.5f).b1 == Catch::Approx(1.0f));
}

// ------------------------------------------------------------------------------
// STATE-COMPAT: version 5 stored absolute b1 / b3 (b1 = 0.2 + n * 49.8 s^-1,
// b3 = n * 1e-3 s, -1 = derive). The read path re-encodes them so every pad
// plays the law it played. state_v5_macros.bin is the version-5 state T1.22
// captured on the code before it (default kit with macros: derived kick / tom /
// hat pads, explicit snare / cymbal / clap pads). Its render fingerprints
// (test_macro_offsets.cpp) pin the sound of notes 36 and 38.
// ------------------------------------------------------------------------------
namespace {

std::vector<char> readGoldenBytes(const char* name)
{
    const auto path =
        std::filesystem::path(MEMBRUM_RESOURCES_DIR) / ".." / "tests" / "golden" / name;
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Krate::DSP::ModalResonatorBank::DampingLaw playedLaw(const Membrum::PadConfig& stored)
{
    return Membrum::Bodies::playedBodyDamping(Membrum::applyMacroOffsets(stored));
}

Krate::DSP::ModalResonatorBank::DampingLaw legacyLaw(const Membrum::PadConfig& played,
                                                     double rawB1, double rawB3)
{
    const auto derived = Membrum::Bodies::derivedBodyDamping(
        Membrum::Bodies::bodyParamsFromPad(played), played.bodyModel);
    return {.b1 = rawB1 < 0.0 ? derived.b1 : 0.2f + static_cast<float>(rawB1) * 49.8f,
            .b3 = rawB3 < 0.0 ? derived.b3 : static_cast<float>(rawB3) * 1.0e-3f};
}

} // namespace

TEST_CASE("Body damping: a version-5 state and pad preset keep the damping law they played",
          "[body_damping][state]")
{
    auto bytes = readGoldenBytes("state_v5_macros.bin");
    int32 version = 0;
    std::memcpy(&version, bytes.data(), sizeof(version));
    REQUIRE(version == 5);

    // The fixture's raw slots: read with its version patched to the current
    // one, so no migration runs.
    Membrum::State::KitSnapshot raw;
    {
        auto patched = bytes;
        const int32 current = Membrum::State::kBlobVersion;
        std::memcpy(patched.data(), &current, sizeof(current));
        MemoryStream s;
        s.write(patched.data(), static_cast<int32>(patched.size()), nullptr);
        s.seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(Membrum::State::readKitBlob(&s, raw) == kResultOk);
    }
    Membrum::State::KitSnapshot migrated;
    {
        MemoryStream s;
        s.write(bytes.data(), static_cast<int32>(bytes.size()), nullptr);
        s.seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(Membrum::State::readKitBlob(&s, migrated) == kResultOk);
    }

    int derivedPads = 0;
    int explicitPads = 0;
    for (int p = 0; p < Membrum::kNumPads; ++p)
    {
        CAPTURE(p);
        const auto& rawPad = raw.pads[static_cast<std::size_t>(p)];
        Membrum::PadConfig played;  // version-5 fields: what the pad played
        Membrum::State::applyPadSnapshot(rawPad, played);
        const auto want = legacyLaw(played, rawPad.sound[42], rawPad.sound[43]);

        Membrum::PadConfig stored;
        Membrum::State::applyPadSnapshot(migrated.pads[static_cast<std::size_t>(p)], stored);
        const auto got = playedLaw(stored);
        INFO("raw b1 " << rawPad.sound[42] << " b3 " << rawPad.sound[43] << " -> stored "
                       << stored.bodyDampingB1 << " / " << stored.bodyDampingB3 << "; law "
                       << got.b1 << " / " << got.b3 << ", was " << want.b1 << " / " << want.b3);
        CHECK(got.b1 == Catch::Approx(want.b1).epsilon(1e-5));
        CHECK(got.b3 == Catch::Approx(want.b3).margin(1e-9));
        if (rawPad.sound[42] < 0.0)
        {
            ++derivedPads;
            CHECK(stored.bodyDampingB1 == Membrum::kBodyDampingDerived);
        }
        else
        {
            ++explicitPads;
        }
    }
    CHECK(derivedPads > 0);
    CHECK(explicitPads > 0);

    // A version-5 pad preset: absolute snare damping (b1 0.60, b3 0.40).
    std::array<Membrum::PadConfig, Membrum::kNumPads> kit{};
    Membrum::DefaultKit::apply(kit);
    Membrum::PadConfig snare = kit[2];
    auto preset = Membrum::State::toPadPresetSnapshot(snare);
    preset.sound[42] = 0.60;
    preset.sound[43] = 0.40;
    MemoryStream ps;
    REQUIRE(Membrum::State::writePadPresetBlob(&ps, preset) == kResultOk);
    ps.seek(0, IBStream::kIBSeekSet, nullptr);
    int32 v5 = 5;
    int32 written = 0;
    ps.write(&v5, sizeof(v5), &written);
    ps.seek(0, IBStream::kIBSeekSet, nullptr);
    Membrum::State::PadPresetSnapshot readBack;
    REQUIRE(Membrum::State::readPadPresetBlob(&ps, readBack) == kResultOk);
    Membrum::PadConfig loaded;
    Membrum::State::applyPadPresetSnapshot(readBack, loaded);
    const auto law = playedLaw(loaded);
    CHECK(law.b1 == Catch::Approx(0.2 + 0.60 * 49.8).epsilon(1e-5));
    CHECK(law.b3 == Catch::Approx(0.40e-3).margin(1e-9));
    // The default kit's own snare already plays that law.
    const auto kitLaw = playedLaw(snare);
    CHECK(kitLaw.b1 == Catch::Approx(0.2 + 0.60 * 49.8).epsilon(1e-5));
    CHECK(kitLaw.b3 == Catch::Approx(0.40e-3).margin(1e-9));
}

// ------------------------------------------------------------------------------
// STATE-COMPAT: the regenerated factory kits (body damping re-encoded relative
// to each pad's derived law) survive a save / load cycle byte for byte.
// ------------------------------------------------------------------------------
TEST_CASE("Body damping: getState(setState(kit)) is idempotent for the 20 factory kits",
          "[body_damping][state][preset]")
{
    std::vector<std::uint8_t> defaultState;
    {
        Rig fresh;
        defaultState = processorState(*fresh.proc);
    }
    for (const char* entry : kKitNames)
    {
        const std::string full(entry);
        CAPTURE(full);
        const auto slash = full.find('/');
        Rig first;
        REQUIRE(loadFactoryKit(*first.proc, full.substr(0, slash), full.substr(slash + 1)));
        first.block();
        const auto saved = processorState(*first.proc);
        CHECK(saved != defaultState);  // the kit is what was saved

        Rig second;
        MemoryStream in;
        in.write(const_cast<std::uint8_t*>(saved.data()), static_cast<int32>(saved.size()),
                 nullptr);
        in.seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(second.proc->setState(&in) == kResultOk);
        second.block();
        const auto again = processorState(*second.proc);
        CHECK(again.size() == saved.size());
        CHECK(again == saved);

        int differing = 0;
        for (int pad = 0; pad < Membrum::kNumPads; ++pad)
        {
            const auto& a = first.proc->voicePoolForTest().padConfig(pad);
            const auto& b = second.proc->voicePoolForTest().padConfig(pad);
            if (a.bodyDampingB1 != b.bodyDampingB1 || a.bodyDampingB3 != b.bodyDampingB3)
                ++differing;
        }
        CHECK(differing == 0);
    }
}
