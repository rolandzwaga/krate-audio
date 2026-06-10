// ============================================================================
// Snare "no body" diagnostic (one-off, tagged [.snare-diag], hidden by default)
// ============================================================================
// Loads the REAL shipped "Acoustic Studio Kit" factory preset, then renders a
// set of WAVs so we can A/B *why* the snare sounds like a thin hi-hat:
//
//   snare_full.wav       - snare exactly as shipped (pad 2, MIDI 38)
//   snare_no_noise.wav   - same, noise layer muted (noiseLayerMix = 0)
//   snare_no_click.wav   - same, click layer muted (clickLayerMix = 0)
//   snare_body_only.wav  - both muted: the bare modal body + shell coupling
//   kick_full.wav        - kick as shipped (pad 0, MIDI 36), reference
//
// If snare_body_only sounds like a believable snare shell -> the fix is a
// noise/click rebalance. If it is still a weak unbodied thud -> the membrane
// body model is the wrong body for a snare (structural).
//
// Run:  membrum_tests.exe "[.snare-diag]" --success
// WAVs land in F:/tmp/.
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "public.sdk/source/vst/vstpresetfile.h"
#include "base/source/fstreamer.h"

#include "vst_param_changes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr int    kBlockSize  = 256;
constexpr double kSampleRate = 48000.0;
constexpr int    kRenderSecondsBlocks = 244;  // ~1.3 s tail at 48k/256

using MultiParamChanges = Krate::Test::ParameterChanges;

class NoteEventList : public IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events_.size()); }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override
    {
        if (index < 0 || index >= static_cast<int32>(events_.size())) return kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override { events_.push_back(e); return kResultOk; }
    void noteOn(int16 midi, float velocity)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = 0;
        e.noteOn.pitch = midi;
        e.noteOn.velocity = velocity;
        e.noteOn.channel = 0;
        e.noteOn.length = 0;
        e.noteOn.tuning = 0.0f;
        events_.push_back(e);
    }
    void clear() { events_.clear(); }
private:
    std::vector<Event> events_;
};

ProcessSetup makeSetup(double sr, int bs)
{
    ProcessSetup s{};
    s.processMode = kRealtime;
    s.symbolicSampleSize = kSample32;
    s.maxSamplesPerBlock = bs;
    s.sampleRate = sr;
    return s;
}

struct Fixture
{
    Membrum::Processor processor;
    NoteEventList events;
    std::array<float, kBlockSize> outL{};
    std::array<float, kBlockSize> outR{};
    float* outChans[2];
    AudioBusBuffers outBus{};
    ProcessData data{};

    Fixture()
    {
        outChans[0] = outL.data();
        outChans[1] = outR.data();
        outBus.numChannels = 2;
        outBus.channelBuffers32 = outChans;
        outBus.silenceFlags = 0;

        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kBlockSize;
        data.numOutputs = 1;
        data.outputs = &outBus;
        data.numInputs = 0;
        data.inputs = nullptr;
        data.inputParameterChanges = nullptr;
        data.outputParameterChanges = nullptr;
        data.inputEvents = &events;
        data.outputEvents = nullptr;
        data.processContext = nullptr;

        processor.initialize(nullptr);
        auto setup = makeSetup(kSampleRate, kBlockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~Fixture()
    {
        processor.setActive(false);
        processor.terminate();
    }

    void clearBuffers()
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
    }

    void processBlock()
    {
        clearBuffers();
        processor.process(data);
    }

    void sendParams(const std::vector<std::pair<ParamID, ParamValue>>& params)
    {
        MultiParamChanges changes;
        for (const auto& p : params) changes.add(p.first, p.second);
        data.inputParameterChanges = &changes;
        events.clear();
        clearBuffers();
        processor.process(data);
        data.inputParameterChanges = nullptr;
    }

    // Drain any ringing voices so renders don't bleed into each other.
    void silence()
    {
        for (int n = 36; n <= 67; ++n) { events.clear(); events.noteOn(static_cast<int16>(n), 0.0f); processBlock(); }
        events.clear();
        for (int b = 0; b < 80; ++b) processBlock();
        events.clear();
    }

    // Mono (L+R)/2 render of one pad.
    std::vector<float> render(int16 midi)
    {
        silence();
        events.clear();
        events.noteOn(midi, 1.0f);
        std::vector<float> mono;
        mono.reserve(static_cast<size_t>(kRenderSecondsBlocks) * kBlockSize);
        for (int b = 0; b < kRenderSecondsBlocks; ++b) {
            processBlock();
            for (int s = 0; s < kBlockSize; ++s)
                mono.push_back(0.5f * (outL[s] + outR[s]));
        }
        events.clear();
        return mono;
    }
};

double peakAbs(const std::vector<float>& s)
{
    double p = 0.0;
    for (float x : s) p = std::max(p, std::abs(static_cast<double>(x)));
    return p;
}

void writeWav(const std::string& path, const std::vector<float>& samples)
{
    const std::uint32_t numSamples = static_cast<std::uint32_t>(samples.size());
    const std::uint32_t byteRate   = static_cast<std::uint32_t>(kSampleRate) * 1 * 2;
    const std::uint32_t dataSize   = numSamples * 2;
    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4);
    std::uint32_t chunkSize = 36 + dataSize;
    f.write(reinterpret_cast<const char*>(&chunkSize), 4);
    f.write("WAVEfmt ", 8);
    std::uint32_t fmtSize = 16;
    f.write(reinterpret_cast<const char*>(&fmtSize), 4);
    std::uint16_t fmt = 1, nch = 1, bps = 16;
    f.write(reinterpret_cast<const char*>(&fmt), 2);
    f.write(reinterpret_cast<const char*>(&nch), 2);
    std::uint32_t srate = static_cast<std::uint32_t>(kSampleRate);
    f.write(reinterpret_cast<const char*>(&srate), 4);
    f.write(reinterpret_cast<const char*>(&byteRate), 4);
    std::uint16_t ba = 2;
    f.write(reinterpret_cast<const char*>(&ba), 2);
    f.write(reinterpret_cast<const char*>(&bps), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char*>(&dataSize), 4);
    for (float s : samples) {
        const std::int16_t i = static_cast<std::int16_t>(
            std::max(-1.0f, std::min(1.0f, s)) * 32767.0f);
        f.write(reinterpret_cast<const char*>(&i), 2);
    }
}

// Load the component-state chunk of a .vstpreset into the processor.
bool loadPresetComponentState(Membrum::Processor& proc, const std::string& path)
{
    auto* stream = Vst::FileStream::open(path.c_str(), "rb");
    if (!stream) return false;
    bool ok = false;
    {
        Vst::PresetFile pf(stream);
        if (pf.readChunkList() && pf.seekToComponentState()) {
            const auto* entry = pf.getEntry(Vst::kComponentState);
            if (entry) {
                auto comp = owned(new Vst::ReadOnlyBStream(stream, entry->offset, entry->size));
                comp->seek(0, IBStream::kIBSeekSet, nullptr);
                ok = (proc.setState(comp) == kResultOk);
            }
        }
    }
    stream->release();
    return ok;
}

} // namespace

TEST_CASE("Snare body diagnostic: render full vs bare-body A/B set",
          "[membrum][diagnose][.snare-diag]")
{
    const std::string presetPath =
        "F:/projects/iterum/plugins/membrum/resources/presets/"
        "Kit Presets/Acoustic/Acoustic Studio Kit.vstpreset";
    const std::string outDir = "F:/tmp/";

    const int kSnarePad = 2;   // MIDI 38 Acoustic Snare
    const int16 kSnareMidi = 38;
    const int16 kKickMidi  = 36;

    auto noiseMixId = static_cast<ParamID>(Membrum::padParamId(kSnarePad, Membrum::kPadNoiseLayerMix));
    auto clickMixId = static_cast<ParamID>(Membrum::padParamId(kSnarePad, Membrum::kPadClickLayerMix));

    Fixture fix;
    REQUIRE(loadPresetComponentState(fix.processor, presetPath));

    // Confirm the snare pad really carries the noise-dominant config we expect.
    const auto& snareCfg = fix.processor.voicePoolForTest().padConfig(kSnarePad);
    INFO("snare material=" << snareCfg.material << " size=" << snareCfg.size
         << " decay=" << snareCfg.decay
         << " noiseMix=" << snareCfg.noiseLayerMix
         << " clickMix=" << snareCfg.clickLayerMix);

    // (1) Kick reference + full snare, exactly as shipped.
    const auto kickFull  = fix.render(kKickMidi);
    const auto snareFull = fix.render(kSnareMidi);

    // (2) Mute noise only.
    fix.sendParams({{noiseMixId, 0.0}});
    const auto snareNoNoise = fix.render(kSnareMidi);

    // (3) Reload (restores noiseMix), mute click only.
    REQUIRE(loadPresetComponentState(fix.processor, presetPath));
    fix.sendParams({{clickMixId, 0.0}});
    const auto snareNoClick = fix.render(kSnareMidi);

    // (4) Mute both -> bare modal body + shell coupling (as shipped: NoiseBurst exciter).
    REQUIRE(loadPresetComponentState(fix.processor, presetPath));
    fix.sendParams({{noiseMixId, 0.0}, {clickMixId, 0.0}});
    const auto snareBodyOnly = fix.render(kSnareMidi);

    // (5) Bare body but STRUCK instead of noise-excited.
    //     Exciter param: stepCount 5 -> Impulse=0.0, Mallet=0.2, NoiseBurst=0.4.
    auto exciterId = static_cast<ParamID>(Membrum::padParamId(kSnarePad, Membrum::kPadExciterType));
    REQUIRE(loadPresetComponentState(fix.processor, presetPath));
    fix.sendParams({{noiseMixId, 0.0}, {clickMixId, 0.0}, {exciterId, 0.0}});  // Impulse
    const auto snareBodyImpulse = fix.render(kSnareMidi);

    REQUIRE(loadPresetComponentState(fix.processor, presetPath));
    fix.sendParams({{noiseMixId, 0.0}, {clickMixId, 0.0}, {exciterId, 0.2}});  // Mallet
    const auto snareBodyMallet = fix.render(kSnareMidi);

    // (6) Control: a TOM's bare body (Membrane + Mallet, as shipped). Proves the
    //     Membrane model CAN sound like a struck drum when excited correctly.
    const int   kTomPad  = 5;
    const int16 kTomMidi = 41;
    auto tomNoiseId = static_cast<ParamID>(Membrum::padParamId(kTomPad, Membrum::kPadNoiseLayerMix));
    auto tomClickId = static_cast<ParamID>(Membrum::padParamId(kTomPad, Membrum::kPadClickLayerMix));
    REQUIRE(loadPresetComponentState(fix.processor, presetPath));
    fix.sendParams({{tomNoiseId, 0.0}, {tomClickId, 0.0}});
    const auto tomBodyOnly = fix.render(kTomMidi);

    // (7) Candidate "fixed" full snares: struck body + rebalanced wires + click.
    auto noiseCutId = static_cast<ParamID>(Membrum::padParamId(kSnarePad, Membrum::kPadNoiseLayerCutoff));
    auto noiseColId = static_cast<ParamID>(Membrum::padParamId(kSnarePad, Membrum::kPadNoiseLayerColor));

    // A: Impulse strike, wires + click pulled back to accent level, wires as-shipped (bright).
    REQUIRE(loadPresetComponentState(fix.processor, presetPath));
    fix.sendParams({{exciterId, 0.0}, {noiseMixId, 0.5}, {clickMixId, 0.5}});
    const auto snareFixImpulse = fix.render(kSnareMidi);

    // B: Mallet strike, same balance.
    REQUIRE(loadPresetComponentState(fix.processor, presetPath));
    fix.sendParams({{exciterId, 0.2}, {noiseMixId, 0.5}, {clickMixId, 0.5}});
    const auto snareFixMallet = fix.render(kSnareMidi);

    // C: Impulse strike, darker/less-sizzly wires (cutoff 0.5 ~800Hz, color 0.4 pink-ish).
    REQUIRE(loadPresetComponentState(fix.processor, presetPath));
    fix.sendParams({{exciterId, 0.0}, {noiseMixId, 0.5}, {clickMixId, 0.5},
                    {noiseCutId, 0.5}, {noiseColId, 0.4}});
    const auto snareFixDarkWires = fix.render(kSnareMidi);

    writeWav(outDir + "snare_fix_A_impulse.wav",    snareFixImpulse);
    writeWav(outDir + "snare_fix_B_mallet.wav",     snareFixMallet);
    writeWav(outDir + "snare_fix_C_darkwires.wav",  snareFixDarkWires);

    WARN("fixed-snare peaks: A_impulse=" << peakAbs(snareFixImpulse)
         << " B_mallet=" << peakAbs(snareFixMallet)
         << " C_darkwires=" << peakAbs(snareFixDarkWires));

    // (8) Metallic-ring isolation. Baseline = candidate A (Impulse, wires/click 0.5).
    //     Change ONE thing each to find what kills the metallic ring.
    WARN("snare shipped coupling/damping: couplingStrength=" << snareCfg.couplingStrength
         << " secondaryEnabled=" << snareCfg.secondaryEnabled
         << " secondarySize=" << snareCfg.secondarySize
         << " secondaryMaterial=" << snareCfg.secondaryMaterial
         << " bodyDampingB1=" << snareCfg.bodyDampingB1
         << " bodyDampingB3=" << snareCfg.bodyDampingB3
         << " material=" << snareCfg.material << " decay=" << snareCfg.decay);

    auto secEnId    = static_cast<ParamID>(Membrum::padParamId(kSnarePad, Membrum::kPadSecondaryEnabled));
    auto couplingId = static_cast<ParamID>(Membrum::padParamId(kSnarePad, Membrum::kPadCouplingStrength));
    auto b3Id       = static_cast<ParamID>(Membrum::padParamId(kSnarePad, Membrum::kPadBodyDampingB3));
    auto decayId    = static_cast<ParamID>(Membrum::padParamId(kSnarePad, Membrum::kPadDecay));
    auto materialId = static_cast<ParamID>(Membrum::padParamId(kSnarePad, Membrum::kPadMaterial));
    const std::vector<std::pair<ParamID, ParamValue>> baseA =
        {{exciterId, 0.0}, {noiseMixId, 0.5}, {clickMixId, 0.5}};
    auto renderVariant = [&](std::vector<std::pair<ParamID, ParamValue>> extra) {
        REQUIRE(loadPresetComponentState(fix.processor, presetPath));
        auto p = baseA;
        for (auto& e : extra) p.push_back(e);
        fix.sendParams(p);
        return fix.render(kSnareMidi);
    };

    const auto vNoCoupling = renderVariant({{secEnId, 0.0}, {couplingId, 0.0}});
    const auto vWoodyDamp  = renderVariant({{b3Id, 0.5}});                 // strong HF damping
    const auto vShortDecay = renderVariant({{decayId, 0.12}});
    const auto vFullWoody  = renderVariant({{secEnId, 0.0}, {couplingId, 0.0},
                                            {b3Id, 0.5}, {decayId, 0.15}, {materialId, 0.3}});

    writeWav(outDir + "snare_iso_no_coupling.wav", vNoCoupling);
    writeWav(outDir + "snare_iso_woody_damp.wav",  vWoodyDamp);
    writeWav(outDir + "snare_iso_short_decay.wav", vShortDecay);
    writeWav(outDir + "snare_iso_full_woody.wav",  vFullWoody);
    WARN("iso peaks: noCoupling=" << peakAbs(vNoCoupling)
         << " woodyDamp=" << peakAbs(vWoodyDamp)
         << " shortDecay=" << peakAbs(vShortDecay)
         << " fullWoody=" << peakAbs(vFullWoody));

    // (9) Dial-in: b3 is the metal-ring lever. Find the right amount; check
    //     whether coupling / brightness need touching once b3 is up.
    const auto fb3_050        = renderVariant({{b3Id, 0.5}});                  // current best
    const auto fb3_070        = renderVariant({{b3Id, 0.7}});                  // more damping
    const auto fb3_100        = renderVariant({{b3Id, 1.0}});                  // max damping
    const auto fb3_070_nocpl  = renderVariant({{b3Id, 0.7}, {secEnId, 0.0}, {couplingId, 0.0}});
    const auto fb3_070_mat040 = renderVariant({{b3Id, 0.7}, {materialId, 0.4}});

    writeWav(outDir + "snare_final_b3_050.wav",        fb3_050);
    writeWav(outDir + "snare_final_b3_070.wav",        fb3_070);
    writeWav(outDir + "snare_final_b3_100.wav",        fb3_100);
    writeWav(outDir + "snare_final_b3_070_nocpl.wav",  fb3_070_nocpl);
    writeWav(outDir + "snare_final_b3_070_mat040.wav", fb3_070_mat040);
    WARN("dial peaks: b3_050=" << peakAbs(fb3_050)
         << " b3_070=" << peakAbs(fb3_070)
         << " b3_100=" << peakAbs(fb3_100)
         << " b3_070_nocpl=" << peakAbs(fb3_070_nocpl)
         << " b3_070_mat040=" << peakAbs(fb3_070_mat040));

    writeWav(outDir + "kick_full.wav",              kickFull);
    writeWav(outDir + "snare_full.wav",             snareFull);
    writeWav(outDir + "snare_no_noise.wav",         snareNoNoise);
    writeWav(outDir + "snare_no_click.wav",         snareNoClick);
    writeWav(outDir + "snare_body_only.wav",        snareBodyOnly);
    writeWav(outDir + "snare_body_impulse.wav",     snareBodyImpulse);
    writeWav(outDir + "snare_body_mallet.wav",      snareBodyMallet);
    writeWav(outDir + "tom_body_only.wav",          tomBodyOnly);

    WARN("peaks: kickFull="      << peakAbs(kickFull)
         << " snareFull="        << peakAbs(snareFull)
         << " snareNoNoise="     << peakAbs(snareNoNoise)
         << " snareNoClick="     << peakAbs(snareNoClick)
         << " snareBodyOnly(NoiseBurst)=" << peakAbs(snareBodyOnly)
         << " snareBodyImpulse="  << peakAbs(snareBodyImpulse)
         << " snareBodyMallet="   << peakAbs(snareBodyMallet)
         << " tomBodyOnly="       << peakAbs(tomBodyOnly));
    WARN("WAVs written to " << outDir);

    CHECK(peakAbs(snareFull) > 1e-4);
    CHECK(peakAbs(snareBodyOnly) > 1e-6);
}
