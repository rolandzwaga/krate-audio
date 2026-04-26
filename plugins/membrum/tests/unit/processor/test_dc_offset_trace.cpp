// Trace where the +0.06 DC offset comes from in the Membrum render path.
// Builds a real ModalResonatorBank (same code path as the plugin), fires an
// impulse, dumps modeSum before/after softclip and bank output statistics.
#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/processors/modal_resonator_bank.h>
#include <krate/dsp/processors/modal_resonator_bank_simd.h>

#include "dsp/bodies/membrane_mapper.h"
#include "dsp/drum_voice.h"
#include "dsp/exciters/mallet_exciter.h"
#include "dsp/voice_common_params.h"
#include "processor/processor.h"

#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "state/state_codec.h"

#include "public.sdk/source/vst/vstpresetfile.h"
#include "public.sdk/source/common/memorystream.h"

#include <filesystem>
#include <fstream>

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

// Verify Smith's constant-peak-gain formula gives bounded peak in our
// coupled-form (Gordon-Smith) bank. For the b=[1,0,-1] direct-form bandpass,
// g = sqrt((1-R^2)/2) yields peak frequency-response gain = 1. Question: does
// the same scaling cap the impulse-response peak in coupled-form?
// Empirical validation: Simper TPT-SVF in BP_NORMALIZED mode, single-mode
// driven by the Mallet exciter, output is amplitude-weighted. Per Mutable
// Elements (resonator.cc): peak gain of bp*r is unity regardless of Q,
// regardless of input spectrum.
TEST_CASE("Simper TPT-SVF BP_NORMALIZED: peak bounded under Mallet drive",
          "[.trace][dsp][svf][simper]")
{
    // f = 80 Hz (matches low-tom mode 0), Q computed for our decay budget.
    constexpr float kSr = 48000.0f;
    const float f = 80.0f;
    // Worst case: long-sustain Membrum mode (R≈0.99999 → decay_rate ≈ 0.5).
    const float decay_rate = 0.5f; // ~14 s T60 — high-Q regime
    // Q = pi * f / decay_rate
    const float Q = static_cast<float>(3.14159265358979323846) * f / decay_rate;
    std::printf("Single TPT-SVF mode: f=%.1f Hz, Q=%.3f\n", f, Q);

    // Trapezoidal/TPT coefficients (Simper):
    //   g = tan(pi * f / SR), r = 1/Q, h = 1 / (1 + r*g + g*g)
    const float g  = std::tan(static_cast<float>(3.14159265358979323846) * f / kSr);
    const float r  = 1.0f / Q;
    const float h  = 1.0f / (1.0f + r * g + g * g);

    float s1 = 0.0f, s2 = 0.0f;
    Membrum::MalletExciter mallet;
    mallet.prepare(static_cast<double>(kSr), 0);
    mallet.trigger(1.0f);
    constexpr int N = 30 * 256;
    std::vector<float> out(N, 0.0f);
    constexpr float kPad = 0.125f; // Elements pre-bank pad
    constexpr float kRecoveryGain = 8.0f; // 1/kPad (per-mode amp weight ×8)
    const float amp = 0.5f;
    for (int i = 0; i < N; ++i) {
        const float ex = mallet.process(0.0f) * kPad;
        // SVF TPT update
        const float hp = (ex - r * s1 - g * s1 - s2) * h;
        const float bp = g * hp + s1;
        s1 = g * hp + bp;
        const float lp = g * bp + s2;
        s2 = g * bp + lp;
        const float bp_norm = bp * r; // unit peak gain
        out[i] = bp_norm * amp * kRecoveryGain;
    }
    float peak = 0.0f;
    for (float s : out) peak = std::max(peak, std::abs(s));
    std::printf("Single TPT-SVF mode + Mallet drive: peak=%.4f\n", peak);
}

// Peak BANK output (pre-DrumVoice softclip) when driven by the real Mallet
// exciter. This is what determines whether the bank's safety limiter is in
// saturation — if peak < 0.707, we're below threshold.
TEST_CASE("Bank peak when driven by MalletExciter",
          "[.trace][dsp][bank][mallet_drive]")
{
    Krate::DSP::ModalResonatorBank bank;
    bank.prepare(48000.0);
    Membrum::VoiceCommonParams params{};
    params.material = 0.20f; params.size = 0.85f;
    params.decay = 0.55f; params.strikePos = 0.30f; params.level = 0.85f;
    params.modeStretch = 1.0f; params.airLoading = 0.6f;
    params.bodyDampingB1 = -1.0f; params.bodyDampingB3 = -1.0f;
    auto result = Membrum::Bodies::MembraneMapper::map(params, 0.0f);
    bank.setModes(result.frequencies, result.amplitudes,
                  result.numPartials, result.damping,
                  result.stretch, result.scatter);

    Membrum::MalletExciter mallet;
    mallet.prepare(48000.0, 0);

    auto run = [&](float vel, float excScale) {
        bank.reset();
        mallet.prepare(48000.0, 0);
        mallet.trigger(vel);
        constexpr int kBlock = 256, kBlocks = 30;
        std::vector<float> exc(kBlock, 0.0f);
        std::vector<float> out(kBlock, 0.0f);
        float peakBankOut = 0.0f;
        for (int b = 0; b < kBlocks; ++b) {
            for (int i = 0; i < kBlock; ++i)
                exc[i] = mallet.process(0.0f) * excScale;
            bank.processBlock(exc.data(), out.data(), kBlock);
            for (int i = 0; i < kBlock; ++i)
                peakBankOut = std::max(peakBankOut, std::abs(out[i]));
        }
        std::printf("Bank+Mallet vel=%.2f excScale=%.4f peak=%.4f\n",
                    vel, excScale, peakBankOut);
    };
    // Sweep input scale at vel=1.0 to find threshold below which bank
    // does NOT hit its internal softclip (threshold = 0.707).
    run(1.0f, 1.0f);
    run(1.0f, 0.1f);
    run(1.0f, 0.05f);
    run(1.0f, 0.02f);
    run(1.0f, 0.01f);
}

// What's the actual peak output of MalletExciter at vel=1.0?
TEST_CASE("MalletExciter peak measurement",
          "[.trace][dsp][exciter][mallet]")
{
    Membrum::MalletExciter mallet;
    mallet.prepare(48000.0, 0);
    mallet.trigger(1.0f);
    constexpr int N = 4096;
    std::vector<float> out(N, 0.0f);
    for (int i = 0; i < N; ++i)
        out[i] = mallet.process(0.0f);
    float peak = 0.0f;
    for (float s : out) peak = std::max(peak, std::abs(s));
    std::printf("Mallet peak at vel=1.0 = %.4f\n", peak);

    mallet.prepare(48000.0, 0);
    mallet.trigger(0.1f);
    for (int i = 0; i < N; ++i)
        out[i] = mallet.process(0.0f);
    peak = 0.0f;
    for (float s : out) peak = std::max(peak, std::abs(s));
    std::printf("Mallet peak at vel=0.1 = %.4f\n", peak);
}

// For coupled-form (Gordon-Smith), the IR peak is just g_k itself (right at
// the impulse sample). Verify this by setting g_k = 1 and checking peak ~= 1.
TEST_CASE("Coupled-form: per-mode peak = gain_k (not Smith formula)",
          "[.trace][dsp][bank][couplform]")
{
    Krate::DSP::ModalResonatorBank bank;
    bank.prepare(48000.0);
    constexpr int N = 1;
    float freqs[N] = {200.0f};
    float amps[N]  = {1.0f};
    Krate::DSP::ModalResonatorBank::DampingLaw law{0.5f, 0.0f};
    bank.setModes(freqs, amps, N, law, 0.0f, 0.0f);

    constexpr int kBlocks = 30, kBlock = 256;
    std::vector<float> exc(kBlock, 0.0f);
    std::vector<float> out(kBlock * kBlocks, 0.0f);
    exc[0] = 1.0f;
    bank.processBlock(exc.data(), out.data(), kBlock);
    exc[0] = 0.0f;
    for (int b = 1; b < kBlocks; ++b)
        bank.processBlock(exc.data(), out.data() + b * kBlock, kBlock);
    float peak = 0.0f;
    for (float s : out) peak = std::max(peak, std::abs(s));
    std::printf("Single-mode g=1 peak = %.4f (target ~1.0 if coupled-form)\n", peak);
}

TEST_CASE("Smith peak-gain norm: per-mode peak after impulse",
          "[.trace][dsp][bank][smith]")
{
    Krate::DSP::ModalResonatorBank bank;
    bank.prepare(48000.0);

    // Single-mode probe: f0 = 100 Hz, R = 0.99999.
    constexpr int N = 1;
    constexpr float kSr = 48000.0f;
    constexpr float kF0 = 100.0f;
    const float decay_rate = 0.5f; // s^-1
    const float R = std::exp(-decay_rate / kSr);
    const float oneMinusR2 = 1.0f - R * R;
    const float gSmith = std::sqrt(oneMinusR2 * 0.5f);
    std::printf("R=%.6f, 1-R^2=%.6e, g_smith=%.6e\n", R, oneMinusR2, gSmith);

    // Apply normalized gain by passing the scaled amplitude through setModes.
    float freqs[N] = {kF0};
    float amps[N]  = {gSmith};
    Krate::DSP::ModalResonatorBank::DampingLaw law{decay_rate, 0.0f};
    bank.setModes(freqs, amps, N, law, 0.0f, 0.0f);

    constexpr int kBlocks = 30;
    constexpr int kBlock = 256;
    std::vector<float> exc(kBlock, 0.0f);
    std::vector<float> out(kBlock * kBlocks, 0.0f);
    exc[0] = 1.0f;
    bank.processBlock(exc.data(), out.data(), kBlock);
    exc[0] = 0.0f;
    for (int b = 1; b < kBlocks; ++b)
        bank.processBlock(exc.data(), out.data() + b * kBlock, kBlock);

    float peak = 0.0f;
    for (float s : out) peak = std::max(peak, std::abs(s));
    std::printf("Single-mode w/ Smith norm: peak=%.4f (target ~1.0)\n", peak);
}

TEST_CASE("Membrane bank: trace DC offset and saturation",
          "[.trace][dsp][bank]")
{
    Krate::DSP::ModalResonatorBank bank;
    bank.prepare(48000.0);

    // Match the test_kit_preset_load_render "low pad" config:
    Membrum::VoiceCommonParams params{};
    params.material      = 0.20f;
    params.size          = 0.85f;
    params.decay         = 0.55f;
    params.strikePos     = 0.30f;
    params.level         = 0.85f;
    params.modeStretch   = 1.0f;
    params.decaySkew     = 0.0f;
    params.bodyDampingB1 = -1.0f;
    params.bodyDampingB3 = -1.0f;
    params.airLoading    = 0.6f;
    params.modeScatter   = 0.0f;

    auto result = Membrum::Bodies::MembraneMapper::map(params, 0.0f);
    bank.setModes(result.frequencies, result.amplitudes,
                  result.numPartials, result.damping,
                  result.stretch, result.scatter);

    // Apply pitch-env scaling (80 Hz target) just like updateMembraneFundamental.
    const float natFundHz = 500.0f * std::pow(0.1f, params.size);
    const float ratio = 80.0f / natFundHz;
    std::vector<float> scaled(result.numPartials);
    for (int k = 0; k < result.numPartials; ++k)
        scaled[k] = result.frequencies[k] * ratio;
    bank.updateModes(scaled.data(), result.amplitudes,
                     result.numPartials, result.damping,
                     result.stretch, result.scatter);

    std::printf("nat=%.2f scaled mode0=%.2f mode4=%.2f mode8=%.2f\n",
                natFundHz, bank.getModeFrequency(0),
                bank.getModeFrequency(4), bank.getModeFrequency(8));

    // Drive with a single impulse, then process zeros for 7680 samples
    // (160 ms) and measure DC mean per ~10 ms window.
    constexpr int kBlock = 256;
    constexpr int kBlocks = 30;
    std::vector<float> out(kBlocks * kBlock, 0.0f);
    std::vector<float> excitation(kBlock, 0.0f);

    // Single impulse at sample 0. Use vel=1.0 to mimic the saturation
    // scenario the user hears in their DAW.
    excitation[0] = 1.0f;
    bank.processBlock(excitation.data(), out.data(), kBlock);

    // Zero-input subsequent blocks
    excitation[0] = 0.0f;
    for (int b = 1; b < kBlocks; ++b) {
        bank.processBlock(excitation.data(),
                          out.data() + b * kBlock, kBlock);
    }

    const int win = 480;
    std::printf("BANK only - mean per 10ms: ");
    for (int i = 0; i + win < static_cast<int>(out.size()); i += win) {
        double sum = 0.0;
        float mn = out[i], mx = out[i];
        for (int j = 0; j < win; ++j) {
            mn = std::min(mn, out[i + j]);
            mx = std::max(mx, out[i + j]);
            sum += out[i + j];
        }
        std::printf("[%.4f,%.4f,%.4f] ", mn, mx, sum / win);
    }
    std::printf("\n");

    float peak = 0.0f;
    for (float s : out) peak = std::max(peak, std::abs(s));
    double sumAll = 0.0;
    for (float s : out) sumAll += s;
    std::printf("Bank peak=%.4f, total mean=%.6f\n",
                peak, sumAll / static_cast<double>(out.size()));
}

TEST_CASE("DrumVoice: trace DC offset and saturation through full chain",
          "[.trace][dsp][drum_voice]")
{
    Membrum::DrumVoice v;
    v.prepare(48000.0, 0);

    // Mirror the test_kit_preset_load_render low-pad config.
    v.setExciterType(Membrum::ExciterType::Mallet);
    v.setBodyModel(Membrum::BodyModelType::Membrane);
    v.setMaterial(0.20f);
    v.setSize(0.85f);
    v.setDecay(0.55f);
    v.setStrikePosition(0.30f);
    v.setLevel(0.85f);
    v.setBodyDampingB1(-1.0f);
    v.setBodyDampingB3(-1.0f);
    v.setAirLoading(0.6f);

    v.toneShaper().setPitchEnvStartHz(80.0f);
    v.toneShaper().setPitchEnvEndHz(80.0f);
    v.toneShaper().setPitchEnvTimeMs(25.0f);

    // Disable parallel layers + click so only the body path is measured.
    v.setNoiseLayerMix(0.0f);
    v.setClickLayerMix(0.0f);

    v.noteOn(1.0f); // user hits pads at FULL velocity in DAW

    constexpr int kBlock = 256;
    constexpr int kBlocks = 30;
    std::vector<float> out(kBlocks * kBlock, 0.0f);
    for (int b = 0; b < kBlocks; ++b)
        v.processBlock(out.data() + b * kBlock, kBlock);

    const int win = 480;
    std::printf("DrumVoice (vel=1.0) mean per 10ms: ");
    for (int i = 0; i + win < static_cast<int>(out.size()); i += win) {
        double sum = 0.0;
        float mn = out[i], mx = out[i];
        for (int j = 0; j < win; ++j) {
            mn = std::min(mn, out[i + j]);
            mx = std::max(mx, out[i + j]);
            sum += out[i + j];
        }
        std::printf("[%.4f,%.4f,%.4f] ", mn, mx, sum / win);
    }
    std::printf("\n");

    float peak = 0.0f;
    for (float s : out) peak = std::max(peak, std::abs(s));
    double sumAll = 0.0;
    for (float s : out) sumAll += s;
    std::printf("DrumVoice (vel=1.0) peak=%.4f, total mean=%.6f\n",
                peak, sumAll / static_cast<double>(out.size()));

    // Count how many samples are at the soft-clip ceiling (|sample| >= 0.99).
    int satCount = 0;
    for (float s : out) if (std::abs(s) >= 0.99f) ++satCount;
    std::printf("DrumVoice (vel=1.0) saturated-sample count: %d / %d (%.1f%%)\n",
                satCount, static_cast<int>(out.size()),
                100.0 * satCount / static_cast<double>(out.size()));
}

// User reports all toms render at 158.1 Hz (= mode 0 of default size=0.5).
// Simulate the EXACT VST3 host load path: Processor::setState(componentState)
// with the on-disk 808 preset, then trigger each tom and FFT-analyse.
// Sanity: with NO preset loaded (pure defaults), what pitch do MIDI 41-50
// trigger? If this matches the user's 158 Hz uniform render, the user's path
// must somehow be skipping/discarding the per-pad config.
TEST_CASE("Default-cfg toms: render MIDI 41-50 with no preset loaded",
          "[.trace][preset][default_toms]")
{
    Membrum::Processor p;
    p.initialize(nullptr);
    Steinberg::Vst::ProcessSetup ps{};
    ps.processMode = Steinberg::Vst::kRealtime;
    ps.symbolicSampleSize = Steinberg::Vst::kSample32;
    ps.maxSamplesPerBlock = 1024;
    ps.sampleRate = 44100.0;
    p.setupProcessing(ps);
    p.setActive(true);
    // NO setState. Plugin uses initialize-time DefaultKit::apply.

    constexpr int kBlock = 1024;
    std::array<float, kBlock> outL{};
    std::array<float, kBlock> outR{};
    float* outChans[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outChans;

    struct EvList : Steinberg::Vst::IEventList {
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override
        { return Steinberg::kNoInterface; }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
        Steinberg::int32 PLUGIN_API getEventCount() override
        { return static_cast<Steinberg::int32>(events.size()); }
        Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 i,
                                               Steinberg::Vst::Event& e) override
        {
            if (i < 0 || i >= static_cast<Steinberg::int32>(events.size())) return Steinberg::kResultFalse;
            e = events[static_cast<size_t>(i)];
            return Steinberg::kResultOk;
        }
        Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event&) override
        { return Steinberg::kResultFalse; }
        std::vector<Steinberg::Vst::Event> events;
    };
    EvList ev;

    Steinberg::Vst::ProcessData d{};
    d.processMode = Steinberg::Vst::kRealtime;
    d.symbolicSampleSize = Steinberg::Vst::kSample32;
    d.numSamples = kBlock;
    d.numOutputs = 1;
    d.outputs = &outBus;
    d.inputEvents = &ev;

    auto runBlock = [&]() {
        outL.fill(0.0f);
        outR.fill(0.0f);
        p.process(d);
    };

    const int hitMidi[6] = {41, 43, 45, 47, 48, 50};
    for (int hit = 0; hit < 6; ++hit) {
        for (int n = 36; n <= 67; ++n) {
            ev.events.clear();
            Steinberg::Vst::Event e{};
            e.type = Steinberg::Vst::Event::kNoteOnEvent;
            e.noteOn.pitch = static_cast<Steinberg::int16>(n);
            e.noteOn.velocity = 0.0f;
            ev.events.push_back(e);
            runBlock();
        }
        ev.events.clear();
        for (int b = 0; b < 30; ++b) runBlock();

        ev.events.clear();
        Steinberg::Vst::Event e2{};
        e2.type = Steinberg::Vst::Event::kNoteOnEvent;
        e2.noteOn.pitch = static_cast<Steinberg::int16>(hitMidi[hit]);
        e2.noteOn.velocity = 1.0f;
        ev.events.push_back(e2);
        constexpr int kBlocks = 22;
        std::vector<float> audio(kBlocks * kBlock, 0.0f);
        for (int b = 0; b < kBlocks; ++b) {
            runBlock();
            for (int s = 0; s < kBlock; ++s)
                audio[b * kBlock + s] = outL[s];
            ev.events.clear();
        }
        const size_t startSample = 30 * 44;
        const size_t winLen = 200 * 44;
        double bestHz = 0, bestMag = 0;
        for (double f = 40.0; f <= 1500.0; f += 2.0) {
            double re = 0, im = 0;
            const double w = 2.0 * 3.14159265358979323846 * f / 44100.0;
            for (size_t i = 0; i < winLen; ++i) {
                const double s = audio[startSample + i];
                re += s * std::cos(w * static_cast<double>(i));
                im -= s * std::sin(w * static_cast<double>(i));
            }
            const double m = std::sqrt(re * re + im * im);
            if (m > bestMag) { bestMag = m; bestHz = f; }
        }
        std::printf("MIDI %d (no preset): dominant=%.0f Hz mag=%.2f\n",
                    hitMidi[hit], bestHz, bestMag);
    }
    p.setActive(false);
    p.terminate();
}

TEST_CASE("808 .vstpreset via Processor::setState: per-tom rendered pitch",
          "[.trace][preset][processor_setstate]")
{
    namespace fs = std::filesystem;
    fs::path kitPath;
    fs::path cur = fs::current_path();
    for (int up = 0; up < 8; ++up) {
        const auto candidate = cur / "plugins" / "membrum" / "resources"
            / "presets" / "Kit Presets" / "Electronic" / "808 Electronic Kit.vstpreset";
        if (fs::exists(candidate)) { kitPath = candidate; break; }
        if (!cur.has_parent_path() || cur.parent_path() == cur) break;
        cur = cur.parent_path();
    }
    if (kitPath.empty()) { std::printf("preset not found\n"); return; }

    std::ifstream in(kitPath, std::ios::binary);
    in.seekg(0, std::ios::end);
    const auto sz = static_cast<std::streamsize>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<size_t>(sz));
    in.read(reinterpret_cast<char*>(bytes.data()), sz);

    auto stream = Steinberg::owned(new Steinberg::MemoryStream(
        const_cast<std::uint8_t*>(bytes.data()),
        static_cast<Steinberg::TSize>(bytes.size())));
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::Vst::PresetFile pf(stream);
    REQUIRE(pf.readChunkList());
    const auto* compEntry = pf.getEntry(Steinberg::Vst::kComponentState);
    REQUIRE(compEntry != nullptr);

    // Build a stream containing JUST the component-state bytes.
    std::vector<std::uint8_t> compBytes(static_cast<size_t>(compEntry->size));
    stream->seek(compEntry->offset, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::int32 got = 0;
    stream->read(compBytes.data(),
                 static_cast<Steinberg::int32>(compBytes.size()), &got);
    Steinberg::MemoryStream componentStream;
    Steinberg::int32 written = 0;
    componentStream.write(compBytes.data(),
                          static_cast<Steinberg::int32>(compBytes.size()),
                          &written);
    componentStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    // Initialise Processor and feed it the component state via setState
    // (this is what a VST3 host does when loading a .vstpreset).
    Membrum::Processor p;
    p.initialize(nullptr);
    Steinberg::Vst::ProcessSetup ps{};
    ps.processMode = Steinberg::Vst::kRealtime;
    ps.symbolicSampleSize = Steinberg::Vst::kSample32;
    ps.maxSamplesPerBlock = 1024; // typical DAW block size
    ps.sampleRate = 44100.0;
    p.setupProcessing(ps);
    p.setActive(true);

    REQUIRE(p.setState(&componentStream) == Steinberg::kResultOk);

    // Print loaded cfg for each tom to verify setState applied them.
    const int tomPads[6] = {5, 7, 9, 11, 12, 14};
    for (int i = 0; i < 6; ++i) {
        const auto& cfg = p.voicePoolForTest().padConfig(tomPads[i]);
        const float startHz = 20.0f * std::pow(100.0f,
                                               std::clamp(cfg.tsPitchEnvStart, 0.0f, 1.0f));
        std::printf("[after setState] tom%d (pad %d): size=%.3f pitchEnvStartHz=%.1f time=%.3f\n",
                    i+1, tomPads[i], cfg.size, startHz, cfg.tsPitchEnvTime);
    }

    constexpr int kBlock = 1024; // match DAW block size
    std::array<float, kBlock> outL{};
    std::array<float, kBlock> outR{};
    float* outChans[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outChans;

    struct EvList : Steinberg::Vst::IEventList {
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override
        { return Steinberg::kNoInterface; }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
        Steinberg::int32 PLUGIN_API getEventCount() override
        { return static_cast<Steinberg::int32>(events.size()); }
        Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 i,
                                               Steinberg::Vst::Event& e) override
        {
            if (i < 0 || i >= static_cast<Steinberg::int32>(events.size())) return Steinberg::kResultFalse;
            e = events[static_cast<size_t>(i)];
            return Steinberg::kResultOk;
        }
        Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event&) override
        { return Steinberg::kResultFalse; }
        std::vector<Steinberg::Vst::Event> events;
    };
    EvList ev;

    Steinberg::Vst::ProcessData d{};
    d.processMode = Steinberg::Vst::kRealtime;
    d.symbolicSampleSize = Steinberg::Vst::kSample32;
    d.numSamples = kBlock;
    d.numOutputs = 1;
    d.outputs = &outBus;
    d.inputEvents = &ev;

    auto runBlock = [&]() {
        outL.fill(0.0f);
        outR.fill(0.0f);
        p.process(d);
    };

    auto fftPeak = [&](int padIdx, const char* tag) {
        for (int n = 36; n <= 67; ++n) {
            ev.events.clear();
            Steinberg::Vst::Event e{};
            e.type = Steinberg::Vst::Event::kNoteOnEvent;
            e.noteOn.pitch = static_cast<Steinberg::int16>(n);
            e.noteOn.velocity = 0.0f;
            ev.events.push_back(e);
            runBlock();
        }
        ev.events.clear();
        for (int b = 0; b < 30; ++b) runBlock();

        ev.events.clear();
        Steinberg::Vst::Event e2{};
        e2.type = Steinberg::Vst::Event::kNoteOnEvent;
        e2.noteOn.pitch = static_cast<Steinberg::int16>(36 + padIdx);
        e2.noteOn.velocity = 1.0f;
        ev.events.push_back(e2);
        constexpr int kBlocks = 96;
        std::vector<float> audio(kBlocks * kBlock, 0.0f);
        for (int b = 0; b < kBlocks; ++b) {
            runBlock();
            for (int s = 0; s < kBlock; ++s)
                audio[b * kBlock + s] = outL[s];
            ev.events.clear();
        }
        const size_t startSample = 30 * 44; // 30 ms @ 44.1k
        const size_t winLen = 200 * 44;
        double bestHz = 0, bestMag = 0;
        for (double f = 40.0; f <= 1500.0; f += 2.0) {
            double re = 0, im = 0;
            const double w = 2.0 * 3.14159265358979323846 * f / 44100.0;
            for (size_t i = 0; i < winLen; ++i) {
                const double s = audio[startSample + i];
                re += s * std::cos(w * static_cast<double>(i));
                im -= s * std::sin(w * static_cast<double>(i));
            }
            const double m = std::sqrt(re * re + im * im);
            if (m > bestMag) { bestMag = m; bestHz = f; }
        }
        float winPeak = 0;
        for (size_t i = 0; i < winLen; ++i)
            winPeak = std::max(winPeak, std::abs(audio[startSample + i]));
        std::printf("%s: dominant=%.0f Hz (mag=%.2f) winPeak=%.4f\n",
                    tag, bestHz, bestMag, winPeak);
    };
    fftPeak(5,  "tom1 (pad 5,  MIDI 41) [isolated]");
    fftPeak(7,  "tom2 (pad 7,  MIDI 43) [isolated]");
    fftPeak(9,  "tom3 (pad 9,  MIDI 45) [isolated]");
    fftPeak(11, "tom4 (pad 11, MIDI 47) [isolated]");
    fftPeak(12, "tom5 (pad 12, MIDI 48) [isolated]");
    fftPeak(14, "tom6 (pad 14, MIDI 50) [isolated]");

    // ---- Save synthetic audio to WAV so user can A/B compare with their render.
    // Format: stereo, 44.1k, 24-bit (matches user's render).
    auto writeWav = [](const std::string& path,
                        const std::vector<float>& mono,
                        int sampleRate) {
        const int channels = 2;
        const int bps = 24;
        const int byteRate = sampleRate * channels * (bps / 8);
        const int blockAlign = channels * (bps / 8);
        const int dataBytes = static_cast<int>(mono.size()) * channels * (bps / 8);
        std::ofstream f(path, std::ios::binary);
        auto wr32 = [&](std::uint32_t v){ f.write(reinterpret_cast<const char*>(&v), 4); };
        auto wr16 = [&](std::uint16_t v){ f.write(reinterpret_cast<const char*>(&v), 2); };
        f.write("RIFF", 4);
        wr32(36 + dataBytes);
        f.write("WAVE", 4);
        f.write("fmt ", 4);
        wr32(16);
        wr16(1); // PCM
        wr16(static_cast<std::uint16_t>(channels));
        wr32(static_cast<std::uint32_t>(sampleRate));
        wr32(static_cast<std::uint32_t>(byteRate));
        wr16(static_cast<std::uint16_t>(blockAlign));
        wr16(static_cast<std::uint16_t>(bps));
        f.write("data", 4);
        wr32(static_cast<std::uint32_t>(dataBytes));
        for (float s : mono) {
            const float c = std::clamp(s, -1.0f, 1.0f);
            const std::int32_t v24 =
                static_cast<std::int32_t>(c * 8388607.0f);
            for (int ch = 0; ch < channels; ++ch) {
                f.write(reinterpret_cast<const char*>(&v24), 3);
            }
        }
    };

    // ---- User-flow simulation: trigger all 6 toms in rapid sequence
    // every ~500 ms, capturing per-hit windows. NO drain between hits.
    std::printf("\n--- USER FLOW: 6 toms in rapid succession (500ms apart) ---\n");
    {
        // Drain ALL pads once.
        for (int n = 36; n <= 67; ++n) {
            ev.events.clear();
            Steinberg::Vst::Event e{};
            e.type = Steinberg::Vst::Event::kNoteOnEvent;
            e.noteOn.pitch = static_cast<Steinberg::int16>(n);
            e.noteOn.velocity = 0.0f;
            ev.events.push_back(e);
            runBlock();
        }
        ev.events.clear();
        for (int b = 0; b < 30; ++b) runBlock();

        // Now trigger 6 toms back to back, with 500 ms (~86 blocks @ 256/44.1k)
        // between hits.
        constexpr int kBlocksPerHit = 86;
        const int hitMidi[6] = {41, 43, 45, 47, 48, 50};
        std::vector<float> audio;
        std::vector<size_t> hitOffsets;
        audio.reserve(6 * kBlocksPerHit * kBlock);
        for (int hit = 0; hit < 6; ++hit) {
            ev.events.clear();
            Steinberg::Vst::Event e{};
            e.type = Steinberg::Vst::Event::kNoteOnEvent;
            e.noteOn.pitch = static_cast<Steinberg::int16>(hitMidi[hit]);
            e.noteOn.velocity = 1.0f;
            ev.events.push_back(e);
            hitOffsets.push_back(audio.size());
            for (int b = 0; b < kBlocksPerHit; ++b) {
                runBlock();
                for (int s = 0; s < kBlock; ++s)
                    audio.push_back(outL[s]);
                ev.events.clear();
            }
        }

        // Save the 6-tom rendering to disk so user can A/B compare.
        writeWav("C:/test/synth_toms.wav", audio, 44100);
        std::printf("Wrote C:/test/synth_toms.wav (%d frames)\n",
                    static_cast<int>(audio.size()));

        // FFT-analyse each hit's 30-200ms window.
        for (int hit = 0; hit < 6; ++hit) {
            const size_t startSample = hitOffsets[hit] + 30 * 44;
            const size_t winLen = 200 * 44;
            if (startSample + winLen >= audio.size()) continue;
            double bestHz = 0, bestMag = 0;
            for (double f = 40.0; f <= 1500.0; f += 2.0) {
                double re = 0, im = 0;
                const double w = 2.0 * 3.14159265358979323846 * f / 44100.0;
                for (size_t i = 0; i < winLen; ++i) {
                    const double s = audio[startSample + i];
                    re += s * std::cos(w * static_cast<double>(i));
                    im -= s * std::sin(w * static_cast<double>(i));
                }
                const double m = std::sqrt(re * re + im * im);
                if (m > bestMag) { bestMag = m; bestHz = f; }
            }
            float winPeak = 0;
            for (size_t i = 0; i < winLen; ++i)
                winPeak = std::max(winPeak, std::abs(audio[startSample + i]));
            std::printf("hit %d (MIDI %d): dominant=%.0f Hz mag=%.2f winPeak=%.4f\n",
                        hit + 1, hitMidi[hit], bestHz, bestMag, winPeak);
        }
    }

    p.setActive(false);
    p.terminate();
}

// Decode the on-disk 808 .vstpreset and print per-tom params to confirm what
// the disk actually contains.
TEST_CASE("808 .vstpreset: decode and print tom params",
          "[.trace][preset][toms_disk]")
{
    namespace fs = std::filesystem;
    fs::path kitPath;
    fs::path cur = fs::current_path();
    for (int up = 0; up < 8; ++up) {
        const auto candidate = cur / "plugins" / "membrum" / "resources"
            / "presets" / "Kit Presets" / "Electronic" / "808 Electronic Kit.vstpreset";
        if (fs::exists(candidate)) { kitPath = candidate; break; }
        if (!cur.has_parent_path() || cur.parent_path() == cur) break;
        cur = cur.parent_path();
    }
    if (kitPath.empty()) {
        std::printf("808 preset NOT FOUND\n");
        return;
    }
    std::printf("Reading: %s\n", kitPath.string().c_str());
    std::ifstream in(kitPath, std::ios::binary);
    in.seekg(0, std::ios::end);
    const auto sz = static_cast<std::streamsize>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<size_t>(sz));
    in.read(reinterpret_cast<char*>(bytes.data()), sz);

    auto stream = Steinberg::owned(new Steinberg::MemoryStream(
        const_cast<std::uint8_t*>(bytes.data()),
        static_cast<Steinberg::TSize>(bytes.size())));
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::Vst::PresetFile pf(stream);
    if (!pf.readChunkList()) { std::printf("readChunkList failed\n"); return; }
    const auto* compEntry = pf.getEntry(Steinberg::Vst::kComponentState);
    if (!compEntry) { std::printf("No component-state chunk\n"); return; }
    std::vector<std::uint8_t> compBytes(static_cast<size_t>(compEntry->size));
    stream->seek(compEntry->offset, Steinberg::IBStream::kIBSeekSet, nullptr);
    Steinberg::int32 got = 0;
    stream->read(compBytes.data(), static_cast<Steinberg::int32>(compBytes.size()), &got);
    Steinberg::MemoryStream componentStream;
    Steinberg::int32 written = 0;
    componentStream.write(compBytes.data(),
                          static_cast<Steinberg::int32>(compBytes.size()), &written);
    componentStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    Membrum::State::KitSnapshot kit;
    if (Membrum::State::readKitBlob(&componentStream, kit) != Steinberg::kResultOk) {
        std::printf("readKitBlob failed\n"); return;
    }

    Steinberg::int32 onDiskVersion = 0;
    std::memcpy(&onDiskVersion, compBytes.data(), sizeof(onDiskVersion));
    std::printf("on-disk blob version=%d (kBlobVersion=%d)\n",
                onDiskVersion, Membrum::State::kBlobVersion);

    const int tomPads[6] = {5, 7, 9, 11, 12, 14};
    for (int i = 0; i < 6; ++i) {
        Membrum::PadConfig cfg{};
        Membrum::State::applyPadSnapshot(
            kit.pads[static_cast<size_t>(tomPads[i])], cfg);
        const float startHz = 20.0f * std::pow(100.0f, std::clamp(cfg.tsPitchEnvStart, 0.0f, 1.0f));
        const float endHz   = 20.0f * std::pow(100.0f, std::clamp(cfg.tsPitchEnvEnd,   0.0f, 1.0f));
        std::printf("tom%d (pad %d): exciter=%d body=%d size=%.3f material=%.3f decay=%.3f "
                    "level=%.3f pitchStartHz=%.1f pitchEndHz=%.1f pitchTime=%.3f "
                    "noiseMix=%.3f clickMix=%.3f bodyB1=%.3f bodyB3=%.3f "
                    "tensionAmt=%.3f airLoading=%.3f enabled=%.0f\n",
                    i+1, tomPads[i],
                    static_cast<int>(cfg.exciterType),
                    static_cast<int>(cfg.bodyModel),
                    cfg.size, cfg.material, cfg.decay, cfg.level,
                    startHz, endHz, cfg.tsPitchEnvTime,
                    cfg.noiseLayerMix, cfg.clickLayerMix,
                    cfg.bodyDampingB1, cfg.bodyDampingB3,
                    cfg.tensionModAmt, cfg.airLoading, cfg.enabled);
    }
}

// User-facing repro: load the 808 factory tom configuration into a Processor
// via the same parameter-dispatch path the host uses, trigger each of the 6
// toms (MIDI 41, 43, 45, 47, 48, 50), and dump:
//   * the in-cfg pitch-env values that arrived per pad
//   * the in-voice mode 0 frequency reported by the modal bank
// If mode 0 is the SAME for every tom, the per-pad param dispatch chain is
// broken and the modes never see the per-tom config. If mode 0 differs but
// the user still hears identical pitch, the dominant audible pitch is coming
// from somewhere downstream of the bank (click/noise layer, exciter, etc.).
TEST_CASE("808 toms: per-pad mode 0 readback through real load path",
          "[.trace][processor][toms_real]")
{
    Membrum::Processor p;
    p.initialize(nullptr);
    Steinberg::Vst::ProcessSetup ps{};
    ps.processMode = Steinberg::Vst::kRealtime;
    ps.symbolicSampleSize = Steinberg::Vst::kSample32;
    ps.maxSamplesPerBlock = 256;
    ps.sampleRate = 48000.0;
    p.setupProcessing(ps);
    p.setActive(true);

    constexpr int kBlock = 256;
    std::array<float, kBlock> outL{};
    std::array<float, kBlock> outR{};
    float* outChans[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outChans;

    struct Q : Steinberg::Vst::IParamValueQueue {
        Q(Steinberg::Vst::ParamID i, Steinberg::Vst::ParamValue v) : id(i), val(v) {}
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override
        { return Steinberg::kNoInterface; }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
        Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id; }
        Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }
        Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32, Steinberg::int32& off,
                                                Steinberg::Vst::ParamValue& v) override
        { off = 0; v = val; return Steinberg::kResultOk; }
        Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32, Steinberg::Vst::ParamValue,
                                                Steinberg::int32&) override { return Steinberg::kResultFalse; }
        Steinberg::Vst::ParamID id; Steinberg::Vst::ParamValue val;
    };
    struct PC : Steinberg::Vst::IParameterChanges {
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override
        { return Steinberg::kNoInterface; }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
        Steinberg::int32 PLUGIN_API getParameterCount() override
        { return static_cast<Steinberg::int32>(qs.size()); }
        Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 i) override
        { return (i < 0 || i >= static_cast<Steinberg::int32>(qs.size())) ? nullptr : &qs[static_cast<size_t>(i)]; }
        Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID&,
                                                                       Steinberg::int32&) override { return nullptr; }
        std::vector<Q> qs;
    };
    struct EvList : Steinberg::Vst::IEventList {
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override
        { return Steinberg::kNoInterface; }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
        Steinberg::int32 PLUGIN_API getEventCount() override
        { return static_cast<Steinberg::int32>(events.size()); }
        Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 i,
                                               Steinberg::Vst::Event& e) override
        {
            if (i < 0 || i >= static_cast<Steinberg::int32>(events.size())) return Steinberg::kResultFalse;
            e = events[static_cast<size_t>(i)];
            return Steinberg::kResultOk;
        }
        Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event&) override
        { return Steinberg::kResultFalse; }
        std::vector<Steinberg::Vst::Event> events;
    };
    EvList ev;

    Steinberg::Vst::ProcessData d{};
    d.processMode = Steinberg::Vst::kRealtime;
    d.symbolicSampleSize = Steinberg::Vst::kSample32;
    d.numSamples = kBlock;
    d.numOutputs = 1;
    d.outputs = &outBus;
    d.inputEvents = &ev;

    auto runBlock = [&](Steinberg::Vst::IParameterChanges* pc = nullptr) {
        outL.fill(0.0f); outR.fill(0.0f);
        d.inputParameterChanges = pc;
        p.process(d);
        d.inputParameterChanges = nullptr;
    };

    auto pid = [](int padIdx, int off) {
        return static_cast<Steinberg::Vst::ParamID>(Membrum::padParamId(padIdx, off));
    };

    // Match the 808 preset's tom rows EXACTLY (tools/membrum_preset_generator.cpp).
    const int   tomPads[6]       = {5, 7, 9, 11, 12, 14};
    const float tomSizes[6]      = {0.85f, 0.75f, 0.65f, 0.55f, 0.48f, 0.40f};
    const float tomPitchStartHz[6] = {220.0f, 260.0f, 310.0f, 370.0f, 430.0f, 500.0f};
    const float tomPitchEndHz[6]   = { 80.0f,  95.0f, 115.0f, 140.0f, 175.0f, 220.0f};
    const float tomPitchTime[6]    = {0.50f, 0.42f, 0.36f, 0.30f, 0.24f, 0.18f};
    const float tomMaterial[6]     = {0.18f, 0.25f, 0.32f, 0.40f, 0.50f, 0.60f};
    const float tomDecay[6]        = {0.65f, 0.58f, 0.50f, 0.43f, 0.35f, 0.28f};
    const float tomDampingB1[6]    = {0.10f, 0.15f, 0.20f, 0.25f, 0.32f, 0.42f};
    auto toLogNorm = [](float hz) {
        return std::log(hz / 20.0f) / std::log(100.0f);
    };

    PC pc;
    for (int i = 0; i < 6; ++i) {
        const int p_ = tomPads[i];
        pc.qs.emplace_back(pid(p_, Membrum::kPadExciterType),
            static_cast<Steinberg::Vst::ParamValue>(Membrum::ExciterType::Mallet)
              / static_cast<Steinberg::Vst::ParamValue>(Membrum::ExciterType::kCount));
        pc.qs.emplace_back(pid(p_, Membrum::kPadBodyModel),
            static_cast<Steinberg::Vst::ParamValue>(Membrum::BodyModelType::Membrane)
              / static_cast<Steinberg::Vst::ParamValue>(Membrum::BodyModelType::kCount));
        pc.qs.emplace_back(pid(p_, Membrum::kPadSize),         tomSizes[i]);
        pc.qs.emplace_back(pid(p_, Membrum::kPadMaterial),     tomMaterial[i]);
        pc.qs.emplace_back(pid(p_, Membrum::kPadDecay),        tomDecay[i]);
        pc.qs.emplace_back(pid(p_, Membrum::kPadLevel),        0.85);
        pc.qs.emplace_back(pid(p_, Membrum::kPadTSPitchEnvStart), toLogNorm(tomPitchStartHz[i]));
        pc.qs.emplace_back(pid(p_, Membrum::kPadTSPitchEnvEnd),   toLogNorm(tomPitchEndHz[i]));
        pc.qs.emplace_back(pid(p_, Membrum::kPadTSPitchEnvTime),  tomPitchTime[i]);
        pc.qs.emplace_back(pid(p_, Membrum::kPadTSPitchEnvCurve), 1.0); // Linear
        pc.qs.emplace_back(pid(p_, Membrum::kPadAirLoading),     0.0);
        pc.qs.emplace_back(pid(p_, Membrum::kPadTensionModAmt),  0.30);
        pc.qs.emplace_back(pid(p_, Membrum::kPadNoiseLayerMix),  0.05);
        pc.qs.emplace_back(pid(p_, Membrum::kPadClickLayerMix),  0.05);
        pc.qs.emplace_back(pid(p_, Membrum::kPadBodyDampingB1),  tomDampingB1[i]);
        pc.qs.emplace_back(pid(p_, Membrum::kPadBodyDampingB3),  0.10);
        pc.qs.emplace_back(pid(p_, Membrum::kPadEnabled),        1.0);
    }
    runBlock(&pc);

    // Trigger each tom in turn (drain between) and read back mode 0.
    for (int i = 0; i < 6; ++i) {
        const Steinberg::int16 midi = static_cast<Steinberg::int16>(36 + tomPads[i]);
        // Drain prior voices.
        for (int n = 36; n <= 67; ++n) {
            ev.events.clear();
            Steinberg::Vst::Event e{};
            e.type = Steinberg::Vst::Event::kNoteOnEvent;
            e.noteOn.pitch = static_cast<Steinberg::int16>(n);
            e.noteOn.velocity = 0.0f;
            ev.events.push_back(e);
            runBlock();
        }
        ev.events.clear();
        for (int b = 0; b < 30; ++b) runBlock();
        // Trigger this tom.
        ev.events.clear();
        Steinberg::Vst::Event ev2{};
        ev2.type = Steinberg::Vst::Event::kNoteOnEvent;
        ev2.noteOn.pitch = midi;
        ev2.noteOn.velocity = 1.0f;
        ev.events.push_back(ev2);
        runBlock();

        auto& vp = p.voicePoolForTest();
        const auto& cfg = vp.padConfig(tomPads[i]);
        float mode0 = -1.0f, natFund = -1.0f;
        float startHz = -1.0f;
        bool found = false;
        vp.forEachMainVoice([&](Membrum::DrumVoice& v) {
            if (found) return;
            const auto& bank = v.bodyBank().getSharedBank();
            if (bank.getNumActiveModes() == 0) return;
            found = true;
            mode0   = bank.getModeFrequency(0);
            natFund = v.getNaturalFundamentalHz();
            startHz = v.toneShaper().getPitchEnvStartHz();
        });
        std::printf(
            "tom%d (pad %d, MIDI %d): cfg.size=%.3f cfg.pitchEnvStart=%.3f "
            "cfg.pitchEnvTime=%.3f -> natFund=%.1fHz toneShaper.startHz=%.1fHz "
            "bank.mode0=%.1fHz\n",
            i+1, tomPads[i], midi,
            cfg.size, cfg.tsPitchEnvStart, cfg.tsPitchEnvTime,
            natFund, startHz, mode0);
    }
    // -------------------------------------------------------------------
    // FFT analysis: render tom1 and tom6 audio fully, then sweep DFT
    // bins to find the dominant audible peak in the rendered output.
    // -------------------------------------------------------------------
    auto fftPeakHz = [&](Steinberg::int16 midi, const char* tag,
                         bool stripLayers) {
        // Optionally zero-out noise + click layers for THIS pad so we can
        // tell whether they mask the mode-0 pitch.
        if (stripLayers) {
            const int padIdx = midi - 36;
            PC pcStrip;
            pcStrip.qs.emplace_back(pid(padIdx, Membrum::kPadNoiseLayerMix), 0.0);
            pcStrip.qs.emplace_back(pid(padIdx, Membrum::kPadClickLayerMix), 0.0);
            runBlock(&pcStrip);
        }
        for (int n = 36; n <= 67; ++n) {
            ev.events.clear();
            Steinberg::Vst::Event e{};
            e.type = Steinberg::Vst::Event::kNoteOnEvent;
            e.noteOn.pitch = static_cast<Steinberg::int16>(n);
            e.noteOn.velocity = 0.0f;
            ev.events.push_back(e);
            runBlock();
        }
        ev.events.clear();
        for (int b = 0; b < 30; ++b) runBlock();

        ev.events.clear();
        Steinberg::Vst::Event e2{};
        e2.type = Steinberg::Vst::Event::kNoteOnEvent;
        e2.noteOn.pitch = midi;
        e2.noteOn.velocity = 1.0f;
        ev.events.push_back(e2);

        // Capture 0.5 s of audio.
        constexpr int kBlocks = 96;
        std::vector<float> audio(kBlocks * kBlock, 0.0f);
        for (int b = 0; b < kBlocks; ++b) {
            runBlock();
            for (int s = 0; s < kBlock; ++s)
                audio[b * kBlock + s] = outL[s];
            ev.events.clear();
        }
        // Skip first 30ms attack, take 200ms window for analysis.
        const size_t startSample = 30 * 48;
        const size_t winLen = 200 * 48; // 9600 samples
        // Naive DFT scan over [40, 1200] Hz at 5 Hz resolution. The body's
        // mode 0 / dominant audible body pitch should fall in this range.
        double bestHz = 0.0, bestMag = 0.0;
        for (double f = 40.0; f <= 1200.0; f += 2.0) {
            double re = 0.0, im = 0.0;
            const double w = 2.0 * 3.14159265358979323846 * f / 48000.0;
            for (size_t i = 0; i < winLen; ++i) {
                const double s = audio[startSample + i];
                re += s * std::cos(w * static_cast<double>(i));
                im -= s * std::sin(w * static_cast<double>(i));
            }
            const double mag = std::sqrt(re * re + im * im);
            if (mag > bestMag) { bestMag = mag; bestHz = f; }
        }
        // Also report the peak amplitude in window for context.
        float winPeak = 0.0f;
        for (size_t i = 0; i < winLen; ++i)
            winPeak = std::max(winPeak, std::abs(audio[startSample + i]));
        std::printf("%s strip=%d: dominant=%.0f Hz (mag=%.2f) winPeak=%.4f\n",
                    tag, stripLayers ? 1 : 0, bestHz, bestMag, winPeak);
    };

    // Layers ENABLED (preset state) — what user actually hears.
    fftPeakHz(41, "tom1 (size=0.85)", false);
    fftPeakHz(50, "tom6 (size=0.40)", false);
    // Layers DISABLED — body-only signal.
    fftPeakHz(41, "tom1 (size=0.85)", true);
    fftPeakHz(50, "tom6 (size=0.40)", true);

    p.setActive(false);
    p.terminate();
}

TEST_CASE("Processor full path: trace DC offset (high tom +0.04 mean repro)",
          "[.trace][processor][repro]")
{
    // EXACT mirror of the earlier failing observation:
    //   - send per-pad params for pads 5 + 14 (size, material, decay, level,
    //     pitch env, enabled, noiseLayerMix=0, clickLayerMix=0)
    //   - drain voices, render pad 14 (MIDI 50) at vel=0.1
    Membrum::Processor p;
    p.initialize(nullptr);
    Steinberg::Vst::ProcessSetup ps{};
    ps.processMode = Steinberg::Vst::kRealtime;
    ps.symbolicSampleSize = Steinberg::Vst::kSample32;
    ps.maxSamplesPerBlock = 256;
    ps.sampleRate = 48000.0;
    p.setupProcessing(ps);
    p.setActive(true);

    constexpr int kBlock = 256;
    std::array<float, kBlock> outL{};
    std::array<float, kBlock> outR{};
    float* outChans[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outChans;

    struct Q : Steinberg::Vst::IParamValueQueue {
        Q(Steinberg::Vst::ParamID i, Steinberg::Vst::ParamValue v) : id(i), val(v) {}
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override
        { return Steinberg::kNoInterface; }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
        Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id; }
        Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }
        Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32, Steinberg::int32& off,
                                                Steinberg::Vst::ParamValue& v) override
        { off = 0; v = val; return Steinberg::kResultOk; }
        Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32, Steinberg::Vst::ParamValue,
                                                Steinberg::int32&) override { return Steinberg::kResultFalse; }
        Steinberg::Vst::ParamID id; Steinberg::Vst::ParamValue val;
    };
    struct PC : Steinberg::Vst::IParameterChanges {
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override
        { return Steinberg::kNoInterface; }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
        Steinberg::int32 PLUGIN_API getParameterCount() override
        { return static_cast<Steinberg::int32>(qs.size()); }
        Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 i) override
        { return (i < 0 || i >= static_cast<Steinberg::int32>(qs.size())) ? nullptr : &qs[static_cast<size_t>(i)]; }
        Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID&,
                                                                       Steinberg::int32&) override { return nullptr; }
        std::vector<Q> qs;
    };
    struct Events : Steinberg::Vst::IEventList {
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override
        { return Steinberg::kNoInterface; }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
        Steinberg::int32 PLUGIN_API getEventCount() override
        { return static_cast<Steinberg::int32>(events.size()); }
        Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 i,
                                               Steinberg::Vst::Event& e) override
        {
            if (i < 0 || i >= static_cast<Steinberg::int32>(events.size())) return Steinberg::kResultFalse;
            e = events[static_cast<size_t>(i)];
            return Steinberg::kResultOk;
        }
        Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event&) override
        { return Steinberg::kResultFalse; }
        std::vector<Steinberg::Vst::Event> events;
    };
    Events ev;

    Steinberg::Vst::ProcessData d{};
    d.processMode = Steinberg::Vst::kRealtime;
    d.symbolicSampleSize = Steinberg::Vst::kSample32;
    d.numSamples = kBlock;
    d.numOutputs = 1;
    d.outputs = &outBus;
    d.inputEvents = &ev;

    auto runBlock = [&](Steinberg::Vst::IParameterChanges* pc = nullptr) {
        outL.fill(0.0f);
        outR.fill(0.0f);
        d.inputParameterChanges = pc;
        p.process(d);
        d.inputParameterChanges = nullptr;
    };

    auto pid = [](int padIdx, int off) {
        return static_cast<Steinberg::Vst::ParamID>(Membrum::padParamId(padIdx, off));
    };

    // Send pad 5 + pad 14 params (mirror of test_kit_preset_load_render).
    PC pc;
    pc.qs.emplace_back(pid(5,  Membrum::kPadSize),            0.85);
    pc.qs.emplace_back(pid(5,  Membrum::kPadMaterial),        0.20);
    pc.qs.emplace_back(pid(5,  Membrum::kPadDecay),           0.55);
    pc.qs.emplace_back(pid(5,  Membrum::kPadLevel),           0.85);
    pc.qs.emplace_back(pid(5,  Membrum::kPadTSPitchEnvStart), 0.30);
    pc.qs.emplace_back(pid(5,  Membrum::kPadTSPitchEnvEnd),   0.30);
    pc.qs.emplace_back(pid(5,  Membrum::kPadTSPitchEnvTime),  0.05);
    pc.qs.emplace_back(pid(5,  Membrum::kPadEnabled),         1.0);
    pc.qs.emplace_back(pid(5,  Membrum::kPadNoiseLayerMix),   0.0);
    pc.qs.emplace_back(pid(5,  Membrum::kPadClickLayerMix),   0.0);
    pc.qs.emplace_back(pid(14, Membrum::kPadSize),            0.40);
    pc.qs.emplace_back(pid(14, Membrum::kPadMaterial),        0.50);
    pc.qs.emplace_back(pid(14, Membrum::kPadDecay),           0.30);
    pc.qs.emplace_back(pid(14, Membrum::kPadLevel),           0.85);
    pc.qs.emplace_back(pid(14, Membrum::kPadTSPitchEnvStart), 0.65);
    pc.qs.emplace_back(pid(14, Membrum::kPadTSPitchEnvEnd),   0.65);
    pc.qs.emplace_back(pid(14, Membrum::kPadTSPitchEnvTime),  0.05);
    pc.qs.emplace_back(pid(14, Membrum::kPadEnabled),         1.0);
    pc.qs.emplace_back(pid(14, Membrum::kPadNoiseLayerMix),   0.0);
    pc.qs.emplace_back(pid(14, Membrum::kPadClickLayerMix),   0.0);
    runBlock(&pc);

    // Render LOW pad (pad 5, MIDI 41) and HIGH pad (pad 14, MIDI 50) and
    // dump min/max/mean per 10ms windows.
    auto render = [&](Steinberg::int16 midi, const char* tag) {
        for (int n = 36; n <= 67; ++n) {
            ev.events.clear();
            Steinberg::Vst::Event e{};
            e.type = Steinberg::Vst::Event::kNoteOnEvent;
            e.noteOn.pitch = static_cast<Steinberg::int16>(n);
            e.noteOn.velocity = 0.0f;
            ev.events.push_back(e);
            runBlock();
        }
        ev.events.clear();
        for (int b = 0; b < 50; ++b) runBlock();

        ev.events.clear();
        Steinberg::Vst::Event ev2{};
        ev2.type = Steinberg::Vst::Event::kNoteOnEvent;
        ev2.noteOn.pitch = midi;
        ev2.noteOn.velocity = 0.1f;
        ev.events.push_back(ev2);

        constexpr int kBlocks = 30;
        std::vector<float> out(kBlocks * kBlock, 0.0f);
        for (int b = 0; b < kBlocks; ++b) {
            runBlock();
            for (int s = 0; s < kBlock; ++s) out[b * kBlock + s] = outL[s];
            ev.events.clear();
        }
        const int win = 480;
        std::printf("%s windows: ", tag);
        for (int i = 0; i + win < static_cast<int>(out.size()); i += win) {
            double sum = 0.0;
            float mn = out[i], mx = out[i];
            for (int j = 0; j < win; ++j) {
                mn = std::min(mn, out[i + j]);
                mx = std::max(mx, out[i + j]);
                sum += out[i + j];
            }
            std::printf("[%.4f,%.4f,%.4f] ", mn, mx, sum / win);
        }
        std::printf("\n");
        float peak = 0.0f;
        for (float s : out) peak = std::max(peak, std::abs(s));
        double sumAll = 0.0;
        for (float s : out) sumAll += s;
        std::printf("%s peak=%.4f, total mean=%.6f\n",
                    tag, peak, sumAll / static_cast<double>(out.size()));
    };
    render(41, "LOW (pad5)");
    render(50, "HIGH (pad14)");

    // Re-run at vel=1.0 (matches user's "hit pad in DAW" scenario) and count
    // zero crossings: if mode-frequency info SURVIVES saturation, the high
    // pad must show many more crossings than the low pad.
    auto countCrossings = [&](Steinberg::int16 midi, const char* tag, float vel) {
        for (int n = 36; n <= 67; ++n) {
            ev.events.clear();
            Steinberg::Vst::Event e{};
            e.type = Steinberg::Vst::Event::kNoteOnEvent;
            e.noteOn.pitch = static_cast<Steinberg::int16>(n);
            e.noteOn.velocity = 0.0f;
            ev.events.push_back(e);
            runBlock();
        }
        ev.events.clear();
        for (int b = 0; b < 50; ++b) runBlock();
        ev.events.clear();
        Steinberg::Vst::Event ev2{};
        ev2.type = Steinberg::Vst::Event::kNoteOnEvent;
        ev2.noteOn.pitch = midi;
        ev2.noteOn.velocity = vel;
        ev.events.push_back(ev2);
        constexpr int kBlocks = 30;
        std::vector<float> out(kBlocks * kBlock, 0.0f);
        for (int b = 0; b < kBlocks; ++b) {
            runBlock();
            for (int s = 0; s < kBlock; ++s) out[b * kBlock + s] = outL[s];
            ev.events.clear();
        }
        // Skip first 5ms (attack), measure crossings over 5..150ms.
        const size_t s0 = 240;
        const size_t s1 = std::min(out.size(), static_cast<size_t>(7200));
        int crossings = 0;
        for (size_t i = s0 + 1; i < s1; ++i) {
            if ((out[i - 1] < 0.0f && out[i] >= 0.0f) ||
                (out[i - 1] >= 0.0f && out[i] < 0.0f))
                ++crossings;
        }
        const double durationS = (s1 - s0) / 48000.0;
        const double dominantHz = crossings / (2.0 * durationS);
        float peak = 0.0f;
        for (float s : out) peak = std::max(peak, std::abs(s));
        std::printf("%s vel=%.2f crossings=%d -> ~%.1f Hz, peak=%.4f\n",
                    tag, vel, crossings, dominantHz, peak);
    };
    countCrossings(41, "LOW  (pad5,  size=0.85, env=80Hz)",  1.0f);
    countCrossings(50, "HIGH (pad14, size=0.40, env=400Hz)", 1.0f);
    countCrossings(41, "LOW  (pad5)",  0.1f);
    countCrossings(50, "HIGH (pad14)", 0.1f);

    p.setActive(false);
    p.terminate();
}

TEST_CASE("Processor full path: trace DC offset",
          "[.trace][processor][simple]")
{
    // Mimic test_kit_preset_load_render's renderPad path.
    Membrum::Processor p;
    p.initialize(nullptr);
    Steinberg::Vst::ProcessSetup ps{};
    ps.processMode = Steinberg::Vst::kRealtime;
    ps.symbolicSampleSize = Steinberg::Vst::kSample32;
    ps.maxSamplesPerBlock = 256;
    ps.sampleRate = 48000.0;
    p.setupProcessing(ps);
    p.setActive(true);

    constexpr int kBlock = 256;
    std::array<float, kBlock> outL{};
    std::array<float, kBlock> outR{};
    float* outChans[2] = {outL.data(), outR.data()};
    Steinberg::Vst::AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outChans;

    // Build a minimal IEventList stand-in inline.
    struct Events : Steinberg::Vst::IEventList {
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override
        { return Steinberg::kNoInterface; }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
        Steinberg::int32 PLUGIN_API getEventCount() override
        { return static_cast<Steinberg::int32>(events.size()); }
        Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 i,
                                               Steinberg::Vst::Event& e) override
        {
            if (i < 0 || i >= static_cast<Steinberg::int32>(events.size())) return Steinberg::kResultFalse;
            e = events[static_cast<size_t>(i)];
            return Steinberg::kResultOk;
        }
        Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event&) override
        { return Steinberg::kResultFalse; }
        std::vector<Steinberg::Vst::Event> events;
    };
    Events ev;

    Steinberg::Vst::ProcessData d{};
    d.processMode = Steinberg::Vst::kRealtime;
    d.symbolicSampleSize = Steinberg::Vst::kSample32;
    d.numSamples = kBlock;
    d.numOutputs = 1;
    d.outputs = &outBus;
    d.inputEvents = &ev;

    auto runBlock = [&]() {
        outL.fill(0.0f);
        outR.fill(0.0f);
        p.process(d);
    };

    // Mirror renderPad: drain via noteOn(vel=0) for every pad, then 50 silent
    // blocks, then trigger pad 5 (MIDI 41).
    for (int n = 36; n <= 67; ++n) {
        ev.events.clear();
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOnEvent;
        e.noteOn.pitch = static_cast<Steinberg::int16>(n);
        e.noteOn.velocity = 0.0f;
        ev.events.push_back(e);
        runBlock();
    }
    ev.events.clear();
    for (int b = 0; b < 50; ++b) runBlock();

    ev.events.clear();
    Steinberg::Vst::Event e{};
    e.type = Steinberg::Vst::Event::kNoteOnEvent;
    e.noteOn.pitch = 41;
    e.noteOn.velocity = 0.1f;
    ev.events.push_back(e);

    // Render 30 blocks
    constexpr int kBlocks = 30;
    std::vector<float> out(kBlocks * kBlock, 0.0f);
    for (int b = 0; b < kBlocks; ++b) {
        runBlock();
        for (int s = 0; s < kBlock; ++s) out[b * kBlock + s] = outL[s];
        ev.events.clear();
    }

    const int win = 480;
    std::printf("Processor mean per 10ms: ");
    for (int i = 0; i + win < static_cast<int>(out.size()); i += win) {
        double sum = 0.0;
        float mn = out[i], mx = out[i];
        for (int j = 0; j < win; ++j) {
            mn = std::min(mn, out[i + j]);
            mx = std::max(mx, out[i + j]);
            sum += out[i + j];
        }
        std::printf("[%.4f,%.4f,%.4f] ", mn, mx, sum / win);
    }
    std::printf("\n");
    float peak = 0.0f;
    for (float s : out) peak = std::max(peak, std::abs(s));
    double sumAll = 0.0;
    for (float s : out) sumAll += s;
    std::printf("Processor peak=%.4f, total mean=%.6f\n",
                peak, sumAll / static_cast<double>(out.size()));

    p.setActive(false);
    p.terminate();
}

TEST_CASE("DrumVoice WITH noise+click layers: trace DC",
          "[.trace][dsp][drum_voice][layers]")
{
    Membrum::DrumVoice v;
    v.prepare(48000.0, 0);
    v.setExciterType(Membrum::ExciterType::Mallet);
    v.setBodyModel(Membrum::BodyModelType::Membrane);
    v.setMaterial(0.20f);
    v.setSize(0.85f);
    v.setDecay(0.55f);
    v.setStrikePosition(0.30f);
    v.setLevel(0.85f);
    v.setBodyDampingB1(-1.0f);
    v.setBodyDampingB3(-1.0f);
    v.setAirLoading(0.6f);
    v.toneShaper().setPitchEnvStartHz(80.0f);
    v.toneShaper().setPitchEnvEndHz(80.0f);
    v.toneShaper().setPitchEnvTimeMs(25.0f);

    // ENABLE noise and click at default mix levels.
    v.setNoiseLayerMix(0.35f);
    v.setNoiseLayerCutoff(0.5f);
    v.setNoiseLayerResonance(0.2f);
    v.setNoiseLayerDecay(0.3f);
    v.setNoiseLayerColor(0.5f);
    v.setClickLayerMix(0.5f);
    v.setClickLayerContactMs(0.3f);
    v.setClickLayerBrightness(0.6f);

    v.noteOn(0.1f);

    constexpr int kBlock = 256;
    constexpr int kBlocks = 30;
    std::vector<float> out(kBlocks * kBlock, 0.0f);
    for (int b = 0; b < kBlocks; ++b)
        v.processBlock(out.data() + b * kBlock, kBlock);

    const int win = 480;
    std::printf("DrumVoice+layers mean per 10ms: ");
    for (int i = 0; i + win < static_cast<int>(out.size()); i += win) {
        double sum = 0.0;
        float mn = out[i], mx = out[i];
        for (int j = 0; j < win; ++j) {
            mn = std::min(mn, out[i + j]);
            mx = std::max(mx, out[i + j]);
            sum += out[i + j];
        }
        std::printf("[%.4f,%.4f,%.4f] ", mn, mx, sum / win);
    }
    std::printf("\n");
    float peak = 0.0f;
    for (float s : out) peak = std::max(peak, std::abs(s));
    double sumAll = 0.0;
    for (float s : out) sumAll += s;
    std::printf("DrumVoice+layers peak=%.4f, total mean=%.6f\n",
                peak, sumAll / static_cast<double>(out.size()));
}
