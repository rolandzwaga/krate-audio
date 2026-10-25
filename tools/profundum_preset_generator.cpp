// ==============================================================================
// Profundum Phase 13 - factory preset generator (T010; FR-024 - FR-027; plan 4.5)
// ==============================================================================
// Spec:  specs/profundum-phase13-presets-release/spec.md
// Plan:  specs/profundum-phase13-presets-release/plan.md sections 4.4, 4.5
// Tasks: specs/profundum-phase13-presets-release/tasks.md T010 (this file), T011 (CMake target)
//
// Usage: profundum_preset_generator [output_dir]
//   Default output_dir: plugins/profundum/resources/presets
//   Target name and ${CMAKE_BINARY_DIR}/bin output are fixed by release.yml.
//
// The whole body is ProfundumPluginTest::writeFactoryTree (plugins/profundum/tests/profundum_preset_host.h), shared
// with Profundum_GeneratorSmoke: each preset's `Comp` chunk is whatever the SHIPPED Profundum::Processor::getState()
// writes after the C-4 drive (buildPresetComponentState). No second serializer exists here.
//
// DETERMINISM (FR-024): definition order of PresetDefs::allPresets(), never a directory iteration; no timestamp,
// no path string, no RNG enters any byte.
//
// NO VSTGUI, NO Catch2: the include graph is processor + SDK + the header-only, Catch2-free test helpers
// (profundum_preset_host.h, preset_test_support.h, vst_param_changes.h) + profundum_preset_defs.h.
// ==============================================================================

#include "profundum_preset_host.h"   // ${CMAKE_SOURCE_DIR}/plugins/profundum/tests

#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

// Satisfies moduleinit.cpp's `extern void* moduleHandle;` - the Profundum stub
// (plugins/profundum/tests/vstgui_test_stubs.cpp) defines only GetPluginFactory.
// MUST be a mutable global with external linkage (the Vorago / Seraphis tools do the same).
// NOLINTNEXTLINE(misc-use-internal-linkage,cppcoreguidelines-avoid-non-const-global-variables)
void* moduleHandle = nullptr;

// Every statement is inside try / catch (...); bugprone-exception-escape still reports the
// std::filesystem::path(argv[1]) conversion (MSVC _Throw_bad_array_new_length) as escaping.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char* argv[]) {
    try {
        const std::filesystem::path outputBase = argc > 1 ? std::filesystem::path(argv[1])
                                                          : std::filesystem::path("plugins/profundum/resources/presets");

        std::size_t written = 0;
        std::string why;
        const bool ok = ProfundumPluginTest::writeFactoryTree(outputBase, written, why);

        std::cout << "wrote " << written << " presets" << '\n';
        if (!ok) {
            std::cerr << "ERROR: " << why << "\n";
            return 1;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "ERROR: unknown exception\n";
        return 1;
    }
}
