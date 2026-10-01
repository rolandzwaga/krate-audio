// ==============================================================================
// Vorago Phase 14 - factory preset generator; FR-018..FR-020, FR-023, FR-025
// ==============================================================================
// Spec:  specs/vorago-phase14-presets-release/spec.md
// Plan:  specs/vorago-phase14-presets-release/plan.md section 5.7
// Tasks: specs/vorago-phase14-presets-release/tasks.md (T003 skeleton, T028 body)
//
// Usage: vorago_preset_generator [output_dir]
//   Default output_dir: plugins/vorago/resources/presets
//   Target name and ${CMAKE_BINARY_DIR}/bin output are fixed by release.yml.
//
// Follows tools/seraphis_preset_generator.cpp (without its [partials] block):
// each preset's `Comp` chunk is whatever the SHIPPED Vorago::Processor::getState()
// writes after the authored normalized values were driven through the shipped
// parameter fan-out - VoragoTest::buildPresetComponentState
// (plugins/vorago/tests/vorago_preset_host.h), the one drive shared with the
// in-process harness (FR-021, C-4). No second serializer exists here.
//
// DETERMINISM (FR-023): definition order of PresetDefs::allPresets(), never a
// directory iteration; no timestamp, no path string, no RNG enters any byte.
//
// NO VSTGUI, NO Catch2: the include graph below is processor + SDK + the
// header-only test helpers; the target links KrateDSP KratePluginsShared sdk.
// ==============================================================================

#include "plugin_ids.h"            // ${CMAKE_SOURCE_DIR}/plugins/vorago/src
#include "vorago_preset_defs.h"    // ${CMAKE_SOURCE_DIR}/tools
#include "vorago_preset_host.h"    // ${CMAKE_SOURCE_DIR}/plugins/vorago/tests

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Satisfies moduleinit.cpp's `extern void* moduleHandle;` - the Vorago stub
// (plugins/vorago/tests/vstgui_test_stubs.cpp) defines only GetPluginFactory.
// MUST be a mutable global with external linkage (see the Seraphis tool).
// NOLINTNEXTLINE(misc-use-internal-linkage,cppcoreguidelines-avoid-non-const-global-variables)
void* moduleHandle = nullptr;

using Vorago::PresetDefs::VoragoPresetDef;

namespace {

// Little-endian scalar writers (duplicated from seraphis_preset_generator.cpp).
void writeLE32(std::ofstream& f, std::uint32_t v) {
    f.write(reinterpret_cast<const char*>(&v), 4);
}

void writeLE64(std::ofstream& f, std::int64_t v) {
    f.write(reinterpret_cast<const char*>(&v), 8);
}

// The class id, derived at run time from kProcessorUID - never a literal.
// FUID::toString writes 32 hex characters plus a terminator into a char8[33].
[[nodiscard]] std::string voragoClassIdAscii() {
    Steinberg::char8 buf[33] = {};
    ::Vorago::kProcessorUID.toString(buf);
    return std::string(buf);
}

// .vstpreset layout (seraphis_preset_generator.cpp:224-265, duplicated so the
// Seraphis tool is not edited):
//   offset 0    "VST3"                     4 B
//   offset 4    uint32 version = 1         4 B
//   offset 8    classId ASCII             32 B
//   offset 40   int64 listOffset           8 B
//   offset 48   Comp payload, Info payload
//   listOffset  "List", uint32 2, {"Comp", off, size}, {"Info", off, size}
[[nodiscard]] bool writeVstPreset(const std::filesystem::path& path,
                                  const std::vector<std::uint8_t>& comp,
                                  const std::string& classIdAscii,
                                  const std::string& info) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "ERROR: failed to create " << path.string() << "\n";
        return false;
    }

    constexpr std::int64_t kHeaderSize = 48;
    const std::int64_t compOffset = kHeaderSize;
    const auto compSize = static_cast<std::int64_t>(comp.size());
    const std::int64_t infoOffset = compOffset + compSize;
    const auto infoSize = static_cast<std::int64_t>(info.size());
    const std::int64_t listOffset = infoOffset + infoSize;

    // Header
    f.write("VST3", 4);
    writeLE32(f, 1u);
    f.write(classIdAscii.data(), 32);
    writeLE64(f, listOffset);

    // Comp payload - the component state stream, verbatim.
    f.write(reinterpret_cast<const char*>(comp.data()),
            static_cast<std::streamsize>(comp.size()));

    // Info payload - the metadata XML, no terminator.
    f.write(info.data(), static_cast<std::streamsize>(info.size()));

    // Chunk list: 2 entries.
    f.write("List", 4);
    writeLE32(f, 2u);
    f.write("Comp", 4);
    writeLE64(f, compOffset);
    writeLE64(f, compSize);
    f.write("Info", 4);
    writeLE64(f, infoOffset);
    writeLE64(f, infoSize);

    return f.good();
}

// A preset may only land in one of the seven category directories the browser scans.
[[nodiscard]] bool isKnownCategory(std::string_view category) {
    for (const std::string_view c : Vorago::PresetDefs::kCategories) {
        if (c == category) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path outputBase = "plugins/vorago/resources/presets";
    if (argc > 1) {
        outputBase = argv[1];
    }

    const std::string classIdAscii = voragoClassIdAscii();
    if (classIdAscii.size() != 32) {
        std::cerr << "ERROR: kProcessorUID.toString() yielded " << classIdAscii.size()
                  << " characters, expected 32\n";
        return 1;
    }

    // All seven category directories exist, whether or not a preset lands in each.
    for (const std::string_view category : Vorago::PresetDefs::kCategories) {
        const std::filesystem::path dir = outputBase / std::string(category);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            std::cerr << "ERROR: failed to create " << dir.string() << ": " << ec.message()
                      << "\n";
            return 1;
        }
    }

    const std::vector<VoragoPresetDef>& presets = Vorago::PresetDefs::allPresets();

    std::size_t written = 0;
    std::size_t failures = 0;

    // FR-023: definition order, always.
    for (const VoragoPresetDef& def : presets) {
        const std::string stem(def.name);
        const std::string category(def.category);

        if (!isKnownCategory(def.category)) {
            std::cerr << "ERROR: preset \"" << stem << "\" declares category \"" << category
                      << "\", which is not one of the seven in PresetDefs::kCategories\n";
            ++failures;
            continue;
        }

        std::vector<std::uint8_t> comp;
        std::string why;
        if (!VoragoTest::buildPresetComponentState(def, comp, why)) {
            std::cerr << "ERROR: " << category << "/" << stem << ": " << why << "\n";
            ++failures;
            continue;
        }

        const std::string info =
            Vorago::PresetDefs::buildVoragoInfoXml(def.name, def.category, def.description);
        const std::filesystem::path path = outputBase / category / (stem + ".vstpreset");

        if (!writeVstPreset(path, comp, classIdAscii, info)) {
            std::cerr << "ERROR: " << category << "/" << stem << ": write failed\n";
            ++failures;
            continue;
        }

        std::cout << "  Wrote " << comp.size() << " state bytes to " << path.string() << "\n";
        ++written;
    }

    std::cout << "wrote " << written << " presets" << std::endl;

    return failures == 0 ? 0 : 1;
}
