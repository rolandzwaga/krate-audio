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
    void clear() { events_.clear(); }
private:
    std::vector<Event> events_;
};

class EmptyParamChanges : public IParameterChanges {
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getParameterCount() override { return 0; }
    IParamValueQueue* PLUGIN_API getParameterData(int32) override { return nullptr; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override { return nullptr; }
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
        proc_->initialize(nullptr);

        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;
        proc_->setupProcessing(setup);

        // Ruinae defers preset application through RTTransferT; activate first.
        proc_->setActive(static_cast<TBool>(true));
        if (!loadPresetFileIntoProcessor(presetPath, *proc_)) return false;

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
        loaded_ = true;
        return true;
    }

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
        data.inputParameterChanges = &emptyParams_;
        data.processContext = &processContext_;

        proc_->process(data);

        // The note-on is only delivered in the first block; hold it after that.
        inEvents_.clear();
        outEvents_.clear();

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
    EmptyParamChanges emptyParams_;
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
