// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "dsp/sweep_morph_link.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "public.sdk/source/common/memorystream.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/note_value.h>

#include "display/shared_display_bridge.h"
#include "display/display_bridge_log.h"

#include <algorithm>  // for std::max, std::min, std::copy_n, std::fill
#include <cmath>      // for std::log10, std::pow
#include <cstring>    // for std::strcmp
#include <random>     // for instance ID generation
#include <vector>     // for the LoadPresetState bytes

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
            // sizeof(SpectrumBlock) rounded UP to the alignment: the SDK fallback
            // allocates each block with std::aligned_alloc(alignment, blockSize),
            // and macOS returns null for a size that is not a multiple of the
            // alignment (16392 % 32 = 8), so every lockBlock() failed and no
            // spectrum ever reached the controller there.
            constexpr Steinberg::uint32 kAlignment = 32;
            constexpr Steinberg::uint32 kBlockSize =
                ((static_cast<Steinberg::uint32>(sizeof(SpectrumBlock)) + kAlignment - 1u) /
                 kAlignment) * kAlignment;
            static_assert(kBlockSize % kAlignment == 0 && kBlockSize >= sizeof(SpectrumBlock));
            config.blockSize = kBlockSize;
            config.numBlocks = 8;  // More blocks for IMessage fallback headroom
            config.alignment = kAlignment;
            config.userContextID = 0;
            return true;
        };

        dataExchange_ = std::make_unique<Steinberg::Vst::DataExchangeHandler>(
            this, configCallback);
        dataExchangeThread_ = std::this_thread::get_id();
        dataExchange_->onConnect(other, getHostContext());
        // Connected after (or reconnected during) an activation: setActive(true)
        // has already run, so the queue is opened here (D-172)
        if (active_.load(std::memory_order_acquire))
            dataExchange_->onActivate(processSetup);
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
// LoadPresetState: a preset-browser load (R-204)
// ==============================================================================
// The controller hands over the whole component state; it takes the setState
// path (one parser, one hand-off, one apply), so the per-type shape shadows,
// which are not parameters, reach the processor too. Runs on the message
// thread, like setState.

Steinberg::tresult PLUGIN_API Processor::notify(Steinberg::Vst::IMessage* message)
{
    if (message == nullptr)
        return Steinberg::kInvalidArgument;

    if (std::strcmp(message->getMessageID(), "LoadPresetState") == 0)
    {
        auto* attrs = message->getAttributes();
        if (attrs == nullptr)
            return Steinberg::kResultFalse;

        const void* data = nullptr;
        Steinberg::uint32 size = 0;
        if (attrs->getBinary("bytes", data, size) != Steinberg::kResultOk || data == nullptr || size == 0)
            return Steinberg::kResultFalse;

        const auto* first = static_cast<const char*>(data);
        std::vector<char> bytes(first, first + size);
        Steinberg::MemoryStream stream(bytes.data(), static_cast<Steinberg::TSize>(bytes.size()));
        return loadState(&stream, false);
    }

    return AudioEffect::notify(message);
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

    // The block's last `count` samples (process() captured the input mono of the same span)
    const auto totalSamples = static_cast<uint32_t>(numSamples);
    const auto count = std::min(totalSamples, kSpectrumBlockMaxSamples);
    const uint32_t offset = totalSamples - count;
    float* const inputMono = spectrumBlockBuffer_.inputSamples;
    float* const outputMono = spectrumBlockBuffer_.outputSamples;
    for (uint32_t i = 0; i < count; ++i)
        outputMono[i] = (outputL[offset + i] + outputR[offset + i]) * 0.5f;

    // Tier 3 fallback (works even without connect()): every block, unthrottled,
    // since the FIFOs are lock-free; the analyser reads contiguous audio (D-133)
    sharedInputFIFO_.push(inputMono, count);
    sharedOutputFIFO_.push(outputMono, count);
    sharedSampleRate_.store(static_cast<float>(sampleRate_), std::memory_order_release);

    // History rings, written every block so a throttled send is contiguous (D-133)
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t w = (spectrumRingWritePos_ + i) & kSpectrumRingMask;
        spectrumInputRing_[w] = inputMono[i];
        spectrumOutputRing_[w] = outputMono[i];
    }
    spectrumRingWritePos_ = (spectrumRingWritePos_ + count) & kSpectrumRingMask;
    spectrumRingFilled_ = std::min(spectrumRingFilled_ + count, kSpectrumBlockMaxSamples);

    // Throttle spectrum sends to ~30Hz (matching UI update cadence) to avoid
    // overflowing the queue in hosts that drain the IMessage fallback slowly.
    if (spectrumSendIntervalSamples_ <= 0)
        spectrumSendIntervalSamples_ = std::max(1, static_cast<int>(sampleRate_ * 0.03));

    spectrumSendAccumulatorSamples_ += static_cast<int>(numSamples);
    if (spectrumSendAccumulatorSamples_ < spectrumSendIntervalSamples_)
        return;

    spectrumSendAccumulatorSamples_ %= spectrumSendIntervalSamples_;

    // Level meters: the window accumulated since the last send; Tier 3 gets it
    // whether or not DataExchange is connected
    const MeterWindow meters = meterAccum_.take();
    sharedMeters_.publish(meters);

    // Mod-slider overlay for Tier 3: the offsets a DataExchange block carries (D-045),
    // so the mod rings keep moving while DataExchange stalls (sized by the static_assert below)
    const float* const modOffsets = modulationEngine_.getModOffsetsArray();
    for (uint32_t d = 0; d < ModDest::kTotalDestinations; ++d)
        sharedModOffsets_[d].store(modOffsets[d], std::memory_order_relaxed);

    // Tier 1/2: Send via DataExchange (if host called connect())
    if (!dataExchange_)
        return;

    auto block = dataExchange_->getCurrentOrNewBlock();
    if (block.blockID == Steinberg::Vst::InvalidDataExchangeBlockID
        || block.data == nullptr)
        return;

    auto* specBlock = static_cast<SpectrumBlock*>(block.data);

    // The latest spectrumRingFilled_ samples, oldest first, in two spans of the ring
    const uint32_t sendCount = spectrumRingFilled_;
    const uint32_t start = (spectrumRingWritePos_ - sendCount) & kSpectrumRingMask;
    const uint32_t firstSpan = std::min(sendCount, kSpectrumBlockMaxSamples - start);
    const uint32_t secondSpan = sendCount - firstSpan;
    std::copy_n(spectrumInputRing_.data() + start, firstSpan, specBlock->inputSamples);
    std::copy_n(spectrumInputRing_.data(), secondSpan, specBlock->inputSamples + firstSpan);
    std::copy_n(spectrumOutputRing_.data() + start, firstSpan, specBlock->outputSamples);
    std::copy_n(spectrumOutputRing_.data(), secondSpan, specBlock->outputSamples + firstSpan);

    specBlock->numSamples = sendCount;
    specBlock->sampleRate = static_cast<float>(sampleRate_);

    // Mod-slider overlay: the offset values travel in the block (D-045)
    static_assert(ModDest::kTotalDestinations <= kSpectrumBlockModOffsets);
    std::copy_n(modulationEngine_.getModOffsetsArray(), ModDest::kTotalDestinations,
        specBlock->modOffsets);
    std::fill(specBlock->modOffsets + ModDest::kTotalDestinations,
        specBlock->modOffsets + kSpectrumBlockModOffsets, 0.0f);

    specBlock->meters = meters;

    dataExchange_->sendCurrentBlock();
}
} // namespace Disrumpo
