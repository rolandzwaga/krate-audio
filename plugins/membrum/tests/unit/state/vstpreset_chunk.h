#pragma once

// ==============================================================================
// componentChunkFromFile -- the component-state chunk (the kit blob) of a
// .vstpreset file on disk, in a MemoryStream that Processor::setState,
// Controller::setComponentState and State::readKitBlob can read.
// ==============================================================================

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Membrum::TestHelpers {

/// Returns nullptr when the file is missing, unreadable or has no component
/// chunk. The stream is positioned at its start.
inline Steinberg::IPtr<Steinberg::MemoryStream> componentChunkFromFile(
    const std::filesystem::path& path)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    if (!std::filesystem::exists(path))
        return nullptr;

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return nullptr;
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::streamsize>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (size > 0)
        in.read(reinterpret_cast<char*>(bytes.data()), size);

    // Parse the VST3 PresetFile header and extract the Comp chunk.
    auto presetStream = owned(new MemoryStream(
        reinterpret_cast<char*>(bytes.data()), static_cast<TSize>(bytes.size())));
    presetStream->seek(0, IBStream::kIBSeekSet, nullptr);
    PresetFile pf(presetStream);
    if (!pf.readChunkList())
        return nullptr;
    const auto* compEntry = pf.getEntry(kComponentState);
    if (compEntry == nullptr)
        return nullptr;

    auto out = owned(new MemoryStream());
    presetStream->seek(compEntry->offset, IBStream::kIBSeekSet, nullptr);
    std::vector<std::uint8_t> chunk(static_cast<std::size_t>(compEntry->size));
    int32 got = 0;
    presetStream->read(chunk.data(), static_cast<int32>(chunk.size()), &got);
    int32 written = 0;
    out->write(chunk.data(), static_cast<int32>(chunk.size()), &written);
    out->seek(0, IBStream::kIBSeekSet, nullptr);
    return out;
}

} // namespace Membrum::TestHelpers
