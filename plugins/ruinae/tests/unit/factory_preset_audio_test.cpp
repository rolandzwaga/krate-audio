// ==============================================================================
// Ruinae Factory Preset Audibility Test
// ==============================================================================
// Every factory preset must make a sound when a note is played. A preset that
// renders silence is indistinguishable from a broken plugin to the user, and the
// existing factory-preset coverage (byte-identical MIDI, state round-trip) only
// looks at arp presets and at state bytes -- never at the audio.
//
// Each preset is loaded into a real processor, sent a single held note, and
// rendered long enough to clear the slowest factory amp-envelope attack. The
// rendered peak must be audible.
// ==============================================================================

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"
#include "parameters/dropdown_mappings.h"
#include "vst_param_changes.h"

#include <krate/dsp/core/db_utils.h>

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int32 kBlockSize = 512;

// The slowest factory amp attack is 2000 ms and some pads layer a slow filter
// sweep on top, so render well past onset before judging a preset silent.
constexpr double kRenderSeconds = 6.0;

// -60 dBFS. Comfortably above denormal/noise-floor dither, far below any level
// a preset could use intentionally.
constexpr float kAudibleThreshold = 0.001f;

class TestEventList : public IEventList {
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events_.size()); }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override {
        if (index < 0 || index >= static_cast<int32>(events_.size())) return kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return kResultTrue;
    }
    tresult PLUGIN_API addEvent(Event& e) override { events_.push_back(e); return kResultTrue; }
    void addNoteOn(int16 pitch, float vel, int32 sampleOffset) {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = vel;
        e.noteOn.noteId = -1;
        events_.push_back(e);
    }
    void addNoteOff(int16 pitch, int32 sampleOffset) {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.sampleOffset = sampleOffset;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = -1;
        events_.push_back(e);
    }
    void clear() { events_.clear(); }
private:
    std::vector<Event> events_;
};

bool loadPresetFileIntoProcessor(const std::filesystem::path& presetPath,
                                 Ruinae::Processor& processor) {
    auto* stream = FileStream::open(presetPath.string().c_str(), "rb");
    if (!stream) return false;

    PresetFile presetFile(stream);
    bool ok = presetFile.readChunkList() && presetFile.seekToComponentState();
    if (!ok) { stream->release(); return false; }

    const auto* entry = presetFile.getEntry(kComponentState);
    if (!entry) { stream->release(); return false; }

    auto componentStream = owned(new ReadOnlyBStream(stream, entry->offset, entry->size));
    auto setResult = processor.setState(componentStream);
    stream->release();

    return setResult == kResultTrue;
}

struct PresetLevels {
    float peak{0.0f};          ///< Peak absolute sample over the whole render.
    float sustainedRms{0.0f};  ///< RMS once the slowest attacks have opened.
    bool loaded{false};
};

/// @brief A fresh processor with one factory preset loaded, holding C4.
class HeldNoteRender {
public:
    /// Initialize, activate, load the preset and drain its deferred snapshot.
    bool load(const std::filesystem::path& presetPath) {
        activate();
        if (!loadPresetFileIntoProcessor(presetPath, *proc_)) return false;
        return finishLoad();
    }

    /// As load(), from a component state saved by savedState().
    bool loadState(const std::vector<char>& bytes) {
        activate();
        MemoryStream in(const_cast<char*>(bytes.data()), static_cast<TSize>(bytes.size()));
        if (proc_->setState(&in) != kResultTrue) return false;
        return finishLoad();
    }

    /// The processor's component state (getState).
    [[nodiscard]] std::vector<char> savedState() {
        MemoryStream out;
        proc_->getState(&out);
        return std::vector<char>(out.getData(), out.getData() + out.getSize());
    }

    void activate() {
        proc_->initialize(nullptr);

        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;
        proc_->setupProcessing(setup);

        // Ruinae defers preset application through RTTransferT; activate first.
        proc_->setActive(static_cast<TBool>(true));
        // Set before loading so the destructor tears down a half-loaded render too.
        loaded_ = true;
    }

    bool finishLoad() {
        // Drain the deferred preset snapshot.
        ProcessData drainData{};
        drainData.numSamples = 0;
        proc_->process(drainData);

        outputBus_.numChannels = 2;
        outputBus_.channelBuffers32 = outChannels_;

        processContext_.state = ProcessContext::kPlaying
                              | ProcessContext::kTempoValid
                              | ProcessContext::kProjectTimeMusicValid;
        processContext_.sampleRate = kSampleRate;
        processContext_.tempo = 120.0;

        // C4, moderately hard, so velocity-sensitive presets open up.
        inEvents_.addNoteOn(60, 0.8f, 0);
        return true;
    }

    /// Queue the note-off for C4 in the next rendered block.
    void releaseNote() { inEvents_.addNoteOff(60, 0); }

    /// Queue a normalized parameter change for the next rendered block.
    void setParam(ParamID id, double normalized) { params_.addChange(id, normalized); }

    /// Render the next block into left() / right().
    void renderBlock() {
        std::fill(outL_.begin(), outL_.end(), 0.0f);
        std::fill(outR_.begin(), outR_.end(), 0.0f);

        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kBlockSize;
        data.numInputs = 0;
        data.numOutputs = 1;
        data.outputs = &outputBus_;
        data.inputEvents = &inEvents_;
        data.outputEvents = &outEvents_;
        data.inputParameterChanges = &params_;
        data.processContext = &processContext_;

        proc_->process(data);

        // The note-on is only delivered in the first block; hold it after that.
        inEvents_.clear();
        outEvents_.clear();
        params_.clear();

        processContext_.projectTimeSamples += kBlockSize;
        processContext_.projectTimeMusic +=
            static_cast<double>(kBlockSize) / kSampleRate
            * (processContext_.tempo / 60.0);
    }

    [[nodiscard]] const std::vector<float>& left() const { return outL_; }
    [[nodiscard]] const std::vector<float>& right() const { return outR_; }

    ~HeldNoteRender() {
        if (loaded_) {
            proc_->setActive(static_cast<TBool>(false));
            proc_->terminate();
        }
    }

    HeldNoteRender() = default;
    HeldNoteRender(const HeldNoteRender&) = delete;
    HeldNoteRender& operator=(const HeldNoteRender&) = delete;
    HeldNoteRender(HeldNoteRender&&) = delete;
    HeldNoteRender& operator=(HeldNoteRender&&) = delete;

private:
    std::unique_ptr<Ruinae::Processor> proc_ = std::make_unique<Ruinae::Processor>();
    std::vector<float> outL_ = std::vector<float>(static_cast<size_t>(kBlockSize), 0.0f);
    std::vector<float> outR_ = std::vector<float>(static_cast<size_t>(kBlockSize), 0.0f);
    float* outChannels_[2] = { outL_.data(), outR_.data() };
    AudioBusBuffers outputBus_{};
    TestEventList inEvents_;
    TestEventList outEvents_;
    Krate::Test::ParameterChanges params_;
    ProcessContext processContext_{};
    bool loaded_{false};
};

/// @brief Load a preset, hold one note, and measure its output levels.
PresetLevels renderPresetLevels(const std::filesystem::path& presetPath) {
    PresetLevels levels{};

    HeldNoteRender render;
    if (!render.load(presetPath)) return levels;
    levels.loaded = true;

    const int numBlocks =
        static_cast<int>((kRenderSeconds * kSampleRate) / kBlockSize);

    // Measure sustained level only after the slowest factory attack (2000 ms)
    // plus filter-sweep settling, so a slow swell is not mistaken for a quiet
    // preset.
    const int sustainStartBlock =
        static_cast<int>((3.5 * kSampleRate) / kBlockSize);

    double sumSquares = 0.0;
    size_t sustainSamples = 0;

    float peak = 0.0f;
    for (int block = 0; block < numBlocks; ++block) {
        render.renderBlock();

        for (int32 i = 0; i < kBlockSize; ++i) {
            const float l = render.left()[static_cast<size_t>(i)];
            const float r = render.right()[static_cast<size_t>(i)];
            peak = std::max(peak, std::abs(l));
            peak = std::max(peak, std::abs(r));

            if (block >= sustainStartBlock) {
                sumSquares += static_cast<double>(l) * l
                            + static_cast<double>(r) * r;
                sustainSamples += 2;
            }
        }
    }

    levels.peak = peak;
    levels.sustainedRms = (sustainSamples > 0)
        ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(sustainSamples)))
        : 0.0f;
    return levels;
}

/// @brief What a preset does over a held note, its release and its tail (R-125).
struct ReleaseLevels {
    bool loaded{false};
    float peak{0.0f};
    double fractionAbove085{0.0};  ///< Share of samples above the limiter knee.
    double sustainMean{0.0};       ///< Mean over the sustain window (DC).
    float sustainedRms{0.0f};      ///< RMS over the sustain window.
    float tailRms{0.0f};           ///< RMS of the last second, 10 s after release.
    size_t nonFinite{0};
};

constexpr double kNoteOffSeconds = 3.0;
constexpr double kTailSeconds = 10.0;
// The slowest factory amp attack is 2000 ms; judge the held level after it.
constexpr double kSustainStartSeconds = 2.0;

/// A normalized parameter value applied from the first block on top of the preset.
struct ParamOverride {
    ParamID id;
    double normalized;
};

/// @param overrides  Parameters changed from the first block, so a preset is heard
///                   with one setting removed (the latch disengaged, the delay's
///                   feedback at 0) to show what keeps it sounding.
ReleaseLevels renderReleaseLevels(const std::filesystem::path& presetPath,
                                  const std::vector<ParamOverride>& overrides = {}) {
    ReleaseLevels out{};
    HeldNoteRender render;
    if (!render.load(presetPath)) return out;
    out.loaded = true;
    for (const auto& o : overrides) render.setParam(o.id, o.normalized);

    const int noteOffBlock = static_cast<int>((kNoteOffSeconds * kSampleRate) / kBlockSize);
    const int numBlocks =
        static_cast<int>(((kNoteOffSeconds + kTailSeconds) * kSampleRate) / kBlockSize);
    const int sustainStartBlock =
        static_cast<int>((kSustainStartSeconds * kSampleRate) / kBlockSize);
    const int tailStartBlock = numBlocks - static_cast<int>(kSampleRate / kBlockSize);

    double sustainSum = 0.0, sustainSq = 0.0, tailSq = 0.0;
    size_t sustainN = 0, tailN = 0, total = 0, above = 0;
    for (int block = 0; block < numBlocks; ++block) {
        if (block == noteOffBlock) render.releaseNote();
        render.renderBlock();
        for (size_t i = 0; i < static_cast<size_t>(kBlockSize); ++i) {
            for (const float x : {render.left()[i], render.right()[i]}) {
                ++total;
                if (!Krate::DSP::detail::isFinite(x)) { ++out.nonFinite; continue; }
                const float a = std::abs(x);
                out.peak = std::max(out.peak, a);
                if (a > 0.85f) ++above;
                if (block >= sustainStartBlock && block < noteOffBlock) {
                    sustainSum += x;
                    sustainSq += static_cast<double>(x) * x;
                    ++sustainN;
                }
                if (block >= tailStartBlock) {
                    tailSq += static_cast<double>(x) * x;
                    ++tailN;
                }
            }
        }
    }
    out.fractionAbove085 = total > 0 ? static_cast<double>(above) / static_cast<double>(total) : 0.0;
    if (sustainN > 0) {
        out.sustainMean = sustainSum / static_cast<double>(sustainN);
        out.sustainedRms = static_cast<float>(std::sqrt(sustainSq / static_cast<double>(sustainN)));
    }
    if (tailN > 0) out.tailRms = static_cast<float>(std::sqrt(tailSq / static_cast<double>(tailN)));
    return out;
}

float toDbfs(float linear) {
    return (linear > 0.0f) ? 20.0f * std::log10(linear) : -200.0f;
}

std::vector<std::filesystem::path> enumerateAllPresets() {
    const std::filesystem::path presetRoot{
        std::filesystem::path(__FILE__).parent_path().parent_path()
            .parent_path() / "resources" / "presets"};
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(presetRoot)) return result;

    for (const auto& subdir : std::filesystem::directory_iterator(presetRoot)) {
        if (!subdir.is_directory()) continue;
        for (const auto& file :
             std::filesystem::directory_iterator(subdir.path()))
        {
            if (!file.is_regular_file()) continue;
            if (file.path().extension() == ".vstpreset") {
                result.push_back(file.path());
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace

TEST_CASE("Every Ruinae factory preset produces audible output for a held note",
          "[processor][presets][audio][regression]")
{
    auto presets = enumerateAllPresets();
    REQUIRE_FALSE(presets.empty());

    for (const auto& p : presets) {
        DYNAMIC_SECTION("preset " << p.stem().string()) {
            const auto levels = renderPresetLevels(p);
            INFO("preset: " << p.stem().string());
            INFO("peak: " << levels.peak << " (" << toDbfs(levels.peak) << " dBFS)");
            REQUIRE(levels.loaded);
            REQUIRE(levels.peak > kAudibleThreshold);
        }
    }
}

// R-125: hold C4 for 3 s, release it, render 10 s more. Every preset must stay
// finite, keep its level under the limiter knee, carry no DC and run down to
// silence. The bars never change.
//
// Known failures (user ruling 2026-11-22): one entry per (preset, check) that fails
// today, with the value measured when the entry was written and the task that removes
// it. A listed check that still fails is skipped; a listed check that PASSES fails the
// test as stale, so the list only shrinks. Since T5.3d it is empty: presets meant to
// ring on live in kRingOn below, each with a numeric reason.
enum class Check { Peak, Above085, Mean, Tail };

struct KnownFailure {
    const char* preset;
    Check check;
    double measured;   ///< peak, share above 0.85, |mean|, or tail dBFS
    const char* owner;
};

const std::array<KnownFailure, 0> kKnownFailures{};

const KnownFailure* findKnownFailure(const std::string& stem, Check check) {
    for (const auto& k : kKnownFailures)
        if (stem == k.preset && k.check == check) return &k;
    return nullptr;
}

// Ring-on list: presets meant to keep sounding after the key is released. For these
// the tail bar is inverted (a ring-on preset that falls silent is a stale entry) and
// the reason is checked by removing the cause:
// - Latch: with Latch Off the preset falls below the bar.
// - DelayFeedback: the delay's recirculation alone holds the tail past 10 s. With the
//   tape noise off (Wear 0, Age 0, Splice off) it still fails the bar; with Feedback 0
//   as well it passes.
// - TapeNoise: the tape delay adds hiss (Wear/Age) and splice clicks after its loop,
//   input or not, which the R-154 ruling keeps above Wear 0 / Age 0. With Wear 0, Age 0
//   and Splice off the preset passes.
// Tape delay decay (T5.3d): its three heads sit at d, 1.5d and 2d and each feeds back
// the Feedback value g, so the loop falls by -20 log10(y) dB per d, where y solves
// g (y + y^1.5 + y^2) = 1. A synced delay at 120 BPM: 1/8 = 250 ms, 1/8D = 375 ms,
// 1/4D = 750 ms. Tails are the RMS of the last second, 9-10 s after release.
enum class RingCause { Latch, DelayFeedback, TapeNoise };

struct RingOn {
    const char* preset;
    RingCause cause;
    const char* reason;
};

const RingOn kRingOn[] = {
    {"Pulse_Climb", RingCause::Latch,
     "arp Latch = Hold (generator: 'LATCHED so the climb sustains hands-free'): the 1/16 "
     "up-arp keeps playing the released chord; tail -16.8 dBFS latched, digital silence "
     "with Latch Off (T5.3a)"},
    {"Fill_Cascade", RingCause::Latch,
     "arp Latch = Add (generator: 'held notes ACCUMULATE into the cascade'): the 1/16 "
     "up-arp keeps playing every note ever pressed; tail -21.0 dBFS latched, -217.8 "
     "dBFS with Latch Off (T5.3a)"},
    {"Dub_Chamber", RingCause::DelayFeedback,
     "Tape 1/4D (750 ms), Feedback 0.21 on 3 heads: loop gain 0.739, -2.63 dB per pass "
     "= -3.5 dB/s (measured -3.3 dB/s with LFO1 off); LFO1 (0.1 Hz) -> Effect Mix swells "
     "the wet back up from 6 s, so the last second reads -44.8 dBFS; -177 with Feedback 0 "
     "(T5.3d)"},
    {"Chaos_Wind", RingCause::DelayFeedback,
     "Tape synced 1/8 (250 ms; Sync stays on, so Time 750 ms is unused), Feedback 0.29 on "
     "3 heads: loop gain 0.912, -0.80 dB per pass = -3.2 dB/s (measured -3.3 dB/s with the "
     "Hall off), into a size 0.85 Hall: last second -51.1 dBFS; -350 with Feedback 0 (T5.3d)"},
    {"Echo_Bass", RingCause::DelayFeedback,
     "Tape 1/8D (375 ms), Feedback 0.28 on 3 heads: loop gain 0.891, -1.00 dB per pass = "
     "-2.7 dB/s (measured -2.7 dB/s): -28.4 dBFS 1 s after release reaches -80 only after "
     "~20 s; last second -54.5; -200 with Feedback 0 (T5.3d)"},
    {"Dreamscape", RingCause::DelayFeedback,
     "Tape 400 ms, Feedback 0.28 on 3 heads: loop gain 0.891, -1.00 dB per pass = -2.5 dB/s "
     "(measured -2.5 dB/s), into a size 0.8 Plate: last second -65.6 dBFS; -121 with "
     "Feedback 0 (T5.3d)"},
    {"Tape_Shuffle", RingCause::DelayFeedback,
     "Tape 1/8D (375 ms), Feedback 0.22 on 3 heads: loop gain 0.761, -2.37 dB per pass = "
     "-6.3 dB/s (measured -6.3 dB/s): -22.7 dBFS 1 s after release reaches -80 at ~10.1 s "
     "(last second -78.0 with the tape noise off); Wear 0.5 / Age 0.4 hiss (-56 dB) holds "
     "it at -73.7; -176 with Feedback 0 (T5.3d)"},
    {"Spectral_Drift", RingCause::DelayFeedback,
     "Spectral delay 600 ms + Spread 400 ms High-to-Low, Feedback 0.4 with Tilt -0.3: the "
     "C4/C5 bins feed back 0.52 every ~995 ms = -5.7 dB/s (measured -6.0 dB/s): last second "
     "-75.6 dBFS; -200 with Feedback 0. Reverb Freeze is on but holds nothing (Freeze Off "
     "reads the same -75.6) (T5.3d)"},
    {"Lorenz_Choir", RingCause::DelayFeedback,
     "Tape 1/8D (375 ms), Feedback 0.23 on 3 heads: loop gain 0.783, -2.12 dB per pass = "
     "-5.7 dB/s (measured -5.7 dB/s): last second -76.3 dBFS (-77.6 with the tape noise "
     "off); -143 with Feedback 0 (T5.3d)"},
    {"Foldbeast", RingCause::DelayFeedback,
     "Tape 1/8 (250 ms), Feedback 0.25 on 3 heads: loop gain 0.827, -1.65 dB per pass = "
     "-6.6 dB/s (measured -6.5 dB/s): -21.2 dBFS 1 s after release reaches -80 at ~10 s; "
     "last second -78.8; -265 with Feedback 0 (T5.3d)"},
    {"FM_Grunge", RingCause::TapeNoise,
     "Splice on (intensity 0.5) and Wear 0.4 hiss (-64 dB) after the tape loop: with "
     "Feedback 0 the output holds -74.6 dBFS (-84.4 with Splice off); the echoes (synced "
     "1/8 = 250 ms, Feedback 0.23 on 3 heads = -8.5 dB/s) fall to -107 with the tape noise "
     "off (T5.3d)"},
    {"Degauss_Drift", RingCause::TapeNoise,
     "Wear 0.5 / Age 0.3 hiss (-57 dB, Master Gain 2.0) after the tape loop: with Feedback "
     "0 the output holds -74.5 dBFS; the echoes (synced 1/8 = 250 ms, Feedback 0.25 on 3 "
     "heads = -6.6 dB/s) fall to -88.9 with the tape noise off (T5.3d)"},
    {"Chaos_Machine", RingCause::TapeNoise,
     "Splice on (intensity 0.4) and Wear 0.3 / Age 0.4 hiss (-64 dB) after the tape loop: "
     "with Feedback 0 the output holds -79.9 dBFS; the echoes (synced 1/8 = 250 ms, Feedback "
     "0.25 on 3 heads = -6.6 dB/s) fall to -98.5 with the tape noise off (T5.3d)"},
};

const RingOn* findRingOn(const std::string& stem) {
    for (const auto& r : kRingOn)
        if (stem == r.preset) return &r;
    return nullptr;
}

/// Apply one bar: an unlisted check must pass, a listed one must still fail.
void applyBar(const std::string& stem, Check check, const char* name, bool passes, double value) {
    const KnownFailure* known = findKnownFailure(stem, check);
    INFO(stem << " " << name << " = " << value);
    if (known == nullptr) {
        CHECK(passes);
    } else {
        INFO("known failure owned by " << known->owner << ", measured " << known->measured
             << ": it passes now, so the entry is stale; delete it");
        CHECK_FALSE(passes);
    }
}

TEST_CASE("Every Ruinae factory preset releases cleanly and stays within level and DC bounds",
          "[processor][presets][audio][release][long]")
{
    const auto presets = enumerateAllPresets();
    REQUIRE_FALSE(presets.empty());

    // A known-failure entry whose preset no longer exists is stale too.
    for (const auto& k : kKnownFailures) {
        const bool exists = std::any_of(presets.begin(), presets.end(),
            [&](const auto& p) { return p.stem().string() == k.preset; });
        INFO("known failure for missing preset " << k.preset);
        CHECK(exists);
    }
    for (const auto& r : kRingOn) {
        const bool exists = std::any_of(presets.begin(), presets.end(),
            [&](const auto& p) { return p.stem().string() == r.preset; });
        INFO("ring-on entry for missing preset " << r.preset);
        CHECK(exists);
        INFO("ring-on preset " << r.preset << " also carries a known tail failure");
        CHECK(findKnownFailure(r.preset, Check::Tail) == nullptr);
    }

    for (const auto& p : presets) {
        const std::string stem = p.stem().string();
        DYNAMIC_SECTION("preset " << stem) {
            const auto l = renderReleaseLevels(p);
            REQUIRE(l.loaded);
            INFO("preset: " << stem << " peak " << l.peak << " above0.85 " << l.fractionAbove085
                 << " mean " << l.sustainMean << " sustainRms " << l.sustainedRms
                 << " (" << toDbfs(l.sustainedRms) << " dBFS) tailRms " << l.tailRms
                 << " (" << toDbfs(l.tailRms) << " dBFS) nonFinite " << l.nonFinite);
            CHECK(l.nonFinite == size_t{0});
            CHECK(l.sustainedRms > kAudibleThreshold);
            applyBar(stem, Check::Peak, "peak", l.peak < 0.99f, l.peak);
            applyBar(stem, Check::Above085, "share above 0.85", l.fractionAbove085 < 0.01,
                     l.fractionAbove085);
            applyBar(stem, Check::Mean, "|sustain mean|", std::abs(l.sustainMean) < 0.01,
                     std::abs(l.sustainMean));
            const RingOn* ring = findRingOn(stem);
            if (ring == nullptr) {
                applyBar(stem, Check::Tail, "tail dBFS", toDbfs(l.tailRms) < -80.0f,
                         toDbfs(l.tailRms));
            } else {
                INFO("ring-on entry: " << ring->reason);
                INFO("tail " << toDbfs(l.tailRms)
                     << " dBFS: a ring-on preset that falls silent is a stale entry");
                CHECK_FALSE(toDbfs(l.tailRms) < -80.0f);
                // Tape noise off: Wear 0, Age 0, Splice off.
                const std::vector<ParamOverride> noiseOff = {
                    {Ruinae::kDelayTapeWearId, 0.0}, {Ruinae::kDelayTapeAgeId, 0.0},
                    {Ruinae::kDelayTapeSpliceEnabledId, 0.0}};
                if (ring->cause == RingCause::Latch) {
                    // list index 0 = Off
                    const auto unlatched = renderReleaseLevels(p, {{Ruinae::kArpLatchModeId, 0.0}});
                    REQUIRE(unlatched.loaded);
                    INFO("with Latch Off: tail " << toDbfs(unlatched.tailRms) << " dBFS");
                    CHECK(unlatched.nonFinite == size_t{0});
                    CHECK(unlatched.sustainedRms > kAudibleThreshold);
                    CHECK(toDbfs(unlatched.tailRms) < -80.0f);
                } else if (ring->cause == RingCause::DelayFeedback) {
                    const auto quiet = renderReleaseLevels(p, noiseOff);
                    auto dryOverrides = noiseOff;
                    dryOverrides.push_back({Ruinae::kDelayFeedbackId, 0.0});
                    const auto dry = renderReleaseLevels(p, dryOverrides);
                    REQUIRE(quiet.loaded);
                    REQUIRE(dry.loaded);
                    INFO("tape noise off: tail " << toDbfs(quiet.tailRms)
                         << " dBFS; and Feedback 0: tail " << toDbfs(dry.tailRms) << " dBFS");
                    CHECK(dry.nonFinite == size_t{0});
                    CHECK(dry.sustainedRms > kAudibleThreshold);
                    CHECK_FALSE(toDbfs(quiet.tailRms) < -80.0f);
                    CHECK(toDbfs(dry.tailRms) < -80.0f);
                } else {
                    const auto quiet = renderReleaseLevels(p, noiseOff);
                    REQUIRE(quiet.loaded);
                    INFO("tape noise off: tail " << toDbfs(quiet.tailRms) << " dBFS");
                    CHECK(quiet.nonFinite == size_t{0});
                    CHECK(quiet.sustainedRms > kAudibleThreshold);
                    CHECK(toDbfs(quiet.tailRms) < -80.0f);
                }
            }
        }
    }
}

// R-076: setState rejects a stream cut inside the required prefix. Every
// factory preset is a complete stream, so every one must still load.
TEST_CASE("Every Ruinae factory preset passes Processor::setState",
          "[processor][presets][state]")
{
    const auto presets = enumerateAllPresets();
    REQUIRE_FALSE(presets.empty());

    Ruinae::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    for (const auto& p : presets) {
        INFO("preset: " << p.stem().string());
        CHECK(loadPresetFileIntoProcessor(p, processor));
    }
    processor.terminate();
}

// R-211: the loaders clamp what a damaged state holds, and nothing else. Every
// factory preset holds in-range values only, so getState after setState gives
// back the preset's component state byte for byte.
TEST_CASE("Every Ruinae factory preset state round-trips byte-identically",
          "[processor][presets][state]")
{
    const auto presets = enumerateAllPresets();
    REQUIRE_FALSE(presets.empty());

    Ruinae::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    for (const auto& p : presets) {
        INFO("preset: " << p.stem().string());
        auto* file = FileStream::open(p.string().c_str(), "rb");
        REQUIRE(file != nullptr);
        PresetFile presetFile(file);
        const bool found = presetFile.readChunkList() && presetFile.seekToComponentState();
        const auto* entry = presetFile.getEntry(kComponentState);
        std::vector<char> bytes(entry != nullptr ? static_cast<size_t>(entry->size) : 0);
        int32 read = 0;
        const bool readOk = found && entry != nullptr &&
                            file->read(bytes.data(), static_cast<int32>(bytes.size()), &read) == kResultOk &&
                            read == static_cast<int32>(bytes.size());
        file->release();
        REQUIRE(readOk);

        MemoryStream in(bytes.data(), static_cast<TSize>(bytes.size()));
        REQUIRE(processor.setState(&in) == kResultTrue);
        MemoryStream out;
        REQUIRE(processor.getState(&out) == kResultTrue);
        const std::vector<char> saved(out.getData(), out.getData() + out.getSize());
        size_t firstDiff = 0;
        while (firstDiff < std::min(saved.size(), bytes.size()) && saved[firstDiff] == bytes[firstDiff]) ++firstDiff;
        INFO("first differing byte " << firstDiff << " of " << bytes.size());
        CHECK(saved == bytes);
    }
    processor.terminate();
}

// =============================================================================
// R-161: the spectral delay renders deterministically
// =============================================================================
// SpectralDelay seeded its stereo-phase RNG from its own address, so the same
// preset bounced twice differed (Spectral_Wash: 0.1451 peak between two fresh
// loads). Both processors stay alive for the whole render, so their delays sit
// at different addresses.

TEST_CASE("Spectral_Wash renders identically from two fresh processors",
          "[presets][determinism]")
{
    const auto presets = enumerateAllPresets();
    const auto it = std::find_if(presets.begin(), presets.end(), [](const auto& p) {
        return p.stem().string() == "Spectral_Wash";
    });
    REQUIRE(it != presets.end());

    HeldNoteRender first;
    HeldNoteRender second;
    REQUIRE(first.load(*it));
    REQUIRE(second.load(*it));

    const int numBlocks =
        static_cast<int>((kRenderSeconds * kSampleRate) / kBlockSize);
    float maxDiff = 0.0f;
    float peak = 0.0f;
    for (int block = 0; block < numBlocks; ++block) {
        first.renderBlock();
        second.renderBlock();
        for (size_t i = 0; i < static_cast<size_t>(kBlockSize); ++i) {
            maxDiff = std::max(maxDiff, std::abs(first.left()[i] - second.left()[i]));
            maxDiff = std::max(maxDiff, std::abs(first.right()[i] - second.right()[i]));
            peak = std::max(peak, std::abs(first.left()[i]));
        }
    }

    INFO("peak " << peak << ", max sample difference " << maxDiff);
    REQUIRE(peak > kAudibleThreshold);  // not vacuous: the preset sounds
    REQUIRE(maxDiff <= 1e-7f);
}

TEST_CASE("No Ruinae factory preset is drastically quieter than the bank",
          "[processor][presets][audio][loudness][long]")
{
    // Passing the audibility test is not enough: a preset that renders far below
    // its neighbours reads as broken when a user auditions the bank, which is
    // exactly how the Xenoform pad was reported.
    //
    // This compares peak, not sustained RMS. Sustained RMS punishes presets that
    // are percussive by design -- a bell or a ping has decayed to near nothing by
    // the time it is measured, which says nothing about whether it is too quiet.
    // Peak level is the property that actually distinguishes a broken-sounding
    // preset from a short one.
    auto presets = enumerateAllPresets();
    REQUIRE_FALSE(presets.empty());

    struct Measured {
        std::string name;
        float peak;
    };
    std::vector<Measured> measured;
    measured.reserve(presets.size());

    for (const auto& p : presets) {
        const auto levels = renderPresetLevels(p);
        REQUIRE(levels.loaded);
        measured.push_back({p.stem().string(), levels.peak});
    }

    std::vector<float> sorted;
    sorted.reserve(measured.size());
    for (const auto& m : measured) sorted.push_back(m.peak);
    std::sort(sorted.begin(), sorted.end());
    const float medianPeak = sorted[sorted.size() / 2];
    const float medianDb = toDbfs(medianPeak);

    // Factory presets legitimately span a wide dynamic range -- an airy texture
    // is not a bass patch -- so this is a floor on outliers, not a normaliser.
    constexpr float kMaxBelowMedianDb = 20.0f;

    for (const auto& m : measured) {
        const float db = toDbfs(m.peak);
        INFO("preset: " << m.name);
        INFO("peak: " << db << " dBFS, bank median peak: " << medianDb << " dBFS");
        // CHECK, not REQUIRE: report every outlier in one run rather than
        // aborting on the first.
        CHECK(db > medianDb - kMaxBelowMedianDb);
    }
}

// =============================================================================
// Filter resonance is a Q value, not a normalized 0-1 fraction
// =============================================================================
// kFilterResonanceId denormalizes as 0.1 + value * 29.9, and the preset stream
// stores the resulting Q directly (filter_params.h: "0.1-30.0"). A preset
// author writing 0.3 for "30% resonance" therefore gets Q = 0.3 -- below
// Butterworth, so the filter has no resonant peak at all. Nothing caught this:
// the format compatibility test byte-compares the layout without ever checking
// what the numbers mean.

TEST_CASE("Ruinae factory presets use real Q values for filter resonance",
          "[processor][presets][resonance]")
{
    auto presets = enumerateAllPresets();
    REQUIRE_FALSE(presets.empty());

    // Butterworth. At or below this a filter is critically damped or
    // overdamped: no resonant peak, which is never a deliberate choice across
    // an entire factory bank.
    constexpr double kMinUsefulQ = 0.7;

    for (const auto& p : presets) {
        auto proc = std::make_unique<Ruinae::Processor>();
        proc->initialize(nullptr);

        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;
        proc->setupProcessing(setup);
        proc->setActive(true);
        REQUIRE(loadPresetFileIntoProcessor(p, *proc));

        {
            ProcessData drainData{};
            drainData.numSamples = 0;
            proc->process(drainData);
        }

        MemoryStream saved;
        REQUIRE(proc->getState(&saved) == kResultTrue);
        saved.seek(0, IBStream::kIBSeekSet, nullptr);

        Ruinae::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);
        REQUIRE(controller.setComponentState(&saved) == kResultTrue);

        const double normalized =
            controller.getParamNormalized(Ruinae::kFilterResonanceId);
        const double q = 0.1 + normalized * 29.9;

        INFO("preset: " << p.stem().string());
        INFO("resonance Q: " << q);
        CHECK(q >= kMinUsefulQ);

        proc->setActive(false);
        proc->terminate();
        controller.terminate();
    }
}

TEST_CASE("Ruinae filter resonance defaults to Butterworth",
          "[processor][presets][resonance][defaults]")
{
    // The default sat at Q 0.1 -- the minimum of the [0.1, 30] range -- so a
    // fresh instance, or any preset that simply omitted the field, had a filter
    // with no resonant peak whatsoever. GlobalFilterState already defaulted to
    // 0.707; this brings the per-voice filter in line.
    constexpr double kButterworthQ = 0.707;

    SECTION("processor default state") {
        auto proc = std::make_unique<Ruinae::Processor>();
        proc->initialize(nullptr);

        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;
        proc->setupProcessing(setup);

        MemoryStream saved;
        REQUIRE(proc->getState(&saved) == kResultTrue);
        saved.seek(0, IBStream::kIBSeekSet, nullptr);

        Ruinae::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);
        REQUIRE(controller.setComponentState(&saved) == kResultTrue);

        const double q = 0.1 + controller.getParamNormalized(Ruinae::kFilterResonanceId) * 29.9;
        INFO("default resonance Q: " << q);
        CHECK(q == Approx(kButterworthQ).margin(0.01));

        proc->terminate();
        controller.terminate();
    }

    SECTION("controller's registered parameter default") {
        // What a host restores on "reset to default"; it must agree with the
        // processor's own default rather than snapping back to the range floor.
        Ruinae::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);

        const double normalized =
            controller.getParamNormalized(Ruinae::kFilterResonanceId);
        const double q = 0.1 + normalized * 29.9;
        INFO("registered default Q: " << q);
        CHECK(q == Approx(kButterworthQ).margin(0.01));

        controller.terminate();
    }
}

// =============================================================================
// R-022: Formant presets keep the vowel position they always played
// =============================================================================
// Before state v9 the Formant Vowel was ignored and Morph alone was the vowel
// position. Morph is now an offset from the Vowel, so the generator re-authors
// every Formant oscillator as vowel = floor(old position), morph = the rest.
// Two keep their authored vowel on purpose and take the new sound their comments
// describe: Vowel Descent OSC B (pure E) and Vocal Bass OSC A (just past O).

TEST_CASE("Ruinae Formant factory presets play their re-authored vowel position",
          "[processor][presets][formant]")
{
    struct Expected {
        const char* stem;
        float oscA;  ///< vowel + morph, or -1 when that oscillator is not Formant
        float oscB;
    };
    const std::vector<Expected> expected = {
        {.stem = "Vowel_Descent", .oscA = 0.0f, .oscB = 1.0f},  // OSC B: pure E (new)
        {.stem = "Vox_Oracle", .oscA = 0.6f, .oscB = 3.4f},
        {.stem = "Lydian_Aurora", .oscA = 1.5f, .oscB = -1.0f},
        {.stem = "Choir", .oscA = 0.0f, .oscB = -1.0f},
        {.stem = "Vox_Automata", .oscA = 0.0f, .oscB = 2.0f},
        {.stem = "Formant_Sea", .oscA = 0.3f, .oscB = 3.5f},
        {.stem = "Crystal_Choir", .oscA = 1.0f, .oscB = -1.0f},
        {.stem = "Vox_Machina", .oscA = 2.2f, .oscB = -1.0f},
        {.stem = "Vocal_Bass", .oscA = 3.6f, .oscB = -1.0f},    // O + 0.6 (new)
        {.stem = "Vowel_Ghost", .oscA = 2.0f, .oscB = 3.8f},
        {.stem = "Vowel_Cloud", .oscA = 1.0f, .oscB = 2.8f},
        {.stem = "Glossolalia", .oscA = 1.5f, .oscB = -1.0f},
        {.stem = "Arp_Morph_Sequence", .oscA = 1.2f, .oscB = -1.0f},
        {.stem = "Macro_Performer", .oscA = -1.0f, .oscB = 1.5f},
        {.stem = "Swing_Vox", .oscA = 0.3f, .oscB = -1.0f},
    };

    constexpr double kFormantTypeNorm =
        static_cast<double>(Krate::DSP::OscType::Formant) / (Ruinae::kOscTypeCount - 1);

    struct Slot {
        ParamID type, vowel, morph;
    };
    const Slot slots[2] = {
        {Ruinae::kOscATypeId, Ruinae::kOscAFormantVowelId, Ruinae::kOscAFormantMorphId},
        {Ruinae::kOscBTypeId, Ruinae::kOscBFormantVowelId, Ruinae::kOscBFormantMorphId},
    };

    /// Vowel position per slot (-1 when not Formant) the controller shows after `state`.
    const auto positionsAfter = [&](IBStream* state) {
        Ruinae::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);
        REQUIRE(controller.setComponentState(state) == kResultTrue);
        std::array<float, 2> pos{};
        for (size_t s = 0; s < 2; ++s) {
            const bool formant =
                std::abs(controller.getParamNormalized(slots[s].type) - kFormantTypeNorm) < 1e-6;
            pos[s] = formant
                ? static_cast<float>(std::round(controller.getParamNormalized(slots[s].vowel) * 4.0) +
                                     controller.getParamNormalized(slots[s].morph) * 4.0)
                : -1.0f;
        }
        controller.terminate();
        return pos;
    };

    size_t matched = 0;
    for (const auto& p : enumerateAllPresets()) {
        const std::string stem = p.stem().string();
        const auto it = std::find_if(expected.begin(), expected.end(),
                                     [&](const Expected& e) { return stem == e.stem; });

        // Processor load path: setState, snapshot drain, getState.
        auto proc = std::make_unique<Ruinae::Processor>();
        proc->initialize(nullptr);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;
        proc->setupProcessing(setup);
        proc->setActive(true);
        REQUIRE(loadPresetFileIntoProcessor(p, *proc));
        {
            ProcessData drainData{};
            drainData.numSamples = 0;
            proc->process(drainData);
        }
        MemoryStream saved;
        REQUIRE(proc->getState(&saved) == kResultTrue);
        saved.seek(0, IBStream::kIBSeekSet, nullptr);
        const auto fromProcessor = positionsAfter(&saved);
        proc->setActive(false);
        proc->terminate();

        // Controller load path: the preset's own component state.
        auto* file = FileStream::open(p.string().c_str(), "rb");
        REQUIRE(file != nullptr);
        PresetFile presetFile(file);
        REQUIRE((presetFile.readChunkList() && presetFile.seekToComponentState()));
        const auto* entry = presetFile.getEntry(kComponentState);
        REQUIRE(entry != nullptr);
        auto component = owned(new ReadOnlyBStream(file, entry->offset, entry->size));
        const auto fromController = positionsAfter(component);
        file->release();

        INFO("preset: " << stem);
        if (it == expected.end()) {
            // A Formant oscillator outside the table would be unchecked.
            CHECK(fromProcessor[0] < 0.0f);
            CHECK(fromProcessor[1] < 0.0f);
            continue;
        }
        ++matched;
        for (const auto& pos : {fromProcessor, fromController}) {
            CHECK(std::abs(pos[0] - it->oscA) < 1e-4f);
            CHECK(std::abs(pos[1] - it->oscB) < 1e-4f);
        }
    }
    CHECK(matched == expected.size());
}

// T6.5: every factory preset loads, is audible, stays under 0 dBFS and renders the same
// after a getState -> setState round trip (audit probe "presets shard"). The byte-level
// round trip is "Every Ruinae factory preset state round-trips byte-identically".
TEST_CASE("Every Ruinae factory preset loads, is audible, stays under 0 dBFS and round-trips its audio",
          "[presets][regression][long]")
{
    const auto presets = enumerateAllPresets();
    REQUIRE(presets.size() == size_t{168});
    const int numBlocks = static_cast<int>((3.0 * kSampleRate) / kBlockSize);
    const int noteOffBlock = static_cast<int>((2.0 * kSampleRate) / kBlockSize);

    const auto renderNote = [&](HeldNoteRender& r, float& peak, size_t& nonFinite) {
        std::vector<float> out;
        for (int block = 0; block < numBlocks; ++block) {
            if (block == noteOffBlock) r.releaseNote();
            r.renderBlock();
            for (size_t i = 0; i < static_cast<size_t>(kBlockSize); ++i) {
                for (const float x : {r.left()[i], r.right()[i]}) {
                    if (!Krate::DSP::detail::isFinite(x)) { ++nonFinite; out.push_back(0.0f); continue; }
                    peak = std::max(peak, std::abs(x));
                    out.push_back(x);
                }
            }
        }
        return out;
    };

    for (const auto& p : presets) {
        const std::string stem = p.stem().string();
        INFO("preset: " << stem);
        HeldNoteRender first;
        REQUIRE(first.load(p));
        const std::vector<char> saved = first.savedState();
        float peakA = 0.0f;
        size_t nonFiniteA = 0;
        const auto a = renderNote(first, peakA, nonFiniteA);

        HeldNoteRender second;
        REQUIRE(second.loadState(saved));
        float peakB = 0.0f;
        size_t nonFiniteB = 0;
        const auto b = renderNote(second, peakB, nonFiniteB);

        REQUIRE(a.size() == b.size());
        float maxDiff = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) maxDiff = std::max(maxDiff, std::abs(a[i] - b[i]));
        INFO("peak " << peakA << " round-trip max diff " << maxDiff);
        CHECK(nonFiniteA == size_t{0});
        CHECK(nonFiniteB == size_t{0});
        CHECK(peakA > kAudibleThreshold);
        CHECK(peakA <= 1.0f);
        CHECK(maxDiff <= 1e-6f);
    }
}
