#pragma once

// ==============================================================================
// Vorago Phase 12 - Phase 11 reference chain helper (plan section 6.1, T035)
// ==============================================================================
// renderPhase11ReferenceChain(): a standalone engine + cavern render that mirrors
// the Phase 11 processor's signal chain exactly, used as the golden-free
// reference for SC-002 / SC-004 / SC-005 / SC-014 (no checked-in render).
//
// Mirrored from plugins/vorago/src/processor/processor.cpp:
//   setupProcessing (:118-146) - setSeed(kEngineSeed) BEFORE prepare; engine
//     prepared with makeVoragoEngineConfig(kMaxBlockSamples), cavern with
//     makeVoragoCavernConfig(kMaxBlockSamples); setPolyphony after prepare.
//   process (:204-278) - once per host block: matrix apply(engine) +
//     applyCavernTargets(cavern, computeCavernTargets()); then slices split at
//     every event offset, the block end, or cursor + kMaxBlockSamples; every
//     event due at a slice start is dispatched before that slice renders.
//   renderSlice (:433-448) - engine -> cavern (in place) -> gain -> engine's
//     processOutputStage (limiter last).
//
// Gain: the processor snaps its master-gain smoother to the registered default
// (1.0 linear, global_params.h:41) on the first block, so the reference applies
// a constant 1.0.
//
// The optional hook is a template callable (no std::function) invoked at the
// start of every host block, BEFORE the matrix is applied:
//   hook(std::size_t blockIndex, Krate::DSP::VoragoEngine&, Krate::DSP::CavernVerb&,
//        Krate::DSP::VoragoMacroMatrix&)
// The matrix is owned by the helper, default constructed (== the processor's
// macros_ member, processor.h:100) and persists across blocks, so a hook may set
// macros once at block 0 or re-set them every block. A cavern setter the matrix
// does not own (density, breath, ...) applied by the hook survives the per-block
// applyCavernTargets(), which writes only the seven matrix-owned cavern targets
// (vorago_engine_config.h:64-73).
//
// NAMESPACE HAZARD (vorago_test_fixture.h:16-19): plugin types are spelled
// ::Vorago::... here.
// ==============================================================================

#include "engine/vorago_engine_config.h"

#include <krate/dsp/effects/cavern_verb.h>
#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace VoragoTest {

/// One scripted note event at an absolute sample position of the render.
/// velocity 0 on a note-on is a note-off (vorago_engine.h:621-624), matching the
/// processor's dispatch of a zero-velocity NoteOn (processor.cpp:411-415).
struct RefChainNote {
    std::size_t at = 0;
    std::uint8_t note = 36;
    std::uint8_t velocity = 100;
    bool on = true;
};

struct RefChainSpec {
    double sampleRate = 48000.0;
    std::size_t totalSamples = 0;
    std::size_t blockSize = 512;  ///< host block size (the partition the plugin sees)
    std::size_t polyphony = Krate::DSP::VoragoEngine::kDefaultPolyphony;  // == 4
    std::vector<RefChainNote> notes;
};

struct RefChainOutput {
    std::vector<float> L;
    std::vector<float> R;
};

template <typename Hook>
[[nodiscard]] RefChainOutput renderPhase11ReferenceChain(const RefChainSpec& spec, Hook hook) {
    RefChainOutput out;
    out.L.assign(spec.totalSamples, 0.0f);
    out.R.assign(spec.totalSamples, 0.0f);
    if (spec.totalSamples == 0 || spec.blockSize == 0) {
        return out;
    }

    // Heap, as the processor holds them (processor.cpp:80-81).
    auto engine = std::make_unique<Krate::DSP::VoragoEngine>();
    auto cavern = std::make_unique<Krate::DSP::CavernVerb>();

    engine->setSeed(::Vorago::kEngineSeed);
    engine->prepare(spec.sampleRate, ::Vorago::makeVoragoEngineConfig(::Vorago::kMaxBlockSamples));
    cavern->prepare(spec.sampleRate, ::Vorago::makeVoragoCavernConfig(::Vorago::kMaxBlockSamples));
    engine->setPolyphony(spec.polyphony);

    Krate::DSP::VoragoMacroMatrix matrix{};

    // Stable order on absolute position: equal positions keep script order, as the
    // processor's stable insertion sort keeps list order (processor.cpp:369-372).
    std::vector<RefChainNote> notes = spec.notes;
    std::stable_sort(notes.begin(), notes.end(),
                     [](const RefChainNote& a, const RefChainNote& b) { return a.at < b.at; });
    // Past-the-end positions clamp to the last sample (processor.cpp:36-44).
    for (RefChainNote& n : notes) {
        n.at = std::min(n.at, spec.totalSamples - 1u);
    }
    std::size_t nextNote = 0;

    const auto dispatch = [&](const RefChainNote& n) {
        if (n.on && n.velocity > 0u) {
            engine->noteOn(n.note, n.velocity);
        } else {
            engine->noteOff(n.note);
        }
    };

    constexpr float kUnityGain = 1.0f;

    std::size_t blockStart = 0;
    std::size_t blockIndex = 0;
    while (blockStart < spec.totalSamples) {
        const std::size_t blockEnd = std::min(spec.totalSamples, blockStart + spec.blockSize);

        // Once per host block (processor.cpp:207-209), hook first.
        hook(blockIndex, *engine, *cavern, matrix);
        matrix.apply(*engine);
        ::Vorago::applyCavernTargets(*cavern, matrix.computeCavernTargets());

        std::size_t cursor = blockStart;
        while (cursor < blockEnd) {
            // Every event due at this slice start (processor.cpp:259-265).
            while (nextNote < notes.size() && notes[nextNote].at <= cursor) {
                dispatch(notes[nextNote]);
                ++nextNote;
            }
            // Slice end (processor.cpp:266-270).
            std::size_t sliceEnd = std::min(blockEnd, cursor + ::Vorago::kMaxBlockSamples);
            if (nextNote < notes.size()) {
                sliceEnd = std::min(sliceEnd, notes[nextNote].at);
            }
            const std::size_t n = sliceEnd - cursor;
            float* l = out.L.data() + cursor;
            float* r = out.R.data() + cursor;

            // renderSlice (processor.cpp:433-448).
            engine->processStereoBlock(l, r, n);
            cavern->processStereoBlock(l, r, l, r, n);
            for (std::size_t s = 0; s < n; ++s) {
                l[s] *= kUnityGain;
                r[s] *= kUnityGain;
            }
            engine->processOutputStage(l, r, n);

            cursor = sliceEnd;
        }

        blockStart = blockEnd;
        ++blockIndex;
    }
    return out;
}

[[nodiscard]] inline RefChainOutput renderPhase11ReferenceChain(const RefChainSpec& spec) {
    return renderPhase11ReferenceChain(
        spec, [](std::size_t, Krate::DSP::VoragoEngine&, Krate::DSP::CavernVerb&,
                 Krate::DSP::VoragoMacroMatrix&) noexcept {});
}

}  // namespace VoragoTest
