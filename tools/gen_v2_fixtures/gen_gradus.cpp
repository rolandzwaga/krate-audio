// ==============================================================================
// Gradus fixture + golden MIDI generation (spec 142, Phase 1)
// ==============================================================================

#include "common.h"
#include "host_mocks.h"

// Gradus-only includes — MUST be the first plugin headers in this TU.
#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>

namespace KrateFixtures {
namespace {

// Apply a set of parameter changes by running ONE process() block with the
// changes attached to data.inputParameterChanges. Gradus's processor stores
// every relevant param into the underlying ArpeggiatorParams atomic the
// moment the changes are processed (no transport advance required).
//
// We can't simply call Gradus::Processor::processParameterChanges directly
// because it is private — driving via process() is the canonical entry point.
void applyParamsViaOneBlock(Gradus::Processor& processor,
                            FixtureParamChanges& changes);

std::vector<char> extractBytes(Steinberg::MemoryStream& stream) {
    Steinberg::int64 size = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<char> data(static_cast<size_t>(size));
    Steinberg::int32 bytesRead = 0;
    stream.read(data.data(), static_cast<Steinberg::int32>(size), &bytesRead);
    return data;
}

void configureGradusDefault(FixtureParamChanges& changes) {
    // Stock arp settings — no custom lane data.
    changes.add(Gradus::kArpOperatingModeId, 1.0 / 3.0);
    changes.add(Gradus::kArpModeId, 0.0);                              // Up
    changes.add(Gradus::kArpOctaveRangeId, 0.0);                       // 1
    changes.add(Gradus::kArpNoteValueId, 7.0 / 29.0);                  // 1/16
    changes.add(Gradus::kArpTempoSyncId, 1.0);
    changes.add(Gradus::kArpGateLengthId, (80.0 - 1.0) / 199.0);       // 80%
    changes.add(Gradus::kArpLatchModeId, 0.0);
    changes.add(Gradus::kArpRetriggerId, 0.0);
}

void configureGradusHeavyLanes(FixtureParamChanges& changes) {
    // Default arp + populated modulator lanes (velocity / gate / pitch /
    // modifier / ratchet) with non-default lengths and per-step values.
    configureGradusDefault(changes);

    // Velocity lane: length 16, ramp 0.5..1.0 across steps
    changes.add(Gradus::kArpVelocityLaneLengthId, (16.0 - 1.0) / 31.0);
    for (int i = 0; i < 16; ++i) {
        double v = 0.5 + 0.5 * (static_cast<double>(i) / 15.0);
        changes.add(
            static_cast<Steinberg::Vst::ParamID>(Gradus::kArpVelocityLaneStep0Id + i),
            v);
    }

    // Gate lane: length 8, alternating long/short
    changes.add(Gradus::kArpGateLaneLengthId, (8.0 - 1.0) / 31.0);
    for (int i = 0; i < 8; ++i) {
        double g = (i % 2 == 0) ? (1.5 - 0.01) / 1.99 : (0.5 - 0.01) / 1.99;
        changes.add(
            static_cast<Steinberg::Vst::ParamID>(Gradus::kArpGateLaneStep0Id + i),
            g);
    }

    // Pitch lane: length 4, [0, +7, +12, -5] semitones
    changes.add(Gradus::kArpPitchLaneLengthId, (4.0 - 1.0) / 31.0);
    int pitchOffsets[4] = { 0, 7, 12, -5 };
    for (int i = 0; i < 4; ++i) {
        double p = (static_cast<double>(pitchOffsets[i]) + 24.0) / 48.0;
        changes.add(
            static_cast<Steinberg::Vst::ParamID>(Gradus::kArpPitchLaneStep0Id + i),
            p);
    }

    // Modifier lane: length 8, all kStepActive (0x01 of 255)
    changes.add(Gradus::kArpModifierLaneLengthId, (8.0 - 1.0) / 31.0);
    for (int i = 0; i < 8; ++i) {
        changes.add(
            static_cast<Steinberg::Vst::ParamID>(Gradus::kArpModifierLaneStep0Id + i),
            1.0 / 255.0);
    }

    // Ratchet lane: length 8, [1,1,2,1,1,1,3,1]
    changes.add(Gradus::kArpRatchetLaneLengthId, (8.0 - 1.0) / 31.0);
    int ratchetSteps[8] = { 1, 1, 2, 1, 1, 1, 3, 1 };
    for (int i = 0; i < 8; ++i) {
        double r = static_cast<double>(ratchetSteps[i] - 1) / 3.0;
        changes.add(
            static_cast<Steinberg::Vst::ParamID>(Gradus::kArpRatchetLaneStep0Id + i),
            r);
    }

    // Per-lane speed multipliers != 1.0 (polymetric divergence)
    changes.add(Gradus::kArpVelocityLaneSpeedId,  0.5);
    changes.add(Gradus::kArpGateLaneSpeedId,      0.5);
    changes.add(Gradus::kArpPitchLaneSpeedId,     0.5);
    changes.add(Gradus::kArpModifierLaneSpeedId,  0.5);

    // Spice + humanize
    changes.add(Gradus::kArpSpiceId,    0.2);
    changes.add(Gradus::kArpHumanizeId, 0.1);
}

void configureGradusMidiDelay(FixtureParamChanges& changes) {
    // Default arp + MIDI delay lane active on step 0.
    configureGradusDefault(changes);

    changes.add(Gradus::kArpMidiDelayLaneLengthId, (4.0 - 1.0) / 31.0);
    changes.add(Gradus::kArpMidiDelayActiveStep0Id,    1.0);
    changes.add(Gradus::kArpMidiDelayFeedbackStep0Id,  3.0 / 16.0);
    changes.add(Gradus::kArpMidiDelayVelDecayStep0Id,  0.5);
    changes.add(Gradus::kArpMidiDelayPitchShiftStep0Id, 24.0 / 48.0);
    changes.add(Gradus::kArpMidiDelayGateScaleStep0Id, (100.0 - 10.0) / 190.0);
    changes.add(Gradus::kArpMidiDelayTimeModeStep0Id,  0.0);
    changes.add(Gradus::kArpMidiDelayTimeStep0Id,      (250.0 - 10.0) / 1990.0);
}

// Drive Gradus's processor for one block with the given parameter changes
// attached, then discard the output. This is the canonical way to apply
// host-side parameter changes; Gradus's private processParameterChanges() is
// not directly callable from outside the class.
void applyParamsViaOneBlock(Gradus::Processor& processor,
                            FixtureParamChanges& changes) {
    std::vector<float> outL(static_cast<size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlockSize), 0.0f);
    float* outChannelBuffers[2] = { outL.data(), outR.data() };
    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outChannelBuffers;

    FixtureEventList inEvents;
    FixtureEventList outEvents;

    Steinberg::Vst::ProcessContext processContext{};
    processContext.state = Steinberg::Vst::ProcessContext::kTempoValid;
    processContext.tempo = 120.0;
    processContext.sampleRate = kSampleRate;

    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = kBlockSize;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = &changes;
    data.outputParameterChanges = nullptr;
    data.inputEvents = &inEvents;
    data.outputEvents = &outEvents;
    data.processContext = &processContext;

    processor.process(data);
}

std::vector<CapturedMidi> driveGradusProcessor(Gradus::Processor& processor) {
    std::vector<float> outL(static_cast<size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlockSize), 0.0f);
    float* outChannelBuffers[2] = { outL.data(), outR.data() };
    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outChannelBuffers;

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
    data.numInputs = 0;          // Gradus has no audio input bus
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

void generateGradusFixture(const std::string& name,
                           const std::function<void(FixtureParamChanges&)>& configure,
                           const std::filesystem::path& fixturesDir) {
    std::cout << "[gradus] generating fixture '" << name << "'...\n";

    auto proc = std::make_unique<Gradus::Processor>();
    proc->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = kBlockSize;
    proc->setupProcessing(setup);

    FixtureParamChanges changes;
    configure(changes);
    applyParamsViaOneBlock(*proc, changes);

    Steinberg::MemoryStream stateStream;
    auto saveResult = proc->getState(&stateStream);
    if (saveResult != Steinberg::kResultTrue) {
        std::cerr << "  ERROR: getState() failed (result=" << saveResult << ")\n";
        proc->terminate();
        return;
    }
    auto bytes = extractBytes(stateStream);

    auto binPath = fixturesDir / ("gradus_v2_preset_" + name + ".bin");
    {
        std::ofstream f(binPath, std::ios::binary | std::ios::trunc);
        if (!f) {
            std::cerr << "  ERROR: cannot write " << binPath.string() << "\n";
            proc->terminate();
            return;
        }
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    std::cout << "  wrote " << binPath.string()
              << " (" << bytes.size() << " bytes)\n";

    proc->terminate();
}

void generateGradusGoldenMidi(const std::string& name,
                              const std::function<void(FixtureParamChanges&)>& configure,
                              const std::filesystem::path& fixturesDir) {
    std::cout << "[gradus] generating golden MIDI '" << name << "'...\n";

    auto proc = std::make_unique<Gradus::Processor>();
    proc->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = kBlockSize;
    proc->setupProcessing(setup);

    FixtureParamChanges changes;
    configure(changes);
    applyParamsViaOneBlock(*proc, changes);

    proc->setActive(true);
    auto captured = driveGradusProcessor(*proc);
    proc->setActive(false);
    proc->terminate();

    auto txtPath = fixturesDir / ("gradus_v2_golden_midi_" + name + ".txt");
    writeGoldenMidi(txtPath, captured);
    std::cout << "  wrote " << txtPath.string()
              << " (" << captured.size() << " MIDI events)\n";
}

}  // anonymous namespace

void generateGradusArtifacts(const std::filesystem::path& fixturesDir) {
    struct Config {
        std::string name;
        std::function<void(FixtureParamChanges&)> configure;
    };
    const Config configs[] = {
        { "default",     configureGradusDefault    },
        { "heavy_lanes", configureGradusHeavyLanes },
        { "midi_delay",  configureGradusMidiDelay  },
    };
    for (const auto& c : configs) {
        generateGradusFixture(c.name, c.configure, fixturesDir);
        generateGradusGoldenMidi(c.name, c.configure, fixturesDir);
    }
}

}  // namespace KrateFixtures
