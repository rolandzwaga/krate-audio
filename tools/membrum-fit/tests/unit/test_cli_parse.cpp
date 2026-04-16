// CLI parser smoke -- per-pad subcommand, flag parsing.
#include "src/cli.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <vector>

namespace {

std::vector<char*> mkArgv(std::vector<std::string>& store) {
    std::vector<char*> argv;
    for (auto& s : store) argv.push_back(s.data());
    return argv;
}

}

TEST_CASE("CLI: per-pad subcommand parses input/output + options") {
    std::vector<std::string> args = {
        "membrum_fit", "per-pad", "in.wav", "out.vstpreset",
        "--target-sample-rate", "44100",
        "--max-evals", "50",
        "--w-stft", "0.5",
        "--w-mfcc", "0.3",
        "--w-env", "0.2",
        "--preset-name", "MyKick",
        "--subcategory", "Kick",
    };
    auto argv = mkArgv(args);
    MembrumFit::CliArgs out;
    REQUIRE(MembrumFit::parseCli(static_cast<int>(argv.size()), argv.data(), out) == 0);
    REQUIRE(out.mode == MembrumFit::CliMode::PerPad);
    REQUIRE(out.input.string()  == "in.wav");
    REQUIRE(out.output.string() == "out.vstpreset");
    REQUIRE(out.options.maxBobyqaEvals == 50);
    REQUIRE(out.options.wSTFT == Catch::Approx(0.5f));
    REQUIRE(out.options.wMFCC == Catch::Approx(0.3f));
    REQUIRE(out.options.wEnv  == Catch::Approx(0.2f));
    REQUIRE(out.presetName  == "MyKick");
    REQUIRE(out.subcategory == "Kick");
}

TEST_CASE("CLI: kit subcommand selects ESPRIT modal method") {
    std::vector<std::string> args = {
        "membrum_fit", "kit", "kit.json", "out_dir",
        "--modal-method", "esprit",
        "--global",
    };
    auto argv = mkArgv(args);
    MembrumFit::CliArgs out;
    REQUIRE(MembrumFit::parseCli(static_cast<int>(argv.size()), argv.data(), out) == 0);
    REQUIRE(out.mode == MembrumFit::CliMode::Kit);
    REQUIRE(out.options.modalMethod == MembrumFit::ModalMethod::ESPRIT);
    REQUIRE(out.options.enableGlobalCMAES);
}
