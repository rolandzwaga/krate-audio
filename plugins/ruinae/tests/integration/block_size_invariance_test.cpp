// ==============================================================================
// Integration Test: output does not depend on the host buffer size (T2.31)
// ==============================================================================
// R-150, R-151, R-051: the same note stream rendered in buffers of 32, 100, 441
// and 2048 samples must equal the 512-sample render. The modulation grid held
// a partial cell's offsets at every buffer cut (0.136 at 32), the mono
// portamento chunk restarted at every engine call (1.87), the delays and the
// Hall reverb stepped per call (0.15-0.89), and self-analysis read a
// previous-output buffer that each call overwrote from index 0.
// R-052: the Arp Pitch source kept its last value after the arp left a Mod mode.
//
// Every patch reaches the processor through setState before the first block,
// so a type switch (delay, reverb) is not what is measured.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "parameters/arpeggiator_params.h"
#include "parameters/dropdown_mappings.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <krate/dsp/core/modulation_types.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "vst_event_list.h"
#include "vst_param_changes.h"

namespace {

using Steinberg::Vst::ParamID;

constexpr double kRate = 48000.0;
constexpr Steinberg::int32 kMaxBlock = 2048;
constexpr int64_t kTotal = 96000;  // 2 s

class InvarianceProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::engine;
};

using Patch = std::vector<std::pair<ParamID, double>>;

struct TimedEvent {
    int64_t at = 0;
    Steinberg::Vst::Event ev{};
};

double envNorm(double ms) { return std::cbrt(std::clamp(ms, 0.0, 10000.0) / 10000.0); }

/// OSC A saw only, filter open, Clean, every effect off.
Patch basePatch() {
    return {
        {Ruinae::kMasterGainId, 0.5},
        {Ruinae::kVoiceModeId, 0.0},
        {Ruinae::kPolyphonyId, 7.0 / 15.0},
        {Ruinae::kSoftLimitId, 1.0},
        {Ruinae::kWidthId, 0.5},
        {Ruinae::kSpreadId, 0.0},
        {Ruinae::kOscATypeId, 0.0},
        {Ruinae::kOscAWaveformId, 0.25},
        {Ruinae::kOscALevelId, 1.0},
        {Ruinae::kOscATuneId, 0.5},
        {Ruinae::kOscAFineId, 0.5},
        {Ruinae::kOscBLevelId, 0.0},
        {Ruinae::kMixerModeId, 0.0},
        {Ruinae::kMixerPositionId, 0.0},
        {Ruinae::kFilterTypeId, 0.0},
        {Ruinae::kFilterCutoffId, 0.6},
        {Ruinae::kFilterResonanceId, 0.3},
        {Ruinae::kFilterEnvAmountId, 0.7},
        {Ruinae::kFilterEnvAttackId, envNorm(30)},
        {Ruinae::kFilterEnvDecayId, envNorm(300)},
        {Ruinae::kFilterEnvSustainId, 0.3},
        {Ruinae::kFilterKeyTrackId, 0.0},
        {Ruinae::kDistortionTypeId, 0.0},
        {Ruinae::kDistortionMixId, 1.0},
        {Ruinae::kTranceGateEnabledId, 0.0},
        {Ruinae::kAmpEnvAttackId, envNorm(5)},
        {Ruinae::kAmpEnvDecayId, envNorm(200)},
        {Ruinae::kAmpEnvSustainId, 1.0},
        {Ruinae::kAmpEnvReleaseId, envNorm(100)},
        {Ruinae::kDelayEnabledId, 0.0},
        {Ruinae::kReverbEnabledId, 0.0},
        {Ruinae::kModulationTypeId, 0.0},
        {Ruinae::kHarmonizerEnabledId, 0.0},
        {Ruinae::kGlobalFilterEnabledId, 0.0},
        {Ruinae::kSettingsTuningReferenceId, 0.5},
        {Ruinae::kSettingsPitchBendRangeId, 2.0 / 24.0},
        {Ruinae::kSettingsVelocityCurveId, 0.0},
        {Ruinae::kArpOperatingModeId, 0.0},
    };
}

Patch with(Patch p, const Patch& extra) {
    p.insert(p.end(), extra.begin(), extra.end());
    return p;
}

double sourceNorm(Krate::DSP::ModSource s) {
    return static_cast<double>(static_cast<int>(s)) / static_cast<double>(Ruinae::kModSourceCount - 1);
}
double destNorm(int index) { return static_cast<double>(index) / static_cast<double>(Ruinae::kModDestCount - 1); }

TimedEvent noteOn(int pitch, float velocity, int64_t at) {
    TimedEvent t;
    t.at = at;
    t.ev.type = Steinberg::Vst::Event::kNoteOnEvent;
    t.ev.noteOn.pitch = static_cast<Steinberg::int16>(pitch);
    t.ev.noteOn.velocity = velocity;
    t.ev.noteOn.noteId = -1;
    return t;
}

TimedEvent noteOff(int pitch, int64_t at) {
    TimedEvent t;
    t.at = at;
    t.ev.type = Steinberg::Vst::Event::kNoteOffEvent;
    t.ev.noteOff.pitch = static_cast<Steinberg::int16>(pitch);
    t.ev.noteOff.noteId = -1;
    return t;
}

TimedEvent bend(int msb, int lsb, int64_t at) {
    TimedEvent t;
    t.at = at;
    t.ev.type = Steinberg::Vst::Event::kLegacyMIDICCOutEvent;
    t.ev.midiCCOut.controlNumber = Steinberg::Vst::ControllerNumbers::kPitchBend;
    t.ev.midiCCOut.value = static_cast<Steinberg::int8>(msb);
    t.ev.midiCCOut.value2 = static_cast<Steinberg::int8>(lsb);
    return t;
}

/// Notes on at 1000 / 7777 / 30001 and off at 20000 / 40000 / 60000 (none on
/// a 64-sample grid line), plus three legacy pitch bends when `bends`.
std::vector<TimedEvent> noteStream(bool bends) {
    std::vector<TimedEvent> evs{noteOn(60, 0.8f, 1000), noteOn(64, 0.7f, 7777), noteOff(60, 20000),
                                noteOn(67, 0.9f, 30001), noteOff(64, 40000), noteOff(67, 60000)};
    if (bends) {
        evs.push_back(bend(127, 127, 10000));
        evs.push_back(bend(64, 0, 25000));
        evs.push_back(bend(0, 0, 45001));
    }
    std::stable_sort(evs.begin(), evs.end(), [](const TimedEvent& a, const TimedEvent& b) { return a.at < b.at; });
    return evs;
}

void setUp(Ruinae::Processor& p) {
    p.initialize(nullptr);
    Steinberg::Vst::ProcessSetup s{};
    s.processMode = Steinberg::Vst::kRealtime;
    s.symbolicSampleSize = Steinberg::Vst::kSample32;
    s.sampleRate = kRate;
    s.maxSamplesPerBlock = kMaxBlock;
    p.setupProcessing(s);
    p.setActive(static_cast<Steinberg::TBool>(true));
}

void tearDown(Ruinae::Processor& p) {
    p.setActive(static_cast<Steinberg::TBool>(false));
    p.terminate();
}

/// One process() call of `n` samples into `l` / `r`.
void processOnce(Ruinae::Processor& p, float* l, float* r, Steinberg::int32 n, Krate::Test::ParameterChanges& params,
                 Krate::Test::EventList& events) {
    float* ch[2] = {l, r};
    Steinberg::Vst::AudioBusBuffers out{};
    out.numChannels = 2;
    out.channelBuffers32 = ch;
    Krate::Test::EventList outEvents;
    Steinberg::Vst::ProcessData d{};
    d.processMode = Steinberg::Vst::kRealtime;
    d.symbolicSampleSize = Steinberg::Vst::kSample32;
    d.numSamples = n;
    d.numInputs = 0;
    d.numOutputs = 1;
    d.outputs = &out;
    d.inputParameterChanges = &params;
    d.inputEvents = &events;
    d.outputEvents = &outEvents;
    d.processContext = nullptr;
    p.process(d);
}

/// The processor state with `patch` applied: a donor processor takes the
/// patch as parameter changes in one 32-sample block and saves its state.
Steinberg::MemoryStream* stateFor(const Patch& patch) {
    InvarianceProcessor donor;
    setUp(donor);
    Krate::Test::ParameterChanges params;
    for (const auto& [id, v] : patch) params.addChange(id, v);
    Krate::Test::EventList events;
    std::vector<float> l(32), r(32);
    processOnce(donor, l.data(), r.data(), 32, params, events);
    auto* stream = new Steinberg::MemoryStream();
    REQUIRE(donor.getState(stream) == Steinberg::kResultTrue);
    tearDown(donor);
    return stream;
}

/// Render 2 s of `evs` in buffers of `blockSize` after loading `state`; mono.
std::vector<float> render(Steinberg::MemoryStream& state, const std::vector<TimedEvent>& evs, int64_t blockSize) {
    InvarianceProcessor proc;
    setUp(proc);
    state.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc.setState(&state) == Steinberg::kResultTrue);

    std::vector<float> l(static_cast<size_t>(kTotal)), r(static_cast<size_t>(kTotal));
    Krate::Test::ParameterChanges params;
    Krate::Test::EventList events;
    size_t next = 0;
    for (int64_t pos = 0; pos < kTotal; pos += blockSize) {
        const int64_t n = std::min(blockSize, kTotal - pos);
        events.clear();
        while (next < evs.size() && evs[next].at < pos + n) {
            Steinberg::Vst::Event e = evs[next].ev;
            e.sampleOffset = static_cast<Steinberg::int32>(evs[next].at - pos);
            events.addEvent(e);
            ++next;
        }
        processOnce(proc, l.data() + pos, r.data() + pos, static_cast<Steinberg::int32>(n), params, events);
    }
    tearDown(proc);
    std::vector<float> mono(static_cast<size_t>(kTotal));
    for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (l[i] + r[i]);
    return mono;
}

struct Diff {
    double maxAbs = 0.0;
    size_t at = 0;
    double relDb = -300.0;  ///< difference energy relative to the reference's
};

Diff compare(const std::vector<float>& ref, const std::vector<float>& x) {
    Diff d;
    double refSq = 0.0;
    double diffSq = 0.0;
    for (size_t i = 0; i < std::min(ref.size(), x.size()); ++i) {
        const double e = static_cast<double>(x[i]) - static_cast<double>(ref[i]);
        if (std::abs(e) > d.maxAbs) {
            d.maxAbs = std::abs(e);
            d.at = i;
        }
        diffSq += e * e;
        refSq += static_cast<double>(ref[i]) * static_cast<double>(ref[i]);
    }
    d.relDb = 10.0 * std::log10(std::max(diffSq, 1e-30) / std::max(refSq, 1e-30));
    return d;
}

enum class Bar { MaxAbs, RelativeRms };

/// Render `patch` at 512 and at 32 / 100 / 441 / 2048 and check each against
/// the 512 render: max |diff| <= 1e-5, or diff energy <= -60 dB for the FFT
/// and grain delays.
void checkInvariant(const Patch& patch, bool bends, Bar bar) {
    auto* state = stateFor(patch);
    const auto evs = noteStream(bends);
    // ONE call site for every render, the reference first: until R-161 (T3.1)
    // the spectral delay seeds its stereo-phase RNG from its own address, so
    // each render's processor must sit at the same stack address.
    std::vector<float> ref;
    for (int64_t bs : {int64_t{512}, int64_t{32}, int64_t{100}, int64_t{441}, int64_t{2048}}) {
        const auto x = render(*state, evs, bs);
        if (bs == 512) {
            ref = x;
            float peak = 0.0f;
            for (float v : ref) peak = std::max(peak, std::abs(v));
            INFO("512 render peak " << peak);
            REQUIRE(peak > 1e-3f);  // not vacuous: the notes sound
            continue;
        }
        const Diff d = compare(ref, x);
        INFO("block " << bs << " vs 512: max |diff| " << d.maxAbs << " at sample " << d.at
                      << ", diff energy " << d.relDb << " dB re signal");
        if (bar == Bar::MaxAbs) {
            CHECK(d.maxAbs <= 1e-5);
        } else {
            CHECK(d.relDb <= -60.0);
        }
    }
    state->release();
}

Patch delayPatch(int type) {
    return with(basePatch(), {{Ruinae::kDelayEnabledId, 1.0},
                              {Ruinae::kDelayTypeId, static_cast<double>(type) / 4.0},
                              {Ruinae::kDelayMixId, 0.4},
                              {Ruinae::kDelaySyncId, 0.0},
                              {Ruinae::kDelayTimeId, 299.0 / 4999.0}});
}

Patch reverbPatch(int type) {
    return with(basePatch(), {{Ruinae::kReverbEnabledId, 1.0},
                              {Ruinae::kReverbTypeId, static_cast<double>(type)},
                              {Ruinae::kReverbMixId, 0.4}});
}

}  // namespace

TEST_CASE("Render is block-size invariant", "[integration][block_size]") {
    using Krate::DSP::ModSource;

    SECTION("dry engine with pitch bends") {
        checkInvariant(basePatch(), true, Bar::MaxAbs);
    }
    SECTION("LFO1 to global and all-voice filter cutoff") {
        checkInvariant(with(basePatch(), {{Ruinae::kGlobalFilterEnabledId, 1.0},
                                          {Ruinae::kGlobalFilterCutoffId, 0.6},
                                          {Ruinae::kLFO1RateId, 0.622},
                                          {Ruinae::kLFO1DepthId, 1.0},
                                          {Ruinae::kLFO1SyncId, 0.0},
                                          {Ruinae::kModMatrixSlot0SourceId, sourceNorm(ModSource::LFO1)},
                                          {Ruinae::kModMatrixSlot0DestId, destNorm(0)},
                                          {Ruinae::kModMatrixSlot0AmountId, 0.9},
                                          {Ruinae::kModMatrixSlot1SourceId, sourceNorm(ModSource::LFO1)},
                                          {Ruinae::kModMatrixSlot1DestId, destNorm(4)},
                                          {Ruinae::kModMatrixSlot1AmountId, 0.8}}),
                       false, Bar::MaxAbs);
    }
    SECTION("mono legato portamento") {
        checkInvariant(with(basePatch(), {{Ruinae::kVoiceModeId, 1.0},
                                          {Ruinae::kMonoLegatoId, 1.0},
                                          {Ruinae::kMonoPortamentoTimeId, std::cbrt(150.0 / 5000.0)}}),
                       false, Bar::MaxAbs);
    }
    SECTION("Digital delay") { checkInvariant(delayPatch(0), false, Bar::MaxAbs); }
    SECTION("Tape delay") { checkInvariant(delayPatch(1), false, Bar::MaxAbs); }
    SECTION("PingPong delay") { checkInvariant(delayPatch(2), false, Bar::MaxAbs); }
    SECTION("Granular delay") { checkInvariant(delayPatch(3), false, Bar::RelativeRms); }
    SECTION("Spectral delay") { checkInvariant(delayPatch(4), false, Bar::RelativeRms); }
    SECTION("Plate reverb") { checkInvariant(reverbPatch(0), false, Bar::MaxAbs); }
    SECTION("Hall reverb") { checkInvariant(reverbPatch(1), false, Bar::MaxAbs); }
    SECTION("Env Follower to global filter cutoff, no sidechain") {
        checkInvariant(with(basePatch(), {{Ruinae::kGlobalFilterEnabledId, 1.0},
                                          {Ruinae::kGlobalFilterCutoffId, 0.4},
                                          {Ruinae::kModMatrixSlot0SourceId, sourceNorm(ModSource::EnvFollower)},
                                          {Ruinae::kModMatrixSlot0DestId, destNorm(0)},
                                          {Ruinae::kModMatrixSlot0AmountId, 1.0}}),
                       false, Bar::MaxAbs);
    }
}

// R-052: External0 (Arp Pitch) was written only while the arp ran in a Mod
// mode, so a route from it kept the last arp note's offset after the arp was
// switched Off or to MIDI only.
TEST_CASE("Arp Pitch source returns to zero when the arp leaves a Mod mode", "[integration][arp][modmatrix]") {
    const int targetMode = GENERATE(int{Ruinae::kArpOff}, int{Ruinae::kArpMIDI});
    InvarianceProcessor proc;
    setUp(proc);
    std::vector<float> l(512), r(512);
    Krate::Test::EventList events;
    Krate::Test::ParameterChanges params;
    params.addChange(Ruinae::kArpOperatingModeId, static_cast<double>(Ruinae::kArpMod) / 3.0);
    params.addChange(Ruinae::kArpTempoSyncId, 0.0);
    params.addChange(Ruinae::kModMatrixSlot0SourceId, sourceNorm(Krate::DSP::ModSource::External0));
    params.addChange(Ruinae::kModMatrixSlot0DestId, destNorm(0));
    params.addChange(Ruinae::kModMatrixSlot0AmountId, 1.0);
    events.addNoteOn(72, 0.8f, 0);
    processOnce(proc, l.data(), r.data(), 512, params, events);
    params.clear();
    events.clear();
    for (int b = 0; b < 40; ++b) processOnce(proc, l.data(), r.data(), 512, params, events);

    const auto cutoff = Krate::DSP::RuinaeModDest::GlobalFilterCutoff;
    const float whileMod = proc.engine().getGlobalModOffset(cutoff);
    INFO("Global Filter Cutoff offset from Arp Pitch while the arp runs in Mod mode: " << whileMod);
    REQUIRE(whileMod > 0.1f);  // not vacuous: the arp played and the route carries its pitch

    params.addChange(Ruinae::kArpOperatingModeId, static_cast<double>(targetMode) / 3.0);
    processOnce(proc, l.data(), r.data(), 512, params, events);
    const float after = proc.engine().getGlobalModOffset(cutoff);
    INFO("arp mode " << targetMode << ": offset one block after leaving Mod mode " << after);
    CHECK(std::abs(after) <= 1e-6f);
    tearDown(proc);
}
