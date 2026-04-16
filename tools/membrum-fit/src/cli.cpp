#include "cli.h"

#include <CLI/CLI.hpp>

#include <cstdio>

namespace MembrumFit {

int parseCli(int argc, char** argv, CliArgs& outArgs) {
    CLI::App app{"membrum-fit -- offline drum-sample to Membrum-preset fitter"};
    app.require_subcommand(1);
    // Allow subcommands to defer unknown flags back to the parent app so
    // global options (--target-sample-rate, --modal-method, ...) can appear
    // in either order on the CLI.
    app.fallthrough();

    auto* perPad = app.add_subcommand("per-pad", "Fit one WAV to a single Membrum per-pad preset");
    perPad->add_option("input", outArgs.input, "input WAV file")->required();
    perPad->add_option("output", outArgs.output, "output .vstpreset path")->required();

    auto* kit = app.add_subcommand("kit", "Fit a WAV directory / SFZ into a Membrum kit preset");
    kit->add_option("input", outArgs.input, "kit JSON map or SFZ file")->required();
    kit->add_option("output", outArgs.output, "output directory (or .vstpreset path)")->required();

    std::string modalMethod = "mp";
    app.add_option("--modal-method", modalMethod, "Modal extractor: mp|esprit")
        ->check(CLI::IsMember({"mp", "esprit"}));

    app.add_option("--target-sample-rate", outArgs.options.targetSampleRate, "Analysis sample rate")
        ->default_val(44100.0);
    app.add_option("--max-evals", outArgs.options.maxBobyqaEvals, "BOBYQA max evaluations")
        ->default_val(300);
    app.add_flag("--global", outArgs.options.enableGlobalCMAES, "Enable CMA-ES global escape (Phase 4)");
    app.add_flag("--json", outArgs.options.writeJson, "Also write a JSON intermediate");
    app.add_option("--w-stft", outArgs.options.wSTFT, "MSS loss weight")->default_val(0.6f);
    app.add_option("--w-mfcc", outArgs.options.wMFCC, "MFCC loss weight")->default_val(0.2f);
    app.add_option("--w-env",  outArgs.options.wEnv,  "Envelope loss weight")->default_val(0.2f);
    app.add_option("--preset-name", outArgs.presetName, "Preset name written into metadata")
        ->default_val("Fitted");
    app.add_option("--subcategory", outArgs.subcategory, "Preset subcategory")
        ->default_val("Acoustic");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    outArgs.mode = perPad->parsed() ? CliMode::PerPad : CliMode::Kit;
    outArgs.options.modalMethod =
        (modalMethod == "esprit") ? ModalMethod::ESPRIT : ModalMethod::MatrixPencil;
    return 0;
}

}  // namespace MembrumFit
