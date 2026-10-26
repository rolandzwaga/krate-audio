#include <catch2/catch_test_macros.hpp>
#include "profundum_test_fixture.h"

// Profundum_MainPageRenderSet — written by T010 (specs/profundum-phase12-ui/tasks.md)
//
// FR-006 / SC-006 (b) render set (plan 3.3; design/main-page.md section 8). HIDDEN ([.listen]): never in a gate; run by
// specs/profundum-phase12-ui/evidence/listen/render.sh for the user's listening. Landed before ratification under the
// user's P-1 ruling (spec.md FR-001 row, "Exception (user ruling 2026-10-23, P-1)"). Test-only: no plugin source.
//
// Renders (design/main-page.md 8.1 - 8.3, plus the 8.4 proposals as ruled 2026-10-23, spec FR-006 / FR-050 rows,
// "follow all recommendations"):
//   - one render per Main control (the FR-011 set, 17 IDs) at its registered min, default and max, every other Main
//     control at its default; for the two two-value lists (1 Voice Mode, 1001 Edit Target) both list values are
//     rendered as "min" and "max" and no "def" file is written;
//   - the four combined "patch" renders of 8.3;
//   - B endpoint (8.4 item 1): the 105, 1000 and 1001 renders and *Morph and motion* first write 1001 = B and B's values
//     (the *Growl* column: Edge 70 %, Fold 60, Mass 70) in their own block, then 1001 = A and every other Main control's
//     value in the next block, then the rendered 1001 value alone in a third block (the processor applies the LAST point
//     of each queue per block and 1001 before the others, processor.cpp:665-701, so each step needs its own block);
//   - *Morph and motion* also sets Motion Amount 902 to 1.0 (D1 8.3 names no Motion value) so its Cycle source is
//     audible; 902 is not a Main control (FR-050) and is recorded in the manifest line;
//   - legato pairs (8.4 item 3): 412 at min / max and 1 at both values, E1 on at 0 and held 1.5 s, C3 on at 1.0 s
//     (overlapping), C3 released at 3.0 s (both notes released by 3.0 s), 1 s tail: <id>_<min|max>_legato.wav.
// Each render: a heap ProcessorFixture at 48 kHz, host block 512, every ID at its registered default (a fresh
// processor), the render's Main-control values written as parameter changes at sample 0 (Edit Target 1001 first,
// 8.1), normalised through each ID's registered law; one 512-sample pre-roll block; then the note (E1, or C3 in a
// separate render) held 3 s plus a 1 s tail. The pre-roll (3 blocks for a B-endpoint render) is not written to the WAV.
// The ONLY assertion is finiteness (Krate::DSP::detail::isFinite, fast-math immune).
// Output: <id>_<min|def|max|patch>_<E1|C3>.wav (32-bit float stereo) plus manifest.txt, one line per file listing
// every Main control's plain value, in specs/profundum-phase12-ui/evidence/listen/.

#include "parameters/global_params.h"
#include "parameters/morph_params.h"
#include "parameters/note_params.h"
#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using ProfundumPluginTest::ProcessorFixture;
using Steinberg::Vst::ParamID;

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kHostBlock = 512;
constexpr std::size_t kPreRoll = kHostBlock;                          // the parameter block, not written
constexpr std::size_t kPreRollB = 3u * kHostBlock;                    // B values / A values / rendered 1001, not written
constexpr std::size_t kHeldSamples = std::size_t{3} * 48000u;         // 3 s held
constexpr std::size_t kTailSamples = std::size_t{1} * 48000u;         // 1 s tail
constexpr std::size_t kRenderSamples = kHeldSamples + kTailSamples;   // written to the WAV
constexpr float kVelocity = 100.0f / 127.0f;
constexpr std::size_t kLegatoSecondOn = std::size_t{1} * 48000u;      // C3 on at 1.0 s
constexpr std::size_t kLegatoFirstOff = 72000u;                       // E1 held 1.5 s
constexpr std::size_t kLegatoSecondOff = kHeldSamples;                // C3 off at 3.0 s

// -----------------------------------------------------------------------------
// The Main controls (design/main-page.md 3.1, 8.2) and their registered laws
// -----------------------------------------------------------------------------
enum class LawKind : std::uint8_t {
    Sound,     // a kSoundRows E row: lawOf(leaf) (F32 or Stepped)
    Linear01,  // the four macros (macro_params.h registerMacroParams) and Morph Position 105: plain 0..1 == normalised
    Portamento,  // 412: kPortamentoLaw (note_params.h:226)
    List,      // a list parameter that is not an E row: index / (count - 1)
};

struct MainControl {
    ParamID id;
    const char* name;
    LawKind kind;
    int listCount;    // List only
    double minPlain;  // registered min (8.2)
    double defPlain;  // registered default (8.2)
    double maxPlain;  // registered max (8.2)
    bool twoValueList;
};

/// The E row of a Sound control (sound_param_table.h:246 soundRowOf, via kParamRoutes).
[[nodiscard]] const ::Profundum::SoundParamRow& eRowOf(ParamID id) {
    const ::Profundum::SoundParamRow* row = ::Profundum::soundRowOf(id);
    REQUIRE(row != nullptr);
    return *row;
}

/// The default plain value of a Sound row, read from the engine's default leaf word (kDefaultLeafBits).
[[nodiscard]] double soundDefaultPlain(ParamID id) {
    const auto& row = eRowOf(id);
    const auto& f = ::Profundum::lawOf(row.leaf);
    const std::uint32_t word = ::Profundum::kDefaultLeafBits[row.leaf];
    if (f.kind == ::Profundum::MorphState::Kind::F32) {
        return static_cast<double>(std::bit_cast<float>(word));
    }
    return static_cast<double>(word);
}

[[nodiscard]] double soundLo(ParamID id) {
    const auto& f = ::Profundum::lawOf(eRowOf(id).leaf);
    return f.law == ::Profundum::MorphState::Law::Stepped ? 0.0 : static_cast<double>(f.lo);
}

[[nodiscard]] double soundHi(ParamID id) {
    const auto& f = ::Profundum::lawOf(eRowOf(id).leaf);
    return f.law == ::Profundum::MorphState::Law::Stepped ? static_cast<double>(f.maxEnum)
                                                          : static_cast<double>(f.hi);
}

[[nodiscard]] MainControl soundControl(ParamID id, const char* name) {
    return MainControl{.id = id,
                       .name = name,
                       .kind = LawKind::Sound,
                       .listCount = 0,
                       .minPlain = soundLo(id),
                       .defPlain = soundDefaultPlain(id),
                       .maxPlain = soundHi(id),
                       .twoValueList = false};
}

[[nodiscard]] std::vector<MainControl> mainControls() {
    using namespace ::Profundum;
    const double neutral = static_cast<double>(Krate::DSP::ProfundumMacroMatrix::kNeutral);
    auto sound = [](ParamID id, const char* name) { return soundControl(id, name); };
    // Order: Edit Target 1001 FIRST (8.1 write order), then the 8.2 table order.
    return {
        MainControl{.id = kMorphEditTargetId,
                    .name = "EditTarget",
                    .kind = LawKind::List,
                    .listCount = kNumEditTargets,
                    .minPlain = 0.0,
                    .defPlain = static_cast<double>(kDefaultEditTarget),
                    .maxPlain = static_cast<double>(kNumEditTargets - 1),
                    .twoValueList = true},
        MainControl{.id = kVoiceModeId,
                    .name = "VoiceMode",
                    .kind = LawKind::List,
                    .listCount = 2,
                    .minPlain = 0.0,
                    .defPlain = static_cast<double>(kDefaultVoiceModeIndex),
                    .maxPlain = 1.0,
                    .twoValueList = true},
        MainControl{.id = kMacroWeightId,
                    .name = "Weight",
                    .kind = LawKind::Linear01,
                    .listCount = 0,
                    .minPlain = 0.0,
                    .defPlain = neutral,
                    .maxPlain = 1.0,
                    .twoValueList = false},
        MainControl{.id = kMacroDarkBrightId,
                    .name = "DarkBright",
                    .kind = LawKind::Linear01,
                    .listCount = 0,
                    .minPlain = 0.0,
                    .defPlain = neutral,
                    .maxPlain = 1.0,
                    .twoValueList = false},
        MainControl{.id = kMacroCleanViolentId,
                    .name = "CleanViolent",
                    .kind = LawKind::Linear01,
                    .listCount = 0,
                    .minPlain = 0.0,
                    .defPlain = neutral,
                    .maxPlain = 1.0,
                    .twoValueList = false},
        sound(kMassId, "Mass"),
        MainControl{.id = kMorphPositionId,
                    .name = "BassMorph",
                    .kind = LawKind::Linear01,
                    .listCount = 0,
                    .minPlain = 0.0,
                    .defPlain = static_cast<double>(kDefaultMorphPosition),
                    .maxPlain = 1.0,
                    .twoValueList = false},
        sound(kCoreDepthId, "Depth"),
        sound(kCoreBodyId, "Body"),
        sound(kCoreEdgeId, "Edge"),
        sound(kAnchorWaveformId, "AnchorCharacter"),
        sound(kAnchorLevelId, "AnchorLevel"),
        sound(kDropAmountId, "Drop"),
        MainControl{.id = kNotePortamentoId,
                    .name = "Glide",
                    .kind = LawKind::Portamento,
                    .listCount = 0,
                    .minPlain = kPortamentoLaw.lo,
                    .defPlain = kPortamentoLaw.lo,
                    .maxPlain = kPortamentoLaw.hi,
                    .twoValueList = false},
        sound(kFoldAmountId, "Fold"),
        sound(kMotionSourceId, "MotionSource"),
        MainControl{.id = kMorphAnchorModeId,
                    .name = "MorphBehaviour",
                    .kind = LawKind::List,
                    .listCount = kNumAnchorMorphModes,
                    .minPlain = 0.0,
                    .defPlain = static_cast<double>(kDefaultAnchorMorphModeIndex),
                    .maxPlain = static_cast<double>(kNumAnchorMorphModes - 1),
                    .twoValueList = false},
    };
}

/// Plain -> normalised through the ID's registered law.
[[nodiscard]] double normalisedOf(const MainControl& c, double plain) {
    switch (c.kind) {
        case LawKind::Sound:
            return ::Profundum::normalizedFromPlain(::Profundum::lawOf(eRowOf(c.id).leaf), plain);
        case LawKind::Linear01:
            return ::Profundum::normalizedFromPlain(
                ::Profundum::PlainLaw{.lo = 0.0, .hi = 1.0, .logOffset = 0.0, .logarithmic = false}, plain);
        case LawKind::Portamento:
            return ::Profundum::normalizedFromPlain(::Profundum::kPortamentoLaw, plain);
        case LawKind::List:
            return std::round(plain) / static_cast<double>(c.listCount - 1);
    }
    return 0.0;
}

// -----------------------------------------------------------------------------
// TU-local WAV writer (as dsp/tests/unit/systems/profundum_core_spectral_test.cpp:1561-1600 keeps its own; plan 3.3)
// -----------------------------------------------------------------------------
void putU16(std::vector<unsigned char>& b, std::uint16_t v) {
    b.push_back(static_cast<unsigned char>(v & 0xFFu));
    b.push_back(static_cast<unsigned char>((v >> 8) & 0xFFu));
}

void putU32(std::vector<unsigned char>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        b.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFFu));
    }
}

void putTag(std::vector<unsigned char>& b, const char* tag) {
    for (std::size_t i = 0; i < 4; ++i) {
        b.push_back(static_cast<unsigned char>(tag[i]));
    }
}

/// RIFF/WAVE, WAVE_FORMAT_IEEE_FLOAT (3), 2 channels, 32-bit, little-endian, 18-byte fmt chunk plus fact chunk.
bool writeWavFloatStereo(const std::filesystem::path& path, const float* L, const float* R, std::size_t frames,
                         double fs) {
    const std::uint64_t dataBytes = static_cast<std::uint64_t>(frames) * 8u;  // 2 channels x 4 bytes
    if (dataBytes > 0xFFFF0000ull) {
        return false;
    }
    const auto sr = static_cast<std::uint32_t>(std::llround(fs));
    std::vector<unsigned char> b;
    b.reserve(static_cast<std::size_t>(dataBytes) + 64u);
    putTag(b, "RIFF");
    putU32(b, static_cast<std::uint32_t>(4u + (8u + 18u) + (8u + 4u) + 8u + dataBytes));
    putTag(b, "WAVE");
    putTag(b, "fmt ");
    putU32(b, 18u);
    putU16(b, 3u);       // WAVE_FORMAT_IEEE_FLOAT
    putU16(b, 2u);       // channels
    putU32(b, sr);
    putU32(b, sr * 8u);  // byte rate
    putU16(b, 8u);       // block align
    putU16(b, 32u);      // bits per sample
    putU16(b, 0u);       // cbSize
    putTag(b, "fact");
    putU32(b, 4u);
    putU32(b, static_cast<std::uint32_t>(frames));
    putTag(b, "data");
    putU32(b, static_cast<std::uint32_t>(dataBytes));
    for (std::size_t i = 0; i < frames; ++i) {
        putU32(b, std::bit_cast<std::uint32_t>(L[i]));
        putU32(b, std::bit_cast<std::uint32_t>(R[i]));
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(f);
}

[[nodiscard]] std::filesystem::path listenDir() {
    return {PROFUNDUM_RESOURCES_DIR "/../../../specs/profundum-phase12-ui/evidence/listen"};
}

// -----------------------------------------------------------------------------
// One render
// -----------------------------------------------------------------------------
struct ExtraValue {
    MainControl control;  // a non-Main ID (902 Motion Amount), written with the Main values
    double plain;
};

struct RenderSpec {
    std::string stem;             // "<id>_<min|def|max|patch>"
    std::vector<double> plain;    // one value per mainControls() entry, same order
    std::vector<std::pair<std::size_t, double>> bValues;  // (mainControls() index, plain) of B; empty: no B endpoint
    std::vector<ExtraValue> extras;
};

struct NoteSpec {
    Steinberg::int16 pitch;
    const char* name;
};

struct NoteEvent {
    Steinberg::int16 pitch;
    std::size_t onAt;   // relative to the end of the pre-roll
    std::size_t offAt;  // relative to the end of the pre-roll
};

void renderOne(const std::vector<MainControl>& controls, const RenderSpec& spec, const std::string& file,
               const std::vector<NoteEvent>& notes, std::ofstream& manifest) {
    INFO("render " << file);
    const bool withB = !spec.bValues.empty();
    const std::size_t preRoll = withB ? kPreRollB : kPreRoll;

    auto fx = std::make_unique<ProcessorFixture>();
    REQUIRE(fx->prepare(kSampleRate, static_cast<Steinberg::int32>(kHostBlock)) == Steinberg::kResultOk);

    using Event = ProcessorFixture::ScriptedEvent;
    std::vector<Event> script;
    script.reserve(controls.size() + spec.bValues.size() + spec.extras.size() + 3u + 2u * notes.size());
    auto param = [&script](std::size_t sample, const MainControl& c, double plain) {
        Event e{};
        e.sample = static_cast<Steinberg::int64>(sample);
        e.kind = Event::Kind::Param;
        e.id = c.id;
        e.value = normalisedOf(c, plain);
        script.push_back(e);
    };
    constexpr double kEditA = 0.0;
    constexpr double kEditB = 1.0;
    // controls[0] is Edit Target 1001.
    const std::size_t mainBlock = withB ? kHostBlock : 0u;
    if (withB) {
        param(0u, controls[0], kEditB);  // block 0: 1001 = B, then B's values
        for (const auto& [index, plain] : spec.bValues) {
            param(0u, controls[index], plain);
        }
        param(mainBlock, controls[0], kEditA);  // block 1: 1001 = A, then the rendered values
    } else {
        param(mainBlock, controls[0], spec.plain[0]);
    }
    for (std::size_t i = 1; i < controls.size(); ++i) {
        param(mainBlock, controls[i], spec.plain[i]);
    }
    for (const ExtraValue& x : spec.extras) {
        param(mainBlock, x.control, x.plain);
    }
    if (withB) {
        param(2u * kHostBlock, controls[0], spec.plain[0]);  // block 2: the rendered 1001 value alone
    }
    for (const NoteEvent& n : notes) {
        Event on{};
        on.sample = static_cast<Steinberg::int64>(preRoll + n.onAt);
        on.kind = Event::Kind::NoteOn;
        on.pitch = n.pitch;
        on.velocity = kVelocity;
        script.push_back(on);
        Event off{};
        off.sample = static_cast<Steinberg::int64>(preRoll + n.offAt);
        off.kind = Event::Kind::NoteOff;
        off.pitch = n.pitch;
        script.push_back(off);
    }

    fx->renderScript(script, preRoll + kRenderSamples, kHostBlock);
    REQUIRE(fx->capturedL.size() == preRoll + kRenderSamples);
    REQUIRE(fx->capturedR.size() == preRoll + kRenderSamples);

    std::size_t nonFinite = 0;
    for (std::size_t i = 0; i < fx->capturedL.size(); ++i) {
        if (!Krate::DSP::detail::isFinite(fx->capturedL[i]) || !Krate::DSP::detail::isFinite(fx->capturedR[i])) {
            ++nonFinite;
        }
    }
    CHECK(nonFinite == 0u);

    REQUIRE(writeWavFloatStereo(listenDir() / file, fx->capturedL.data() + preRoll, fx->capturedR.data() + preRoll,
                                kRenderSamples, kSampleRate));

    std::ostringstream line;
    line << file << ":";
    for (std::size_t i = 0; i < controls.size(); ++i) {
        line << " " << controls[i].id << "_" << controls[i].name << "=" << spec.plain[i];
    }
    for (const ExtraValue& x : spec.extras) {
        line << " " << x.control.id << "_" << x.control.name << "=" << x.plain;
    }
    if (withB) {
        line << " B[";
        for (std::size_t k = 0; k < spec.bValues.size(); ++k) {
            const auto& [index, plain] = spec.bValues[k];
            line << (k == 0 ? "" : " ") << controls[index].id << "_" << controls[index].name << "=" << plain;
        }
        line << "]";
    }
    line << " notes=";
    for (std::size_t k = 0; k < notes.size(); ++k) {
        line << (k == 0 ? "" : ",") << notes[k].pitch << "@" << static_cast<double>(notes[k].onAt) / kSampleRate << "-"
             << static_cast<double>(notes[k].offAt) / kSampleRate << "s";
    }
    manifest << line.str() << "\n";
}

}  // namespace

TEST_CASE("Profundum_MainPageRenderSet", "[profundum][ui][render][.listen]") {
    const std::vector<MainControl> controls = mainControls();
    REQUIRE(controls.size() == 17u);  // the FR-011 set (design/main-page.md 3.1)

    std::vector<double> defaults;
    defaults.reserve(controls.size());
    for (const auto& c : controls) {
        defaults.push_back(c.defPlain);
    }

    using namespace ::Profundum;
    auto indexOf = [&controls](ParamID id) {
        for (std::size_t i = 0; i < controls.size(); ++i) {
            if (controls[i].id == id) {
                return i;
            }
        }
        FAIL("Main control " << id << " missing");
        return std::size_t{0};
    };

    // 8.4 item 1 (ruled 2026-10-23): the B endpoint, from the *Growl* column of 8.3 (Edge 70 %, Fold 60, Mass 70).
    const std::vector<std::pair<std::size_t, double>> kBEndpoint{
        {indexOf(kCoreEdgeId), 0.7}, {indexOf(kFoldAmountId), 60.0}, {indexOf(kMassId), 70.0}};
    auto isMorphControl = [](ParamID id) {
        return id == kMorphPositionId || id == kMorphAnchorModeId || id == kMorphEditTargetId;
    };

    std::vector<RenderSpec> renders;

    // 8.2: single-control renders.
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const MainControl& c = controls[i];
        const std::string id = std::to_string(c.id);
        RenderSpec lo{.stem = id + "_min", .plain = defaults, .bValues = {}, .extras = {}};
        lo.plain[i] = c.minPlain;
        renders.push_back(lo);
        if (!c.twoValueList) {
            RenderSpec def{.stem = id + "_def", .plain = defaults, .bValues = {}, .extras = {}};
            def.plain[i] = c.defPlain;
            renders.push_back(def);
        }
        RenderSpec hi{.stem = id + "_max", .plain = defaults, .bValues = {}, .extras = {}};
        hi.plain[i] = c.maxPlain;
        renders.push_back(hi);
        if (isMorphControl(c.id)) {
            const std::size_t written = c.twoValueList ? 2u : 3u;
            for (std::size_t k = renders.size() - written; k < renders.size(); ++k) {
                renders[k].bValues = kBEndpoint;
            }
        }
    }

    // 8.3: the four combined patch renders (values not named are at their default).
    struct PatchValue {
        ParamID id;
        double plain;
    };
    auto patch = [&](const char* stem, std::initializer_list<PatchValue> values) -> RenderSpec& {
        RenderSpec r{.stem = std::string(stem) + "_patch", .plain = defaults, .bValues = {}, .extras = {}};
        for (const PatchValue& v : values) {
            r.plain[indexOf(v.id)] = v.plain;
        }
        renders.push_back(r);
        return renders.back();
    };
    constexpr double kMotionCycle = 2.0;  // Off / Env / Cycle / Drift (motion_params.h:37-38)
    // 8.4 item 2 (ruled 2026-10-23): D1 8.3 names no Motion Amount, so 1.0 (registered law [-1, 1],
    // morph_state.h:253-254); 902 stays off the Main page (FR-050).
    constexpr double kMorphMotionAmount = 1.0;
    patch("deep_sub", {{.id = kMacroWeightId, .plain = 0.8},
                       {.id = kMacroCleanViolentId, .plain = 0.3},
                       {.id = kMassId, .plain = 30.0},
                       {.id = kFoldAmountId, .plain = 0.0}});
    patch("round_bass",
          {{.id = kMassId, .plain = 50.0}, {.id = kCoreBodyId, .plain = 0.7}, {.id = kCoreEdgeId, .plain = 0.3}});
    patch("growl", {{.id = kMacroCleanViolentId, .plain = 0.8},
                    {.id = kMassId, .plain = 70.0},
                    {.id = kCoreEdgeId, .plain = 0.7},
                    {.id = kFoldAmountId, .plain = 60.0}});
    {
        RenderSpec& morphMotion = patch(
            "morph_motion", {{.id = kMorphPositionId, .plain = 0.5}, {.id = kMotionSourceId, .plain = kMotionCycle}});
        morphMotion.bValues = kBEndpoint;
        morphMotion.extras.push_back(
            ExtraValue{.control = soundControl(kMotionAmountId, "MotionAmount"), .plain = kMorphMotionAmount});
    }

    // 15 controls x 3 + 2 two-value lists x 2 + 4 patches.
    REQUIRE(renders.size() == 15u * 3u + 2u * 2u + 4u);
    // B endpoint: 105 x 3 + 1000 x 3 + 1001 x 2 + morph_motion.
    std::size_t withB = 0;
    for (const RenderSpec& r : renders) {
        withB += r.bValues.empty() ? 0u : 1u;
    }
    REQUIRE(withB == 3u + 3u + 2u + 1u);

    // 8.4 item 3 (ruled 2026-10-23): legato pairs for 412 at min / max and 1 at both values.
    std::vector<RenderSpec> legato;
    for (const ParamID id : {static_cast<ParamID>(kNotePortamentoId), static_cast<ParamID>(kVoiceModeId)}) {
        const std::size_t i = indexOf(id);
        RenderSpec lo{.stem = std::to_string(id) + "_min", .plain = defaults, .bValues = {}, .extras = {}};
        lo.plain[i] = controls[i].minPlain;
        legato.push_back(lo);
        RenderSpec hi{.stem = std::to_string(id) + "_max", .plain = defaults, .bValues = {}, .extras = {}};
        hi.plain[i] = controls[i].maxPlain;
        legato.push_back(hi);
    }
    REQUIRE(legato.size() == 4u);

    const std::filesystem::path dir = listenDir();
    std::filesystem::create_directories(dir);
    std::ofstream manifest(dir / "manifest.txt", std::ios::trunc);
    REQUIRE(static_cast<bool>(manifest));
    manifest << "# Profundum_MainPageRenderSet (T010): 48 kHz, block 512, MONO unless 1_max, velocity 100/127, "
                "3 s held (legato: E1 0-1.5 s, C3 1.0-3.0 s) + 1 s tail; plain values per Main control; "
                "B[...] = endpoint B written first (1001 = B), then 1001 = A and the values listed\n";

    constexpr std::array<NoteSpec, 2> kNotes{{{.pitch = 28, .name = "E1"}, {.pitch = 48, .name = "C3"}}};
    for (const RenderSpec& r : renders) {
        for (const NoteSpec& n : kNotes) {
            renderOne(controls, r, r.stem + "_" + n.name + ".wav",
                      {NoteEvent{.pitch = n.pitch, .onAt = 0u, .offAt = kHeldSamples}}, manifest);
        }
    }
    const std::vector<NoteEvent> kLegatoPair{
        NoteEvent{.pitch = kNotes[0].pitch, .onAt = 0u, .offAt = kLegatoFirstOff},
        NoteEvent{.pitch = kNotes[1].pitch, .onAt = kLegatoSecondOn, .offAt = kLegatoSecondOff}};
    for (const RenderSpec& r : legato) {
        renderOne(controls, r, r.stem + "_legato.wav", kLegatoPair, manifest);
    }
    manifest.flush();
    REQUIRE(static_cast<bool>(manifest));
}
