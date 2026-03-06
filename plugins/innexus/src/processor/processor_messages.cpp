// ==============================================================================
// Processor IMessage Handling & Display Data
// ==============================================================================

#include "processor.h"

#include <algorithm>
#include <cstring>

namespace Innexus {

// ==============================================================================
// sendDisplayData() -- Display data pipeline (M7: FR-048)
// ==============================================================================
void Processor::sendDisplayData(Steinberg::Vst::ProcessData& /*data*/)
{
    // Populate display buffer from current processor state
    const auto& frame = morphedFrame_;
    const int numPartials = std::min(frame.numPartials,
        static_cast<int>(Krate::DSP::kMaxPartials));

    // Zero-initialize, then fill active partials from frame data
    for (int i = 0; i < 48; ++i)
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

    displayDataBuffer_.f0 = frame.f0;
    displayDataBuffer_.f0Confidence = frame.f0Confidence;

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

    // Increment frame counter
    displayDataBuffer_.frameCounter++;

    // Send via IMessage
    auto* msg = allocateMessage();
    if (msg)
    {
        msg->setMessageID("DisplayData");
        auto* attrs = msg->getAttributes();
        if (attrs)
        {
            attrs->setBinary("data", &displayDataBuffer_,
                sizeof(DisplayData));
        }
        sendMessage(msg);
        msg->release();
    }
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
