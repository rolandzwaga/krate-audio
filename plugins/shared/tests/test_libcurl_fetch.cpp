// Linux only (tests/CMakeLists.txt lists it under PLATFORM_ID:Linux). The in-editor update check fetches the feed
// through libcurl loaded at runtime: the feed is https, and the plugins' github.io address answers with a 301 to
// krateaudio.com, so the fetch must speak TLS and follow redirects.
#include <catch2/catch_test_macros.hpp>
#include "update/libcurl_fetch.h"

#include <httplib.h>

#include <string>
#include <thread>

using Krate::Plugins::detail::fetchWithLibcurl;
using Krate::Plugins::detail::libcurlSupportsHttps;

namespace {

constexpr const char* kBody = R"({"schema_version":1,"plugins":{}})";

// A plain-HTTP server on a free loopback port, listening on its own thread for the lifetime of the object.
class LoopbackServer {
public:
    LoopbackServer() {
        server_.Get("/versions.json", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(kBody, "application/json");
        });
        server_.Get("/old/versions.json", [this](const httplib::Request&, httplib::Response& res) {
            res.set_redirect(url("/versions.json"), 301);
        });
        server_.Get("/missing.json", [](const httplib::Request&, httplib::Response& res) {
            res.status = 404;
            res.set_content("not found", "text/plain");
        });
        port_ = server_.bind_to_any_port("127.0.0.1");
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        server_.wait_until_ready();
    }

    ~LoopbackServer() {
        server_.stop();
        thread_.join();
    }

    LoopbackServer(const LoopbackServer&) = delete;
    LoopbackServer& operator=(const LoopbackServer&) = delete;

    [[nodiscard]] int port() const { return port_; }

    [[nodiscard]] std::string url(const std::string& path) const {
        return "http://127.0.0.1:" + std::to_string(port_) + path;
    }

private:
    httplib::Server server_;
    int port_ = -1;
    std::thread thread_;
};

} // namespace

TEST_CASE("libcurl fetch - the loaded libcurl speaks https", "[update]") {
    REQUIRE(libcurlSupportsHttps());
}

TEST_CASE("libcurl fetch - body, redirect and failures", "[update]") {
    LoopbackServer server;
    REQUIRE(server.port() > 0);

    SECTION("a 200 returns the body") {
        CHECK(fetchWithLibcurl(server.url("/versions.json")) == kBody);
    }

    SECTION("a 301 is followed to the final body") {
        CHECK(fetchWithLibcurl(server.url("/old/versions.json")) == kBody);
    }

    SECTION("a non-200 status returns empty") {
        CHECK(fetchWithLibcurl(server.url("/missing.json")).empty());
    }

    SECTION("a scheme other than http or https returns empty") {
        CHECK(fetchWithLibcurl("file:///etc/hostname").empty());
    }
}

TEST_CASE("libcurl fetch - a refused connection returns empty", "[update]") {
    int closedPort = 0;
    {
        LoopbackServer server;
        closedPort = server.port();
    }
    CHECK(fetchWithLibcurl("http://127.0.0.1:" + std::to_string(closedPort) + "/versions.json").empty());
}
