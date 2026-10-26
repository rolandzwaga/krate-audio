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
    UiTooltip{1, "Mono plays one note at a time, with glide and legato. Poly plays chords; the voice count is on the Advanced page."},
    UiTooltip{100, "Weight: more depth, more body and a louder anchor together. Centre is neutral."},
    UiTooltip{101, "Dark \xE2\x86\x94 Bright: moves the edge, the spectral shift and the character tone together. Centre is neutral."},
    UiTooltip{103, "Clean \xE2\x86\x94 Violent: more drive, more fold and more character together. Centre is neutral."},
    UiTooltip{104, "Mass: how heavy the note feels, from a pure, sine-like sub at 0 to an enormous driven bass at 100. 50 is neutral; the mod wheel and channel pressure push it higher."},
    UiTooltip{105, "Bass Morph: moves the sound from endpoint A (0 %) to endpoint B (100 %). Automate it to travel between two complete sounds."},
    UiTooltip{200, "Depth: high keeps the energy in the fundamental; low spreads it up through the harmonics."},
    UiTooltip{201, "Body: the weight of the low harmonics that make the note sound full."},
    UiTooltip{204, "Edge: the bite of the upper harmonics. Turn it up for buzz and presence, down for a smooth, round bass."},
    UiTooltip{300, "Anchor Waveform: the shape of the sub tone under the mass: Sine, Triangle, Rounded Sine, Asymmetric Sine or Octave Down."},
    UiTooltip{302, "Anchor Level: how loud the stable sub tone under the mass is. It stays centred and clean whatever the mass does."},
    UiTooltip{400, "Drop: each note starts this many semitones above its pitch and falls onto it (0 to 48). 0 is off; its time and shape are on the Advanced page."},
    UiTooltip{412, "Glide: time to slide between notes in Mono. The readout unit follows Glide Mode on the Advanced page."},
    UiTooltip{500, "Harmonic Fold: grows new harmonics, from soft saturation through harmonic compression to folding at 100. 0 is clean; the anchor stays clean unless Anchor Fold Feed sends it in."},
    UiTooltip{900, "Motion: what moves the sound over time. Its target, amount and timing are on the Advanced page."},
    UiTooltip{1000, "Morph Behaviour: how the anchor follows the Bass Morph. Hold keeps A's anchor, Blend follows the morph, Yield hands over late."},
    UiTooltip{1001, "Edit Target: whether the knobs edit endpoint A or endpoint B. It changes what you edit, not what you hear. Not automatable."},
    UiTooltip{9100, "Switch between the Main and Advanced pages."},
    UiTooltip{9200, "Compare: hear endpoint A, the morph, or endpoint B. Pressing A or B also sets Edit Target."},
    UiTooltip{9201, "Copy endpoint A's sound onto B. The name is not copied."},
    UiTooltip{9202, "Copy endpoint B's sound onto A. The name is not copied."},
    UiTooltip{9210, "Name of endpoint A. Saved with the preset."},
    UiTooltip{9211, "Name of endpoint B. Saved with the preset."},
    UiTooltip{9220, "How endpoint A sounds: shape, colour and force."},
    UiTooltip{9221, "How endpoint B sounds: shape, colour and force."},
    UiTooltip{9300, "Editor zoom: 100, 125, 150 or 200 %. Kept for the session, not saved with the project."},
}};

/// D2 5.2: every Advanced control, by bound ID or session tag.
inline constexpr std::array<UiTooltip, 78> kAdvancedTooltips{{
    UiTooltip{0, "Output Gain: final level after the limiter, from silence up to 0 dB. It only turns down, so it never pushes past the Output Ceiling."},
    UiTooltip{2, "Polyphony: how many notes can sound at once in Poly (1 to 8). Mono ignores it."},
    UiTooltip{3, "Output Ceiling: the highest true-peak level the output reaches (\xE2\x88\x92" "12 to 0 dBTP, default \xE2\x88\x92" "1). The limiter holds everything under it."},
    UiTooltip{102, "Tight \xE2\x86\x94 Loose: toward Tight, shorter attack, decay and release, a harder transient and more Compression; toward Loose the reverse. Compression has no control of its own."},
    UiTooltip{200, "Depth: how steeply the harmonics fall away above the fundamental. Higher is rounder, lower is richer."},
    UiTooltip{201, "Body: lifts the low harmonics (about 2 to 8) that carry the bass's body."},
    UiTooltip{202, "Body Curvature: narrows or broadens the region Body lifts."},
    UiTooltip{203, "Body Emphasis: at 0 all harmonics sound; toward \xE2\x88\x92" "100 % the even harmonics fade for a hollower tone."},
    UiTooltip{204, "Edge: lifts the upper harmonics that give the bass its bite."},
    UiTooltip{205, "Spectral Shift: moves the Body and Edge regions down (darker) or up (brighter) together."},
    UiTooltip{206, "Excitation Mix: blends the driven excitation into the clean tone. 0 is clean."},
    UiTooltip{207, "Excitation Drive: how hard the excitation stage is driven. More drive grows more new harmonics; it is heard only when Excitation Mix is above 0."},
    UiTooltip{208, "Excitation Asymmetry: biases the drive for uneven, even-harmonic grit."},
    UiTooltip{300, "Anchor Waveform: the shape of the sub tone: Sine (purest), Triangle, Rounded Sine, Asymmetric Sine or Octave Down (one octave below the note)."},
    UiTooltip{301, "Anchor Mode: how the sub tone behaves: Lock stays constant, Follow tracks the pitch you hear, Punch is louder at the note start, Ghost fades in after it."},
    UiTooltip{302, "Anchor Level: level of the pure sub tone under the bass. It stays centred and outside the drive stages."},
    UiTooltip{303, "Punch Boost: extra sub level at the note start in Punch mode, 0 to +12 dB."},
    UiTooltip{304, "Punch Time: how long the Punch boost lasts, 5 to 250 ms."},
    UiTooltip{305, "Ghost Time: in Ghost mode, how long the sub tone takes to emerge after the note starts, 1 to 100 ms."},
    UiTooltip{306, "Anchor Fold Feed: sends some of the sub tone into Harmonic Fold, so the fold works on the sub too. 0 keeps the sub clean."},
    UiTooltip{400, "Drop Amount: how far the pitch falls into each note, in semitones."},
    UiTooltip{401, "Drop Time: how long the pitch drop takes, from 1 ms to 4 s."},
    UiTooltip{402, "Drop Shape: the curve of the pitch drop: 0 % exponential (fast, then slow), 33 % linear, 67 % logarithmic (slow, then fast), 100 % elastic (it overshoots and settles)."},
    UiTooltip{403, "Drop Rebound: how much the pitch overshoots and settles at the end of the drop."},
    UiTooltip{410, "Mono Priority: which held note sounds in Mono: Last, Low or High."},
    UiTooltip{411, "Legato: overlapping notes glide without restarting the envelope."},
    UiTooltip{412, "Portamento: glide time between notes, from 0 (off) to 10 s. Glide Mode decides whether this is a time or a rate."},
    UiTooltip{413, "Portamento Mode: glide on every note, or only between overlapping notes."},
    UiTooltip{414, "Glide Mode: Constant Time takes the same time for any interval, Constant Rate takes longer for wider intervals, Quantized steps through the semitones."},
    UiTooltip{415, "Note Memory: what carries over between notes in Mono: Always Retrigger restarts everything, Legato carries on across overlapping notes, Envelope Retrigger restarts only the envelope, Oscillator Continuous keeps the waveform running."},
    UiTooltip{416, "Note Retrigger: Reset restarts the note's movement, Continue carries it on."},
    UiTooltip{417, "Pitch Bend Range: how far the bend wheel bends, 0 to 24 semitones each way."},
    UiTooltip{420, "Attack: time to full level after a note starts, 0.1 ms to 10 s."},
    UiTooltip{421, "Decay: time to fall from full level to the Sustain level, 0.1 ms to 10 s."},
    UiTooltip{422, "Sustain: level held while the key is down, 0 to 100 %."},
    UiTooltip{423, "Release: time to silence after the key is released, 0.1 ms to 10 s."},
    UiTooltip{424, "Attack Curve: bends the attack segment. 0 is a straight line, + is exponential, \xE2\x88\x92 is logarithmic."},
    UiTooltip{425, "Decay Curve: bends the decay segment. 0 is a straight line, + is exponential, \xE2\x88\x92 is logarithmic."},
    UiTooltip{426, "Release Curve: bends the release segment. 0 is a straight line, + is exponential, \xE2\x88\x92 is logarithmic."},
    UiTooltip{500, "Harmonic Fold: folds the waveform back on itself to add new harmonics. 0 is clean."},
    UiTooltip{501, "Shape: the SHAPE stage, one control across three characters: 0 % ROUND (soft and dark), 50 % HOLLOW (resonant and scooped), 100 % BITE (bright and edgy)."},
    UiTooltip{600, "Character: the type of colouring stage: Tube, Wavefold, Rectify, Bite, Crush or Fracture. Character Amount sets how much is heard. In WIDE, left and right get slightly different Character settings, so the sub is a little less mono than in Solid or Spread."},
    UiTooltip{601, "Character Amount: how much of the Character stage is heard. 0 is off. In WIDE, left and right get slightly different Character settings, so the sub is a little less mono than in Solid or Spread."},
    UiTooltip{602, "Character Tone: darkens or brightens the Character stage. In WIDE, left and right get slightly different Character settings, so the sub is a little less mono than in Solid or Spread."},
    UiTooltip{700, "Transient Attack: boosts or softens the start of each note, \xE2\x88\x92" "12 to +12 dB."},
    UiTooltip{701, "Transient Sustain: boosts or softens the body that follows the start, \xE2\x88\x92" "12 to +12 dB."},
    UiTooltip{800, "Image Mode: Solid is mono, Spread widens the upper harmonics above the crossover, Wide also gives left and right slightly different Character settings. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{801, "Spread: overall stereo width above the crossover. 0 is centred. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{802, "Gravity: how far below the crossover the width begins. At 0 the width reaches lowest; at 100 % the low band is pulled firmly to the centre. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{803, "Crossover: the frequency below which the bass stays centred, 60 to 250 Hz. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{804, "Curve: how quickly the width rises between the low and high knees. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{805, "Field Motion Depth: how much the width moves over time. 0 holds it still. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{806, "Field Motion Rate: speed of the width movement. Cycle runs from 0.05 to 1 Hz; Drift wanders from slowly to quickly. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{807, "Field Motion Source: Drift wanders, Cycle repeats. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{808, "Field Sync: lock the width movement to the host tempo; Field Sync Rate then sets the note value. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{809, "Field Sync Rate: note value of the synced width movement. Synced rates above 1 Hz fold into 0.5\xE2\x80\x93" "1 Hz. Mono-safe: width only pans, so the sub stays centred (correlation \xE2\x89\xA5 0.9989), and even at Gravity 0 the sides below twice the crossover are cut by 29 dB."},
    UiTooltip{900, "Motion Source: what moves the target: Env rises and falls with each note, Cycle repeats, Drift wanders. Off disables Motion."},
    UiTooltip{901, "Motion Target: what Motion moves: Mass, Depth, Body, Edge or Shift."},
    UiTooltip{902, "Motion Amount: how far and in which direction Motion moves its target."},
    UiTooltip{903, "Motion Depth: scales all of Motion at once, 0 to 100 %. Motion Amount sets the direction and size for the target."},
    UiTooltip{904, "Cycle Rate: speed of the Cycle source, 0.01 to 20 Hz, when Cycle Sync is off."},
    UiTooltip{905, "Cycle Shape: waveform of the Cycle source: Sine, Triangle, Sawtooth, Square, S&H or Smooth Random."},
    UiTooltip{906, "Cycle Sync: lock the Cycle to the host tempo; Cycle Sync Rate then sets the note value."},
    UiTooltip{907, "Cycle Sync Rate: note value of one synced Cycle, 1/64T to 4/1D."},
    UiTooltip{908, "Cycle Phase: where in its waveform the Cycle starts, 0 to 360\xC2\xB0."},
    UiTooltip{909, "Cycle Retrigger: restart the Cycle at its start phase on each new note. Off lets it run freely."},
    UiTooltip{910, "Cycle Fade-In: time for the Cycle to reach full depth after a note starts, 0 to 5 s."},
    UiTooltip{911, "Motion Polarity: Bipolar ENV rests at \xE2\x88\x92" "amount between notes; Unipolar rests at the set value."},
    UiTooltip{912, "Drift Smoothness: how smoothly the Drift source wanders. Higher is gentler."},
    UiTooltip{913, "Env Attack: rise time of the Env source, 0.1 ms to 10 s."},
    UiTooltip{914, "Env Decay: fall time of the Env source back to rest, 0.1 ms to 10 s."},
    UiTooltip{1000, "Morph behaviour of the sub tone: Hold keeps A's sub, Blend follows the morph, Yield hands over late."},
    UiTooltip{1002, "Morph smoothing: how quickly the sound follows a change of the morph position, 5 ms to 2 s. Longer glides smoothly between A and B."},
    UiTooltip{9110, "Advanced page sections: choose which group of controls to show."},
    UiTooltip{9203, "Compare: hear A, the morph, or B. Choosing A or B also edits that side."},
    UiTooltip{9204, "Copy A's sound to B. Names are not copied."},
    UiTooltip{9205, "Copy B's sound to A. Names are not copied."},
    UiTooltip{9300, "Window size: 100, 125, 150 or 200 %. Kept for the session, not saved with the project."},
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
    TagName{"OutputGain", 0},
    TagName{"VoiceMode", 1},
    TagName{"Polyphony", 2},
    TagName{"OutputCeiling", 3},
    TagName{"MacroWeight", 100},
    TagName{"MacroDarkBright", 101},
    TagName{"MacroTightLoose", 102},
    TagName{"MacroCleanViolent", 103},
}};

/// FR-004 (D2 1.2): container label -> bound ID or session tag; an ID listed under two containers is bound in both.
struct SectionMember {
    const char* container;
    std::int32_t tag;
};
inline constexpr std::array<SectionMember, 79> kFr004Members{{
    SectionMember{"section-core-spectrum", 200},
    SectionMember{"section-core-spectrum", 201},
    SectionMember{"section-core-spectrum", 202},
    SectionMember{"section-core-spectrum", 203},
    SectionMember{"section-core-spectrum", 204},
    SectionMember{"section-core-spectrum", 205},
    SectionMember{"section-core-spectrum", 500},
    SectionMember{"section-core-spectrum", 102},
    SectionMember{"section-sub-anchor", 302},
    SectionMember{"section-sub-anchor", 300},
    SectionMember{"section-sub-anchor", 301},
    SectionMember{"section-sub-anchor", 303},
    SectionMember{"section-sub-anchor", 304},
    SectionMember{"section-sub-anchor", 305},
    SectionMember{"section-sub-anchor", 306},
    SectionMember{"section-sub-anchor", 1000},
    SectionMember{"section-excitation-fold", 207},
    SectionMember{"section-excitation-fold", 206},
    SectionMember{"section-excitation-fold", 208},
    SectionMember{"section-excitation-fold", 500},
    SectionMember{"section-shape-character", 501},
    SectionMember{"section-shape-character", 600},
    SectionMember{"section-shape-character", 601},
    SectionMember{"section-shape-character", 602},
    SectionMember{"section-mass", 700},
    SectionMember{"section-mass", 701},
    SectionMember{"section-pitch-envelopes", 400},
    SectionMember{"section-pitch-envelopes", 401},
    SectionMember{"section-pitch-envelopes", 402},
    SectionMember{"section-pitch-envelopes", 403},
    SectionMember{"section-pitch-envelopes", 420},
    SectionMember{"section-pitch-envelopes", 421},
    SectionMember{"section-pitch-envelopes", 422},
    SectionMember{"section-pitch-envelopes", 423},
    SectionMember{"section-pitch-envelopes", 424},
    SectionMember{"section-pitch-envelopes", 425},
    SectionMember{"section-pitch-envelopes", 426},
    SectionMember{"section-pitch-envelopes", 410},
    SectionMember{"section-pitch-envelopes", 411},
    SectionMember{"section-pitch-envelopes", 412},
    SectionMember{"section-pitch-envelopes", 413},
    SectionMember{"section-pitch-envelopes", 414},
    SectionMember{"section-pitch-envelopes", 415},
    SectionMember{"section-pitch-envelopes", 416},
    SectionMember{"section-pitch-envelopes", 417},
    SectionMember{"section-stereo-output", 800},
    SectionMember{"section-stereo-output", 801},
    SectionMember{"section-stereo-output", 802},
    SectionMember{"section-stereo-output", 803},
    SectionMember{"section-stereo-output", 804},
    SectionMember{"section-stereo-output", 805},
    SectionMember{"section-stereo-output", 806},
    SectionMember{"section-stereo-output", 807},
    SectionMember{"section-stereo-output", 808},
    SectionMember{"section-stereo-output", 809},
    SectionMember{"section-stereo-output", 0},
    SectionMember{"section-stereo-output", 2},
    SectionMember{"section-stereo-output", 3},
    SectionMember{"section-morph-behaviour", 1000},
    SectionMember{"section-morph-behaviour", 1002},
    SectionMember{"section-morph-behaviour", 9203},
    SectionMember{"section-morph-behaviour", 9204},
    SectionMember{"section-morph-behaviour", 9205},
    SectionMember{"tab-modulation", 900},
    SectionMember{"tab-modulation", 901},
    SectionMember{"tab-modulation", 902},
    SectionMember{"tab-modulation", 903},
    SectionMember{"tab-modulation", 904},
    SectionMember{"tab-modulation", 905},
    SectionMember{"tab-modulation", 906},
    SectionMember{"tab-modulation", 907},
    SectionMember{"tab-modulation", 908},
    SectionMember{"tab-modulation", 909},
    SectionMember{"tab-modulation", 910},
    SectionMember{"tab-modulation", 911},
    SectionMember{"tab-modulation", 912},
    SectionMember{"tab-modulation", 913},
    SectionMember{"tab-modulation", 914},
    SectionMember{"tab-settings", 9300},
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
    ColourToken{"bg", 0x10, 0x10, 0x14},
    ColourToken{"panel", 0x1A, 0x1A, 0x20},
    ColourToken{"track", 0x22, 0x22, 0x2A},
    ColourToken{"text", 0xD8, 0xD8, 0xE0},
    ColourToken{"text-dim", 0x9A, 0x9A, 0xA8},
    ColourToken{"accent", 0x7A, 0x8C, 0xA8},
    ColourToken{"highlight", 0xE0, 0xB0, 0x60},
    ColourToken{"anchor", 0xD0, 0x8A, 0x5C},
    ColourToken{"mass", 0x7F, 0xB0, 0xD8},
}};

}  // namespace ProfundumPluginTest
