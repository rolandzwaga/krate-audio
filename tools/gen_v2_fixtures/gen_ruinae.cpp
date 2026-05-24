// ==============================================================================
// Ruinae factory-preset golden MIDI generation (spec 142, Phase 1, SC-004b)
// ==============================================================================
// Loads each Ruinae factory arp preset, drives the processor with the standard
// 60-second MIDI input, and dumps emitted MIDI events to a golden text file.
// SC-004b uses these to assert byte-identical MIDI after the kNumLanes 9->10
// extension lands.
// ==============================================================================

#include "common.h"
#include "host_mocks.h"

// Ruinae-only includes — MUST be the first plugin headers in this TU.
#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

namespace KrateFixtures {
namespace {

// Subclass to expose Ruinae::Processor::processParameterChanges (protected)
// so the harness can force midiOut=on after preset load — most factory
// presets ship with midiOut=off (Ruinae is primarily a synth), but for SC-004b
// regression we need observable MIDI output for byte-identical comparison.
class RuinaeHarnessProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
};

bool loadPresetFileIntoProcessor(const std::filesystem::path& presetPath,
                                 Ruinae::Processor& processor) {
    auto* stream = Steinberg::Vst::FileStream::open(
        presetPath.string().c_str(), "rb");
    if (!stream) {
        std::cerr << "  ERROR: cannot open preset " << presetPath.string() << "\n";
        return false;
    }

    Steinberg::Vst::PresetFile presetFile(stream);
    bool ok = presetFile.readChunkList() && presetFile.seekToComponentState();
    if (!ok) {
        std::cerr << "  ERROR: preset chunk list / seek failed for "
                  << presetPath.string() << "\n";
        stream->release();
        return false;
    }

    const auto* entry = presetFile.getEntry(Steinberg::Vst::kComponentState);
    if (!entry) {
        std::cerr << "  ERROR: preset missing component state for "
                  << presetPath.string() << "\n";
        stream->release();
        return false;
    }

    auto componentStream = Steinberg::owned(
        new Steinberg::Vst::ReadOnlyBStream(stream, entry->offset, entry->size));
    auto setResult = processor.setState(componentStream);
    stream->release();

    if (setResult != Steinberg::kResultTrue) {
        std::cerr << "  ERROR: setState failed (result=" << setResult << ") for "
                  << presetPath.string() << "\n";
        return false;
    }
    return true;
}

std::vector<CapturedMidi> driveRuinaeProcessor(Ruinae::Processor& processor) {
    std::vector<float> outL(static_cast<size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlockSize), 0.0f);
    float* outChannelBuffers[2] = { outL.data(), outR.data() };
    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outChannelBuffers;

    // Ruinae has an auxiliary sidechain audio input. Supply zero-filled
    // buffers — the bus is registered with `0` (not default-active), so the
    // host normally only activates it when audio is routed. Mirror that by
    // passing numInputs=0; the processor handles the no-sidechain case.
    FixtureEventList         inEvents;
    FixtureEventList         outEvents;
    FixtureEmptyParamChanges emptyParams;

    Steinberg::Vst::ProcessContext processContext{};
    processContext.state = Steinberg::Vst::ProcessContext::kPlaying
                         | Steinberg::Vst::ProcessContext::kTempoValid
                         | Steinberg::Vst::ProcessContext::kTimeSigValid
                         | Steinberg::Vst::ProcessContext::kProjectTimeMusicValid;
    processContext.tempo = 120.0;
    processContext.timeSigNumerator = 4;
    processContext.timeSigDenominator = 4;
    processContext.sampleRate = kSampleRate;
    processContext.projectTimeMusic = 0.0;
    processContext.projectTimeSamples = 0;

    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = kBlockSize;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = &emptyParams;
    data.outputParameterChanges = nullptr;
    data.inputEvents = &inEvents;
    data.outputEvents = &outEvents;
    data.processContext = &processContext;

    auto sequence = makeStandardMidiSequence();
    size_t nextSeqIdx = 0;

    std::vector<CapturedMidi> captured;
    captured.reserve(8192);

    for (int blockIdx = 0; blockIdx < kNumBlocks; ++blockIdx) {
        const int64_t blockStartSample =
            static_cast<int64_t>(blockIdx) * kBlockSize;
        const int64_t blockEndSample = blockStartSample + kBlockSize;

        inEvents.clear();
        while (nextSeqIdx < sequence.size()
            && sequence[nextSeqIdx].sampleTime < blockEndSample)
        {
            const auto& s = sequence[nextSeqIdx];
            const int32_t sampleOffset =
                static_cast<int32_t>(s.sampleTime - blockStartSample);
            if (s.isNoteOn) {
                inEvents.addNoteOn(s.pitch, s.velocity, sampleOffset);
            } else {
                inEvents.addNoteOff(s.pitch, sampleOffset);
            }
            ++nextSeqIdx;
        }

        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        outEvents.clear();

        processor.process(data);

        Steinberg::int32 evCount = outEvents.getEventCount();
        for (Steinberg::int32 i = 0; i < evCount; ++i) {
            Steinberg::Vst::Event e{};
            if (outEvents.getEvent(i, e) != Steinberg::kResultTrue) continue;
            CapturedMidi c{};
            c.absoluteSample = blockStartSample + e.sampleOffset;
            if (e.type == Steinberg::Vst::Event::kNoteOnEvent) {
                c.isNoteOn = true;
                c.pitch    = e.noteOn.pitch;
                c.velocity = std::clamp(
                    static_cast<int>(e.noteOn.velocity * 127.0f + 0.5f),
                    0, 127);
            } else if (e.type == Steinberg::Vst::Event::kNoteOffEvent) {
                c.isNoteOn = false;
                c.pitch    = e.noteOff.pitch;
                c.velocity = 0;
            } else {
                continue;
            }
            captured.push_back(c);
        }

        processContext.projectTimeSamples += kBlockSize;
        processContext.projectTimeMusic +=
            static_cast<double>(kBlockSize) / kSampleRate
            * (processContext.tempo / 60.0);
    }

    return captured;
}

void generateRuinaeGoldenMidi(const std::filesystem::path& presetPath,
                              const std::filesystem::path& fixturesDir) {
    const std::string stem = presetPath.stem().string();
    const std::string safe = sanitizeForFilename(stem);
    std::cout << "[ruinae] generating golden MIDI for preset '" << stem << "'...\n";

    auto proc = std::make_unique<RuinaeHarnessProcessor>();
    proc->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = kBlockSize;
    proc->setupProcessing(setup);

    // Ruinae must be active BEFORE setState so the RTTransferT path is set
    // up correctly; the staged snapshot is then drained via a zero-sample
    // process() call (mirrors arp_preset_e2e_test.cpp pattern).
    proc->setActive(true);

    if (!loadPresetFileIntoProcessor(presetPath, *proc)) {
        std::cerr << "  SKIP: failed to load preset\n";
        proc->setActive(false);
        proc->terminate();
        return;
    }

    // Drain the deferred preset snapshot before driving the test sequence.
    {
        Steinberg::Vst::ProcessData drainData{};
        drainData.numSamples = 0;
        proc->process(drainData);
    }

    // Force-enable MIDI output. Most factory presets ship with midiOut=off
    // because Ruinae is primarily a synthesizer; for SC-004b regression we
    // need observable MIDI bytes to compare. Toggling midiOut on does not
    // alter the arp's NOTE timing — it only gates whether the arp's emitted
    // notes are also routed to the MIDI output bus.
    {
        FixtureParamChanges enableMidiOut;
        enableMidiOut.add(Ruinae::kArpMidiOutId, 1.0);
        proc->processParameterChanges(&enableMidiOut);
    }

    auto captured = driveRuinaeProcessor(*proc);
    proc->setActive(false);
    proc->terminate();

    auto txtPath = fixturesDir / ("ruinae_factory_" + safe + "_golden_midi.txt");
    writeGoldenMidi(txtPath, captured);
    std::cout << "  wrote " << txtPath.string()
              << " (" << captured.size() << " MIDI events)\n";
}

// Enumerate factory arp presets. The kNumLanes 9->10 extension only affects
// ArpeggiatorCore code paths, so non-arp presets (synth pads, leads, basses,
// etc.) do not exercise the changed surface. We restrict to the 25 arp-
// specific factory presets across the 8 "Arp *" subdirectories.
std::vector<std::filesystem::path> enumerateRuinaeArpPresets(
    const std::filesystem::path& presetRoot)
{
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(presetRoot)) {
        return result;
    }
    for (const auto& subdir : std::filesystem::directory_iterator(presetRoot)) {
        if (!subdir.is_directory()) continue;
        const auto subName = subdir.path().filename().string();
        if (subName.rfind("Arp", 0) != 0) continue;
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

}  // anonymous namespace

void generateRuinaeArtifacts(const std::filesystem::path& presetsDir,
                             const std::filesystem::path& fixturesDir) {
    auto presets = enumerateRuinaeArpPresets(presetsDir);
    std::cout << "Found " << presets.size()
              << " Ruinae arp factory presets.\n";
    for (const auto& p : presets) {
        generateRuinaeGoldenMidi(p, fixturesDir);
    }
}

}  // namespace KrateFixtures
