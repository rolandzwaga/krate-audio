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

    // Body classifier + per-body inversion dispatch. `options.forcedBody`
    // (from `--body-override MIDI=body`) bypasses the classifier entirely
    // for samples where the spectral features mislead it -- e.g. ambient
    // kicks that get classified as NoiseBody and therefore miss the Phase 8
    // Membrane seeds (secondary bank + tension mod).
    const auto bodyScores = classifyBody(modes, features);
    const auto bestBody   = options.forcedBody.has_value()
                                ? *options.forcedBody
                                : pickBestBody(bodyScores);

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

    // Phase 8 per-body defaults. Plan async-wandering-rainbow.md §C/D/E
    // prescribes per-body seeds so BOBYQA has a viable starting point --
    // especially for secondaryEnabled (the secondary bank stays silent
    // unless enabled, so BOBYQA can't see a gradient on coupling strength
    // from an all-zero start). Without these seeds the fitter stays at
    // sentinel -1.0 damping and 0.0 coupling, i.e. Phase 7 behaviour.
    auto applyPhase8Defaults = [](Membrum::PadConfig& c, Membrum::BodyModelType b) noexcept {
        switch (b) {
            case Membrum::BodyModelType::Membrane:
                // Kick / snare: prominent air-loading, enable shell coupling,
                // moderate tension-mod for the energy-dependent pitch glide.
                c.bodyDampingB1     = 0.40f;
                c.bodyDampingB3     = 0.55f;
                c.airLoading        = 0.65f;
                c.modeScatter       = 0.04f;
                c.couplingStrength  = 0.15f;
                c.secondaryEnabled  = 1.0f;
                c.secondarySize     = 0.55f;
                c.secondaryMaterial = 0.30f;
                c.tensionModAmt     = 0.12f;
                break;
            case Membrum::BodyModelType::Shell:
                // Tom: canonical case for tension-mod (JASA 2021 Kirby &
                // Sandler "tom-tom kerthump").
                c.bodyDampingB1     = 0.35f;
                c.bodyDampingB3     = 0.50f;
                c.airLoading        = 0.55f;
                c.modeScatter       = 0.03f;
                c.couplingStrength  = 0.12f;
                c.secondaryEnabled  = 1.0f;
                c.secondarySize     = 0.50f;
                c.secondaryMaterial = 0.35f;
                c.tensionModAmt     = 0.15f;
                break;
            case Membrum::BodyModelType::Plate:
                // Perc plate: moderate damping, no shell body.
                c.bodyDampingB1     = 0.50f;
                c.bodyDampingB3     = 0.45f;
                c.airLoading        = 0.30f;
                c.modeScatter       = 0.05f;
                c.couplingStrength  = 0.0f;
                c.secondaryEnabled  = 0.0f;
                c.secondarySize     = 0.50f;
                c.secondaryMaterial = 0.40f;
                c.tensionModAmt     = 0.0f;
                break;
            case Membrum::BodyModelType::Bell:
                // Metallic long ring: low damping, no air loading, no tension.
                c.bodyDampingB1     = 0.25f;
                c.bodyDampingB3     = 0.10f;
                c.airLoading        = 0.0f;
                c.modeScatter       = 0.06f;
                c.couplingStrength  = 0.0f;
                c.secondaryEnabled  = 0.0f;
                c.secondarySize     = 0.50f;
                c.secondaryMaterial = 0.40f;
                c.tensionModAmt     = 0.0f;
                break;
            case Membrum::BodyModelType::String:
                // Waveguide-driven; modal bank is secondary. Keep neutral.
                c.bodyDampingB1     = 0.50f;
                c.bodyDampingB3     = 0.50f;
                c.airLoading        = 0.0f;
                c.modeScatter       = 0.0f;
                c.couplingStrength  = 0.0f;
                c.secondaryEnabled  = 0.0f;
                c.secondarySize     = 0.50f;
                c.secondaryMaterial = 0.40f;
                c.tensionModAmt     = 0.0f;
                break;
            case Membrum::BodyModelType::NoiseBody:
            default:
                // Hi-hat / cymbal: scatter for metallic inharmonicity,
                // no air-loading (open air), no shell body, no tension mod.
                c.bodyDampingB1     = 0.35f;
                c.bodyDampingB3     = 0.20f;
                c.airLoading        = 0.0f;
                c.modeScatter       = 0.08f;
                c.couplingStrength  = 0.0f;
                c.secondaryEnabled  = 0.0f;
                c.secondarySize     = 0.50f;
                c.secondaryMaterial = 0.40f;
                c.tensionModAmt     = 0.0f;
                break;
        }
    };
    applyPhase8Defaults(cfg, bestBody);

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
    // Plan async-wandering-rainbow.md phase-8 fit-tool extension: enable the
    // Phase 8 fields (per-mode damping, air-loading, scatter, coupling,
    // tension) only when the user asks for a longer budget. BOBYQA quality
    // degrades rapidly if dimensionality jumps to 14 at the default 300 evals.
    if (options.maxBobyqaEvals > 300) {
        // Replace sentinel b1/b3 (-1.0f = "derive from legacy") with a
        // neutral normalised midpoint so BOBYQA starts from a well-defined
        // point in [0, 1] rather than the clamped-to-zero sentinel.
        if (rctx.initial.bodyDampingB1 < 0.0f) rctx.initial.bodyDampingB1 = 0.5f;
        if (rctx.initial.bodyDampingB3 < 0.0f) rctx.initial.bodyDampingB3 = 0.5f;
        // Add Phase 8 continuous fields. secondaryEnabled (55) stays out:
        // it is a float-as-bool switch, not meaningful as a continuous
        // BOBYQA dimension -- the DefaultKit / preset decides activation.
        for (int idx : {50, 51, 52, 53, 54, 56, 57, 58}) {
            rctx.optimisable.push_back(idx);
        }
    }
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
    // Seed every pad with the Perc template so that any user who later
    // re-enables a pad inside the plugin gets a sane starting voice instead
    // of a raw default. Mark every seeded pad as disabled though: only pads
    // the fit loop below actually generates from a sample should sound on
    // load. fitSample()'s PadConfig defaults `enabled` back to 1.0, so the
    // assignment `pads[padIdx] = fit.padConfig` flips the slot back on.
    for (auto& p : pads) {
        Membrum::DefaultKit::applyTemplate(p, Membrum::DrumTemplate::Perc);
        p.enabled = 0.0f;
    }

    for (const auto& [midiNote, wav] : spec.midiNoteToFile) {
        const int padIdx = midiNote - 36;
        if (padIdx < 0 || padIdx >= 32) continue;
        FitOptions perPadOptions = args.options;
        perPadOptions.bodyOverrides.clear();  // only the per-pad field is consumed downstream
        if (auto it = args.options.bodyOverrides.find(midiNote);
            it != args.options.bodyOverrides.end()) {
            perPadOptions.forcedBody = it->second;
        }
        const auto fit = fitSample(wav, perPadOptions);
        pads[padIdx] = fit.padConfig;
        std::fprintf(stdout,
            "  pad %2d (MIDI %d, %s): loss %.4f -> %.4f (%d evals%s%s%s)\n",
            padIdx, midiNote, wav.filename().string().c_str(),
            fit.quality.initialLoss, fit.quality.finalLoss, fit.quality.bobyqaEvals,
            fit.quality.bobyqaConverged ? ", converged" : "",
            fit.quality.cmaesUsed ? ", CMA-ES" : "",
            perPadOptions.forcedBody.has_value() ? ", forced body" : "");
        std::fflush(stdout);
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
