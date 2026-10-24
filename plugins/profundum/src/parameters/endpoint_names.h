#pragma once

// ==============================================================================
// Profundum - endpoint names (Phase 12 T014; FR-047; plan 4.5 "parameters/endpoint_names.h")
// ==============================================================================
// The user-chosen name of a morph endpoint (A or B). Used by the processor (state v3 name blocks, the
// kMsgEndpointName notify branch) and by the controller (captions, setEndpointName).
//
// State block (plan 4.5): u8 length L (0 - 31), then L UTF-8 bytes, zero-padded to 31 = 32 bytes per endpoint.
// kEndpointNameBytes / kEndpointNameBlock are declared here so this header stays independent of plugin_ids.h;
// T019 ties them to kEndpointNameMaxBytes / kEndpointNameBlockBytes with static_asserts.
//
// Not for the audio thread: process() never touches the names. Nothing here allocates.
// ==============================================================================

#include "base/source/fstreamer.h"

#include <krate/dsp/systems/bass_morph_engine.h>   // BassMorphEngine::Endpoint

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Profundum {

inline constexpr std::size_t kEndpointNameBytes = 31;                       // UTF-8 bytes of payload
inline constexpr std::size_t kEndpointNameBlock = kEndpointNameBytes + 1u;  // + the u8 length byte = 32
static_assert(kEndpointNameBlock == 32u);
static_assert(kEndpointNameBytes <= 0xFFu, "the length must fit the u8 length byte");

struct EndpointName {
    std::array<char, kEndpointNameBytes> bytes{};
    std::uint8_t length = 0;

    [[nodiscard]] std::string_view view() const noexcept {
        return {bytes.data(), length <= kEndpointNameBytes ? length : kEndpointNameBytes};
    }
};

namespace detail::endpoint_names {

[[nodiscard]] constexpr bool isControlByte(unsigned char c) noexcept { return c < 0x20u || c == 0x7Fu; }

[[nodiscard]] constexpr bool isContinuation(unsigned char c) noexcept { return (c & 0xC0u) == 0x80u; }

/// Length of the UTF-8 sequence a lead byte announces, 0 when it cannot start a well-formed sequence
/// (continuation bytes, the overlong leads C0 / C1, and F5 - FF).
[[nodiscard]] constexpr std::size_t sequenceLength(unsigned char lead) noexcept {
    if (lead < 0x80u) {
        return 1u;
    }
    if (lead >= 0xC2u && lead <= 0xDFu) {
        return 2u;
    }
    if (lead >= 0xE0u && lead <= 0xEFu) {
        return 3u;
    }
    if (lead >= 0xF0u && lead <= 0xF4u) {
        return 4u;
    }
    return 0u;
}

/// The well-formed range of the byte after @p lead (Unicode Table 3-7): rejects overlongs (E0, F0),
/// surrogates (ED) and code points above U+10FFFF (F4).
[[nodiscard]] constexpr bool secondByteValid(unsigned char lead, unsigned char second) noexcept {
    switch (lead) {
        case 0xE0u: return second >= 0xA0u && second <= 0xBFu;
        case 0xEDu: return second >= 0x80u && second <= 0x9Fu;
        case 0xF0u: return second >= 0x90u && second <= 0xBFu;
        case 0xF4u: return second >= 0x80u && second <= 0x8Fu;
        default: return isContinuation(second);
    }
}

}  // namespace detail::endpoint_names

/// "A" or "B" (plan 4.5).
[[nodiscard]] inline EndpointName defaultEndpointName(Krate::DSP::BassMorphEngine::Endpoint e) noexcept {
    EndpointName n{};
    n.bytes[0] = (e == Krate::DSP::BassMorphEngine::Endpoint::B) ? 'B' : 'A';
    n.length = 1u;
    return n;
}

/// Plan 4.5, the three steps in order:
///   1. drop control bytes (< 0x20 and 0x7F);
///   2. drop invalid UTF-8 sequences (the offending lead byte; a stray continuation byte is a lead that
///      cannot start a sequence, so it is dropped alone);
///   3. truncate at the last code-point boundary that fits in 31 bytes.
/// Step 1 is applied to the whole input before step 2 judges a sequence: the decoder reads past control
/// bytes. An empty result means the caller uses the default name.
[[nodiscard]] inline EndpointName sanitizeEndpointName(std::string_view in) noexcept {
    namespace en = detail::endpoint_names;
    const auto at = [in](std::size_t i) noexcept { return static_cast<unsigned char>(in[i]); };
    const auto skipControls = [&](std::size_t i) noexcept {
        while (i < in.size() && en::isControlByte(at(i))) {
            ++i;
        }
        return i;
    };

    EndpointName out{};
    std::size_t len = 0;
    std::size_t i = skipControls(0);
    while (i < in.size()) {
        const unsigned char lead = at(i);
        const std::size_t need = en::sequenceLength(lead);
        std::array<unsigned char, 4> seq{};
        seq[0] = lead;
        bool valid = need != 0u;
        std::size_t last = i;
        for (std::size_t k = 1; valid && k < need; ++k) {
            const std::size_t j = skipControls(last + 1u);
            if (j >= in.size()) {
                valid = false;
                break;
            }
            const unsigned char c = at(j);
            valid = (k == 1u) ? en::secondByteValid(lead, c) : en::isContinuation(c);
            seq[k] = c;
            last = j;
        }
        if (!valid) {
            i = skipControls(i + 1u);   // step 2: drop the lead byte only
            continue;
        }
        if (len + need > kEndpointNameBytes) {
            break;                      // step 3: cut at the code-point boundary
        }
        for (std::size_t k = 0; k < need; ++k) {
            out.bytes[len + k] = static_cast<char>(seq[k]);
        }
        len += need;
        i = skipControls(last + 1u);
    }
    out.length = static_cast<std::uint8_t>(len);
    return out;
}

/// Writes exactly kEndpointNameBlock (32) bytes: the u8 length, then 31 bytes zero-padded after the name.
/// Returns false if the stream took fewer bytes.
inline bool writeEndpointName(Steinberg::IBStreamer& s, const EndpointName& name) {
    std::array<char, kEndpointNameBlock> block{};
    const std::string_view v = name.view();
    block[0] = static_cast<char>(static_cast<unsigned char>(v.size()));
    for (std::size_t k = 0; k < v.size(); ++k) {
        block[1u + k] = v[k];
    }
    return s.writeRaw(block.data(), static_cast<Steinberg::TSize>(block.size())) ==
           static_cast<Steinberg::TSize>(block.size());
}

/// Reads exactly kEndpointNameBlock (32) bytes. False on a short block, a length above 31, or a payload
/// that sanitizeEndpointName would change (invalid UTF-8 or a control byte); @p out is written only on
/// success. Padding bytes after the payload are not inspected.
[[nodiscard]] inline bool readEndpointName(Steinberg::IBStreamer& s, EndpointName& out) {
    std::array<char, kEndpointNameBlock> block{};
    if (s.readRaw(block.data(), static_cast<Steinberg::TSize>(block.size())) !=
        static_cast<Steinberg::TSize>(block.size())) {
        return false;
    }
    const auto length = static_cast<std::size_t>(static_cast<unsigned char>(block[0]));
    if (length > kEndpointNameBytes) {
        return false;
    }
    const std::string_view payload{block.data() + 1, length};
    const EndpointName clean = sanitizeEndpointName(payload);
    if (clean.view() != payload) {
        return false;
    }
    out = clean;
    return true;
}

}  // namespace Profundum
