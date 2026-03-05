#include "update_checker.h"
#include "platform/preset_paths.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

// Platform-native HTTPS support
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#elif defined(__APPLE__)
// macOS uses NSURLSession via Objective-C runtime (see .mm file)
#else
// Linux fallback: use httplib for HTTP-only
#include <httplib.h>
#endif

namespace Krate::Plugins {

UpdateChecker::UpdateChecker(UpdateCheckerConfig config)
    : config_(std::move(config))
{
    loadState();
}

UpdateChecker::~UpdateChecker() {
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void UpdateChecker::checkForUpdate(bool force) {
    // Don't start a new check if one is already running
    if (checking_.load(std::memory_order_acquire))
        return;

    // Respect cooldown unless forced
    if (!force) {
        auto now = std::chrono::system_clock::now();
        if (now - lastCheckTime_ < kCooldownDuration)
            return;
    }

    // Wait for any previous thread to finish
    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    checking_.store(true, std::memory_order_release);
    workerThread_ = std::thread([this]() {
        performCheck();
        checking_.store(false, std::memory_order_release);
    });
}

UpdateCheckResult UpdateChecker::getResult() const {
    std::lock_guard<std::mutex> lock(resultMutex_);
    return result_;
}

void UpdateChecker::dismissVersion(const std::string& version) {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        dismissedVersion_ = version;
    }
    saveState();
}

bool UpdateChecker::isVersionDismissed(const std::string& version) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return !dismissedVersion_.empty() && dismissedVersion_ == version;
}

void UpdateChecker::clearResult() {
    std::lock_guard<std::mutex> lock(resultMutex_);
    result_ = {};
    resultReady_.store(false, std::memory_order_release);
}

std::string UpdateChecker::fetchJson(const std::string& url) {
    // Parse URL to extract scheme, host, and path
    std::string scheme, host, path;
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
        return {};

    scheme = url.substr(0, schemeEnd);
    auto hostStart = schemeEnd + 3;
    auto pathStart = url.find('/', hostStart);
    if (pathStart == std::string::npos)
        return {};

    host = url.substr(hostStart, pathStart - hostStart);
    path = url.substr(pathStart);

#ifdef _WIN32
    // Windows: use WinHTTP for native HTTPS support
    auto* session = WinHttpOpen(L"KrateAudio/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return {};

    // Convert host to wide string
    int hostLen = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
    std::wstring wideHost(static_cast<size_t>(hostLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, wideHost.data(), hostLen);

    bool useHttps = (scheme == "https");
    INTERNET_PORT port = useHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;

    auto* connect = WinHttpConnect(session, wideHost.c_str(), port, 0);
    if (!connect) { WinHttpCloseHandle(session); return {}; }

    // Convert path to wide string
    int pathLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring widePath(static_cast<size_t>(pathLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, widePath.data(), pathLen);

    DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
    auto* request = WinHttpOpenRequest(connect, L"GET", widePath.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return {};
    }

    // Set timeout (10 seconds)
    WinHttpSetTimeouts(request, 10000, 10000, 10000, 10000);

    std::string result;
    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr))
    {
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
            WINHTTP_NO_HEADER_INDEX);

        if (statusCode == 200) {
            DWORD bytesAvailable = 0;
            while (WinHttpQueryDataAvailable(request, &bytesAvailable) && bytesAvailable > 0) {
                std::string chunk(bytesAvailable, '\0');
                DWORD bytesRead = 0;
                WinHttpReadData(request, chunk.data(), bytesAvailable, &bytesRead);
                result.append(chunk.data(), bytesRead);
            }
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
#elif defined(__APPLE__)
    // macOS: use NSURLSession (implemented in update_checker_mac.mm)
    return fetchJsonMac(url);
#else
    // Linux: use httplib (HTTP only, HTTPS not supported without OpenSSL)
    if (scheme == "https") {
        // Fallback: try HTTP instead
        return {};
    }

    httplib::Client client(host);
    client.set_connection_timeout(10, 0);
    client.set_read_timeout(10, 0);

    auto res = client.Get(path);
    if (res && res->status == 200) {
        return res->body;
    }
    return {};
#endif
}

void UpdateChecker::performCheck() {
    auto json = fetchJson(config_.endpointUrl);
    if (json.empty()) {
        // Network error or endpoint not found — still signal completion
        // so the UI doesn't get stuck in "Checking..." state
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            result_ = UpdateCheckResult{};
        }
        resultReady_.store(true, std::memory_order_release);
        return;
    }

    auto result = parseResponse(json);

    // Update last check time
    lastCheckTime_ = std::chrono::system_clock::now();
    saveState();

    // Check if this version is dismissed
    if (result.updateAvailable && isVersionDismissed(result.latestVersion)) {
        result.updateAvailable = false;
    }

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        result_ = std::move(result);
    }
    resultReady_.store(true, std::memory_order_release);
}

UpdateCheckResult UpdateChecker::parseResponse(const std::string& jsonStr) const {
    UpdateCheckResult result;

    try {
        auto json = nlohmann::json::parse(jsonStr);

        if (!json.contains("plugins") || !json["plugins"].is_object())
            return result;

        auto& plugins = json["plugins"];
        if (!plugins.contains(config_.pluginName) || !plugins[config_.pluginName].is_object())
            return result;

        auto& entry = plugins[config_.pluginName];

        if (!entry.contains("version") || !entry["version"].is_string())
            return result;

        std::string remoteVersion = entry["version"].get<std::string>();
        auto remote = SemVer::parse(remoteVersion);
        auto current = SemVer::parse(config_.currentVersion);

        if (!remote || !current)
            return result;

        if (*remote > *current) {
            result.updateAvailable = true;
            result.latestVersion = remoteVersion;

            if (entry.contains("download_url") && entry["download_url"].is_string())
                result.downloadUrl = entry["download_url"].get<std::string>();

            if (entry.contains("release_notes") && entry["release_notes"].is_string())
                result.releaseNotes = entry["release_notes"].get<std::string>();
        }
    }
    catch (...) {
        // Malformed JSON — return empty result
    }

    return result;
}

void UpdateChecker::loadState() {
    auto path = getStateFilePath();
    if (path.empty())
        return;

    std::ifstream file(path);
    if (!file.is_open())
        return;

    try {
        auto json = nlohmann::json::parse(file);

        if (json.contains("last_check_epoch") && json["last_check_epoch"].is_number_integer()) {
            auto epoch = json["last_check_epoch"].get<int64_t>();
            lastCheckTime_ = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(epoch));
        }

        if (json.contains("dismissed_version") && json["dismissed_version"].is_string()) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            dismissedVersion_ = json["dismissed_version"].get<std::string>();
        }
    }
    catch (...) {
        // Corrupted state file — ignore, will be overwritten on next save
    }
}

void UpdateChecker::saveState() const {
    auto path = getStateFilePath();
    if (path.empty())
        return;

    // Ensure directory exists
    Platform::ensureDirectoryExists(path.parent_path());

    nlohmann::json json;
    json["last_check_epoch"] = std::chrono::system_clock::to_time_t(lastCheckTime_);

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        json["dismissed_version"] = dismissedVersion_;
    }

    std::ofstream file(path);
    if (file.is_open()) {
        file << json.dump(2);
    }
}

std::filesystem::path UpdateChecker::getStateFilePath() const {
    auto dir = Platform::getAppSettingsDirectory(config_.pluginName);
    if (dir.empty())
        return {};
    return dir / "update_state.json";
}

} // namespace Krate::Plugins
