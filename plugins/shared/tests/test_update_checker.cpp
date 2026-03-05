#include <catch2/catch_test_macros.hpp>
#include "update/update_checker.h"

#include <filesystem>
#include <fstream>
#include <thread>

using Krate::Plugins::UpdateChecker;
using Krate::Plugins::UpdateCheckerConfig;
using Krate::Plugins::UpdateCheckResult;

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

static const char* kValidJson = R"({
    "schema_version": 1,
    "plugins": {
        "Iterum":   { "version": "0.15.0", "download_url": "https://example.com/iterum", "release_notes": "New features" },
        "Disrumpo": { "version": "0.10.0", "download_url": "https://example.com/disrumpo", "release_notes": "Bug fixes" }
    }
})";

static const char* kSameVersionJson = R"({
    "schema_version": 1,
    "plugins": {
        "Iterum": { "version": "0.14.2", "download_url": "https://example.com/iterum", "release_notes": "" }
    }
})";

static const char* kOlderVersionJson = R"({
    "schema_version": 1,
    "plugins": {
        "Iterum": { "version": "0.13.0", "download_url": "https://example.com/iterum", "release_notes": "" }
    }
})";

// ==============================================================================
// Tests
// ==============================================================================

TEST_CASE("UpdateChecker - JSON parsing", "[update]") {
    UpdateCheckerConfig config{"Iterum", "0.14.2", "https://example.com/versions.json"};

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
        UpdateCheckerConfig ruinaeConfig{"Ruinae", "0.5.0", "https://example.com/versions.json"};
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
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK_FALSE(checker.hasResult());
    }
}

TEST_CASE("UpdateChecker - cooldown", "[update]") {
    UpdateCheckerConfig config{"Iterum", "0.14.2", "https://example.com/versions.json"};

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
    UpdateCheckerConfig config{"Iterum", "0.14.2", "https://example.com/versions.json"};

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
