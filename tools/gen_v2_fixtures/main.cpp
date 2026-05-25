// ==============================================================================
// gen_v2_fixtures — main entry (spec 142, Phase 1)
// ==============================================================================
// Orchestrates fixture generation. Pure plugin-agnostic main; the heavy lifting
// lives in gen_gradus.cpp and gen_ruinae.cpp, which are separate translation
// units to avoid the plugin_ids.h enum-namespace collision between Gradus and
// Ruinae.
// ==============================================================================

#include "common.h"

#include <filesystem>
#include <iostream>
#include <string>

// Provide moduleHandle symbol required by VST3 SDK's moduleinit.cpp (which we
// link in for IBStreamer / MemoryStream support). The harness is not a loaded
// plugin module, so nullptr is the correct value.
void* moduleHandle = nullptr;

int main(int argc, char** argv) {
    using KrateFixtures::generateGradusArtifacts;
    using KrateFixtures::generateRuinaeArtifacts;

    // Resolve the repository root. Either accept it as argv[1], or walk up
    // from the cwd to find a directory containing both CMakeLists.txt and
    // plugins/{gradus,ruinae}.
    std::filesystem::path repoRoot;
    if (argc > 1) {
        repoRoot = std::filesystem::path(argv[1]);
    } else {
        repoRoot = std::filesystem::current_path();
        while (!repoRoot.empty()) {
            if (std::filesystem::exists(repoRoot / "CMakeLists.txt")
             && std::filesystem::exists(repoRoot / "plugins" / "gradus")
             && std::filesystem::exists(repoRoot / "plugins" / "ruinae"))
            {
                break;
            }
            auto parent = repoRoot.parent_path();
            if (parent == repoRoot) {
                std::cerr << "ERROR: could not locate repo root from "
                          << std::filesystem::current_path().string() << "\n"
                          << "Pass the repo root as the first argument.\n";
                return 1;
            }
            repoRoot = parent;
        }
    }
    std::cout << "Repo root: " << repoRoot.string() << "\n";

    const auto gradusFixturesDir =
        repoRoot / "plugins" / "gradus" / "tests" / "fixtures";
    const auto ruinaeFixturesDir =
        repoRoot / "plugins" / "ruinae" / "tests" / "fixtures";
    const auto ruinaePresetsDir =
        repoRoot / "plugins" / "ruinae" / "resources" / "presets";

    std::filesystem::create_directories(gradusFixturesDir);
    std::filesystem::create_directories(ruinaeFixturesDir);

    generateGradusArtifacts(gradusFixturesDir);
    generateRuinaeArtifacts(ruinaePresetsDir, ruinaeFixturesDir);

    std::cout << "Done.\n";
    return 0;
}
