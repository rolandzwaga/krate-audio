// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "dsp/sweep_morph_link.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/note_value.h>

#include "display/shared_display_bridge.h"
#include "display/display_bridge_log.h"

#include <algorithm>  // for std::max, std::min
#include <cmath>      // for std::log10, std::pow
#include <cstring>    // for memcpy
#include <random>     // for instance ID generation

namespace Disrumpo {

// ==============================================================================
// Processor IConnectionPoint messaging (spectrum + mod-offset blocks)
// ==============================================================================


// ==============================================================================
// DataExchange lifecycle (Spectrum Analyzer)
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::connect(
    Steinberg::Vst::IConnectionPoint* other)
{
    auto result = AudioEffect::connect(other);
    if (result == Steinberg::kResultTrue)
    {
        auto configCallback = [] (Steinberg::Vst::DataExchangeHandler::Config& config,
                                  const Steinberg::Vst::ProcessSetup& /*setup*/) {
            config.blockSize = static_cast<Steinberg::uint32>(sizeof(SpectrumBlock));
            config.numBlocks = 8;  // More blocks for IMessage fallback headroom
            config.alignment = 32;
            config.userContextID = 0;
            return true;
        };

        dataExchange_ = std::make_unique<Steinberg::Vst::DataExchangeHandler>(
            this, configCallback);
        dataExchange_->onConnect(other, getHostContext());
        KRATE_BRIDGE_LOG("Disrumpo::Processor::connect() — DataExchange created");
    }
    else
    {
        KRATE_BRIDGE_LOG("Disrumpo::Processor::connect() — base connect failed (result=%d)",
            static_cast<int>(result));
    }
    return result;
}

Steinberg::tresult PLUGIN_API Processor::disconnect(
    Steinberg::Vst::IConnectionPoint* other)
{
    KRATE_BRIDGE_LOG("Disrumpo::Processor::disconnect()");
    if (dataExchange_)
    {
        dataExchange_->onDisconnect(other);
        dataExchange_.reset();
    }
    return AudioEffect::disconnect(other);
}

// ==============================================================================
// sendSpectrumBlock -- send audio samples via DataExchange
// ==============================================================================

void Processor::sendSpectrumBlock(
    const float* /*inputL*/, const float* /*inputR*/,
    const float* outputL, const float* outputR,
    Steinberg::int32 numSamples)
{
    if (numSamples <= 0)
        return;

    // Throttle spectrum sends to ~30Hz (matching UI update cadence) to avoid
    // overflowing the queue in hosts that drain the IMessage fallback slowly.
    if (spectrumSendIntervalSamples_ <= 0)
        spectrumSendIntervalSamples_ = std::max(1, static_cast<int>(sampleRate_ * 0.03));

    spectrumSendAccumulatorSamples_ += static_cast<int>(numSamples);
    if (spectrumSendAccumulatorSamples_ < spectrumSendIntervalSamples_)
        return;

    spectrumSendAccumulatorSamples_ %= spectrumSendIntervalSamples_;

    const auto count = std::min(
        static_cast<uint32_t>(numSamples), kSpectrumBlockMaxSamples);

    // Always push to shared FIFOs (Tier 3 fallback — works even without connect())
    sharedInputFIFO_.push(spectrumBlockBuffer_.inputSamples, count);
    // Compute output mono mixdown into the shared FIFO
    {
        float outputMono[kSpectrumBlockMaxSamples];
        for (uint32_t i = 0; i < count; ++i)
            outputMono[i] = (outputL[i] + outputR[i]) * 0.5f;
        sharedOutputFIFO_.push(outputMono, count);
    }
    sharedSampleRate_.store(static_cast<float>(sampleRate_), std::memory_order_release);

    // Tier 1/2: Send via DataExchange (if host called connect())
    if (!dataExchange_)
        return;

    auto block = dataExchange_->getCurrentOrNewBlock();
    if (block.blockID == Steinberg::Vst::InvalidDataExchangeBlockID
        || block.data == nullptr)
        return;

    auto* specBlock = static_cast<SpectrumBlock*>(block.data);

    // Copy pre-computed input mono mixdown from buffer
    std::memcpy(specBlock->inputSamples, spectrumBlockBuffer_.inputSamples,
        count * sizeof(float));

    // Compute output mono mixdown directly into the block
    for (uint32_t i = 0; i < count; ++i) {
        specBlock->outputSamples[i] = (outputL[i] + outputR[i]) * 0.5f;
    }

    specBlock->numSamples = count;
    specBlock->sampleRate = static_cast<float>(sampleRate_);

    dataExchange_->sendCurrentBlock();
}

void Processor::sendModOffsetsMessage() {
    auto msg = Steinberg::owned(allocateMessage());
    if (!msg)
        return;

    msg->setMessageID("ModOffsets");
    auto* attrs = msg->getAttributes();
    if (!attrs)
        return;

    // Send pointer to modulation offset array (safe: both components in-process)
    attrs->setInt("ptr",
        static_cast<Steinberg::int64>(
            reinterpret_cast<intptr_t>(modulationEngine_.getModOffsetsArray())));

    sendMessage(msg);
}
} // namespace Disrumpo
