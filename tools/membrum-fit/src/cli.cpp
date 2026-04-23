#include "cli.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>

namespace {
// Parse "36=membrane,38=shell,51=bell" into {36->Membrane, 38->Shell, 51->Bell}.
// Accepts case-insensitive body names. Returns empty map on malformed input so
// the CLI surfaces an error rather than silently applying a partial map.
bool parseBodyOverrides(const std::string& raw,
                        std::map<int, Membrum::BodyModelType>& out) {
    out.clear();
    if (raw.empty()) return true;
    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, ',')) {
        const auto eq = token.find('=');
        if (eq == std::string::npos) return false;
        const std::string midiStr = token.substr(0, eq);
        std::string bodyStr = token.substr(eq + 1);
        std::transform(bodyStr.begin(), bodyStr.end(), bodyStr.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        int midi = 0;
        try { midi = std::stoi(midiStr); }
        catch (...) { return false; }
        Membrum::BodyModelType body;
        if      (bodyStr == "membrane")  body = Membrum::BodyModelType::Membrane;
        else if (bodyStr == "shell")     body = Membrum::BodyModelType::Shell;
        else if (bodyStr == "plate")     body = Membrum::BodyModelType::Plate;
        else if (bodyStr == "bell")      body = Membrum::BodyModelType::Bell;
        else if (bodyStr == "string")    body = Membrum::BodyModelType::String;
        else if (bodyStr == "noisebody" || bodyStr == "noise") body = Membrum::BodyModelType::NoiseBody;
        else return false;
        out[midi] = body;
    }
    return true;
}
}  // namespace

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
    std::string bodyOverridesRaw;
    app.add_option("--body-override", bodyOverridesRaw,
                   "Skip the body classifier for one or more pads. "
                   "Format: MIDI=body[,MIDI=body]... "
                   "body: membrane|shell|plate|bell|string|noisebody");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    if (!parseBodyOverrides(bodyOverridesRaw, outArgs.options.bodyOverrides)) {
        std::fprintf(stderr,
                     "Invalid --body-override value: %s\n"
                     "Expected MIDI=body[,MIDI=body]... "
                     "where body is membrane|shell|plate|bell|string|noisebody\n",
                     bodyOverridesRaw.c_str());
        return 1;
    }

    outArgs.mode = perPad->parsed() ? CliMode::PerPad : CliMode::Kit;
    outArgs.options.modalMethod =
        (modalMethod == "esprit") ? ModalMethod::ESPRIT : ModalMethod::MatrixPencil;
    return 0;
}

}  // namespace MembrumFit
