#include "main.h"

#include "body_classifier.h"
#include "exciter_classifier.h"
#include "features.h"
#include "ingestion/sfz_ingest.h"
#include "ingestion/wav_dir.h"
#include "loader.h"
#include "mapper_inversion/bell_inverse.h"
#include "mapper_inversion/membrane_inverse.h"
#include "mapper_inversion/noise_body_inverse.h"
#include "mapper_inversion/plate_inverse.h"
#include "mapper_inversion/shell_inverse.h"
#include "mapper_inversion/string_inverse.h"
#include "modal/esprit.h"
#include "modal/matrix_pencil.h"
#include "modal/mode_selection.h"
#include "preset_io/json_writer.h"
#include "preset_io/kit_preset_writer.h"
#include "preset_io/pad_preset_writer.h"
#include "refinement/bobyqa_refine.h"
#include "refinement/cmaes_refine.h"
#include "refinement/render_voice.h"
#include "segmentation.h"
#include "tone_shaper_fit.h"
#include "unnatural_fit.h"

#include "dsp/default_kit.h"

#include <array>
#include <cstdio>
#include <span>

namespace MembrumFit {

FitResult fitSample(const std::filesystem::path& wavPath, const FitOptions& options) {
    FitResult res{};
    auto loaded = loadSample(wavPath, options.targetSampleRate);
    if (!loaded) {
        res.quality.warnings.push_back("failed to load: " + wavPath.string());
        res.quality.hasWarnings = true;
        return res;
    }
    if (loaded->channelCorrelation < 0.7f) {
        res.quality.warnings.push_back("low channel correlation; mono-summing wide stereo input");
        res.quality.hasWarnings = true;
    }

    const auto seg = segmentSample(loaded->samples, loaded->sampleRate);
    if (!isSegmentationUsable(seg, loaded->sampleRate)) {
        res.quality.warnings.push_back("decay window < 100 ms; sample rejected");
        res.quality.hasWarnings = true;
        // Return DefaultKit::Kick as fallback so downstream stays sane.
        Membrum::DefaultKit::applyTemplate(res.padConfig, Membrum::DrumTemplate::Kick);
        return res;
    }

    const auto features = extractAttackFeatures(loaded->samples, seg, loaded->sampleRate);
    const auto exciter  = classifyExciter(features, ExciterDecisionSet::FullSixWay);

    const std::span<const float> decay(
        loaded->samples.data() + seg.decayStartSample,
        seg.decayEndSample - seg.decayStartSample);

    // Model-order selection + modal extraction (MP or ESPRIT per CLI flag).
    const int modelOrder = Modal::selectModelOrder(decay, loaded->sampleRate, 8, 32);
    const auto modes = (options.modalMethod == ModalMethod::MatrixPencil)
        ? Modal::extractModesMatrixPencil(decay, loaded->sampleRate, modelOrder)
        : Modal::extractModesESPRIT(decay, loaded->sampleRate, modelOrder);

    // Body classifier + per-body inversion dispatch.
    const auto bodyScores = classifyBody(modes, features);
    const auto bestBody   = pickBestBody(bodyScores);

    Membrum::PadConfig cfg{};
    switch (bestBody) {
        case Membrum::BodyModelType::Plate:
            cfg = MapperInversion::invertPlate(modes, features, loaded->sampleRate);  break;
        case Membrum::BodyModelType::Shell:
            cfg = MapperInversion::invertShell(modes, features, loaded->sampleRate);  break;
        case Membrum::BodyModelType::Bell:
            cfg = MapperInversion::invertBell(modes, features, loaded->sampleRate);   break;
        case Membrum::BodyModelType::String:
            cfg = MapperInversion::invertString(modes, features, loaded->sampleRate); break;
        case Membrum::BodyModelType::NoiseBody:
            cfg = MapperInversion::invertNoiseBody(modes, features, loaded->sampleRate); break;
        case Membrum::BodyModelType::Membrane:
        default:
            cfg = MapperInversion::invertMembrane(modes, features, loaded->sampleRate); break;
    }
    cfg.exciterType = exciter;

    // Phase 7: seed parallel noise-layer + always-on click-layer defaults based
    // on the chosen body type. Research-backed: every commercial physical-model
    // drum voice has an always-on stochastic layer; fitted kits must also carry
    // these defaults to avoid the "glass tap" failure mode.
    // Phase 7.1 defaults: mix values raised to match the amplitude-calibrated
    // NoiseLayer/ClickLayer gains. A mix of 1.0 now produces a layer peak
    // comparable to the modal body, so defaults sit in [0.55, 0.9] for
    // audible effect.
    auto applyPhase7Defaults = [](Membrum::PadConfig& c, Membrum::BodyModelType b) noexcept {
        switch (b) {
            case Membrum::BodyModelType::Membrane:
                // Snare / membrane: heavy noise (wires / head rustle) +
                // prominent click. This fits both snares and kicks that land
                // in Membrane -- the fitter has no "is this a kick" signal.
                c.noiseLayerMix        = 0.85f;
                c.noiseLayerCutoff     = 0.35f;
                c.noiseLayerResonance  = 0.2f;
                c.noiseLayerDecay      = 0.4f;
                c.noiseLayerColor      = 0.25f;  // pink-leaning, broadband
                c.clickLayerMix        = 0.7f;
                c.clickLayerContactMs  = 0.25f;
                c.clickLayerBrightness = 0.55f;
                break;
            case Membrum::BodyModelType::Shell:
                // Tom/shell: moderate head noise + felt-mallet click.
                c.noiseLayerMix        = 0.6f;
                c.noiseLayerCutoff     = 0.3f;
                c.noiseLayerResonance  = 0.2f;
                c.noiseLayerDecay      = 0.4f;
                c.noiseLayerColor      = 0.3f;
                c.clickLayerMix        = 0.75f;
                c.clickLayerContactMs  = 0.35f;
                c.clickLayerBrightness = 0.5f;
                break;
            case Membrum::BodyModelType::Plate:
                // Perc/plate: prominent click + moderate noise body.
                c.noiseLayerMix        = 0.55f;
                c.noiseLayerCutoff     = 0.55f;
                c.noiseLayerResonance  = 0.25f;
                c.noiseLayerDecay      = 0.2f;
                c.noiseLayerColor      = 0.6f;
                c.clickLayerMix        = 0.75f;
                c.clickLayerContactMs  = 0.2f;
                c.clickLayerBrightness = 0.65f;
                break;
            case Membrum::BodyModelType::Bell:
                // Bell: shimmer noise dominates, short tick.
                c.noiseLayerMix        = 0.9f;
                c.noiseLayerCutoff     = 0.8f;
                c.noiseLayerResonance  = 0.3f;
                c.noiseLayerDecay      = 0.7f;
                c.noiseLayerColor      = 0.85f;
                c.clickLayerMix        = 0.45f;
                c.clickLayerContactMs  = 0.15f;
                c.clickLayerBrightness = 0.8f;
                break;
            case Membrum::BodyModelType::String:
                // String: minimal noise; click provides plectrum attack.
                c.noiseLayerMix        = 0.3f;
                c.noiseLayerCutoff     = 0.7f;
                c.noiseLayerResonance  = 0.2f;
                c.noiseLayerDecay      = 0.2f;
                c.noiseLayerColor      = 0.7f;
                c.clickLayerMix        = 0.7f;
                c.clickLayerContactMs  = 0.2f;
                c.clickLayerBrightness = 0.7f;
                break;
            case Membrum::BodyModelType::NoiseBody:
            default:
                // Hi-hat / cymbal style: heavy noise, bright short click.
                c.noiseLayerMix        = 0.95f;
                c.noiseLayerCutoff     = 0.85f;
                c.noiseLayerResonance  = 0.25f;
                c.noiseLayerDecay      = 0.3f;
                c.noiseLayerColor      = 0.9f;
                c.clickLayerMix        = 0.55f;
                c.clickLayerContactMs  = 0.1f;
                c.clickLayerBrightness = 0.85f;
                break;
        }
    };
    applyPhase7Defaults(cfg, bestBody);

    fitToneShaper(loaded->samples, seg, loaded->sampleRate, modes, cfg);
    fitUnnaturalZone(loaded->samples, seg, loaded->sampleRate, modes, cfg, cfg);

    // BOBYQA refinement on a small, Kick-friendly subset (spec §8 Phase 1).
    RenderableMembrumVoice voice;
    voice.prepare(loaded->sampleRate);

    RefineContext rctx;
    rctx.target = std::span<const float>(loaded->samples.data(), loaded->samples.size());
    rctx.sampleRate = loaded->sampleRate;
    rctx.initial    = cfg;
    rctx.optimisable = { 2 /*material*/, 3 /*size*/, 4 /*decay*/, 8 /*tsFilterCutoff*/,
                         9 /*tsFilterResonance*/, 15 /*tsPitchEnvTime*/ };
    rctx.weights = LossWeights{ options.wSTFT, options.wMFCC, options.wEnv };
    rctx.maxEvals = options.maxBobyqaEvals;
    auto rr = refineBOBYQA(rctx, voice);

    // --global flag (or significant residual loss) -> CRS global escape.
    if (options.enableGlobalCMAES && rr.finalLoss > 1.5f) {
        RefineContext gctx = rctx;
        gctx.initial = rr.final;
        gctx.maxEvals = options.maxBobyqaEvals;
        const auto gr = refineGlobalCRS(gctx, voice);
        if (gr.finalLoss < rr.finalLoss) {
            rr.final = gr.final;
            rr.finalLoss = gr.finalLoss;
            rr.evalCount += gr.evalCount;
            rr.escapedCMAES = gr.escapedCMAES;
        }
    }

    res.padConfig = rr.final;
    res.quality.initialLoss = rr.initialLoss;
    res.quality.finalLoss   = rr.finalLoss;
    res.quality.bobyqaEvals = rr.evalCount;
    res.quality.bobyqaConverged = rr.convergedBOBYQA;
    res.quality.cmaesUsed = rr.escapedCMAES;
    return res;
}

int runMembrumFit(const CliArgs& args) {
    if (args.mode == CliMode::PerPad) {
        const auto fit = fitSample(args.input, args.options);
        if (!PresetIO::writePadPreset(args.output, fit.padConfig, args.presetName, args.subcategory)) {
            std::fprintf(stderr, "Failed to write per-pad preset to %s\n", args.output.string().c_str());
            return 3;
        }
        std::fprintf(stdout, "Wrote per-pad preset: %s (loss %.4f -> %.4f over %d evals)\n",
                     args.output.string().c_str(),
                     fit.quality.initialLoss, fit.quality.finalLoss, fit.quality.bobyqaEvals);
        if (args.options.writeJson) {
            PresetIO::writePadJson(args.output.string() + ".json", fit.padConfig, args.presetName);
        }
        return 0;
    }

    // Kit mode (Phase 2+ functionality with a Phase 1 best-effort path).
    Ingestion::KitSpec spec;
    try {
        if (args.input.extension() == ".sfz") {
            spec = Ingestion::loadKitSFZ(args.input);
        } else {
            spec = Ingestion::loadKitJson(args.input);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Failed to read kit input: %s\n", e.what());
        return 4;
    }

    std::array<Membrum::PadConfig, 32> pads{};
    for (auto& p : pads) Membrum::DefaultKit::applyTemplate(p, Membrum::DrumTemplate::Perc);

    for (const auto& [midiNote, wav] : spec.midiNoteToFile) {
        const int padIdx = midiNote - 36;
        if (padIdx < 0 || padIdx >= 32) continue;
        const auto fit = fitSample(wav, args.options);
        pads[padIdx] = fit.padConfig;
    }

    const std::filesystem::path outFile = (args.output.extension() == ".vstpreset")
        ? args.output
        : (args.output / (args.presetName + ".vstpreset"));
    if (!PresetIO::writeKitPreset(outFile, pads, args.presetName, args.subcategory)) {
        std::fprintf(stderr, "Failed to write kit preset to %s\n", outFile.string().c_str());
        return 5;
    }
    std::fprintf(stdout, "Wrote kit preset: %s\n", outFile.string().c_str());
    if (args.options.writeJson) {
        PresetIO::writeKitJson(outFile.string() + ".json", pads, args.presetName);
    }
    return 0;
}

}  // namespace MembrumFit
