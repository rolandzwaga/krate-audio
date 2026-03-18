#pragma once

// ==============================================================================
// SpectrumBlock - DataExchange audio sample block
// ==============================================================================
// POD struct sent via VST3 DataExchange API from Processor to Controller.
// Contains mono mixdown of input (pre-distortion) and output (post-distortion)
// audio samples for UI-thread FFT analysis.
//
// The block size is fixed at compile time to accommodate the maximum possible
// host buffer size. Actual sample count per block is stored in numSamples.
// ==============================================================================

#include <cstdint>
#include <type_traits>

namespace Disrumpo {

/// @brief Maximum samples per DataExchange block.
/// Covers all typical host buffer sizes (32 to 2048).
static constexpr uint32_t kSpectrumBlockMaxSamples = 2048;

/// @brief Audio sample block for spectrum display DataExchange transport.
///
/// Sent from Processor (audio thread) to Controller (UI thread) each process()
/// call. Contains mono mixdown of stereo input and output buffers.
///
/// @note Must remain POD / trivially copyable for memcpy transport.
struct SpectrumBlock {
    /// Pre-distortion mono samples (L+R)*0.5
    float inputSamples[kSpectrumBlockMaxSamples];

    /// Post-distortion mono samples (L+R)*0.5
    float outputSamples[kSpectrumBlockMaxSamples];

    /// Actual number of valid samples in this block
    uint32_t numSamples = 0;

    /// Audio sample rate (Hz) for FFT bin frequency calculation
    float sampleRate = 44100.0f;
};

static_assert(std::is_trivially_copyable_v<SpectrumBlock>,
    "SpectrumBlock must be trivially copyable for DataExchange memcpy");

} // namespace Disrumpo
