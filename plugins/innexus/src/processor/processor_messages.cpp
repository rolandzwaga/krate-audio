// ==============================================================================
// Processor IMessage Handling & Display Data
// ==============================================================================

#include "processor.h"
#include "display/display_bridge_log.h"

#include "pluginterfaces/vst/ivstdataexchange.h"

#include <algorithm>
#include <cstring>

namespace Innexus {

// ==============================================================================
// connect() -- DataExchange lifecycle
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::connect(
    Steinberg::Vst::IConnectionPoint* other)
{
    auto result = AudioEffect::connect(other);
    if (result == Steinberg::kResultTrue)
    {
        auto configCallback = [] (Steinberg::Vst::DataExchangeHandler::Config& config,
                                  const Steinberg::Vst::ProcessSetup& /*setup*/) {
            config.blockSize = static_cast<Steinberg::uint32>(sizeof(DisplayData));
            config.numBlocks = 8;
            config.alignment = 32;
            config.userContextID = 0;
            return true;
        };

        dataExchange_ = std::make_unique<Steinberg::Vst::DataExchangeHandler>(
            this, configCallback);
        dataExchange_->onConnect(other, getHostContext());
        KRATE_BRIDGE_LOG("Innexus::Processor::connect() — DataExchange created");
    }
    else
    {
        KRATE_BRIDGE_LOG("Innexus::Processor::connect() — base connect failed (result=%d)",
            static_cast<int>(result));
    }
    return result;
}

// ==============================================================================
// disconnect() -- DataExchange lifecycle
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::disconnect(
    Steinberg::Vst::IConnectionPoint* other)
{
    KRATE_BRIDGE_LOG("Innexus::Processor::disconnect()");
    if (dataExchange_)
    {
        dataExchange_->onDisconnect(other);
        dataExchange_.reset();
    }
    return AudioEffect::disconnect(other);
}

// ==============================================================================
// sendDisplayData() -- Display data pipeline (M7: FR-048)
// ==============================================================================
void Processor::sendDisplayData(Steinberg::Vst::ProcessData& data)
{
    if (data.numSamples <= 0)
        return;

    // Throttle display sends to ~30Hz (matching UI timer cadence) to avoid
    // overflowing the queue in hosts that drain the IMessage fallback slowly.
    if (displaySendIntervalSamples_ <= 0)
        displaySendIntervalSamples_ = std::max(1, static_cast<int>(sampleRate_ * 0.03));

    displaySendAccumulatorSamples_ += static_cast<int>(data.numSamples);
    if (displaySendAccumulatorSamples_ < displaySendIntervalSamples_)
        return;

    displaySendAccumulatorSamples_ %= displaySendIntervalSamples_;

    // Populate display buffer from current processor state
    const auto& frame = voice_.morphedFrame;
    const int numPartials = std::min(frame.numPartials,
        static_cast<int>(Krate::DSP::kMaxPartials));

    // Zero-initialize, then fill active partials from frame data
    for (int i = 0; i < static_cast<int>(Krate::DSP::kMaxPartials); ++i)
    {
        displayDataBuffer_.partialAmplitudes[i] = 0.0f;
        displayDataBuffer_.partialActive[i] = 0;
    }
    for (int i = 0; i < numPartials; ++i)
    {
        displayDataBuffer_.partialAmplitudes[i] = frame.partials[static_cast<size_t>(i)].amplitude;
        displayDataBuffer_.partialActive[i] =
            (filterMask_[static_cast<size_t>(i)] > 0.5f) ? 1 : 0;
    }
    displayDataBuffer_.activePartialCount = getActivePartialCount();

    displayDataBuffer_.f0 = frame.f0;
    displayDataBuffer_.f0Confidence = frame.f0Confidence;

    // Polyphonic voice data
    const auto& polyFrame = currentLivePolyFrame_;
    int numVoices = std::min(polyFrame.numSources, 8);
    displayDataBuffer_.numVoices = static_cast<uint8_t>(numVoices);
    displayDataBuffer_.isPolyphonic = liveAnalysis_.isPolyphonicActive() ? 1 : 0;
    displayDataBuffer_.analysisMode = static_cast<uint8_t>(liveAnalysis_.getAnalysisMode());

    for (int i = 0; i < numVoices; ++i)
    {
        displayDataBuffer_.voices[i].f0 = polyFrame.sources[static_cast<size_t>(i)].f0;
        displayDataBuffer_.voices[i].confidence = polyFrame.sources[static_cast<size_t>(i)].f0Confidence;
        displayDataBuffer_.voices[i].amplitude = polyFrame.sources[static_cast<size_t>(i)].globalAmplitude;
    }
    for (int i = numVoices; i < 8; ++i)
        displayDataBuffer_.voices[i] = {};

    // Memory slot status
    for (int i = 0; i < 8; ++i)
        displayDataBuffer_.slotOccupied[i] = memorySlots_[static_cast<size_t>(i)].occupied ? 1 : 0;

    // Evolution position
    displayDataBuffer_.evolutionPosition = evolutionEngine_.getPosition();
    displayDataBuffer_.manualMorphPosition = morphPosition_.load(std::memory_order_relaxed);

    // Modulator state
    displayDataBuffer_.mod1Phase = mod1_.getPhase();
    displayDataBuffer_.mod2Phase = mod2_.getPhase();
    displayDataBuffer_.mod1Active =
        (mod1Enable_.load(std::memory_order_relaxed) > 0.5f) &&
        (mod1Depth_.load(std::memory_order_relaxed) > 0.0f);
    displayDataBuffer_.mod2Active =
        (mod2Enable_.load(std::memory_order_relaxed) > 0.5f) &&
        (mod2Depth_.load(std::memory_order_relaxed) > 0.0f);

    // Increment monotonic frame counter
    displayDataBuffer_.frameCounter = ++displayFrameCounter_;

    // Also write to shared display buffer for Tier 3 fallback
    std::memcpy(&sharedDisplay_.buffer, &displayDataBuffer_, sizeof(DisplayData));
    sharedDisplay_.frameCounter.store(displayFrameCounter_, std::memory_order_release);

    // Tier 1/2: Send via DataExchangeHandler (if host called connect())
    if (!dataExchange_)
        return;

    auto block = dataExchange_->getCurrentOrNewBlock();
    if (block.blockID == Steinberg::Vst::InvalidDataExchangeBlockID
        || block.data == nullptr)
    {
        return;
    }

    std::memcpy(block.data, &displayDataBuffer_, sizeof(DisplayData));
    dataExchange_->sendCurrentBlock();
}

// ==============================================================================
// notify() -- IMessage handler (FR-029: JSON import via IMessage)
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::notify(Steinberg::Vst::IMessage* message)
{
    if (!message)
        return Steinberg::kInvalidArgument;

    if (strcmp(message->getMessageID(), "HarmonicSnapshotImport") == 0)
    {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        // Read slot index
        Steinberg::int64 slotIndex = 0;
        if (attrs->getInt("slotIndex", slotIndex) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        // Validate range 0-7
        if (slotIndex < 0 || slotIndex >= 8)
            return Steinberg::kResultFalse;

        // Read binary snapshot data
        const void* data = nullptr;
        Steinberg::uint32 dataSize = 0;
        if (attrs->getBinary("snapshotData", data, dataSize) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        // Validate size matches HarmonicSnapshot struct
        if (dataSize != sizeof(Krate::DSP::HarmonicSnapshot))
            return Steinberg::kResultFalse;

        // Fixed-size copy into pre-allocated slot (real-time safe: no allocation)
        std::memcpy(&memorySlots_[static_cast<size_t>(slotIndex)].snapshot,
                    data, sizeof(Krate::DSP::HarmonicSnapshot));
        memorySlots_[static_cast<size_t>(slotIndex)].occupied = true;

        return Steinberg::kResultOk;
    }

    // Load sample file from controller's file browser
    if (strcmp(message->getMessageID(), "LoadSampleFile") == 0)
    {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        const void* data = nullptr;
        Steinberg::uint32 dataSize = 0;
        if (attrs->getBinary("path", data, dataSize) != Steinberg::kResultOk
            || dataSize == 0)
            return Steinberg::kResultFalse;

        const std::string filePath(static_cast<const char*>(data),
                                   static_cast<size_t>(dataSize));
        loadSample(filePath);

        // Notify controller that the sample was loaded (for filename display)
        auto* reply = allocateMessage();
        if (reply)
        {
            reply->setMessageID("SampleFileLoaded");
            auto* replyAttrs = reply->getAttributes();
            replyAttrs->setBinary(
                "path", filePath.data(),
                static_cast<Steinberg::uint32>(filePath.size()));
            sendMessage(reply);
            reply->release();
        }

        return Steinberg::kResultOk;
    }

    return AudioEffect::notify(message);
}

} // namespace Innexus
