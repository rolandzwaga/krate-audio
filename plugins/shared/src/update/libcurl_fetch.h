#pragma once

// ==============================================================================
// Linux HTTP(S) GET for the update checker
// ==============================================================================
// libcurl is loaded with dlopen at first use (libcurl.so.4, or Debian's
// libcurl-gnutls.so.4), so the plugins carry no TLS stack and no link-time
// dependency. Without libcurl every fetch returns empty and the update check
// stays silent, as it did before. Linux only (plugins/shared/CMakeLists.txt).
// ==============================================================================

#include <string>

namespace Krate::Plugins::detail {

/// GET url over http or https, following up to 5 redirects.
/// @return the body when the final response is 200, empty on any failure
std::string fetchWithLibcurl(const std::string& url);

/// True when libcurl loaded and lists https among its protocols.
bool libcurlSupportsHttps();

} // namespace Krate::Plugins::detail
