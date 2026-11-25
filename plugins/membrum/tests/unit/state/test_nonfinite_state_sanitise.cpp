// ==============================================================================
// Non-finite and out-of-range state, and the NaN rails (fix plan T2.2)
// ==============================================================================
// MB-010: the codec read every double raw and applyPadSnapshot /
//   applyPadPresetSnapshot copied sound slots 0-27 and 30-33 with a bare cast;
//   std::clamp passes a NaN through. A NaN or Inf in a state blob was stored and
//   written back by getState, and a NaN pad field made a voice emit NaN forever
//   and never retire (flushSilentModes compares energy < threshold, false for
//   NaN), and latched the coupling energy limiter.
// MB-154: hardClip is std::clamp, so it is not a NaN rail, and the bus limiter
//   relies on it; nothing on the output path classified finiteness.
//
// IEEE TU (plugins/membrum/tests/CMakeLists.txt): NaN and Inf are built from bit
// patterns read through a volatile; finiteness is classified only with
// Krate::DSP::detail::isFinite.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"
#include "processor/processor.h"
#include "state/state_codec.h"

#include <krate/dsp/core/db_utils.h>

#include "vst_event_list.h"
#include "vst_param_changes.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Krate::DSP::detail::isFinite;

namespace {

constexpr double kSR = 48000.0;
constexpr int    kBS = 256;
constexpr float  kVel100 = 100.0f / 127.0f;

// ------------------------------------------------------------------------------
// Non-finite values from bit patterns (no numeric_limits constants).
// ------------------------------------------------------------------------------
double doubleFromBits(std::uint64_t pattern)
{
    volatile std::uint64_t bits = pattern;
    const std::uint64_t b = bits;
    double d = 0.0;
    std::memcpy(&d, &b, sizeof(d));
    return d;
}

float floatFromBits(std::uint32_t pattern)
{
    volatile std::uint32_t bits = pattern;
    const std::uint32_t b = bits;
    float f = 0.0f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

double quietNaND() { return doubleFromBits(0x7FF8000000000000ULL); }
double posInfD()   { return doubleFromBits(0x7FF0000000000000ULL); }
double negInfD()   { return doubleFromBits(0xFFF0000000000000ULL); }
float  quietNaNF() { return floatFromBits(0x7FC00000u); }

struct Poison
{
    const char* name;
    double      value;
};

std::array<Poison, 3> poisons()
{
    return {{{"NaN", quietNaND()}, {"+Inf", posInfD()}, {"-Inf", negInfD()}}};
}

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
};

/// Main-bus samples of `blocks` blocks, L and R interleaved per block.
struct Render
{
    std::vector<float> l;
    std::vector<float> r;
    long nonFinite = 0;
};

void renderInto(Rig& rig, int blocks, Render& out)
{
    for (int b = 0; b < blocks; ++b)
    {
        rig.block();
        for (int i = 0; i < kBS; ++i)
        {
            const float sl = rig.left[static_cast<std::size_t>(i)];
            const float sr = rig.right[static_cast<std::size_t>(i)];
            if (!isFinite(sl)) ++out.nonFinite;
            if (!isFinite(sr)) ++out.nonFinite;
            out.l.push_back(sl);
            out.r.push_back(sr);
        }
    }
}

int blocksFor(double seconds)
{
    return static_cast<int>(std::ceil(seconds * kSR / kBS));
}

// ------------------------------------------------------------------------------
// Blob helpers.
// ------------------------------------------------------------------------------
std::vector<char> streamBytes(MemoryStream& s)
{
    const auto size = static_cast<std::size_t>(s.getSize());
    std::vector<char> bytes(size);
    s.seek(0, IBStream::kIBSeekSet, nullptr);
    int32 got = 0;
    s.read(bytes.data(), static_cast<int32>(size), &got);
    return bytes;
}

std::vector<char> processorState(Membrum::Processor& p)
{
    MemoryStream s;
    REQUIRE(p.getState(&s) == kResultOk);
    return streamBytes(s);
}

Membrum::State::KitSnapshot kitFromBytes(const std::vector<char>& bytes)
{
    MemoryStream s;
    s.write(const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), nullptr);
    s.seek(0, IBStream::kIBSeekSet, nullptr);
    Membrum::State::KitSnapshot kit;
    REQUIRE(Membrum::State::readKitBlob(&s, kit) == kResultOk);
    return kit;
}

std::vector<char> bytesFromKit(const Membrum::State::KitSnapshot& kit)
{
    MemoryStream s;
    REQUIRE(Membrum::State::writeKitBlob(&s, kit) == kResultOk);
    return streamBytes(s);
}

std::vector<char> bytesFromPadPreset(const Membrum::State::PadPresetSnapshot& preset)
{
    MemoryStream s;
    REQUIRE(Membrum::State::writePadPresetBlob(&s, preset) == kResultOk);
    return streamBytes(s);
}

void setProcessorState(Rig& rig, const std::vector<char>& bytes)
{
    MemoryStream s;
    s.write(const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), nullptr);
    s.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(rig.proc->setState(&s) == kResultOk);
    rig.block();  // the load applies at the top of the next process()
}

void setControllerState(Membrum::Controller& c, const std::vector<char>& bytes)
{
    MemoryStream s;
    s.write(const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), nullptr);
    s.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(c.setComponentState(&s) == kResultOk);
}

// ------------------------------------------------------------------------------
// Range checks. Documented ranges (state_codec.h, pad_config.h): every sound
// slot is normalised [0, 1], except 28 (choke group, [0, 8]), 29 (output bus,
// [0, 15]) and 57 (noiseLayerGain, [0.1, 16]). Per-pad coupling and macros,
// global coupling, snare buzz, tom resonance and master gain are [0, 1]; the
// coupling delay is [0.5, 2] ms.
// ------------------------------------------------------------------------------
struct Range
{
    double lo;
    double hi;
};

Range soundSlotRange(std::size_t slot)
{
    if (slot == 28) return {0.0, 8.0};
    if (slot == 29) return {0.0, 15.0};
    if (slot == 57) return {0.1, 16.0};
    return {0.0, 1.0};
}

struct Violations
{
    int count = 0;
    std::string first;

    void check(bool ok, const std::string& what)
    {
        if (ok)
            return;
        if (count == 0)
            first = what;
        ++count;
    }
};

bool finiteIn(double v, Range r)
{
    return isFinite(v) && v >= r.lo && v <= r.hi;
}

std::string describe(const char* field, double v)
{
    std::ostringstream os;
    os << field << " = " << v;
    return os.str();
}

void checkKit(const Membrum::State::KitSnapshot& kit, Violations& v)
{
    for (std::size_t p = 0; p < kit.pads.size(); ++p)
    {
        const auto& pad = kit.pads[p];
        for (std::size_t s = 0; s < pad.sound.size(); ++s)
        {
            std::ostringstream os;
            os << "pad " << p << " sound[" << s << "] = " << pad.sound[s];
            v.check(finiteIn(pad.sound[s], soundSlotRange(s)), os.str());
        }
        v.check(finiteIn(pad.couplingAmount, {0.0, 1.0}), describe("couplingAmount", pad.couplingAmount));
        for (double m : pad.macros)
            v.check(finiteIn(m, {0.0, 1.0}), describe("macro", m));
    }
    v.check(finiteIn(kit.globalCoupling, {0.0, 1.0}), describe("globalCoupling", kit.globalCoupling));
    v.check(finiteIn(kit.snareBuzz, {0.0, 1.0}), describe("snareBuzz", kit.snareBuzz));
    v.check(finiteIn(kit.tomResonance, {0.0, 1.0}), describe("tomResonance", kit.tomResonance));
    v.check(finiteIn(kit.couplingDelayMs, {0.5, 2.0}), describe("couplingDelayMs", kit.couplingDelayMs));
    v.check(finiteIn(kit.masterGainNorm, {0.0, 1.0}), describe("masterGainNorm", kit.masterGainNorm));
}

void checkController(Membrum::Controller& c, Violations& v)
{
    const int32 count = c.getParameterCount();
    for (int32 i = 0; i < count; ++i)
    {
        ParameterInfo info{};
        if (c.getParameterInfo(i, info) != kResultOk)
            continue;
        const double n = c.getParamNormalized(info.id);
        std::ostringstream os;
        os << "controller param " << info.id << " = " << n;
        v.check(finiteIn(n, {0.0, 1.0}), os.str());
    }
}

void sendPadPreset(Rig& rig, const std::vector<char>& bytes, int pad)
{
    auto msg = Steinberg::owned(new HostMessage());
    msg->setMessageID("LoadPadPreset");
    auto* attrs = msg->getAttributes();
    REQUIRE(attrs != nullptr);
    attrs->setBinary("bytes", bytes.data(), static_cast<Steinberg::uint32>(bytes.size()));
    attrs->setInt("pad", static_cast<Steinberg::int64>(pad));
    REQUIRE(rig.proc->notify(msg) == kResultOk);
    rig.block();
}

bool loadControllerPadPreset(Membrum::Controller& c, const std::vector<char>& bytes)
{
    MemoryStream s;
    s.write(const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), nullptr);
    s.seek(0, IBStream::kIBSeekSet, nullptr);
    return c.padPresetLoadProvider(&s);
}

double peakOf(const Render& r)
{
    double p = 0.0;
    for (std::size_t i = 0; i < r.l.size(); ++i)
        p = std::max({p, static_cast<double>(std::fabs(r.l[i])),
                      static_cast<double>(std::fabs(r.r[i]))});
    return p;
}

double rmsOf(const Render& r)
{
    double acc = 0.0;
    for (std::size_t i = 0; i < r.l.size(); ++i)
        acc += static_cast<double>(r.l[i]) * r.l[i] + static_cast<double>(r.r[i]) * r.r[i];
    return std::sqrt(acc / static_cast<double>(std::max<std::size_t>(1, 2 * r.l.size())));
}

double toDb(double x)
{
    return x > 1.0e-15 ? 20.0 * std::log10(x) : -300.0;
}

} // namespace

// ------------------------------------------------------------------------------
// MB-010: a NaN or Inf in a state blob loads as a finite in-range value.
// ------------------------------------------------------------------------------
TEST_CASE("State load: a NaN or Inf in any sound slot loads as a finite in-range value on both "
          "sides",
          "[state][nonfinite]")
{
    Rig rig;
    const auto baseBytes = processorState(*rig.proc);
    const auto base = kitFromBytes(baseBytes);
    {
        Violations clean;
        checkKit(base, clean);
        INFO("default kit: " << clean.first);
        REQUIRE(clean.count == 0);
    }

    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    Violations procV;
    Violations ctrlV;

    // Every sound slot of pad 0.
    for (std::size_t slot = 0; slot < base.pads[0].sound.size(); ++slot)
    {
        for (const auto& poison : poisons())
        {
            auto kit = base;
            kit.pads[0].sound[slot] = poison.value;
            const auto bytes = bytesFromKit(kit);

            setProcessorState(rig, bytes);
            const auto saved = kitFromBytes(processorState(*rig.proc));
            Violations v;
            checkKit(saved, v);
            procV.check(v.count == 0, "kit sound[" + std::to_string(slot) + "] " + poison.name +
                                          ": " + v.first);

            setControllerState(controller, bytes);
            Violations c;
            checkController(controller, c);
            ctrlV.check(c.count == 0, "kit sound[" + std::to_string(slot) + "] " + poison.name +
                                          ": " + c.first);
        }
    }

    // The kit-level doubles and pad 0's coupling amount and macros.
    using Poke = void (*)(Membrum::State::KitSnapshot&, double);
    struct KitField
    {
        const char* name;
        Poke        poke;
    };
    const std::array<KitField, 11> kitFields{{
        {"globalCoupling", [](Membrum::State::KitSnapshot& k, double x) { k.globalCoupling = x; }},
        {"snareBuzz", [](Membrum::State::KitSnapshot& k, double x) { k.snareBuzz = x; }},
        {"tomResonance", [](Membrum::State::KitSnapshot& k, double x) { k.tomResonance = x; }},
        {"couplingDelayMs", [](Membrum::State::KitSnapshot& k, double x) { k.couplingDelayMs = x; }},
        {"masterGainNorm", [](Membrum::State::KitSnapshot& k, double x) { k.masterGainNorm = x; }},
        {"pad0.couplingAmount",
         [](Membrum::State::KitSnapshot& k, double x) { k.pads[0].couplingAmount = x; }},
        {"pad0.macro0", [](Membrum::State::KitSnapshot& k, double x) { k.pads[0].macros[0] = x; }},
        {"pad0.macro1", [](Membrum::State::KitSnapshot& k, double x) { k.pads[0].macros[1] = x; }},
        {"pad0.macro2", [](Membrum::State::KitSnapshot& k, double x) { k.pads[0].macros[2] = x; }},
        {"pad0.macro3", [](Membrum::State::KitSnapshot& k, double x) { k.pads[0].macros[3] = x; }},
        {"pad0.macro4", [](Membrum::State::KitSnapshot& k, double x) { k.pads[0].macros[4] = x; }},
    }};
    for (const auto& field : kitFields)
    {
        for (const auto& poison : poisons())
        {
            auto kit = base;
            field.poke(kit, poison.value);
            const auto bytes = bytesFromKit(kit);

            setProcessorState(rig, bytes);
            const auto saved = kitFromBytes(processorState(*rig.proc));
            Violations v;
            checkKit(saved, v);
            procV.check(v.count == 0,
                        std::string(field.name) + " " + poison.name + ": " + v.first);

            setControllerState(controller, bytes);
            Violations c;
            checkController(controller, c);
            ctrlV.check(c.count == 0,
                        std::string(field.name) + " " + poison.name + ": " + c.first);
        }
    }

    // A pad preset through readPadPresetBlob: the processor's LoadPadPreset
    // path and the controller's pad-preset load provider.
    setProcessorState(rig, baseBytes);
    setControllerState(controller, baseBytes);
    Membrum::PadConfig pad0;
    Membrum::State::applyPadSnapshot(base.pads[0], pad0);
    const auto presetBase = Membrum::State::toPadPresetSnapshot(pad0);
    for (std::size_t slot = 0; slot < presetBase.sound.size(); ++slot)
    {
        for (const auto& poison : poisons())
        {
            auto preset = presetBase;
            preset.sound[slot] = poison.value;
            const auto bytes = bytesFromPadPreset(preset);

            sendPadPreset(rig, bytes, 0);
            const auto saved = kitFromBytes(processorState(*rig.proc));
            Violations v;
            checkKit(saved, v);
            procV.check(v.count == 0, "pad preset sound[" + std::to_string(slot) + "] " +
                                          poison.name + ": " + v.first);

            REQUIRE(loadControllerPadPreset(controller, bytes));
            Violations c;
            checkController(controller, c);
            ctrlV.check(c.count == 0, "pad preset sound[" + std::to_string(slot) + "] " +
                                          poison.name + ": " + c.first);
        }
    }

    INFO("processor: " << procV.count << " poisoned loads left a non-finite or out-of-range "
                                          "value; first: "
                       << procV.first);
    CHECK(procV.count == 0);
    INFO("controller: " << ctrlV.count << " poisoned loads left a non-finite or out-of-range "
                                           "parameter; first: "
                        << ctrlV.first);
    CHECK(ctrlV.count == 0);
    controller.terminate();
}

// ------------------------------------------------------------------------------
// MB-010: slots 0-27 and 30-33 are clamped like slots 34+.
// ------------------------------------------------------------------------------
TEST_CASE("State load: out-of-range sound slots 0-33 are clamped to [0, 1]", "[state]")
{
    Rig rig;
    const auto base = kitFromBytes(processorState(*rig.proc));

    {
        auto kit = base;
        kit.pads[0].sound[2] = 1.7;   // decay
        kit.pads[0].sound[0] = -0.3;  // material
        setProcessorState(rig, bytesFromKit(kit));
        const auto& cfg = rig.proc->voicePoolForTest().padConfig(0);
        CHECK(cfg.decay == 1.0f);
        CHECK(cfg.material == 0.0f);
    }

    {
        auto kit = base;
        kit.pads[0].sound.fill(5.0);
        setProcessorState(rig, bytesFromKit(kit));
        const auto saved = kitFromBytes(processorState(*rig.proc));
        std::string above;
        for (std::size_t s = 0; s < saved.pads[0].sound.size(); ++s)
        {
            const double v = saved.pads[0].sound[s];
            // Slot 57 (noiseLayerGain) is documented [0.1, 16]: 5.0 is in range.
            const double hi = (s == 57) ? 16.0 : 1.0;
            if (!(v <= hi))
                above += std::to_string(s) + "=" + std::to_string(v) + " ";
        }
        INFO("slots above their range after a load of 5.0: " << above);
        CHECK(above.empty());
    }
}

// ------------------------------------------------------------------------------
// MB-010 / MB-154: the voice rail.
// ------------------------------------------------------------------------------
TEST_CASE("NaN barrier: a voice whose pad config is NaN outputs finite samples and retires",
          "[voice_pool][nonfinite]")
{
    using Field = float Membrum::PadConfig::*;
    struct Case
    {
        const char* name;
        Field       field;
    };
    // One field per section (body, decay, tone shaper), plus Level, which
    // DrumVoice::setLevel stores unchecked into the output product.
    const std::array<Case, 6> cases{{
        {"material", &Membrum::PadConfig::material},
        {"size", &Membrum::PadConfig::size},
        {"decay", &Membrum::PadConfig::decay},
        {"strikePosition", &Membrum::PadConfig::strikePosition},
        {"tsFilterCutoff", &Membrum::PadConfig::tsFilterCutoff},
        {"level", &Membrum::PadConfig::level},
    }};
    for (const auto& c : cases)
    {
        DYNAMIC_SECTION(c.name)
        {
            Rig rig;
            rig.block();
            rig.proc->voicePoolForTest().padConfigMut(0).*(c.field) = quietNaNF();
            rig.events.addNoteOn(36, kVel100, 0);
            Render r;
            renderInto(rig, blocksFor(20.0), r);
            const int active = rig.proc->voicePoolForTest().getActiveVoiceCount();
            INFO("non-finite samples " << r.nonFinite << " / " << 2 * r.l.size()
                                       << ", active voices after 20 s: " << active);
            CHECK(r.nonFinite == 0);
            CHECK(active == 0);
        }
    }
}

// ------------------------------------------------------------------------------
// MB-010 / MB-154: the bus rail and the coupling energy limiter.
// ------------------------------------------------------------------------------
TEST_CASE("NaN barrier: one NaN voice does not latch the coupling limiter or the main bus",
          "[processor][nonfinite]")
{
    const auto padTwoHit = [](bool poison) {
        Rig rig;
        rig.setParams({{Membrum::kGlobalCouplingId, 1.0}});
        auto& cfg = rig.proc->voicePoolForTest().padConfigMut(0);
        if (poison)
        {
            // Material reaches the coupling partials; Level reaches the voice
            // output after every clamp, so the voice is NaN under every
            // floating-point model (MSVC /fp:fast folds a NaN Material to a
            // clamp bound, which plays an ordinary kick).
            cfg.material = quietNaNF();
            cfg.level = quietNaNF();
        }
        else
        {
            // The fresh reference: a silent, uncoupled pad-0 hit. It takes
            // voice slot 0 as the NaN hit does, so the pad-2 hit lands on the
            // same fresh slot 1 in both renders (each slot seeds its own
            // exciter and noise generators).
            cfg.level = 0.0f;
            cfg.couplingAmount = 0.0f;
        }
        rig.events.addNoteOn(36, kVel100, 0);
        Render before;
        renderInto(rig, blocksFor(1.0), before);
        rig.events.addNoteOn(38, kVel100, 0);
        Render hit;
        renderInto(rig, blocksFor(1.0), hit);
        const float env = rig.proc->energyEnvelopeForTest();
        return std::make_tuple(hit, env, before.nonFinite + hit.nonFinite);
    };

    const auto [ref, refEnv, refBad] = padTwoHit(false);
    const auto [got, gotEnv, gotBad] = padTwoHit(true);
    const double peakRef = toDb(peakOf(ref));
    const double peakGot = toDb(peakOf(got));
    const double rmsRef = toDb(rmsOf(ref));
    const double rmsGot = toDb(rmsOf(got));
    INFO("energy envelope " << gotEnv << " (fresh " << refEnv << "); non-finite samples "
                            << gotBad << "; pad-2 peak " << peakGot << " dB vs " << peakRef
                            << " dB, rms " << rmsGot << " dB vs " << rmsRef << " dB");
    CHECK(refBad == 0);
    CHECK(gotBad == 0);
    CHECK(isFinite(gotEnv));
    CHECK(std::abs(peakGot - peakRef) <= 0.1);
    CHECK(std::abs(rmsGot - rmsRef) <= 0.1);
}
