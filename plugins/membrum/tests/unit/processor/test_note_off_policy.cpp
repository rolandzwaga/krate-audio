// ==============================================================================
// Note-off policy for percussion (fix plan T2.9: MB-264, FR-114)
// ==============================================================================
// FR-114 (user ruling 2026-11-25, option (a)): a MIDI note-off releases only a
// sustaining exciter (Friction, Feedback). The amp envelope, the tone shaper
// and the body decay naturally, so a short MIDI note plays the full hit. The
// voice still retires through its own auto-release (T1.13) once it is quiet.
//
// Pinned by a render comparison through the real Processor: for the default
// kit's kick 36 (Impulse), floor tom 41 (Mallet) and crash 49, a note-on with a
// note-off (one block later, at 125 ms, or in the same block) must render the
// same 2 s as the note-on alone, to within -120 dB.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "vst_event_list.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr double kSr = 48000.0;
constexpr long kRenderSamples = static_cast<long>(2.0 * kSr);

struct NoteOffAt
{
    long sample = 0;  // absolute sample of the note-off
};

// Default kit, one note-on at sample 0 (velocity 100/127), an optional MIDI
// note-off at `off`, 2 s at host block `block`. Returns L and R interleaved as
// two halves (L then R).
std::vector<float> renderNote(Steinberg::int16 pitch, int block, std::optional<NoteOffAt> off)
{
    auto processor = std::make_unique<Membrum::Processor>();
    processor->initialize(nullptr);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = block;
    setup.sampleRate = kSr;
    processor->setupProcessing(setup);
    processor->setActive(true);

    std::vector<float> l(static_cast<std::size_t>(block), 0.0f);
    std::vector<float> r(static_cast<std::size_t>(block), 0.0f);
    float* channels[2] = {l.data(), r.data()};
    AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.channelBuffers32 = channels;

    Krate::Test::EventList events;
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numOutputs = 1;
    data.outputs = &bus;
    data.inputEvents = &events;

    std::vector<float> out(static_cast<std::size_t>(2 * kRenderSamples), 0.0f);
    long rendered = 0;
    while (rendered < kRenderSamples)
    {
        const int n = static_cast<int>(std::min<long>(block, kRenderSamples - rendered));
        events.clear();
        if (rendered == 0)
            events.addNoteOn(pitch, 100.0f / 127.0f, 0);
        if (off && off->sample >= rendered && off->sample < rendered + n)
            events.addNoteOff(pitch, static_cast<Steinberg::int32>(off->sample - rendered));
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        data.numSamples = n;
        processor->process(data);
        std::copy(l.begin(), l.begin() + n, out.begin() + rendered);
        std::copy(r.begin(), r.begin() + n, out.begin() + kRenderSamples + rendered);
        rendered += n;
    }

    processor->setActive(false);
    processor->terminate();
    return out;
}

double rmsOf(const std::vector<float>& x)
{
    double sum = 0.0;
    for (const float v : x)
        sum += static_cast<double>(v) * static_cast<double>(v);
    return std::sqrt(sum / static_cast<double>(x.size()));
}

// RMS of (b - a) in dB relative to the RMS of a (-300 dB for an exact match).
double diffDb(const std::vector<float>& a, const std::vector<float>& b)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const double d = static_cast<double>(b[i]) - static_cast<double>(a[i]);
        sum += d * d;
    }
    const double diffRms = std::sqrt(sum / static_cast<double>(a.size()));
    const double refRms = rmsOf(a);
    if (diffRms <= 0.0)
        return -300.0;
    return 20.0 * std::log10(diffRms / std::max(refRms, 1.0e-30));
}

} // namespace

TEST_CASE("Note-off policy (FR-114): a short MIDI note plays the full hit",
          "[processor][note_off][fr114]")
{
    // Kick 36 (Impulse), floor tom 41 (Mallet), crash 49 (default kit).
    for (const Steinberg::int16 pitch : {static_cast<Steinberg::int16>(36),
                                         static_cast<Steinberg::int16>(41),
                                         static_cast<Steinberg::int16>(49)})
    {
        SECTION("note-off after one block and at 125 ms, block 256, note " + std::to_string(pitch))
        {
            constexpr int kBlock = 256;
            const auto a = renderNote(pitch, kBlock, std::nullopt);
            const auto aAgain = renderNote(pitch, kBlock, std::nullopt);
            const auto b = renderNote(pitch, kBlock, NoteOffAt{kBlock});
            const auto c = renderNote(pitch, kBlock, NoteOffAt{static_cast<long>(0.125 * kSr)});

            const double rmsA = rmsOf(a);
            const double dB = diffDb(a, b);
            const double dC = diffDb(a, c);
            INFO("note " << pitch << ": RMS of the note-on-only render " << rmsA
                         << "; difference re it: note-off after one block " << dB
                         << " dB, at 125 ms " << dC << " dB");
            // Non-vacuity: the hit sounds, and two identical renders match.
            REQUIRE(rmsA > 1.0e-4);
            REQUIRE(diffDb(a, aAgain) <= -120.0);
            CHECK(dB <= -120.0);
            CHECK(dC <= -120.0);
        }
        SECTION("note-on and note-off in the same block, note " + std::to_string(pitch))
        {
            for (const int block : {256, 1024})
            {
                const auto a = renderNote(pitch, block, std::nullopt);
                const auto s = renderNote(pitch, block, NoteOffAt{10});
                const double rmsA = rmsOf(a);
                const double dS = diffDb(a, s);
                INFO("note " << pitch << ", block " << block << ": RMS of the note-on-only render "
                             << rmsA << "; note-off 10 samples after the note-on: " << dS << " dB");
                REQUIRE(rmsA > 1.0e-4);
                CHECK(dS <= -120.0);
            }
        }
    }
}
