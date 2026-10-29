// ==============================================================================
// PROBE: the Freeze factory presets rendered through the processor
// ==============================================================================
// Hidden. Loads every .vstpreset of the Freeze folder (ITERUM_FREEZE_PRESET_DIR, or the resources folder when it
// is unset) into a processor brought up as a host does, renders 8 s at 120 BPM with the transport playing on three
// inputs (a tone that changes pitch every bar, a one-bar drum loop, a sustained chord) and prints, per preset and
// input, what the audition needs to judge the preset by number:
//   out     RMS of the output re the input over [2, 8 s) (dB)
//   diff    energy of (out - in) re the input over [2, 8 s) (dB): how much the preset changes the sound
//   moved   fraction of 10 ms windows in [2, 8 s) where the RMS of (out - in) exceeds -20 dB re the input's RMS
//   peak    largest |out| re the largest |in| (dB)
//   held    on the tone input only: which bar's pitch dominates the output over [5, 6 s) (input there: 415 Hz;
//           the bar before: 311 Hz; the first bar: 233 Hz). A preset with Hold on must hold 233: its 233 Hz
//           power is 10 x the 311 Hz power of the bar a following preset would play.
// It asserts only what every preset must satisfy: a finite output, audible change on at least one input, a peak
// at most +6 dB above the input's, and the Hold presets holding the first bar.
//
// Run: plugin_tests.exe "PROBE Freeze factory presets" (one hidden case by name).

#include "processor/processor.h"
#include "delay_mode.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr double kSr = 44100.0;
constexpr Steinberg::int32 kBlock = 512;
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr size_t kTotal = static_cast<size_t>(kSr * 8.0);

/// The component-state chunk ("Comp") of a .vstpreset file.
[[nodiscard]] std::vector<char> componentChunk(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(bytes.size() > 48);
    REQUIRE(std::memcmp(bytes.data(), "VST3", 4) == 0);
    int64_t listOffset = 0;
    std::memcpy(&listOffset, bytes.data() + 40, sizeof(listOffset));
    REQUIRE(std::memcmp(bytes.data() + listOffset, "List", 4) == 0);
    int32_t count = 0;
    std::memcpy(&count, bytes.data() + listOffset + 4, sizeof(count));
    for (int32_t i = 0; i < count; ++i) {
        const char* entry = bytes.data() + listOffset + 8 + static_cast<ptrdiff_t>(i) * 20;
        if (std::memcmp(entry, "Comp", 4) == 0) {
            int64_t offset = 0;
            int64_t size = 0;
            std::memcpy(&offset, entry + 4, sizeof(offset));
            std::memcpy(&size, entry + 12, sizeof(size));
            return {bytes.begin() + offset, bytes.begin() + offset + size};
        }
    }
    FAIL("no Comp chunk in " << file.string());
    return {};
}

/// Renders @p input (mono, both channels) through a processor restored from @p state; returns the left output.
[[nodiscard]] std::vector<float> render(std::vector<char> state, const std::vector<float>& input) {
    Steinberg::IPtr<Iterum::Processor> proc = Steinberg::owned(new Iterum::Processor());
    REQUIRE(proc->initialize(nullptr) == Steinberg::kResultOk);
    Steinberg::Vst::ProcessSetup setup{.processMode = Steinberg::Vst::kRealtime,
                                       .symbolicSampleSize = Steinberg::Vst::kSample32,
                                       .maxSamplesPerBlock = kBlock,
                                       .sampleRate = kSr};
    REQUIRE(proc->setupProcessing(setup) == Steinberg::kResultOk);
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    Steinberg::int32 written = 0;
    stream->write(state.data(), static_cast<Steinberg::int32>(state.size()), &written);
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc->setState(stream) == Steinberg::kResultOk);
    REQUIRE(proc->setActive(true) == Steinberg::kResultOk);

    Steinberg::Vst::HostProcessData data;
    REQUIRE(data.prepare(*proc, kBlock, Steinberg::Vst::kSample32));
    Steinberg::Vst::ProcessContext context{};
    Steinberg::Vst::ParameterChanges inChanges{8};
    Steinberg::Vst::ParameterChanges outChanges{64};
    data.numSamples = kBlock;
    data.processContext = &context;
    data.inputParameterChanges = &inChanges;
    data.outputParameterChanges = &outChanges;
    context.sampleRate = kSr;
    context.tempo = 120.0;

    std::vector<float> out;
    out.reserve(input.size());
    for (size_t s = 0; s + static_cast<size_t>(kBlock) <= input.size(); s += static_cast<size_t>(kBlock)) {
        for (Steinberg::int32 bus = 0; bus < data.numInputs; ++bus) {
            for (Steinberg::int32 ch = 0; ch < data.inputs[bus].numChannels; ++ch) {
                std::copy_n(input.begin() + static_cast<ptrdiff_t>(s), kBlock, data.inputs[bus].channelBuffers32[ch]);
            }
        }
        context.state = Steinberg::Vst::ProcessContext::kTempoValid | Steinberg::Vst::ProcessContext::kPlaying |
                        Steinberg::Vst::ProcessContext::kProjectTimeMusicValid;
        context.projectTimeMusic = static_cast<double>(s) * 2.0 / kSr;  // 120 BPM
        context.projectTimeSamples = static_cast<Steinberg::int64>(s);
        outChanges.clearQueue();
        REQUIRE(proc->process(data) == Steinberg::kResultOk);
        const float* l = data.outputs[0].channelBuffers32[0];
        out.insert(out.end(), l, l + kBlock);
    }
    data.unprepare();
    proc->setActive(static_cast<Steinberg::TBool>(false));
    proc->terminate();
    return out;
}

[[nodiscard]] std::vector<float> toneInput() {
    std::vector<float> x(kTotal);
    double phase = 0.0;
    for (size_t i = 0; i < kTotal; ++i) {
        const double t = static_cast<double>(i) / kSr;
        const double bars[] = {233.0, 311.0, 415.0};
        const double hz = bars[static_cast<size_t>(t / 2.0) % 3];
        x[i] = static_cast<float>(0.5 * std::sin(phase));
        phase += kTwoPi * hz / kSr;
    }
    return x;
}

[[nodiscard]] std::vector<float> drumInput() {
    // Per beat a kick (60 Hz, 80 ms decay, peak 0.8); on the off-beat a hat (noise, 20 ms decay, peak 0.25);
    // a 233 Hz pad at 0.05 throughout. One beat is 22050 samples.
    std::vector<float> x(kTotal);
    uint32_t rng = 12345u;
    for (size_t i = 0; i < kTotal; ++i) {
        const double inBeat = static_cast<double>(i % 22050) / kSr;
        const double inOff = static_cast<double>((i + 11025) % 22050) / kSr;
        rng = rng * 1664525u + 1013904223u;
        const double noise = static_cast<double>(rng >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
        const double kick = 0.8 * std::exp(-inBeat / 0.08) * std::sin(kTwoPi * 60.0 * inBeat);
        const double hat = 0.25 * std::exp(-inOff / 0.02) * noise;
        const double pad = 0.05 * std::sin(kTwoPi * 233.0 * static_cast<double>(i) / kSr);
        x[i] = static_cast<float>(kick + hat + pad);
    }
    return x;
}

[[nodiscard]] std::vector<float> chordInput() {
    std::vector<float> x(kTotal);
    for (size_t i = 0; i < kTotal; ++i) {
        const double t = static_cast<double>(i) / kSr;
        x[i] = static_cast<float>(0.2 * (std::sin(kTwoPi * 233.0 * t) + std::sin(kTwoPi * 293.7 * t) +
                                         std::sin(kTwoPi * 349.2 * t)));
    }
    return x;
}

[[nodiscard]] double tonePower(const std::vector<float>& x, size_t a, size_t b, double hz) {
    const double w = kTwoPi * hz / kSr;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0;
    double s2 = 0.0;
    for (size_t i = a; i < b; ++i) {
        const double s0 = static_cast<double>(x[i]) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

struct Measures {
    double outDb = 0.0;
    double diffDb = 0.0;
    double moved = 0.0;
    double peakDb = 0.0;
    bool finite = true;
};

[[nodiscard]] Measures measure(const std::vector<float>& in, const std::vector<float>& out) {
    Measures m;
    const auto a = static_cast<size_t>(kSr * 2.0);
    const size_t b = std::min(in.size(), out.size());
    double eIn = 0.0;
    double eOut = 0.0;
    double eDiff = 0.0;
    double pIn = 0.0;
    double pOut = 0.0;
    for (size_t i = a; i < b; ++i) {
        const double x = in[i];
        const double y = out[i];
        if (!(std::abs(y) < 1e9)) {  // also false for NaN
            m.finite = false;
        }
        eIn += x * x;
        eOut += y * y;
        eDiff += (y - x) * (y - x);
        pIn = std::max(pIn, std::abs(x));
        pOut = std::max(pOut, std::abs(y));
    }
    m.outDb = 10.0 * std::log10(std::max(eOut, 1e-30) / std::max(eIn, 1e-30));
    m.diffDb = 10.0 * std::log10(std::max(eDiff, 1e-30) / std::max(eIn, 1e-30));
    m.peakDb = 20.0 * std::log10(std::max(pOut, 1e-15) / std::max(pIn, 1e-15));
    const double inRms = std::sqrt(eIn / static_cast<double>(b - a));
    const size_t win = 441;
    size_t moved = 0;
    size_t windows = 0;
    for (size_t s = a; s + win <= b; s += win) {
        double e = 0.0;
        for (size_t i = s; i < s + win; ++i) {
            const double d = static_cast<double>(out[i]) - in[i];
            e += d * d;
        }
        ++windows;
        if (std::sqrt(e / static_cast<double>(win)) > 0.1 * inRms) {
            ++moved;
        }
    }
    m.moved = windows ? static_cast<double>(moved) / static_cast<double>(windows) : 0.0;
    return m;
}

[[nodiscard]] int32_t holdOf(const std::vector<char>& state) {
    // Hold is the third int32 from the end (Hold, Capture, Dry: the last fields of the stream).
    int32_t hold = 0;
    std::memcpy(&hold, state.data() + state.size() - 12, sizeof(hold));
    return hold;
}

}  // namespace

TEST_CASE("PROBE Freeze factory presets", "[.][probe][freeze-presets]") {
    const char* env = std::getenv("ITERUM_FREEZE_PRESET_DIR");
    const std::filesystem::path dir = env ? std::filesystem::path(env)
                                          : std::filesystem::path(ITERUM_RESOURCES_DIR) / "presets" / "Freeze";
    REQUIRE(std::filesystem::is_directory(dir));
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.path().extension() == ".vstpreset") {
            files.push_back(e.path());
        }
    }
    std::sort(files.begin(), files.end());
    REQUIRE_FALSE(files.empty());

    const std::vector<float> inputs[] = {toneInput(), drumInput(), chordInput()};
    const char* names[] = {"tone ", "drum ", "chord"};
    for (const auto& file : files) {
        const auto state = componentChunk(file);
        const bool hold = holdOf(state) != 0;
        double bestDiff = -300.0;
        for (size_t k = 0; k < 3; ++k) {
            const auto out = render(state, inputs[k]);
            const auto m = measure(inputs[k], out);
            std::printf("%-22s %s | out %+6.1f dB | diff %+6.1f dB | moved %3.0f%% | peak %+5.1f dB",
                        file.stem().string().c_str(), names[k], m.outDb, m.diffDb, 100.0 * m.moved, m.peakDb);
            if (k == 0) {
                const auto a = static_cast<size_t>(kSr * 5.0);
                const auto b = static_cast<size_t>(kSr * 6.0);
                const double p233 = tonePower(out, a, b, 233.0);
                const double p311 = tonePower(out, a, b, 311.0);
                const double p415 = tonePower(out, a, b, 415.0);
                const double top = std::max({p233, p311, p415});
                const char* held = "415 (live)";
                if (top == p233) {
                    held = "233 (first bar)";
                } else if (top == p311) {
                    held = "311 (bar before)";
                }
                std::printf(" | held %s%s", held, hold ? " [Hold on]" : "");
                if (hold) {
                    CAPTURE(file.stem().string(), p233, p311, p415);
                    // Against the bar before, not the live input: under Dry = Mix the dry 415 Hz stays in the output.
                    CHECK(p233 > 10.0 * p311);
                }
            }
            std::printf("\n");
            CAPTURE(file.stem().string(), names[k]);
            CHECK(m.finite);
            CHECK(m.peakDb <= 6.0);
            bestDiff = std::max(bestDiff, m.diffDb);
        }
        CAPTURE(file.stem().string(), bestDiff);
        CHECK(bestDiff >= -20.0);
    }
}
