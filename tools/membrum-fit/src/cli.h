#pragma once

#include "types.h"

#include <filesystem>
#include <string>

namespace MembrumFit {

enum class CliMode {
    PerPad,
    Kit,
};

struct CliArgs {
    CliMode                 mode = CliMode::PerPad;
    std::filesystem::path   input;
    std::filesystem::path   output;
    std::string             presetName = "Fitted";
    std::string             subcategory = "Acoustic";
    FitOptions              options{};
};

// Parse argv using CLI11. Returns 0 on success; non-zero = CLI failure (caller
// should exit with that code). Fills outArgs on success.
int parseCli(int argc, char** argv, CliArgs& outArgs);

}  // namespace MembrumFit
