#pragma once

// Types and helpers for include/services.h. You normally don't edit this file.

#include <stddef.h>

enum class CheckType : unsigned char {
    Http,        // reachability: status code + latency
    StatusPage,  // provider-reported status from an Atlassian Statuspage
};

struct ServiceConfig {
    const char* name;
    const char* url;
    CheckType type;
    int expectedCode;  // Http only; 0 = any 2xx/3xx is healthy
};

// http("Name", "https://host/path")        any 2xx/3xx = healthy, 4xx = degraded, 5xx/error = down
// http("Name", "https://host/health", 200) exactly 200 = healthy, anything else is flagged
constexpr ServiceConfig http(const char* name, const char* url, int expectedCode = 0) {
    return ServiceConfig{name, url, CheckType::Http, expectedCode};
}

// statuspage("Name", "https://status.example.com")
// Reads <base>/api/v2/status.json: none = healthy, minor/maintenance = degraded,
// major/critical = down. Works for any Atlassian Statuspage-hosted status site.
constexpr ServiceConfig statuspage(const char* name, const char* baseUrl) {
    return ServiceConfig{name, baseUrl, CheckType::StatusPage, 0};
}
