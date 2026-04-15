#pragma once

#include "cli.h"
#include "types.h"

namespace MembrumFit {

// End-to-end pipeline for a single sample -> FitResult.
FitResult fitSample(const std::filesystem::path& wavPath,
                    const FitOptions& options);

// Top-level main for the CLI executable. Returns 0 on success.
int runMembrumFit(const CliArgs& args);

}  // namespace MembrumFit
