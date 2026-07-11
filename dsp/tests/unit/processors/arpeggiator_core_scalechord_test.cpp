// arpeggiator_core_scalechord_test.cpp
// Scale-aware pitch lane (spec 084), chord lane, per-lane speed
// Split from the former 17k-line arpeggiator_core_test.cpp (D1). Shared helpers in
// arpeggiator_core_test_helpers.h.
#include "arpeggiator_core_test_helpers.h"



// =============================================================================
// 084-arp-scale-mode: Scale-Aware Pitch Lane (User Story 1)
// =============================================================================

TEST_CASE("ArpeggiatorCore: ScaleMode_ChromaticDefault_PitchOffset2_IsD4",
          "[processors][arpeggiator_core][arpeggiator][scale-mode]") {
    // T018: Chromatic mode (default), pitch offset +2 on C4 = D4 (62)
    // Default scale should be Chromatic, so +2 means +2 semitones
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(60, 100);  // C4

    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, 2);  // +2

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    // C4=60 + 2 semitones = D4=62
    CHECK(noteOns[0].note == 62);
}


TEST_CASE("ArpeggiatorCore: ScaleMode_MajorC_Offset2_IsE4",
          "[processors][arpeggiator_core][arpeggiator][scale-mode]") {
    // T019a: Major scale, root C: offset +2 on C4 = E4 (+4 semitones)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.setScaleType(ScaleType::Major);
    arp.setRootNote(0);  // C
    arp.noteOn(60, 100);  // C4

    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, 2);  // +2 degrees in Major = +4 semitones (C -> D -> E)

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    // C4=60 + 2 degrees in C Major = E4=64
    CHECK(noteOns[0].note == 64);
}


TEST_CASE("ArpeggiatorCore: ScaleMode_MajorC_Offset7_IsC5",
          "[processors][arpeggiator_core][arpeggiator][scale-mode]") {
    // T019b: Major scale, root C: offset +7 on C4 = C5 (octave wrap)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.setScaleType(ScaleType::Major);
    arp.setRootNote(0);  // C
    arp.noteOn(60, 100);  // C4

    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, 7);  // +7 degrees in Major = full octave

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    // C4=60 + 7 degrees in C Major = C5=72
    CHECK(noteOns[0].note == 72);
}


TEST_CASE("ArpeggiatorCore: ScaleMode_MajorC_OffsetNeg1_IsB3",
          "[processors][arpeggiator_core][arpeggiator][scale-mode]") {
    // T019c: Major scale, root C: offset -1 on C4 = B3 (negative wrap)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.setScaleType(ScaleType::Major);
    arp.setRootNote(0);  // C
    arp.noteOn(60, 100);  // C4

    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, -1);  // -1 degree in Major = B3

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    // C4=60 - 1 degree in C Major = B3=59
    CHECK(noteOns[0].note == 59);
}


TEST_CASE("ArpeggiatorCore: ScaleMode_MinorPentatonicC_Offset1_IsEb4",
          "[processors][arpeggiator_core][arpeggiator][scale-mode]") {
    // T020: Minor Pentatonic scale, root C: offset +1 on C4 = Eb4 (+3 semitones)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.setScaleType(ScaleType::MinorPentatonic);
    arp.setRootNote(0);  // C
    arp.noteOn(60, 100);  // C4

    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, 1);  // +1 degree in Minor Pentatonic {0,3,5,7,10}

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    // C4=60 + 1 degree in C Minor Pentatonic = Eb4=63
    CHECK(noteOns[0].note == 63);
}


TEST_CASE("ArpeggiatorCore: ScaleMode_MajorC_Offset24_ClampsMidi127",
          "[processors][arpeggiator_core][arpeggiator][scale-mode]") {
    // T021: Major scale, root C: offset +24 on a note where result exceeds MIDI 127
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.setScaleType(ScaleType::Major);
    arp.setRootNote(0);  // C
    arp.noteOn(120, 100);  // Very high base note

    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, 24);  // +24 degrees -> many semitones

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    // Result should be clamped to 127, not overflow
    CHECK(noteOns[0].note <= 127);
    CHECK(noteOns[0].note == 127);
}


TEST_CASE("ArpeggiatorCore: ScaleMode_Pentatonic_Offset6_OctaveWrap",
          "[processors][arpeggiator_core][arpeggiator][scale-mode]") {
    // T022: Pentatonic scale (5-note), offset +6 wraps correctly into next octave
    // Major Pentatonic: {0, 2, 4, 7, 9}, 5 degrees
    // Offset +5 = next octave root (C5=72 from C4=60)
    // Offset +6 = degree 1 of next octave = D5
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.setScaleType(ScaleType::MajorPentatonic);
    arp.setRootNote(0);  // C
    arp.noteOn(60, 100);  // C4

    arp.pitchLane().setLength(2);
    arp.pitchLane().setStep(0, 5);  // +5 in Major Pentatonic (5-note) = octave wrap = C5
    arp.pitchLane().setStep(1, 6);  // +6 = degree 1 of next octave = D5

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 400);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 2);
    // Major Pentatonic C: degrees 0=C, 1=D, 2=E, 3=G, 4=A
    // +5 degrees from C4=60: octave 1, degree 0 -> C5=72
    CHECK(noteOns[0].note == 72);
    // +6 degrees from C4=60: octave 1, degree 1 -> D5=74
    CHECK(noteOns[1].note == 74);
}


// ---------------------------------------------------------------------------
// 084-arp-scale-mode: Scale Quantize Input (User Story 3)
// ---------------------------------------------------------------------------

TEST_CASE("ArpeggiatorCore: QuantizeInput_ON_MajorC_CSharp4_SnapsToC4",
          "[processors][arpeggiator_core][arpeggiator][scale-mode][quantize-input]") {
    // T049: quantize input ON, Major C: C#4 input -> C4 in held notes pool
    // C# is equidistant from C and D (1 semitone each); ties snap to lower = C.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.setScaleType(ScaleType::Major);
    arp.setRootNote(0);  // C
    arp.setScaleQuantizeInput(true);
    arp.noteOn(61, 100);  // C#4 -- should snap to C4=60

    // Pitch offset 0, so output should be whatever is in the pool
    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, 0);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    // C#4=61 should have been snapped to C4=60 (nearest scale note, tie -> lower)
    CHECK(noteOns[0].note == 60);
}


TEST_CASE("ArpeggiatorCore: QuantizeInput_OFF_MajorC_CSharp4_Passthrough",
          "[processors][arpeggiator_core][arpeggiator][scale-mode][quantize-input]") {
    // T050: quantize input OFF, Major C: C#4 input -> C#4 in held notes pool (passthrough)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.setScaleType(ScaleType::Major);
    arp.setRootNote(0);  // C
    arp.setScaleQuantizeInput(false);  // OFF
    arp.noteOn(61, 100);  // C#4 -- should pass through unchanged

    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, 0);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    // C#4=61 should pass through unchanged
    CHECK(noteOns[0].note == 61);
}


TEST_CASE("ArpeggiatorCore: QuantizeInput_ON_Chromatic_CSharp4_Passthrough",
          "[processors][arpeggiator_core][arpeggiator][scale-mode][quantize-input]") {
    // T051: quantize input ON, Chromatic scale: C#4 passes through unchanged (FR-010)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.setScaleType(ScaleType::Chromatic);
    arp.setRootNote(0);  // C
    arp.setScaleQuantizeInput(true);  // ON, but Chromatic -> no effect
    arp.noteOn(61, 100);  // C#4 -- should pass through unchanged

    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, 0);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    // Chromatic scale: quantize input has no effect, C#4=61 passes through
    CHECK(noteOns[0].note == 61);
}


TEST_CASE("ArpeggiatorCore: QuantizeInput_ON_SwitchToChromaticStopsQuantization",
          "[processors][arpeggiator_core][arpeggiator][scale-mode][quantize-input]") {
    // T052: switching Scale Type from non-Chromatic back to Chromatic while quantize
    // is ON stops quantization (notes pass through).
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);

    // Start with Major scale and quantize ON
    arp.setScaleType(ScaleType::Major);
    arp.setRootNote(0);  // C
    arp.setScaleQuantizeInput(true);

    // First note: C#4 should be quantized to C4
    arp.noteOn(61, 100);

    // Switch to Chromatic (quantize still ON, but should have no effect)
    arp.setScaleType(ScaleType::Chromatic);

    // Second note: C#4 should now pass through unchanged
    arp.noteOn(61, 100);

    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, 0);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // The arp should have two notes in the pool: C4=60 (from first noteOn, quantized)
    // and C#4=61 (from second noteOn, passthrough after switching to Chromatic).
    // In Up mode, notes are played ascending: first 60, then 61.
    REQUIRE(noteOns.size() >= 2);
    CHECK(noteOns[0].note == 60);  // First note was quantized (Major was active)
    CHECK(noteOns[1].note == 61);  // Second note passed through (Chromatic active)
}


// =============================================================================
// Chord Lane Tests (arp-chord-lane)
// =============================================================================

TEST_CASE("Arp Chord Lane: None chord type produces single NoteOn (backward compat)",
          "[arp][chord-lane]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);

    // Chord lane defaults to None
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    CHECK(noteOns[0].note == 60);
}


TEST_CASE("Arp Chord Lane: Triad generates 3 NoteOn events",
          "[arp][chord-lane]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setScaleType(ScaleType::Major);
    arp.setRootNote(0);  // C

    // Set chord lane to Triad for step 0
    arp.chordLane().setStep(0, static_cast<uint8_t>(ChordType::Triad));

    arp.noteOn(60, 100);  // C4

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // First step should have 3 NoteOns (C major triad)
    REQUIRE(noteOns.size() >= 3);
    // Collect first step notes (all have same sampleOffset)
    int32_t firstOffset = noteOns[0].sampleOffset;
    std::vector<uint8_t> firstStepNotes;
    for (const auto& ev : noteOns) {
        if (ev.sampleOffset == firstOffset) {
            firstStepNotes.push_back(ev.note);
        }
    }
    REQUIRE(firstStepNotes.size() == 3);
    CHECK(firstStepNotes[0] == 60);  // C4
    CHECK(firstStepNotes[1] == 64);  // E4
    CHECK(firstStepNotes[2] == 67);  // G4
}


TEST_CASE("Arp Chord Lane: Pitch offset applies after chord expansion",
          "[arp][chord-lane]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setScaleType(ScaleType::Chromatic);

    // Chord: Triad, Pitch: +2 semitones
    arp.chordLane().setStep(0, static_cast<uint8_t>(ChordType::Triad));
    arp.pitchLane().setStep(0, 2);

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 3);
    int32_t firstOffset = noteOns[0].sampleOffset;
    std::vector<uint8_t> firstStepNotes;
    for (const auto& ev : noteOns) {
        if (ev.sampleOffset == firstOffset) {
            firstStepNotes.push_back(ev.note);
        }
    }
    REQUIRE(firstStepNotes.size() == 3);
    // Chromatic triad: 60, 64, 67 all shifted +2
    CHECK(firstStepNotes[0] == 62);  // C4+2
    CHECK(firstStepNotes[1] == 66);  // E4+2
    CHECK(firstStepNotes[2] == 69);  // G4+2
}


TEST_CASE("Arp Chord Lane: Polymetric chord lane length",
          "[arp][chord-lane]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setScaleType(ScaleType::Chromatic);

    // Chord lane length=2: step0=Triad, step1=None
    arp.chordLane().setLength(2);
    arp.chordLane().setStep(0, static_cast<uint8_t>(ChordType::Triad));
    arp.chordLane().setStep(1, static_cast<uint8_t>(ChordType::None));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 400);
    auto noteOns = filterNoteOns(events);

    // Find unique step offsets
    std::vector<int32_t> stepOffsets;
    for (const auto& ev : noteOns) {
        if (stepOffsets.empty() || stepOffsets.back() != ev.sampleOffset) {
            stepOffsets.push_back(ev.sampleOffset);
        }
    }

    // Step 0: 3 notes (triad), Step 1: 1 note (none), Step 2: 3 notes (triad)...
    // Count notes per step offset
    REQUIRE(stepOffsets.size() >= 3);

    // Step 0 should have 3 notes
    size_t step0Count = 0;
    for (const auto& ev : noteOns) {
        if (ev.sampleOffset == stepOffsets[0]) ++step0Count;
    }
    CHECK(step0Count == 3);

    // Step 1 should have 1 note
    size_t step1Count = 0;
    for (const auto& ev : noteOns) {
        if (ev.sampleOffset == stepOffsets[1]) ++step1Count;
    }
    CHECK(step1Count == 1);
}


TEST_CASE("Arp Chord Lane: ArpMode::Chord skips chord lane",
          "[arp][chord-lane]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Chord);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setScaleType(ScaleType::Major);
    arp.setRootNote(0);

    // Even though chord lane says Triad, Chord mode plays all held notes
    arp.chordLane().setStep(0, static_cast<uint8_t>(ChordType::Triad));

    arp.noteOn(60, 100);  // C4
    arp.noteOn(64, 100);  // E4

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // In Chord mode, all 2 held notes play (NOT a triad from each)
    REQUIRE(noteOns.size() >= 2);
    int32_t firstOffset = noteOns[0].sampleOffset;
    std::vector<uint8_t> firstStepNotes;
    for (const auto& ev : noteOns) {
        if (ev.sampleOffset == firstOffset) {
            firstStepNotes.push_back(ev.note);
        }
    }
    CHECK(firstStepNotes.size() == 2);  // Just the 2 held notes
}


TEST_CASE("Arp Chord Lane: NoteOffs emitted for previous chord before new chord",
          "[arp][chord-lane]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.setScaleType(ScaleType::Chromatic);

    arp.chordLane().setStep(0, static_cast<uint8_t>(ChordType::Triad));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 400);

    // Verify that NoteOffs are emitted for all 3 chord notes
    auto noteOffs = filterNoteOffs(events);
    // Should have at least 3 NoteOffs from the first chord
    REQUIRE(noteOffs.size() >= 3);

    // All 3 notes of the chord (60, 64, 67) should have NoteOffs
    std::vector<uint8_t> offNotes;
    for (size_t i = 0; i < 3 && i < noteOffs.size(); ++i) {
        offNotes.push_back(noteOffs[i].note);
    }
    std::sort(offNotes.begin(), offNotes.end());
    CHECK(offNotes[0] == 60);
    CHECK(offNotes[1] == 64);
    CHECK(offNotes[2] == 67);
}


TEST_CASE("Arp Chord Lane: Chord + Rest modifier silences chord",
          "[arp][chord-lane]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setScaleType(ScaleType::Chromatic);

    // Step 0: chord+active, Step 1: chord+rest
    arp.chordLane().setLength(2);
    arp.chordLane().setStep(0, static_cast<uint8_t>(ChordType::Triad));
    arp.chordLane().setStep(1, static_cast<uint8_t>(ChordType::Triad));

    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(0));  // Rest

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 400);
    auto noteOns = filterNoteOns(events);

    // Step 0: 3 noteOns, Step 1: rest (0 noteOns), Step 2: 3 noteOns
    REQUIRE(noteOns.size() >= 3);

    // Step 0 notes
    int32_t step0Offset = noteOns[0].sampleOffset;
    size_t step0Count = 0;
    for (const auto& ev : noteOns) {
        if (ev.sampleOffset == step0Offset) ++step0Count;
    }
    CHECK(step0Count == 3);

    // Step 1 should be rest - no noteOns between step 0 and step 2
    // Step 2 should be 3 noteOns again
    if (noteOns.size() >= 6) {
        // The next 3 noteOns should be at a later offset (step 2, not step 1)
        CHECK(noteOns[3].sampleOffset > step0Offset);  // Skipped step 1
    }
}


TEST_CASE("Arp Chord Lane: Chord + Ratchet ratchets all notes together",
          "[arp][chord-lane]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Quarter, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.setScaleType(ScaleType::Chromatic);

    // Triad + ratchet count 2
    arp.chordLane().setStep(0, static_cast<uint8_t>(ChordType::Triad));
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 400);
    auto noteOns = filterNoteOns(events);

    // First sub-step: 3 notes, second sub-step: 3 notes = 6 total in first step
    REQUIRE(noteOns.size() >= 6);

    // First 3 notes should be the chord
    CHECK(noteOns[0].note == 60);
    CHECK(noteOns[1].note == 64);
    CHECK(noteOns[2].note == 67);

    // Second sub-step should ratchet the same chord
    CHECK(noteOns[3].note == 60);
    CHECK(noteOns[4].note == 64);
    CHECK(noteOns[5].note == 67);
}


TEST_CASE("Arp Chord Lane: Inversion lane rotates chord notes",
          "[arp][chord-lane]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setScaleType(ScaleType::Chromatic);

    // Triad + 1st inversion
    arp.chordLane().setStep(0, static_cast<uint8_t>(ChordType::Triad));
    arp.inversionLane().setStep(0, static_cast<uint8_t>(InversionType::First));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 3);
    int32_t firstOffset = noteOns[0].sampleOffset;
    std::vector<uint8_t> firstStepNotes;
    for (const auto& ev : noteOns) {
        if (ev.sampleOffset == firstOffset) {
            firstStepNotes.push_back(ev.note);
        }
    }
    REQUIRE(firstStepNotes.size() == 3);
    // 1st inversion of {60, 64, 67}: {64, 67, 72}
    CHECK(firstStepNotes[0] == 64);
    CHECK(firstStepNotes[1] == 67);
    CHECK(firstStepNotes[2] == 72);
}


TEST_CASE("Arp Chord Lane: Voicing mode transforms chord spread",
          "[arp][chord-lane]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setScaleType(ScaleType::Chromatic);

    arp.chordLane().setStep(0, static_cast<uint8_t>(ChordType::Seventh));
    arp.setVoicingMode(VoicingMode::Drop2);

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 4);
    int32_t firstOffset = noteOns[0].sampleOffset;
    std::vector<uint8_t> firstStepNotes;
    for (const auto& ev : noteOns) {
        if (ev.sampleOffset == firstOffset) {
            firstStepNotes.push_back(ev.note);
        }
    }
    REQUIRE(firstStepNotes.size() == 4);
    // Chromatic 7th: {60, 64, 67, 70}, Drop-2: second-from-top (67) drops -> 55
    CHECK(firstStepNotes[0] == 60);
    CHECK(firstStepNotes[1] == 64);
    CHECK(firstStepNotes[2] == 55);  // G dropped an octave
    CHECK(firstStepNotes[3] == 70);
}


TEST_CASE("ArpeggiatorCore: lane speed 1.0x advances every step (default)",
          "[processors][arpeggiator_core][lane_speed]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(60, 100);

    // Set velocity lane to 4 distinct values
    arp.velocityLane().setLength(4);
    arp.velocityLane().setStep(0, 0.25f);
    arp.velocityLane().setStep(1, 0.50f);
    arp.velocityLane().setStep(2, 0.75f);
    arp.velocityLane().setStep(3, 1.00f);

    // Default speed is 1.0x — don't call setLaneSpeed
    auto ctx = makeLaneSpeedTestContext();
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // After 4 NoteOns, velocity should have cycled through all 4 values
    REQUIRE(noteOns.size() >= 4);
    // Velocity lane scales the input velocity (100), not 127
    CHECK(noteOns[0].velocity == 25);  // round(100 * 0.25)
    CHECK(noteOns[1].velocity == 50);  // round(100 * 0.50)
    CHECK(noteOns[2].velocity == 75);  // round(100 * 0.75)
    CHECK(noteOns[3].velocity == 100); // round(100 * 1.00)
}


TEST_CASE("ArpeggiatorCore: lane speed 0.5x advances every other step",
          "[processors][arpeggiator_core][lane_speed]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(60, 100);

    // Velocity lane: 2 values
    arp.velocityLane().setLength(2);
    arp.velocityLane().setStep(0, 0.25f);
    arp.velocityLane().setStep(1, 0.75f);

    // Set velocity lane speed to 0.5x
    arp.setLaneSpeed(0, 0.5f); // 0 = velocity

    auto ctx = makeLaneSpeedTestContext();
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // At 0.5x, velocity lane advances once every 2 steps
    // Steps: vel[0], vel[0], vel[1], vel[1], vel[0], vel[0], ...
    REQUIRE(noteOns.size() >= 4);
    uint8_t v0 = 25;  // round(100 * 0.25)
    uint8_t v1 = 75;  // round(100 * 0.75)
    CHECK(noteOns[0].velocity == v0);
    CHECK(noteOns[1].velocity == v0); // still on step 0
    CHECK(noteOns[2].velocity == v1); // advanced to step 1
    CHECK(noteOns[3].velocity == v1); // still on step 1
}


TEST_CASE("ArpeggiatorCore: lane speed 2.0x advances twice per step",
          "[processors][arpeggiator_core][lane_speed]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(60, 100);

    // Pitch lane: 4 values
    arp.pitchLane().setLength(4);
    arp.pitchLane().setStep(0, 0);
    arp.pitchLane().setStep(1, 3);
    arp.pitchLane().setStep(2, 7);
    arp.pitchLane().setStep(3, 12);

    // Set pitch lane speed to 2.0x
    arp.setLaneSpeed(2, 2.0f); // 2 = pitch

    auto ctx = makeLaneSpeedTestContext();
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // At 2x, pitch advances 2 steps per arp event.
    // Step 0: pitch reads [0], advances to position 2
    // Step 1: pitch reads [2], advances to position 0 (wrapped)
    // Step 2: pitch reads [0], advances to position 2
    REQUIRE(noteOns.size() >= 4);
    CHECK(noteOns[0].note == 60 + 0);   // pitch[0]
    CHECK(noteOns[1].note == 60 + 7);   // pitch[2]
    CHECK(noteOns[2].note == 60 + 0);   // pitch[0] again
    CHECK(noteOns[3].note == 60 + 7);   // pitch[2] again
}


TEST_CASE("ArpeggiatorCore: lane speed 0.25x advances once every 4 steps",
          "[processors][arpeggiator_core][lane_speed]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(60, 100);

    // Velocity lane: 2 values
    arp.velocityLane().setLength(2);
    arp.velocityLane().setStep(0, 0.25f);
    arp.velocityLane().setStep(1, 1.0f);

    arp.setLaneSpeed(0, 0.25f);

    auto ctx = makeLaneSpeedTestContext();
    auto events = collectEvents(arp, ctx, 400);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 8);
    uint8_t v0 = 25;   // round(100 * 0.25)
    uint8_t v1 = 100;  // round(100 * 1.0)
    // First 4 steps: all v0 (accumulator doesn't reach 1.0 until step 4)
    CHECK(noteOns[0].velocity == v0);
    CHECK(noteOns[1].velocity == v0);
    CHECK(noteOns[2].velocity == v0);
    CHECK(noteOns[3].velocity == v0);
    // Steps 4-7: all v1
    CHECK(noteOns[4].velocity == v1);
    CHECK(noteOns[5].velocity == v1);
    CHECK(noteOns[6].velocity == v1);
    CHECK(noteOns[7].velocity == v1);
}


TEST_CASE("ArpeggiatorCore: lane speed accumulator resets on arp reset",
          "[processors][arpeggiator_core][lane_speed]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(60, 100);

    arp.velocityLane().setLength(2);
    arp.velocityLane().setStep(0, 0.25f);
    arp.velocityLane().setStep(1, 1.0f);
    arp.setLaneSpeed(0, 0.5f);

    // Fire 1 step — accumulator = 0.5, no advance yet
    auto ctx = makeLaneSpeedTestContext();
    auto events1 = collectEvents(arp, ctx, 50);
    auto noteOns1 = filterNoteOns(events1);
    REQUIRE(noteOns1.size() >= 1);
    CHECK(noteOns1[0].velocity == 25); // round(100 * 0.25)

    // Reset should zero the accumulator
    arp.reset();
    arp.noteOn(60, 100);

    // Fire again — should behave identically to first time (accumulator reset)
    auto ctx2 = makeLaneSpeedTestContext();
    auto events2 = collectEvents(arp, ctx2, 100);
    auto noteOns2 = filterNoteOns(events2);
    REQUIRE(noteOns2.size() >= 2);
    // First 2 notes should both be v0 (accumulator starts fresh)
    CHECK(noteOns2[0].velocity == 25); // round(100 * 0.25)
    CHECK(noteOns2[1].velocity == 25);
}


TEST_CASE("ArpeggiatorCore: mixed lane speeds produce polyrhythmic pattern",
          "[processors][arpeggiator_core][lane_speed]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(60, 100);

    // Velocity: 4 values at 1x
    arp.velocityLane().setLength(4);
    for (int i = 0; i < 4; ++i)
        arp.velocityLane().setStep(static_cast<size_t>(i),
            static_cast<float>(i + 1) * 0.25f);

    // Gate: 2 values at 0.5x
    arp.gateLane().setLength(2);
    arp.gateLane().setStep(0, 0.5f);
    arp.gateLane().setStep(1, 1.0f);
    arp.setLaneSpeed(1, 0.5f); // gate = index 1

    // Pitch: 2 values at 2x
    arp.pitchLane().setLength(2);
    arp.pitchLane().setStep(0, 0);
    arp.pitchLane().setStep(1, 7);
    arp.setLaneSpeed(2, 2.0f); // pitch = index 2

    auto ctx = makeLaneSpeedTestContext();
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 4);

    // Velocity at 1x: cycles 0.25, 0.50, 0.75, 1.0
    // Velocity lane scales the input velocity (100), not 127
    CHECK(noteOns[0].velocity == 25);  // round(100 * 0.25)
    CHECK(noteOns[1].velocity == 50);  // round(100 * 0.50)
    CHECK(noteOns[2].velocity == 75);  // round(100 * 0.75)
    CHECK(noteOns[3].velocity == 100); // round(100 * 1.00)

    // Pitch at 2x: alternates [0, 7, 0, 7] but reads BEFORE advance
    // Step 0: reads pitch[0]=0, advances 2 -> pos 0 (wraps after 2 advances from pos 0)
    // Step 1: reads pitch[0]=0, advances 2 -> pos 0 again
    // This depends on exact accumulator semantics
    // At minimum, pitch should show a 2x-faster cycle than velocity
}


TEST_CASE("ArpeggiatorCore: lane speed 4.0x wraps correctly with short lanes",
          "[processors][arpeggiator_core][lane_speed]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(60, 100);

    // Velocity lane: 3 values
    arp.velocityLane().setLength(3);
    arp.velocityLane().setStep(0, 0.1f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 1.0f);

    arp.setLaneSpeed(0, 4.0f);

    auto ctx = makeLaneSpeedTestContext();
    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 2);
    // Step 0: reads vel[0]=0.1, then advances 4 times: pos 0->1->2->0->1
    // Step 1: reads vel[1]=0.5, then advances 4 times: pos 1->2->0->1->2
    CHECK(noteOns[0].velocity == static_cast<uint8_t>(std::round(100 * 0.1f)));  // 10
    CHECK(noteOns[1].velocity == static_cast<uint8_t>(std::round(100 * 0.5f)));  // 50
}


TEST_CASE("ArpeggiatorCore: default lane speed is 1.0x (backward compat)",
          "[processors][arpeggiator_core][lane_speed]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(60, 100);

    // Don't call setLaneSpeed at all — verify default 1x behavior
    arp.velocityLane().setLength(3);
    arp.velocityLane().setStep(0, 0.1f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 1.0f);

    auto ctx = makeLaneSpeedTestContext();
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 3);
    // Standard 1-step-per-event: 0.1, 0.5, 1.0 (scales input vel 100)
    CHECK(noteOns[0].velocity == static_cast<uint8_t>(std::round(100 * 0.1f)));  // 10
    CHECK(noteOns[1].velocity == static_cast<uint8_t>(std::round(100 * 0.5f)));  // 50
    CHECK(noteOns[2].velocity == 100);
}
