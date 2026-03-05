#pragma once

// ==============================================================================
// UpdateChecker - Async Version Check
// ==============================================================================
// Checks a hosted JSON endpoint for newer plugin versions.
// - Async: spawns std::thread, never blocks UI or audio thread
// - Cooldown: auto-check limited to once per 24h unless forced
// - Dismiss: users can suppress notifications for a specific version
// - Testable: virtual fetchJson() for mock injection
// ==============================================================================

#include "update_checker_config.h"
#include "version_compare.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace Krate::Plugins {

/// Result of an update check
struct UpdateCheckResult {
    bool updateAvailable = false;
    std::string latestVersion;     // e.g., "0.15.0"
    std::string downloadUrl;       // URL to download page
    std::string releaseNotes;      // Brief release notes
};

class UpdateChecker {
public:
    explicit UpdateChecker(UpdateCheckerConfig config);
    virtual ~UpdateChecker();

    // Non-copyable, non-movable (owns thread)
    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    /// Start an async update check.
    /// @param force If false, respects 24h cooldown. If true, always checks.
    void checkForUpdate(bool force = false);

    /// @return true if a check has completed and result is available
    bool hasResult() const { return resultReady_.load(std::memory_order_acquire); }

    /// Get the check result. Only valid after hasResult() returns true.
    UpdateCheckResult getResult() const;

    /// Dismiss notifications for a specific version (persisted to disk)
    void dismissVersion(const std::string& version);

    /// @return true if the given version has been dismissed
    bool isVersionDismissed(const std::string& version) const;

    /// Clear the result (e.g., after banner is hidden)
    void clearResult();

protected:
    /// Fetch JSON from the endpoint URL. Virtual for testing.
    /// @return JSON string on success, empty string on failure
    virtual std::string fetchJson(const std::string& url);

#ifdef __APPLE__
    /// macOS implementation using NSURLSession (defined in update_checker_mac.mm)
    static std::string fetchJsonMac(const std::string& url);
#endif

private:
    /// Perform the check synchronously (called from worker thread)
    void performCheck();

    /// Parse the JSON response and populate result
    UpdateCheckResult parseResponse(const std::string& json) const;

    /// Load persisted state from disk (last check time, dismissed version)
    void loadState();

    /// Save persisted state to disk
    void saveState() const;

    /// Get the state file path
    std::filesystem::path getStateFilePath() const;

    UpdateCheckerConfig config_;

    // Thread management
    std::thread workerThread_;
    std::atomic<bool> checking_{false};

    // Result
    mutable std::mutex resultMutex_;
    UpdateCheckResult result_;
    std::atomic<bool> resultReady_{false};

    // Cooldown (24h)
    static constexpr auto kCooldownDuration = std::chrono::hours(24);
    std::chrono::system_clock::time_point lastCheckTime_{};

    // Dismissed version
    mutable std::mutex stateMutex_;
    std::string dismissedVersion_;
};

} // namespace Krate::Plugins
