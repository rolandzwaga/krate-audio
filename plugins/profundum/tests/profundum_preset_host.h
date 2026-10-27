#pragma once

// ==============================================================================
// Profundum Phase 13 - Catch2-free preset host (T010; FR-024, FR-027, FR-047, FR-049; spec C-4; plan 4.4, P-8)
// ==============================================================================
// ProfundumPluginTest::PresetHost drives ONE heap-held ::Profundum::Processor the way a host does:
// initialize -> setupProcessing -> setActive, then parameter-only or stereo-out process() calls, and component
// state in/out through Steinberg::MemoryStream. Shape copied from plugins/vorago/tests/vorago_preset_host.h.
//
// Catch2-free: shared by profundum_preset_generator (which links neither Catch2 nor vstgui) and the preset test
// TUs. NEVER include profundum_test_fixture.h here (it includes Catch2).
//
// buildPresetComponentState is THE C-4 drive shared by the generator and the tests: validate the definition, then
// three parameter-only blocks (Edit Target A + endpoint A, Edit Target B + endpoint B, controls + Edit Target A),
// the two names, getState, the seed patch, setState, getState, and a bitwise decode compare (FR-049).
// P-8: no audio is rendered before getState, so the runtime blob is the synthesized one (target == position).
//
// writeFactoryTree is the generator's whole body (FR-024): the six category directories in kCategories order,
// then <category>/<name>.vstpreset in definition order. No timestamp, path string or RNG enters any byte.
//
// ALLOCATION BEHAVIOUR: outL_/outR_ are sized once by prepare() and never regrown; process() builds ProcessData /
// AudioBusBuffers on the stack.
// ==============================================================================

#include "parameters/endpoint_names.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"
#include "preset_test_support.h"
#include "processor/processor.h"
#include "profundum_preset_defs.h"  // tools/ is on the include path (plan 9, 4.5).

#include <vst_param_changes.h>

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/profundum_engine.h>

#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace ProfundumPluginTest {

static_assert(::Profundum::PresetDefs::kFactoryDefaultSeed == Krate::DSP::ProfundumEngine::kDefaultSeed,
              "plan 4.4: the factory default seed is the engine's default seed (C-19)");

class PresetHost {
public:
    PresetHost() : proc_(std::make_unique<::Profundum::Processor>()) {}

    ~PresetHost() {
        if (active_) {
            proc_->setActive(false);
            active_ = false;
        }
        if (initialized_) {
            proc_->terminate();
            initialized_ = false;
        }
    }

    PresetHost(const PresetHost&) = delete;
    PresetHost& operator=(const PresetHost&) = delete;

    /// initialize(nullptr) -> setupProcessing({kRealtime, kSample32, maxBlock, sr}) -> setActive(true).
    /// Sizes outL_/outR_ to maxBlock ONCE (never regrown).
    [[nodiscard]] Steinberg::tresult prepare(double sampleRate, Steinberg::int32 maxBlock) {
        if (maxBlock <= 0 || initialized_) {
            return Steinberg::kResultFalse;
        }
        Steinberg::tresult r = proc_->initialize(nullptr);
        if (r != Steinberg::kResultOk) {
            return r;
        }
        initialized_ = true;

        Steinberg::Vst::ProcessSetup setup{.processMode = Steinberg::Vst::kRealtime,
                                           .symbolicSampleSize = Steinberg::Vst::kSample32,
                                           .maxSamplesPerBlock = maxBlock,
                                           .sampleRate = sampleRate};
        r = proc_->setupProcessing(setup);
        if (r != Steinberg::kResultOk) {
            return r;
        }
        r = proc_->setActive(true);
        if (r != Steinberg::kResultOk) {
            return r;
        }
        active_ = true;

        outL_.assign(static_cast<std::size_t>(maxBlock), 0.0f);
        outR_.assign(static_cast<std::size_t>(maxBlock), 0.0f);
        return Steinberg::kResultOk;
    }

    /// P-8: a parameter-only process() call (numOutputs 0). The processor applies the parameters and returns at its
    /// parameter-only guard before anything is rendered or published (processor.cpp:383-386).
    [[nodiscard]] Steinberg::tresult processParamsOnly(Steinberg::Vst::IParameterChanges* pc) {
        if (!active_) {
            return Steinberg::kResultFalse;
        }
        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = 0;
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 0;
        data.outputs = nullptr;
        data.inputParameterChanges = pc;
        data.outputParameterChanges = nullptr;
        data.inputEvents = nullptr;
        data.outputEvents = nullptr;
        data.processContext = nullptr;
        lastBlock_ = 0;
        return proc_->process(data);
    }

    /// One event-in / stereo-out process() call of n samples (n <= maxBlock). outL()/outR() then view exactly those
    /// n samples.
    [[nodiscard]] Steinberg::tresult process(std::size_t n, Steinberg::Vst::IEventList* ev,
                                             Steinberg::Vst::IParameterChanges* pc) {
        if (!active_ || n > outL_.size()) {
            return Steinberg::kResultFalse;
        }

        std::array<float*, 2> channels{outL_.data(), outR_.data()};

        Steinberg::Vst::AudioBusBuffers outBus{};
        outBus.numChannels = 2;
        outBus.silenceFlags = 0;
        outBus.channelBuffers32 = channels.data();

        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(n);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outBus;
        data.inputParameterChanges = pc;
        data.outputParameterChanges = nullptr;
        data.inputEvents = ev;
        data.outputEvents = nullptr;
        data.processContext = nullptr;

        const Steinberg::tresult r = proc_->process(data);
        lastBlock_ = n;
        return r;
    }

    /// A kMsgEndpointName message as the controller sends it: int "endpoint", binary "name" (the UTF-8 bytes).
    [[nodiscard]] Steinberg::tresult sendEndpointName(int endpoint, std::string_view utf8) {
        auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
        msg->setMessageID(::Profundum::kMsgEndpointName);
        Steinberg::Vst::IAttributeList* attrs = msg->getAttributes();
        if (attrs == nullptr) {
            return Steinberg::kResultFalse;
        }
        if (attrs->setInt("endpoint", static_cast<Steinberg::int64>(endpoint)) != Steinberg::kResultOk ||
            attrs->setBinary("name", utf8.data(), static_cast<Steinberg::uint32>(utf8.size())) !=
                Steinberg::kResultOk) {
            return Steinberg::kResultFalse;
        }
        return proc_->notify(msg);
    }

    /// setState() from a MemoryStream holding `comp`, positioned at 0.
    [[nodiscard]] Steinberg::tresult loadState(std::span<const std::uint8_t> comp) {
        auto stream = Steinberg::owned(new Steinberg::MemoryStream());
        Steinberg::int32 written = 0;
        if (!comp.empty()) {
            // IBStream::write takes a non-const pointer but does not modify the source.
            const Steinberg::tresult w = stream->write(
                const_cast<std::uint8_t*>(comp.data()),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
                static_cast<Steinberg::int32>(comp.size()), &written);
            if (w != Steinberg::kResultOk || written != static_cast<Steinberg::int32>(comp.size())) {
                return Steinberg::kResultFalse;
            }
        }
        if (stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) != Steinberg::kResultOk) {
            return Steinberg::kResultFalse;
        }
        return proc_->setState(stream);
    }

    /// getState() into a MemoryStream; `out` receives exactly the written bytes.
    [[nodiscard]] bool saveState(std::vector<std::uint8_t>& out) {
        auto stream = Steinberg::owned(new Steinberg::MemoryStream());
        if (proc_->getState(stream) != Steinberg::kResultOk) {
            return false;
        }
        const auto size = static_cast<std::size_t>(stream->getSize());
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(stream->getData());
        if (size > 0 && bytes == nullptr) {
            return false;
        }
        out.assign(bytes, bytes + size);
        return true;
    }

    [[nodiscard]] std::span<const float> outL() const noexcept { return {outL_.data(), lastBlock_}; }
    [[nodiscard]] std::span<const float> outR() const noexcept { return {outR_.data(), lastBlock_}; }

    [[nodiscard]] ::Profundum::Processor& processor() noexcept { return *proc_; }

private:
    std::unique_ptr<::Profundum::Processor> proc_;
    std::vector<float> outL_, outR_;
    std::size_t lastBlock_ = 0;
    bool initialized_ = false;
    bool active_ = false;
};

namespace preset_host_detail {

/// Plan 4.4 / P-6: the only IDs a definition's `controls` may carry (1, 100-103, 105, 410-416, 1000, 1002, and from
/// Phase 14 the Morph Modulation block 1003-1011, plan 4.13).
[[nodiscard]] inline bool isAllowedControlId(Steinberg::Vst::ParamID id) noexcept {
    namespace pf = ::Profundum;
    return id == pf::kVoiceModeId || (id >= pf::kMacroWeightId && id <= pf::kMacroCleanViolentId) ||
           id == pf::kMorphPositionId || (id >= pf::kNoteMonoPriorityId && id <= pf::kNoteRetriggerId) ||
           id == pf::kMorphAnchorModeId || id == pf::kMorphRampTimeId ||
           (id >= pf::kMorphModSourceId && id <= pf::kMorphModCycleSyncRateId);
}

[[nodiscard]] inline bool isSoundId(Steinberg::Vst::ParamID id) noexcept {
    return id < static_cast<Steinberg::Vst::ParamID>(::Profundum::kNumRoutedIds) &&
           ::Profundum::kParamRoutes[id].kind == ::Profundum::RouteKind::Sound;
}

/// One list of the definition: finite, in [0, 1], the list's ID rule, no duplicate. `why` names preset and point.
[[nodiscard]] inline bool validateList(const std::string& preset, const char* listName,
                                       const std::vector<::Profundum::PresetDefs::ParamSetting>& list,
                                       bool soundList, std::string& why) {
    for (std::size_t i = 0; i < list.size(); ++i) {
        const ::Profundum::PresetDefs::ParamSetting& p = list[i];
        const std::string where =
            preset + ": " + listName + "[" + std::to_string(i) + "] (ID " + std::to_string(p.id) + ")";
        if (!Krate::DSP::detail::isFinite(p.normalized)) {
            why = where + " has a non-finite value";
            return false;
        }
        if (p.normalized < 0.0 || p.normalized > 1.0) {
            why = where + " value " + std::to_string(p.normalized) + " is outside [0, 1]";
            return false;
        }
        if (soundList && !isSoundId(p.id)) {
            why = where + " is not a Sound-route E-parameter";
            return false;
        }
        if (!soundList && !isAllowedControlId(p.id)) {
            why = where + " is not an allowed control ID (1, 100-103, 105, 410-416, 1000, 1002, 1003-1011)";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (list[j].id == p.id) {
                why = where + " duplicates " + listName + "[" + std::to_string(j) + "]";
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] inline bool validateName(const std::string& preset, const char* field, std::string_view name,
                                       std::string_view defaultName, std::string& why) {
    const std::string where = preset + ": " + field + " \"" + std::string(name) + "\"";
    if (name.size() > ::Profundum::kEndpointNameMaxBytes) {
        why = where + " is " + std::to_string(name.size()) + " bytes, more than " +
              std::to_string(::Profundum::kEndpointNameMaxBytes);
        return false;
    }
    if (::Profundum::sanitizeEndpointName(name).view() != name) {
        why = where + " is changed by sanitizeEndpointName";
        return false;
    }
    if (name.empty() || name == defaultName) {
        why = where + " is the default endpoint name";
        return false;
    }
    return true;
}

[[nodiscard]] inline bool validateDef(const ::Profundum::PresetDefs::ProfundumPresetDef& def, std::string& why) {
    const std::string preset(def.name);
    return validateList(preset, "endpointA", def.endpointA, true, why) &&
           validateList(preset, "endpointB", def.endpointB, true, why) &&
           validateList(preset, "controls", def.controls, false, why) &&
           validateName(preset, "nameA", def.nameA, "A", why) && validateName(preset, "nameB", def.nameB, "B", why);
}

inline void writeLE32(std::ofstream& f, std::uint32_t v) {
    const std::array<char, 4> b{static_cast<char>(v & 0xFFu), static_cast<char>((v >> 8) & 0xFFu),
                                static_cast<char>((v >> 16) & 0xFFu), static_cast<char>((v >> 24) & 0xFFu)};
    f.write(b.data(), static_cast<std::streamsize>(b.size()));
}

inline void writeLE64(std::ofstream& f, std::int64_t v) {
    const auto u = static_cast<std::uint64_t>(v);
    writeLE32(f, static_cast<std::uint32_t>(u & 0xFFFFFFFFu));
    writeLE32(f, static_cast<std::uint32_t>(u >> 32));
}

/// The class id, derived at run time from kProcessorUID - never a literal. FUID::toString writes 32 hex characters
/// plus a terminator into a char8[33].
[[nodiscard]] inline std::string profundumClassIdAscii() {
    Steinberg::char8 buf[33] = {};
    ::Profundum::kProcessorUID.toString(buf);
    return std::string(buf);
}

/// .vstpreset layout (tools/vorago_preset_generator.cpp writeVstPreset):
///   offset 0    "VST3"                     4 B
///   offset 4    uint32 version = 1         4 B
///   offset 8    classId ASCII             32 B
///   offset 40   int64 listOffset           8 B
///   offset 48   Comp payload, Info payload
///   listOffset  "List", uint32 2, {"Comp", off, size}, {"Info", off, size}
[[nodiscard]] inline bool writeVstPreset(const std::filesystem::path& path, const std::vector<std::uint8_t>& comp,
                                         const std::string& classIdAscii, const std::string& info) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }

    constexpr std::int64_t kHeaderSize = 48;
    const std::int64_t compOffset = kHeaderSize;
    const auto compSize = static_cast<std::int64_t>(comp.size());
    const std::int64_t infoOffset = compOffset + compSize;
    const auto infoSize = static_cast<std::int64_t>(info.size());
    const std::int64_t listOffset = infoOffset + infoSize;

    f.write("VST3", 4);
    writeLE32(f, 1u);
    f.write(classIdAscii.data(), 32);
    writeLE64(f, listOffset);

    f.write(reinterpret_cast<const char*>(comp.data()), static_cast<std::streamsize>(comp.size()));
    f.write(info.data(), static_cast<std::streamsize>(info.size()));

    f.write("List", 4);
    writeLE32(f, 2u);
    f.write("Comp", 4);
    writeLE64(f, compOffset);
    writeLE64(f, compSize);
    f.write("Info", 4);
    writeLE64(f, infoOffset);
    writeLE64(f, infoSize);

    return f.good();
}

[[nodiscard]] inline bool isKnownCategory(std::string_view category) noexcept {
    for (const std::string_view c : ::Profundum::PresetDefs::kCategories) {
        if (c == category) {
            return true;
        }
    }
    return false;
}

}  // namespace preset_host_detail

/// C-4 (FR-027): validate, drive, getState, seed patch, setState, getState. false + `why` on any failure.
[[nodiscard]] inline bool buildPresetComponentState(const ::Profundum::PresetDefs::ProfundumPresetDef& def,
                                                    std::vector<std::uint8_t>& comp, std::string& why) {
    namespace hd = preset_host_detail;
    using ::Profundum::PresetDefs::ParamSetting;
    comp.clear();
    why.clear();
    const std::string preset(def.name);
    if (!hd::validateDef(def, why)) {
        return false;
    }

    PresetHost host;
    // Step 1 (spec C-4): prepare at 48 kHz / 512.
    if (host.prepare(48000.0, 512) != Steinberg::kResultOk) {
        why = preset + ": PresetHost::prepare failed";
        return false;
    }
    const auto drive = [&host, &preset, &why](const char* step, double editTarget,
                                              const std::vector<ParamSetting>& points) {
        Krate::Test::ParameterChanges changes;
        changes.addChange(::Profundum::kMorphEditTargetId, editTarget);
        for (const ParamSetting& p : points) {
            changes.addChange(p.id, p.normalized);
        }
        if (host.processParamsOnly(&changes) != Steinberg::kResultOk) {
            why = preset + ": the " + step + " parameter-only block failed to process";
            return false;
        }
        return true;
    };
    // Steps 2-4 (plan 4.4): Edit Target A + endpoint A; Edit Target B + endpoint B (A copied when B is empty);
    // the controls with Edit Target A last (FR-047).
    if (!drive("endpoint A", 0.0, def.endpointA) ||
        !drive("endpoint B", 1.0, def.endpointB.empty() ? def.endpointA : def.endpointB) ||
        !drive("controls", 0.0, def.controls)) {
        return false;
    }
    // Step 5: the two names.
    if (host.sendEndpointName(0, def.nameA) != Steinberg::kResultOk ||
        host.sendEndpointName(1, def.nameB) != Steinberg::kResultOk) {
        why = preset + ": an endpoint-name message was refused";
        return false;
    }
    // Step 6: getState, patch the seed word (little-endian), setState, getState.
    std::vector<std::uint8_t> s1;
    if (!host.saveState(s1)) {
        why = preset + ": the first getState failed";
        return false;
    }
    constexpr std::size_t kSeedAt = ::Profundum::kStateEndpointAOffset - ::Profundum::kStateSeedBytes;   // 84
    static_assert(kSeedAt == 84, "spec C-4 step 6: the seed occupies bytes 84-87");
    if (s1.size() != ::Profundum::kStateV3Bytes) {
        why = preset + ": the first getState wrote " + std::to_string(s1.size()) + " bytes, expected " +
              std::to_string(::Profundum::kStateV3Bytes);
        return false;
    }
    for (std::size_t k = 0; k < ::Profundum::kStateSeedBytes; ++k) {
        s1[kSeedAt + k] = static_cast<std::uint8_t>((def.seed >> (8u * k)) & 0xFFu);
    }
    if (host.loadState(s1) != Steinberg::kResultOk) {
        why = preset + ": setState of the seed-patched state failed";
        return false;
    }
    if (!host.saveState(comp)) {
        comp.clear();
        why = preset + ": the second getState failed";
        return false;
    }
    // Step 7 (FR-049): the reloaded state decodes bitwise equal to the patched one.
    const DecodedPresetState d1 = decodePresetState(s1);
    const DecodedPresetState d2 = decodePresetState(comp);
    std::string where;
    if (!d2.ok || !decodedEqual(d2, d1, 0.0, where)) {
        why = preset + ": the reloaded state differs from the patched state (" +
              (where.empty() ? std::string("decode not ok") : where) + ")";
        comp.clear();
        return false;
    }
    return true;
}

/// FR-024 / SC-010: the generator's whole body (directories, files, Info), shared with Profundum_GeneratorSmoke.
/// Every preset is attempted; `written` counts the files written, and every failure is appended to `why`.
[[nodiscard]] inline bool writeFactoryTree(const std::filesystem::path& outDir, std::size_t& written,
                                           std::string& why) {
    namespace hd = preset_host_detail;
    namespace pd = ::Profundum::PresetDefs;
    written = 0;
    why.clear();
    const auto fail = [&why](const std::string& msg) {
        if (!why.empty()) {
            why += "\n";
        }
        why += msg;
    };

    const std::string classIdAscii = hd::profundumClassIdAscii();
    if (classIdAscii.size() != 32) {
        why = "kProcessorUID.toString() yielded " + std::to_string(classIdAscii.size()) +
              " characters, expected 32";
        return false;
    }

    // All six category directories exist, whether or not a preset lands in each (kCategories order).
    for (const std::string_view category : pd::kCategories) {
        const std::filesystem::path dir = outDir / std::string(category);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            why = "failed to create the " + std::string(category) + " directory: " + ec.message();
            return false;
        }
    }

    bool ok = true;
    // FR-024 determinism: definition order, never a directory iteration.
    for (const pd::ProfundumPresetDef& def : pd::allPresets()) {
        const std::string stem(def.name);
        const std::string category(def.category);
        if (!hd::isKnownCategory(def.category)) {
            fail(stem + ": category \"" + category + "\" is not one of the six in PresetDefs::kCategories");
            ok = false;
            continue;
        }
        std::vector<std::uint8_t> comp;
        std::string reason;
        if (!buildPresetComponentState(def, comp, reason)) {
            fail(category + "/" + stem + ": " + reason);
            ok = false;
            continue;
        }
        const std::string info = pd::buildProfundumInfoXml(def.name, def.category, def.description);
        if (!hd::writeVstPreset(outDir / category / (stem + ".vstpreset"), comp, classIdAscii, info)) {
            fail(category + "/" + stem + ": write failed");
            ok = false;
            continue;
        }
        ++written;
    }
    return ok;
}

}  // namespace ProfundumPluginTest
