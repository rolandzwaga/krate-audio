#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "update/update_checker.h"
#include "platform/preset_paths.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

using Krate::Plugins::UpdateChecker;
using Krate::Plugins::UpdateCheckerConfig;

// ==============================================================================
// Settings-dir redirect: UpdateChecker reads and writes
// getAppSettingsDirectory(pluginName) / "update_state.json". Every case below
// points that root at a fresh temp directory first, so no test touches the
// user's real update state.
// ==============================================================================

namespace {

#ifdef _WIN32
constexpr const char* kSettingsEnvVar = "APPDATA";          // preset_paths.cpp: getAppSettingsDirectory, _WIN32
#elif defined(__APPLE__)
constexpr const char* kSettingsEnvVar = "HOME";             // preset_paths.cpp: getAppSettingsDirectory, __APPLE__
#else
constexpr const char* kSettingsEnvVar = "XDG_CONFIG_HOME";  // preset_paths.cpp: getAppSettingsDirectory, Linux
#endif

std::optional<std::string> readEnv(const char* name) {
#ifdef _MSC_VER
    // getenv_s instead of std::getenv avoids C4996.
    std::size_t len = 0;
    if (getenv_s(&len, nullptr, 0, name) != 0 || len == 0u) {
        return std::nullopt;
    }
    std::string value(len, '\0');
    if (getenv_s(&len, value.data(), value.size(), name) != 0 || len == 0u) {
        return std::nullopt;
    }
    value.resize(len - 1u);  // len counts the terminating null
    return value;
#else
    const char* v = std::getenv(name);
    if (v == nullptr) {
        return std::nullopt;
    }
    return std::string(v);
#endif
}

void writeEnv(const char* name, const std::optional<std::string>& value) {
#ifdef _WIN32
    // An empty value removes the variable.
    _putenv_s(name, value ? value->c_str() : "");
#else
    if (value) {
        setenv(name, value->c_str(), 1);
    } else {
        unsetenv(name);
    }
#endif
}

/// True when every component of root (lexically normalised) is a prefix of p's.
bool isUnder(const std::filesystem::path& p, const std::filesystem::path& root) {
    const auto np = p.lexically_normal();
    const auto nr = root.lexically_normal();
    if (nr.empty()) {
        return false;
    }
    return std::mismatch(nr.begin(), nr.end(), np.begin(), np.end()).first == nr.end();
}

/// Redirects the settings root that getAppSettingsDirectory reads to a fresh temp directory for its lifetime.
class ScopedSettingsDirRedirect {
public:
    ScopedSettingsDirRedirect() {
        std::random_device rd;
        constexpr const char* kHex = "0123456789abcdef";
        std::string suffix;
        for (int i = 0; i < 16; ++i) {
            suffix += kHex[rd() & 0xFu];
        }
        root_ = std::filesystem::temp_directory_path() / ("krate-update-test-" + suffix);
        std::filesystem::create_directories(root_);
        previous_ = readEnv(kSettingsEnvVar);
        writeEnv(kSettingsEnvVar, root_.string());
    }

    // Declared before every checker in its scope, so each checker's destructor joins its worker before the restore.
    ~ScopedSettingsDirRedirect() {
        writeEnv(kSettingsEnvVar, previous_);
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    ScopedSettingsDirRedirect(const ScopedSettingsDirRedirect&) = delete;
    ScopedSettingsDirRedirect& operator=(const ScopedSettingsDirRedirect&) = delete;
    ScopedSettingsDirRedirect(ScopedSettingsDirRedirect&&) = delete;
    ScopedSettingsDirRedirect& operator=(ScopedSettingsDirRedirect&&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
    std::optional<std::string> previous_;
};

}  // namespace

// ==============================================================================
// Mock UpdateChecker for testing (overrides fetchJson to avoid real HTTP)
// ==============================================================================

class MockUpdateChecker : public UpdateChecker {
public:
    explicit MockUpdateChecker(UpdateCheckerConfig config, std::string mockResponse = "")
        : UpdateChecker(std::move(config))
        , mockResponse_(std::move(mockResponse))
    {}

    void setMockResponse(std::string response) {
        mockResponse_ = std::move(response);
    }

    int fetchCount() const { return fetchCount_; }

protected:
    std::string fetchJson([[maybe_unused]] const std::string& url) override {
        ++fetchCount_;
        return mockResponse_;
    }

private:
    std::string mockResponse_;
    int fetchCount_ = 0;
};

// Helper: wait for result with timeout
static bool waitForResult(UpdateChecker& checker, int maxWaitMs = 2000) {
    for (int i = 0; i < maxWaitMs / 10; ++i) {
        if (checker.hasResult())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return checker.hasResult();
}

// ==============================================================================
// JSON Fixtures
// ==============================================================================

static const char* const kValidJson = R"({
    "schema_version": 1,
    "plugins": {
        "Iterum":   { "version": "0.15.0", "download_url": "https://example.com/iterum", "release_notes": "New features" },
        "Disrumpo": { "version": "0.10.0", "download_url": "https://example.com/disrumpo", "release_notes": "Bug fixes" }
    }
})";

static const char* const kSameVersionJson = R"({
    "schema_version": 1,
    "plugins": {
        "Iterum": { "version": "0.14.2", "download_url": "https://example.com/iterum", "release_notes": "" }
    }
})";

static const char* const kOlderVersionJson = R"({
    "schema_version": 1,
    "plugins": {
        "Iterum": { "version": "0.13.0", "download_url": "https://example.com/iterum", "release_notes": "" }
    }
})";

// ==============================================================================
// Tests
// ==============================================================================

TEST_CASE("UpdateChecker - JSON parsing", "[update]") {
    ScopedSettingsDirRedirect settings;
    REQUIRE(isUnder(Krate::Plugins::Platform::getAppSettingsDirectory("Iterum"), settings.root()));
    REQUIRE(isUnder(Krate::Plugins::Platform::getAppSettingsDirectory("Ruinae"), settings.root()));
    UpdateCheckerConfig config{.pluginName = "Iterum", .currentVersion = "0.14.2", .endpointUrl = "https://example.com/versions.json"};

    SECTION("valid response with newer version") {
        MockUpdateChecker checker(config, kValidJson);
        checker.checkForUpdate(true);
        REQUIRE(waitForResult(checker));

        auto result = checker.getResult();
        CHECK(result.updateAvailable);
        CHECK(result.latestVersion == "0.15.0");
        CHECK(result.downloadUrl == "https://example.com/iterum");
        CHECK(result.releaseNotes == "New features");
    }

    SECTION("same version - no update") {
        MockUpdateChecker checker(config, kSameVersionJson);
        checker.checkForUpdate(true);
        REQUIRE(waitForResult(checker));

        auto result = checker.getResult();
        CHECK_FALSE(result.updateAvailable);
    }

    SECTION("older version - no update") {
        MockUpdateChecker checker(config, kOlderVersionJson);
        checker.checkForUpdate(true);
        REQUIRE(waitForResult(checker));

        auto result = checker.getResult();
        CHECK_FALSE(result.updateAvailable);
    }

    SECTION("plugin not in response") {
        UpdateCheckerConfig ruinaeConfig{.pluginName = "Ruinae", .currentVersion = "0.5.0", .endpointUrl = "https://example.com/versions.json"};
        MockUpdateChecker checker(ruinaeConfig, kValidJson);
        checker.checkForUpdate(true);
        REQUIRE(waitForResult(checker));

        auto result = checker.getResult();
        CHECK_FALSE(result.updateAvailable);
    }

    SECTION("malformed JSON") {
        MockUpdateChecker checker(config, "not json at all {{{");
        checker.checkForUpdate(true);
        // May or may not produce a result, but should not crash
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    SECTION("missing version field") {
        MockUpdateChecker checker(config, R"({"plugins":{"Iterum":{"download_url":"x"}}})");
        checker.checkForUpdate(true);
        REQUIRE(waitForResult(checker));
        CHECK_FALSE(checker.getResult().updateAvailable);
    }

    SECTION("empty response") {
        MockUpdateChecker checker(config, "");
        checker.checkForUpdate(true);
        REQUIRE(waitForResult(checker));
        // Empty response still signals completion (so UI doesn't stay in "Checking...")
        // but should not report an update available
        CHECK_FALSE(checker.getResult().updateAvailable);
    }
}

TEST_CASE("UpdateChecker - cooldown", "[update]") {
    ScopedSettingsDirRedirect settings;
    REQUIRE(isUnder(Krate::Plugins::Platform::getAppSettingsDirectory("Iterum"), settings.root()));
    UpdateCheckerConfig config{.pluginName = "Iterum", .currentVersion = "0.14.2", .endpointUrl = "https://example.com/versions.json"};

    SECTION("auto-check within 24h is a no-op") {
        MockUpdateChecker checker(config, kValidJson);

        // First check (forced to bypass any saved state)
        checker.checkForUpdate(true);
        REQUIRE(waitForResult(checker));
        CHECK(checker.fetchCount() == 1);

        checker.clearResult();

        // Second check without force — should be a no-op due to cooldown
        checker.checkForUpdate(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(checker.fetchCount() == 1); // No new fetch
    }

    SECTION("force bypasses cooldown") {
        MockUpdateChecker checker(config, kValidJson);

        checker.checkForUpdate(true);
        REQUIRE(waitForResult(checker));
        CHECK(checker.fetchCount() == 1);

        checker.clearResult();

        // Force check — should bypass cooldown
        checker.checkForUpdate(true);
        REQUIRE(waitForResult(checker));
        CHECK(checker.fetchCount() == 2);
    }
}

TEST_CASE("UpdateChecker - dismiss version", "[update]") {
    ScopedSettingsDirRedirect settings;
    REQUIRE(isUnder(Krate::Plugins::Platform::getAppSettingsDirectory("Iterum"), settings.root()));
    UpdateCheckerConfig config{.pluginName = "Iterum", .currentVersion = "0.14.2", .endpointUrl = "https://example.com/versions.json"};

    SECTION("dismissed version is suppressed") {
        MockUpdateChecker checker(config, kValidJson);

        checker.dismissVersion("0.15.0");
        CHECK(checker.isVersionDismissed("0.15.0"));

        checker.checkForUpdate(true);
        REQUIRE(waitForResult(checker));

        auto result = checker.getResult();
        CHECK_FALSE(result.updateAvailable); // Suppressed
    }

    SECTION("different version is not suppressed") {
        MockUpdateChecker checker(config, kValidJson);

        checker.dismissVersion("0.14.0"); // Dismiss an older version
        CHECK_FALSE(checker.isVersionDismissed("0.15.0"));

        checker.checkForUpdate(true);
        REQUIRE(waitForResult(checker));

        auto result = checker.getResult();
        CHECK(result.updateAvailable); // Not suppressed
    }
}

// ==============================================================================
// Feed v2 compatibility: the shipped parser reads a v2 document (and a future
// schema_version) exactly as the v1-only projection of the same entry.
// ==============================================================================

static const char* const kFeedV1OnlyJson = R"({
    "schema_version": 1,
    "plugins": {
        "FeedV2Probe": { "version": "1.1.0", "download_url": "https://example.com/feedv2probe", "release_notes": "" }
    }
})";

static const char* const kFeedV2Json = R"({
    "schema_version": 2,
    "manager": { "latest_json_url": "https://example.com/manager/latest.json" },
    "plugins": {
        "FeedV2Probe": {
            "version": "1.1.0",
            "download_url": "https://example.com/feedv2probe",
            "release_notes": "",
            "tag": "feedv2probe/v1.1.0",
            "pub_date": "2026-10-29T12:00:00Z",
            "channel": "stable",
            "release_notes_url": "https://example.com/feedv2probe/releases/v1.1.0",
            "platforms": {
                "windows-x86_64": {
                    "url": "https://example.com/FeedV2Probe-1.1.0-Windows-x64.exe",
                    "size": 1000,
                    "sha256": "0000000000000000000000000000000000000000000000000000000000000000",
                    "signature": "untrusted comment: probe\nRWQ=\ntrusted comment: probe\nAA==\n",
                    "format": "inno-exe"
                },
                "darwin-universal": {
                    "url": "https://example.com/FeedV2Probe-1.1.0-macOS.pkg",
                    "size": 2000,
                    "sha256": "1111111111111111111111111111111111111111111111111111111111111111",
                    "signature": "untrusted comment: probe\nRWQ=\ntrusted comment: probe\nAA==\n",
                    "format": "pkg",
                    "min_os": "11.0"
                },
                "linux-x86_64": {
                    "url": "https://example.com/FeedV2Probe-1.1.0-Linux-x64.tar.gz",
                    "size": 3000,
                    "sha256": "2222222222222222222222222222222222222222222222222222222222222222",
                    "signature": "untrusted comment: probe\nRWQ=\ntrusted comment: probe\nAA==\n",
                    "format": "tar.gz",
                    "min_os": "glibc-2.35",
                    "payload": {
                        "files": 3,
                        "sha256": "3333333333333333333333333333333333333333333333333333333333333333",
                        "list": ["FeedV2Probe.vst3/Contents/x86_64-linux/FeedV2Probe.so"]
                    }
                }
            },
            "install": {
                "vst3_bundle": "FeedV2Probe.vst3",
                "au_component": "FeedV2Probe.component",
                "windows_uninstall_key": "FeedV2Probe_is1",
                "macos_pkg_id": "com.example.feedv2probe"
            },
            "prerelease": {
                "tag": "feedv2probe/v1.2.0-beta.1",
                "pub_date": "2026-10-30T12:00:00Z",
                "channel": "beta",
                "release_notes_url": "https://example.com/feedv2probe/releases/v1.2.0-beta.1"
            }
        }
    }
})";

TEST_CASE("UpdateChecker - feed schema v2 is read as v1", "[update]") {
    const auto envBefore = readEnv(kSettingsEnvVar);
    {
        ScopedSettingsDirRedirect settings;
        REQUIRE(isUnder(Krate::Plugins::Platform::getAppSettingsDirectory("FeedV2Probe"), settings.root()));

        UpdateCheckerConfig config{.pluginName = "FeedV2Probe", .currentVersion = "1.0.0",
                                   .endpointUrl = "https://example.com/versions.json"};

        // Reference: the v1-only projection of the same entry.
        Krate::Plugins::UpdateCheckResult reference;
        {
            MockUpdateChecker refChecker(config, kFeedV1OnlyJson);
            refChecker.checkForUpdate(true);
            REQUIRE(waitForResult(refChecker));
            reference = refChecker.getResult();
        }
        REQUIRE(reference.updateAvailable);
        REQUIRE(reference.latestVersion == "1.1.0");

        // The reference run saved state; remove it so the exists() check below proves the document's own run wrote it.
        const auto statePath = Krate::Plugins::Platform::getAppSettingsDirectory("FeedV2Probe") / "update_state.json";
        REQUIRE(isUnder(statePath, settings.root()));
        std::filesystem::remove(statePath);
        REQUIRE_FALSE(std::filesystem::exists(statePath));

        const auto checkReadAsV1 = [&](const std::string& document) {
            MockUpdateChecker checker(config, document);
            checker.checkForUpdate(true);
            REQUIRE(waitForResult(checker));

            const auto result = checker.getResult();
            CHECK(result.updateAvailable);
            CHECK(result.updateAvailable == reference.updateAvailable);
            CHECK(result.latestVersion == "1.1.0");
            CHECK(result.latestVersion != "1.2.0-beta.1");
            CHECK(result.latestVersion == reference.latestVersion);
            CHECK(result.downloadUrl == "https://example.com/feedv2probe");
            CHECK(result.downloadUrl == reference.downloadUrl);
            CHECK(result.releaseNotes.empty());
            CHECK(result.releaseNotes == reference.releaseNotes);

            CHECK(std::filesystem::exists(statePath));
            CHECK(isUnder(statePath, settings.root()));
        };

        SECTION("v2 document") {
            checkReadAsV1(kFeedV2Json);
        }

        SECTION("schema_version 99") {
            std::string document = kFeedV2Json;
            const std::string from = R"("schema_version": 2)";
            const std::string to = R"("schema_version": 99)";
            const auto pos = document.find(from);
            REQUIRE(pos != std::string::npos);
            document.replace(pos, from.size(), to);
            REQUIRE(document.find(from) == std::string::npos);  // exactly one replacement
            checkReadAsV1(document);
        }
    }
    CHECK(readEnv(kSettingsEnvVar) == envBefore);  // SC-017: restored after the guard's scope
}

// ==============================================================================
// Krate Manager Phase 3 (FR-046): the committed mixed feed (one v2 entry among
// eight v1 entries) is read exactly as the live v1 snapshot, for every plugin.
// ==============================================================================

#ifndef SHARED_FIXTURES_DIR
#error "SHARED_FIXTURES_DIR is defined by plugins/shared/tests/CMakeLists.txt"
#endif

namespace {
std::string readFixture(const std::filesystem::path& rel) {
    std::ifstream in(std::filesystem::path(SHARED_FIXTURES_DIR) / rel, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
}  // namespace

TEST_CASE("UpdateChecker - one v2 plugin among eight v1 plugins is read as v1", "[update]") {
    const std::string mixed = readFixture(std::filesystem::path("feeds") / "valid" / "one-v2-eight-v1.json");
    const std::string live = readFixture(std::filesystem::path("gen-feed") / "snapshot-2026-10-29" / "live-versions.json");
    REQUIRE_FALSE(mixed.empty());
    REQUIRE_FALSE(live.empty());

    // Bound to a local: a range-for over parse(live).at(...).items() dangles (only the items() proxy is extended).
    const auto liveDoc = nlohmann::json::parse(live);
    std::vector<std::string> names;  // from the live file: no plugin name in this source
    for (const auto& item : liveDoc.at("plugins").items()) names.push_back(item.key());
    REQUIRE(names.size() == 9);

    const auto envBefore = readEnv(kSettingsEnvVar);
    {
        ScopedSettingsDirRedirect settings;  // declared before every checker (its destructor comment, :97)
        for (const auto& name : names) {
            INFO("plugin " << name);
            const auto settingsDir = Krate::Plugins::Platform::getAppSettingsDirectory(name);
            REQUIRE(isUnder(settingsDir, settings.root()));
            const UpdateCheckerConfig config{.pluginName = name, .currentVersion = "0.0.1",
                                             .endpointUrl = "https://example.com/versions.json"};
            const auto resultOf = [&config](const std::string& document) {
                MockUpdateChecker checker(config, document);
                checker.checkForUpdate(true);
                REQUIRE(waitForResult(checker));
                return checker.getResult();
            };
            // After each check, re-derive the state path (a lost redirect would move it) and prove the write landed
            // under the temp root; the state of the live run is removed first, so existence after the mixed run proves
            // the mixed run wrote it (as the Phase 1 case, test_update_checker.cpp:409-412, :432-433).
            const auto statePathNow = [&name] {
                return Krate::Plugins::Platform::getAppSettingsDirectory(name) / "update_state.json";
            };
            const auto onLive = resultOf(live);
            const auto afterLive = statePathNow();
            REQUIRE(isUnder(afterLive, settings.root()));
            REQUIRE(std::filesystem::exists(afterLive));
            std::filesystem::remove(afterLive);
            REQUIRE_FALSE(std::filesystem::exists(afterLive));
            const auto onMixed = resultOf(mixed);
            const auto afterMixed = statePathNow();
            REQUIRE(isUnder(afterMixed, settings.root()));
            REQUIRE(std::filesystem::exists(afterMixed));
            CHECK(onMixed.updateAvailable);
            CHECK(onMixed.updateAvailable == onLive.updateAvailable);
            CHECK(onMixed.latestVersion == onLive.latestVersion);
            CHECK(onMixed.downloadUrl == onLive.downloadUrl);
            CHECK(onMixed.releaseNotes.empty());
            CHECK(onMixed.releaseNotes == onLive.releaseNotes);
        }
    }
    CHECK(readEnv(kSettingsEnvVar) == envBefore);
}
