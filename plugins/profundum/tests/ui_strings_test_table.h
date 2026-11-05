#pragma once

// ==============================================================================
// Profundum Phase 12 - UI string and value table for the editor tests (T027)
// ==============================================================================
// Namespace ProfundumPluginTest. Every literal below was extracted byte for byte from the
// ratified design documents, never retyped:
//   - kMainTooltips: specs/profundum-phase12-ui/design/main-page.md section 3.1 / 3.2 (D1)
//     and the HarmonicBarView #1 sentence under 3.2;
//   - kAdvancedTooltips, the view tooltips and the ruled statements:
//     specs/profundum-phase12-ui/design/advanced-page.md section 5.1 / 5.2 (D2); "+ MONO-GUARANTEE"
//     rows carry their sentence, one space, then the MONO-GUARANTEE literal;
//   - kFr004Members: advanced-page.md section 1.2 (exactly spec FR-004);
//   - kD6UiOnlyTags: design/interaction-automation.md section 1.1 (plan 4.7);
//   - the D7 values: design/accessibility-scaling.md sections 1, 2, 4, 5.
// Non-ASCII code points are spelled as UTF-8 byte escapes (no /utf-8 compiler flag needed).
// ==============================================================================

#include <array>
#include <cstddef>
#include <cstdint>

namespace ProfundumPluginTest {

struct UiTooltip {
    std::int32_t tag;  // a registered ID (< 9000) or a session tag
    const char* text;
};

/// D1: every Main-region control (the header and page-main), by bound ID or session tag.
inline constexpr std::array<UiTooltip, 26> kMainTooltips{{
    UiTooltip{.tag = 1, .text = "Mono plays one note at a time, with glide and legato. Poly plays chords; the voice count is on the Advanced page."},
    UiTooltip{.tag = 100, .text = "Weight: more depth, more body and a louder anchor together. Centre is neutral."},
    UiTooltip{.tag = 101, .text = "Dark \xE2\x86\x94 Bright: moves the edge, the spectral shift and the character tone together. Centre is neutral."},
    UiTooltip{.tag = 103, .text = "Clean \xE2\x86\x94 Violent: more drive, more fold and more character together. Centre is neutral."},
    UiTooltip{.tag = 104, .text = "Mass: how heavy the note feels, from a pure, sine-like sub at 0 to an enormous driven bass at 100. 50 is neutral; the mod wheel and channel pressure push it higher."},
    UiTooltip{.tag = 105, .text = "Bass Morph: moves the sound from endpoint A (0 %) to endpoint B (100 %). Automate it to travel between two complete sounds."},
    UiTooltip{.tag = 200, .text = "Depth: high keeps the energy in the fundamental; low spreads it up through the harmonics."},
    UiTooltip{.tag = 201, .text = "Body: the weight of the low harmonics that make the note sound full."},
    UiTooltip{.tag = 204, .text = "Edge: the bite of the upper harmonics. Turn it up for buzz and presence, down for a smooth, round bass."},
    UiTooltip{.tag = 300, .text = "Anchor Waveform: the shape of the sub tone under the mass: Sine, Triangle, Rounded Sine, Asymmetric Sine or Octave Down."},
    UiTooltip{.tag = 302, .text = "Anchor Level: how loud the stable sub tone under the mass is. It stays centred and clean whatever the mass does."},
    UiTooltip{.tag = 400, .text = "Drop: each note starts this many semitones above its pitch and falls onto it (0 to 48). 0 is off; its time and shape are on the Advanced page."},
    UiTooltip{.tag = 412, .text = "Glide: time to slide between notes in Mono. The readout unit follows Glide Mode on the Advanced page."},
    UiTooltip{.tag = 500, .text = "Harmonic Fold: grows new harmonics, from soft saturation through harmonic compression to folding at 100. 0 is clean; the anchor stays clean unless Anchor Fold Feed sends it in."},
    UiTooltip{.tag = 900, .text = "Motion: what moves the sound over time. Its target, amount and timing are on the Advanced page."},
    UiTooltip{.tag = 1000, .text = "Morph Behaviour: how the anchor follows the Bass Morph. Hold keeps A's anchor, Blend follows the morph, Yield hands over late."},
    UiTooltip{.tag = 1001, .text = "Edit Target: whether the knobs edit endpoint A or endpoint B. It changes what you edit, not what you hear. Not automatable."},
    UiTooltip{.tag = 9100, .text = "Switch between the Main and Advanced pages."},
    UiTooltip{.tag = 9200, .text = "Compare: hear endpoint A, the morph, or endpoint B. Pressing A or B also sets Edit Target."},
    UiTooltip{.tag = 9201, .text = "Copy endpoint A's sound onto B. The name is not copied."},
    UiTooltip{.tag = 9202, .text = "Copy endpoint B's sound onto A. The name is not copied."},
    UiTooltip{.tag = 9210, .text = "Name of endpoint A. Saved with the preset."},
    UiTooltip{.tag = 9211, .text = "Name of endpoint B. Saved with the preset."},
    UiTooltip{.tag = 9220, .text = "How endpoint A sounds: shape, colour and force."},
    UiTooltip{.tag = 9221, .text = "How endpoint B sounds: shape, colour and force."},
    UiTooltip{.tag = 9300, .text = "Editor zoom: 100, 125, 150 or 200 %. Kept for the session, not saved with the project."},
}};

/// D2 5.2: every Advanced control, by bound ID or session tag.
inline constexpr std::array<UiTooltip, 80> kAdvancedTooltips{{
    UiTooltip{.tag = 0, .text = "Output Gain: final level after the limiter, from silence up to 0 dB. It only turns down, so it never pushes past the Output Ceiling."},
    UiTooltip{.tag = 2, .text = "Polyphony: how many notes can sound at once in Poly (1 to 8). Mono ignores it."},
    UiTooltip{.tag = 3, .text = "Output Ceiling: the highest true-peak level the output reaches (\xE2\x88\x92" "12 to 0 dBTP, default \xE2\x88\x92" "1). The limiter holds everything under it."},
    UiTooltip{.tag = 102, .text = "Tight \xE2\x86\x94 Loose: toward Tight, shorter attack, decay and release, a harder transient and more Compression; toward Loose the reverse. Compression has no control of its own."},
    UiTooltip{.tag = 200, .text = "Depth: how steeply the harmonics fall away above the fundamental. Higher is rounder, lower is richer."},
    UiTooltip{.tag = 201, .text = "Body: lifts the low harmonics (about 2 to 8) that carry the bass's body."},
    UiTooltip{.tag = 202, .text = "Body Curvature: narrows or broadens the region Body lifts."},
    UiTooltip{.tag = 203, .text = "Body Emphasis: at 0 all harmonics sound; toward \xE2\x88\x92" "100 % the even harmonics fade for a hollower tone."},
    UiTooltip{.tag = 204, .text = "Edge: lifts the upper harmonics that give the bass its bite."},
    UiTooltip{.tag = 205, .text = "Spectral Shift: moves the Body and Edge regions down (darker) or up (brighter) together."},
    UiTooltip{.tag = 206, .text = "Excitation Mix: blends the driven excitation into the clean tone. 0 is clean."},
    UiTooltip{.tag = 207, .text = "Excitation Drive: how hard the excitation stage is driven. More drive grows more new harmonics; it is heard only when Excitation Mix is above 0."},
    UiTooltip{.tag = 208, .text = "Excitation Asymmetry: biases the drive for uneven, even-harmonic grit."},
    UiTooltip{.tag = 209, .text = "Stiffness: bends the upper harmonics sharp of the harmonic series, as a stiff string or a metal bar does, for a metallic, bell-edged bass. Higher notes are stiffer; 0 keeps the exact series."},
    UiTooltip{.tag = 210, .text = "Phase: turns the upper harmonics against the fundamental. The clean tone hardly changes, but the waveform grows peakier, so Fold and Character bite harder. 0 is the smoothest."},
    UiTooltip{.tag = 300, .text = "Anchor Waveform: the shape of the sub tone: Sine (purest), Triangle, Rounded Sine, Asymmetric Sine or Octave Down (one octave below the note)."},
    UiTooltip{.tag = 301, .text = "Anchor Mode: how the sub tone behaves: Lock stays constant, Follow tracks the pitch you hear, Punch is louder at the note start, Ghost fades in after it."},
    UiTooltip{.tag = 302, .text = "Anchor Level: level of the pure sub tone under the bass. It stays centred and outside the drive stages."},
    UiTooltip{.tag = 303, .text = "Punch Boost: extra sub level at the note start in Punch mode, 0 to +12 dB."},
    UiTooltip{.tag = 304, .text = "Punch Time: how long the Punch boost lasts, 5 to 250 ms."},
    UiTooltip{.tag = 305, .text = "Ghost Time: in Ghost mode, how long the sub tone takes to emerge after the note starts, 1 to 100 ms."},
    UiTooltip{.tag = 306, .text = "Anchor Fold Feed: sends some of the sub tone into Harmonic Fold, so the fold works on the sub too. 0 keeps the sub clean."},
    UiTooltip{.tag = 400, .text = "Drop Amount: how far the pitch falls into each note, in semitones."},
    UiTooltip{.tag = 401, .text = "Drop Time: how long the pitch drop takes, from 1 ms to 4 s."},
    UiTooltip{.tag = 402, .text = "Drop Shape: the curve of the pitch drop: 0 % exponential (fast, then slow), 33 % linear, 67 % logarithmic (slow, then fast), 100 % elastic (it overshoots and settles)."},
    UiTooltip{.tag = 403, .text = "Drop Rebound: how much the pitch overshoots and settles at the end of the drop."},
    UiTooltip{.tag = 410, .text = "Mono Priority: which held note sounds in Mono: Last, Low or High."},
    UiTooltip{.tag = 411, .text = "Legato: overlapping notes glide without restarting the envelope."},
    UiTooltip{.tag = 412, .text = "Portamento: glide time between notes, from 0 (off) to 10 s. Glide Mode decides whether this is a time or a rate."},
    UiTooltip{.tag = 413, .text = "Portamento Mode: glide on every note, or only between overlapping notes."},
    UiTooltip{.tag = 414, .text = "Glide Mode: Constant Time takes the same time for any interval, Constant Rate takes longer for wider intervals, Quantized steps through the semitones."},
    UiTooltip{.tag = 415, .text = "Note Memory: what carries over between notes in Mono: Always Retrigger restarts everything, Legato carries on across overlapping notes, Envelope Retrigger restarts only the envelope, Oscillator Continuous keeps the waveform running."},
    UiTooltip{.tag = 416, .text = "Note Retrigger: Reset restarts the note's movement, Continue carries it on."},
    UiTooltip{.tag = 417, .text = "Pitch Bend Range: how far the bend wheel bends, 0 to 24 semitones each way."},
    UiTooltip{.tag = 420, .text = "Attack: time to full level after a note starts, 0.1 ms to 10 s."},
    UiTooltip{.tag = 421, .text = "Decay: time to fall from full level to the Sustain level, 0.1 ms to 10 s."},
    UiTooltip{.tag = 422, .text = "Sustain: level held while the key is down, 0 to 100 %."},
    UiTooltip{.tag = 423, .text = "Release: time to silence after the key is released, 0.1 ms to 10 s."},
    UiTooltip{.tag = 424, .text = "Attack Curve: bends the attack segment. 0 is a straight line, + is exponential, \xE2\x88\x92 is logarithmic."},
    UiTooltip{.tag = 425, .text = "Decay Curve: bends the decay segment. 0 is a straight line, + is exponential, \xE2\x88\x92 is logarithmic."},
    UiTooltip{.tag = 426, .text = "Release Curve: bends the release segment. 0 is a straight line, + is exponential, \xE2\x88\x92 is logarithmic."},
    UiTooltip{.tag = 500, .text = "Harmonic Fold: folds the waveform back on itself to add new harmonics. 0 is clean."},
    UiTooltip{.tag = 501, .text = "Shape: the SHAPE stage, one control across three characters: 0 % ROUND (soft and dark), 50 % HOLLOW (resonant and scooped), 100 % BITE (bright and edgy)."},
    UiTooltip{.tag = 600, .text = "Character: the type of colouring stage: Tube, Wavefold, Rectify, Bite, Crush or Fracture. Character Amount sets how much is heard. In WIDE, left and right get slightly different Character settings, so the sub is a little less mono than in Solid or Spread."},
    UiTooltip{.tag = 601, .text = "Character Amount: how much of the Character stage is heard. 0 is off. In WIDE, left and right get slightly different Character settings, so the sub is a little less mono than in Solid or Spread."},
    UiTooltip{.tag = 602, .text = "Character Tone: darkens or brightens the Character stage. In WIDE, left and right get slightly different Character settings, so the sub is a little less mono than in Solid or Spread."},
    UiTooltip{.tag = 700, .text = "Transient Attack: boosts or softens the start of each note, \xE2\x88\x92" "12 to +12 dB."},
    UiTooltip{.tag = 701, .text = "Transient Sustain: boosts or softens the body that follows the start, \xE2\x88\x92" "12 to +12 dB."},
    UiTooltip{.tag = 800, .text = "Image Mode: Solid is mono, Spread widens the upper harmonics above the crossover, Wide also gives left and right slightly different Character settings. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{.tag = 801, .text = "Spread: overall stereo width above the crossover. 0 is centred. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{.tag = 802, .text = "Gravity: how far below the crossover the width begins. At 0 the width reaches lowest; at 100 % the low band is pulled firmly to the centre. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{.tag = 803, .text = "Crossover: the frequency below which the bass stays centred, 60 to 250 Hz. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{.tag = 804, .text = "Curve: how quickly the width rises between the low and high knees. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{.tag = 805, .text = "Field Motion Depth: how much the width moves over time. 0 holds it still. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{.tag = 806, .text = "Field Motion Rate: speed of the width movement. Cycle runs from 0.05 to 1 Hz; Drift wanders from slowly to quickly. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{.tag = 807, .text = "Field Motion Source: Drift wanders, Cycle repeats. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{.tag = 808, .text = "Field Sync: lock the width movement to the host tempo; Field Sync Rate then sets the note value. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{.tag = 809, .text = "Field Sync Rate: note value of the synced width movement. Synced rates above 1 Hz fold into 0.5\xE2\x80\x93" "1 Hz. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{.tag = 900, .text = "Motion Source: what moves the target: Env rises and falls with each note, Cycle repeats, Drift wanders. Off disables Motion."},
    UiTooltip{.tag = 901, .text = "Motion Target: what Motion moves: Mass, Depth, Body, Edge or Shift."},
    UiTooltip{.tag = 902, .text = "Motion Amount: how far and in which direction Motion moves its target."},
    UiTooltip{.tag = 903, .text = "Motion Depth: scales all of Motion at once, 0 to 100 %. Motion Amount sets the direction and size for the target."},
    UiTooltip{.tag = 904, .text = "Cycle Rate: speed of the Cycle source, 0.01 to 20 Hz, when Cycle Sync is off."},
    UiTooltip{.tag = 905, .text = "Cycle Shape: waveform of the Cycle source: Sine, Triangle, Sawtooth, Square, S&H or Smooth Random."},
    UiTooltip{.tag = 906, .text = "Cycle Sync: lock the Cycle to the host tempo; Cycle Sync Rate then sets the note value."},
    UiTooltip{.tag = 907, .text = "Cycle Sync Rate: note value of one synced Cycle, 1/64T to 4/1D."},
    UiTooltip{.tag = 908, .text = "Cycle Phase: where in its waveform the Cycle starts, 0 to 360\xC2\xB0."},
    UiTooltip{.tag = 909, .text = "Cycle Retrigger: restart the Cycle at its start phase on each new note. Off lets it run freely."},
    UiTooltip{.tag = 910, .text = "Cycle Fade-In: time for the Cycle to reach full depth after a note starts, 0 to 5 s."},
    UiTooltip{.tag = 911, .text = "Motion Polarity: Bipolar ENV rests at \xE2\x88\x92" "amount between notes; Unipolar rests at the set value."},
    UiTooltip{.tag = 912, .text = "Drift Smoothness: how smoothly the Drift source wanders. Higher is gentler."},
    UiTooltip{.tag = 913, .text = "Env Attack: rise time of the Env source, 0.1 ms to 10 s."},
    UiTooltip{.tag = 914, .text = "Env Decay: fall time of the Env source back to rest, 0.1 ms to 10 s."},
    UiTooltip{.tag = 1000, .text = "Morph behaviour of the sub tone: Hold keeps A's sub, Blend follows the morph, Yield hands over late."},
    UiTooltip{.tag = 1002, .text = "Morph smoothing: how quickly the sound follows a change of the morph position, 5 ms to 2 s. Longer glides smoothly between A and B."},
    UiTooltip{.tag = 9110, .text = "Advanced page sections: choose which group of controls to show."},
    UiTooltip{.tag = 9203, .text = "Compare: hear A, the morph, or B. Choosing A or B also edits that side."},
    UiTooltip{.tag = 9204, .text = "Copy A's sound to B. Names are not copied."},
    UiTooltip{.tag = 9205, .text = "Copy B's sound to A. Names are not copied."},
    UiTooltip{.tag = 9300, .text = "Window size: 100, 125, 150 or 200 %. Kept for the session, not saved with the project."},
}};

/// The display-only views and the dimming label (D1 3.2, D2 5.2).
inline constexpr const char* kBarsMainTooltip = "Measured harmonics of the sound you hear, with the anchor in its own lane.";
inline constexpr const char* kBarsCoreTooltip = "The measured harmonics of the sounding note after Fold and Character, before the amp envelope, in dB below the strongest component. Shows the resolved sound, including morph, macros and Motion.";
inline constexpr const char* kSpreadCurveTooltip = "How wide each frequency is for the endpoint being edited: 0 is mono, 1 is fully wide. Field Motion is not drawn.";
inline constexpr const char* kAdsrDisplayTooltip = "Amp envelope: drag the points to set Attack, Decay, Sustain and Release.";
inline constexpr const char* kHeldAtATooltip = "Hold is on and you are editing B: B's sub settings are stored but not heard.";

/// D2 5.1 ruled statements (SC-005).
inline constexpr const char* kMonoGuaranteeText = "Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB.";  // 800 - 809 (FR-024)
inline constexpr const char* kWideCaveatText = "In WIDE, left and right get slightly different Character settings, so the sub is a little less mono than in Solid or Spread.";  // 600 - 602 (FR-024)
inline constexpr const char* kFieldSyncFoldingText = "Synced rates above 1 Hz fold into 0.5\xE2\x80\x93" "1 Hz.";  // 809 (FR-051)
inline constexpr const char* kBipolarRestText = "Bipolar ENV rests at \xE2\x88\x92" "amount between notes; Unipolar rests at the set value.";  // 911 (FR-052)
inline constexpr const char* kCompressionWord = "Compression";  // 102 (FR-060)

/// FR-012 / SC-005 label texts.
inline constexpr const char* kMorphBehaviourText = "MORPH BEHAVIOUR";
inline constexpr const char* kAnchorModeText = "ANCHOR MODE";
inline constexpr const char* kHeldAtAText = "Held at A";
/// D2 1.2 / D7 7: the SETTINGS note.
inline constexpr const char* kNoRunLoopNote = "No host run loop: the spectrum view stays empty";

/// FR-020 (Main page, case-insensitive; D1 6 rules no word in) and FR-093 (everywhere).
inline constexpr std::array<const char*, 4> kForbiddenMainWords{"oscillator", "filter", "LFO", "partial"};
inline constexpr std::array<const char*, 4> kBannedLabels{"Oversampling", "Quality", "Low-End Protection", "Parameter Curve"};

/// D6 (interaction-automation.md 1.1; plan 4.7): the UI-only session tags.
inline constexpr std::array<std::int32_t, 13> kD6UiOnlyTags{9100, 9110, 9200, 9201, 9202, 9203, 9204, 9205, 9210, 9211, 9220, 9221, 9300};

/// FR-011 / D1 3.1: the Main bound-ID set (no image-mode mirror, D1 5).
inline constexpr std::array<std::int32_t, 17> kFr011MainIds{1, 100, 101, 103, 104, 105, 200, 201, 204, 300, 302, 400, 412, 500, 900, 1000, 1001};

/// FR-033: the hidden performance IDs; never bound.
inline constexpr std::array<std::int32_t, 4> kHiddenPerformanceIds{4, 5, 6, 7};

/// FR-033 / SC-003: the eight Phase 10 control-tag names keep their values
/// (git show 40f6ebca:plugins/profundum/resources/editor.uidesc, lines 19 - 28).
struct TagName {
    const char* name;
    std::int32_t tag;
};
inline constexpr std::array<TagName, 8> kPhase10TagNames{{
    TagName{.name = "OutputGain", .tag = 0},
    TagName{.name = "VoiceMode", .tag = 1},
    TagName{.name = "Polyphony", .tag = 2},
    TagName{.name = "OutputCeiling", .tag = 3},
    TagName{.name = "MacroWeight", .tag = 100},
    TagName{.name = "MacroDarkBright", .tag = 101},
    TagName{.name = "MacroTightLoose", .tag = 102},
    TagName{.name = "MacroCleanViolent", .tag = 103},
}};

/// FR-004 (D2 1.2): container label -> bound ID or session tag; an ID listed under two containers is bound in both.
struct SectionMember {
    const char* container;
    std::int32_t tag;
};
inline constexpr std::array<SectionMember, 81> kFr004Members{{
    SectionMember{.container = "section-core-spectrum", .tag = 200},
    SectionMember{.container = "section-core-spectrum", .tag = 201},
    SectionMember{.container = "section-core-spectrum", .tag = 202},
    SectionMember{.container = "section-core-spectrum", .tag = 203},
    SectionMember{.container = "section-core-spectrum", .tag = 204},
    SectionMember{.container = "section-core-spectrum", .tag = 209},   // Phase 15
    SectionMember{.container = "section-core-spectrum", .tag = 210},   // Phase 15
    SectionMember{.container = "section-core-spectrum", .tag = 205},
    SectionMember{.container = "section-core-spectrum", .tag = 500},
    SectionMember{.container = "section-core-spectrum", .tag = 102},
    SectionMember{.container = "section-sub-anchor", .tag = 302},
    SectionMember{.container = "section-sub-anchor", .tag = 300},
    SectionMember{.container = "section-sub-anchor", .tag = 301},
    SectionMember{.container = "section-sub-anchor", .tag = 303},
    SectionMember{.container = "section-sub-anchor", .tag = 304},
    SectionMember{.container = "section-sub-anchor", .tag = 305},
    SectionMember{.container = "section-sub-anchor", .tag = 306},
    SectionMember{.container = "section-sub-anchor", .tag = 1000},
    SectionMember{.container = "section-excitation-fold", .tag = 207},
    SectionMember{.container = "section-excitation-fold", .tag = 206},
    SectionMember{.container = "section-excitation-fold", .tag = 208},
    SectionMember{.container = "section-excitation-fold", .tag = 500},
    SectionMember{.container = "section-shape-character", .tag = 501},
    SectionMember{.container = "section-shape-character", .tag = 600},
    SectionMember{.container = "section-shape-character", .tag = 601},
    SectionMember{.container = "section-shape-character", .tag = 602},
    SectionMember{.container = "section-mass", .tag = 700},
    SectionMember{.container = "section-mass", .tag = 701},
    SectionMember{.container = "section-pitch-envelopes", .tag = 400},
    SectionMember{.container = "section-pitch-envelopes", .tag = 401},
    SectionMember{.container = "section-pitch-envelopes", .tag = 402},
    SectionMember{.container = "section-pitch-envelopes", .tag = 403},
    SectionMember{.container = "section-pitch-envelopes", .tag = 420},
    SectionMember{.container = "section-pitch-envelopes", .tag = 421},
    SectionMember{.container = "section-pitch-envelopes", .tag = 422},
    SectionMember{.container = "section-pitch-envelopes", .tag = 423},
    SectionMember{.container = "section-pitch-envelopes", .tag = 424},
    SectionMember{.container = "section-pitch-envelopes", .tag = 425},
    SectionMember{.container = "section-pitch-envelopes", .tag = 426},
    SectionMember{.container = "section-pitch-envelopes", .tag = 410},
    SectionMember{.container = "section-pitch-envelopes", .tag = 411},
    SectionMember{.container = "section-pitch-envelopes", .tag = 412},
    SectionMember{.container = "section-pitch-envelopes", .tag = 413},
    SectionMember{.container = "section-pitch-envelopes", .tag = 414},
    SectionMember{.container = "section-pitch-envelopes", .tag = 415},
    SectionMember{.container = "section-pitch-envelopes", .tag = 416},
    SectionMember{.container = "section-pitch-envelopes", .tag = 417},
    SectionMember{.container = "section-stereo-output", .tag = 800},
    SectionMember{.container = "section-stereo-output", .tag = 801},
    SectionMember{.container = "section-stereo-output", .tag = 802},
    SectionMember{.container = "section-stereo-output", .tag = 803},
    SectionMember{.container = "section-stereo-output", .tag = 804},
    SectionMember{.container = "section-stereo-output", .tag = 805},
    SectionMember{.container = "section-stereo-output", .tag = 806},
    SectionMember{.container = "section-stereo-output", .tag = 807},
    SectionMember{.container = "section-stereo-output", .tag = 808},
    SectionMember{.container = "section-stereo-output", .tag = 809},
    SectionMember{.container = "section-stereo-output", .tag = 0},
    SectionMember{.container = "section-stereo-output", .tag = 2},
    SectionMember{.container = "section-stereo-output", .tag = 3},
    SectionMember{.container = "section-morph-behaviour", .tag = 1000},
    SectionMember{.container = "section-morph-behaviour", .tag = 1002},
    SectionMember{.container = "section-morph-behaviour", .tag = 9203},
    SectionMember{.container = "section-morph-behaviour", .tag = 9204},
    SectionMember{.container = "section-morph-behaviour", .tag = 9205},
    SectionMember{.container = "tab-modulation", .tag = 900},
    SectionMember{.container = "tab-modulation", .tag = 901},
    SectionMember{.container = "tab-modulation", .tag = 902},
    SectionMember{.container = "tab-modulation", .tag = 903},
    SectionMember{.container = "tab-modulation", .tag = 904},
    SectionMember{.container = "tab-modulation", .tag = 905},
    SectionMember{.container = "tab-modulation", .tag = 906},
    SectionMember{.container = "tab-modulation", .tag = 907},
    SectionMember{.container = "tab-modulation", .tag = 908},
    SectionMember{.container = "tab-modulation", .tag = 909},
    SectionMember{.container = "tab-modulation", .tag = 910},
    SectionMember{.container = "tab-modulation", .tag = 911},
    SectionMember{.container = "tab-modulation", .tag = 912},
    SectionMember{.container = "tab-modulation", .tag = 913},
    SectionMember{.container = "tab-modulation", .tag = 914},
    SectionMember{.container = "tab-settings", .tag = 9300},
}};

/// D7 1, 2, 4.
inline constexpr double kDesignedWidth = 960.0;
inline constexpr double kDesignedHeight = 600.0;
inline constexpr std::array<double, 4> kD7ZoomSteps{1.0, 1.25, 1.5, 2.0};
inline constexpr double kMinFontPx = 10.0;
inline constexpr double kMinHitTargetPx = 20.0;
inline constexpr double kMinSegmentCellPx = 24.0;
inline constexpr double kMinContrastText = 4.5;
inline constexpr double kMinContrastLargeBold = 3.0;

/// D7 5: the nine colour tokens.
struct ColourToken {
    const char* name;
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};
inline constexpr std::array<ColourToken, 9> kD7Colours{{
    ColourToken{.name = "bg", .r = 0x10, .g = 0x10, .b = 0x14},
    ColourToken{.name = "panel", .r = 0x1A, .g = 0x1A, .b = 0x20},
    ColourToken{.name = "track", .r = 0x22, .g = 0x22, .b = 0x2A},
    ColourToken{.name = "text", .r = 0xD8, .g = 0xD8, .b = 0xE0},
    ColourToken{.name = "text-dim", .r = 0x9A, .g = 0x9A, .b = 0xA8},
    ColourToken{.name = "accent", .r = 0x7A, .g = 0x8C, .b = 0xA8},
    ColourToken{.name = "highlight", .r = 0xE0, .g = 0xB0, .b = 0x60},
    ColourToken{.name = "anchor", .r = 0xD0, .g = 0x8A, .b = 0x5C},
    ColourToken{.name = "mass", .r = 0x7F, .g = 0xB0, .b = 0xD8},
}};

}  // namespace ProfundumPluginTest
