#include "libcurl_fetch.h"

#include <dlfcn.h>

#include <cstddef>
#include <cstring>
#include <mutex>

namespace Krate::Plugins::detail {

namespace {

// The libcurl entry points and constants this file uses, with the values from curl/curl.h. They are part of the
// libcurl.so.4 ABI, which has been stable since 2006, so no curl headers are needed at build time.
using CurlInitFn = void* (*)();
using CurlSetoptFn = int (*)(void*, int, ...);
using CurlPerformFn = int (*)(void*);
using CurlGetinfoFn = int (*)(void*, int, ...);
using CurlCleanupFn = void (*)(void*);
using CurlGlobalInitFn = int (*)(long);
using CurlVersionInfoFn = const void* (*)(int);
using CurlWriteFn = std::size_t (*)(char*, std::size_t, std::size_t, void*);

constexpr int kCurlOk = 0;                         // CURLE_OK
constexpr long kCurlGlobalDefault = 3;             // CURL_GLOBAL_DEFAULT
constexpr int kCurlVersionFourth = 3;              // CURLVERSION_FOURTH: the oldest layout with `protocols`
constexpr int kOptWriteData = 10001;               // CURLOPT_WRITEDATA
constexpr int kOptUrl = 10002;                     // CURLOPT_URL
constexpr int kOptTimeout = 13;                    // CURLOPT_TIMEOUT
constexpr int kOptUserAgent = 10018;               // CURLOPT_USERAGENT
constexpr int kOptFollowLocation = 52;             // CURLOPT_FOLLOWLOCATION
constexpr int kOptMaxRedirs = 68;                  // CURLOPT_MAXREDIRS
constexpr int kOptConnectTimeout = 78;             // CURLOPT_CONNECTTIMEOUT
constexpr int kOptNoSignal = 99;                   // CURLOPT_NOSIGNAL
constexpr int kOptProtocols = 181;                 // CURLOPT_PROTOCOLS
constexpr int kOptRedirProtocols = 182;            // CURLOPT_REDIR_PROTOCOLS
constexpr int kOptWriteFunction = 20011;           // CURLOPT_WRITEFUNCTION
constexpr int kInfoResponseCode = 0x200000 + 2;    // CURLINFO_RESPONSE_CODE
constexpr long kProtoHttpOrHttps = 1 | 2;          // CURLPROTO_HTTP | CURLPROTO_HTTPS

// The leading fields of curl_version_info_data, which every layout since CURLVERSION_FIRST starts with.
struct CurlVersionInfoHead {
    int age;
    const char* version;
    unsigned int versionNum;
    const char* host;
    int features;
    const char* sslVersion;
    long sslVersionNum;
    const char* libzVersion;
    const char* const* protocols;
};

struct Libcurl {
    CurlInitFn init = nullptr;
    CurlSetoptFn setopt = nullptr;
    CurlPerformFn perform = nullptr;
    CurlGetinfoFn getinfo = nullptr;
    CurlCleanupFn cleanup = nullptr;
    CurlVersionInfoFn versionInfo = nullptr;
    bool ok = false;
};

template <typename Fn>
Fn symbol(void* handle, const char* name) {
    return reinterpret_cast<Fn>(dlsym(handle, name));
}

// Loaded once and never closed: the host may hold libcurl too, and closing it from a plugin could pull it out from
// under the host.
const Libcurl& libcurl() {
    static Libcurl lib;
    static std::once_flag once;
    std::call_once(once, [] {
        void* handle = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) {
            handle = dlopen("libcurl-gnutls.so.4", RTLD_NOW | RTLD_LOCAL);
        }
        if (handle == nullptr) {
            return;
        }
        auto globalInit = symbol<CurlGlobalInitFn>(handle, "curl_global_init");
        lib.init = symbol<CurlInitFn>(handle, "curl_easy_init");
        lib.setopt = symbol<CurlSetoptFn>(handle, "curl_easy_setopt");
        lib.perform = symbol<CurlPerformFn>(handle, "curl_easy_perform");
        lib.getinfo = symbol<CurlGetinfoFn>(handle, "curl_easy_getinfo");
        lib.cleanup = symbol<CurlCleanupFn>(handle, "curl_easy_cleanup");
        lib.versionInfo = symbol<CurlVersionInfoFn>(handle, "curl_version_info");
        lib.ok = globalInit != nullptr && lib.init != nullptr && lib.setopt != nullptr && lib.perform != nullptr &&
                 lib.getinfo != nullptr && lib.cleanup != nullptr && lib.versionInfo != nullptr &&
                 globalInit(kCurlGlobalDefault) == kCurlOk;
    });
    return lib;
}

std::size_t appendBody(char* data, std::size_t size, std::size_t count, void* userdata) {
    static_cast<std::string*>(userdata)->append(data, size * count);
    return size * count;
}

} // namespace

std::string fetchWithLibcurl(const std::string& url) {
    const Libcurl& lib = libcurl();
    if (!lib.ok) {
        return {};
    }
    void* curl = lib.init();
    if (curl == nullptr) {
        return {};
    }

    std::string body;
    // Long options take a long, string options a const char*, the callback a function pointer (curl_easy_setopt).
    lib.setopt(curl, kOptUrl, url.c_str());
    lib.setopt(curl, kOptProtocols, kProtoHttpOrHttps);
    lib.setopt(curl, kOptRedirProtocols, kProtoHttpOrHttps);
    lib.setopt(curl, kOptFollowLocation, 1L);
    lib.setopt(curl, kOptMaxRedirs, 5L);
    lib.setopt(curl, kOptConnectTimeout, 10L);
    lib.setopt(curl, kOptTimeout, 20L);
    lib.setopt(curl, kOptNoSignal, 1L); // runs on the checker's worker thread: no SIGALRM
    lib.setopt(curl, kOptUserAgent, "KrateAudio/1.0");
    lib.setopt(curl, kOptWriteFunction, static_cast<CurlWriteFn>(&appendBody));
    lib.setopt(curl, kOptWriteData, static_cast<void*>(&body));

    long status = 0;
    const bool ok = lib.perform(curl) == kCurlOk && lib.getinfo(curl, kInfoResponseCode, &status) == kCurlOk &&
                    status == 200;
    lib.cleanup(curl);
    return ok ? body : std::string{};
}

bool libcurlSupportsHttps() {
    const Libcurl& lib = libcurl();
    if (!lib.ok) {
        return false;
    }
    const auto* info = static_cast<const CurlVersionInfoHead*>(lib.versionInfo(kCurlVersionFourth));
    if (info == nullptr || info->protocols == nullptr) {
        return false;
    }
    for (const char* const* p = info->protocols; *p != nullptr; ++p) {
        if (std::strcmp(*p, "https") == 0) {
            return true;
        }
    }
    return false;
}

} // namespace Krate::Plugins::detail
