// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdlib>
#include <string>

// Which browser origins may call the API. The web UI is served by the Node
// server, a different origin from this device, so the browser needs CORS
// headers. They go only to origins on the local network: a page loaded from
// the internet must not be able to drive the valves through a LAN browser.
// The network itself is the trust boundary (docs/architecture.md); this only
// stops a public web page from using a LAN browser as its way in.
namespace cors {

namespace detail {

// "10.0.0.5" -> true and the four octets; anything else -> false.
inline bool parseIpv4(const std::string& host, int out[4]) {
  int part = 0;
  size_t i = 0;
  for (; part < 4; ++part) {
    if (i >= host.size() || host[i] < '0' || host[i] > '9') return false;
    int v = 0;
    size_t digits = 0;
    while (i < host.size() && host[i] >= '0' && host[i] <= '9') {
      v = v * 10 + (host[i] - '0');
      if (++digits > 3 || v > 255) return false;
      ++i;
    }
    out[part] = v;
    if (part < 3) {
      if (i >= host.size() || host[i] != '.') return false;
      ++i;
    }
  }
  return i == host.size();
}

inline bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() > suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace detail

// True for http(s) origins whose host is localhost, a private or link-local
// IPv4 address, a single-label name, or a .local/.lan/.home.arpa name.
inline bool isAllowedOrigin(const std::string& origin) {
  std::string rest;
  if (origin.rfind("http://", 0) == 0) rest = origin.substr(7);
  else if (origin.rfind("https://", 0) == 0) rest = origin.substr(8);
  else return false;
  if (rest.empty() || rest.find('/') != std::string::npos ||
      rest.find('@') != std::string::npos) {
    return false;
  }

  std::string host = rest;
  const size_t colon = rest.rfind(':');
  if (colon != std::string::npos) {
    const std::string port = rest.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    for (char c : port) if (c < '0' || c > '9') return false;
    if (std::atoi(port.c_str()) > 65535) return false;
    host = rest.substr(0, colon);
  }
  if (host.empty()) return false;
  for (char& c : host) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');

  int ip[4];
  if (detail::parseIpv4(host, ip)) {
    return ip[0] == 10 || ip[0] == 127 ||
           (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) ||
           (ip[0] == 192 && ip[1] == 168) ||
           (ip[0] == 169 && ip[1] == 254);
  }
  for (char c : host) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
    if (!ok) return false;
  }
  if (host == "localhost") return true;
  if (host.find('.') == std::string::npos) return true;  // "pi", "nas"
  return detail::endsWith(host, ".local") || detail::endsWith(host, ".lan") ||
         detail::endsWith(host, ".home.arpa");
}

}  // namespace cors
