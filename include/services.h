#pragma once

#include "service_config.h"

// ===========================================================================
//  DEFAULT monitored endpoints, used until a list is saved on the board.
//  To change the list without flashing, use setup/services.html (USB),
//  http://service-monitor.local/ or tools/push-services.sh (see README).
//  Changes here only apply to boards without a saved list (or after
//  `svc defaults`). Flash with: pio run -e t-display-s3-ota -t upload
//
//  Two kinds of checks:
//
//    statuspage("Name", "https://status.example.com")
//        Provider-reported health from an Atlassian Statuspage
//        (reads /api/v2/status.json). Shows "minor"/"major" when the
//        provider declares an incident, even if the site still responds.
//        Examples: status.claude.com, www.githubstatus.com,
//        www.cloudflarestatus.com, status.openai.com, www.atlassian-status.com
//
//    http("Name", "https://host/path")          any 2xx/3xx is healthy
//    http("Name", "https://host/health", 200)   require an exact status code
//        Reachability from this network: DNS + TCP + TLS + first response.
//        Good for your own APIs/health endpoints and for clouds without a
//        Statuspage. Redirects are not followed (a 3xx counts as healthy).
//
//  Order here is the display order; DOWN/DEGRADED entries float to the top.
//  5 rows fit on screen, the rest show as "+N more" (all healthy by then).
// ===========================================================================

constexpr ServiceConfig SERVICES[] = {
    statuspage("Claude",       "https://status.claude.com"),
    statuspage("GitHub",       "https://www.githubstatus.com"),
    statuspage("Cloudflare",   "https://www.cloudflarestatus.com"),
    http("AWS eu-central-1",   "https://sts.eu-central-1.amazonaws.com/"),
    http("Google",             "https://www.google.com/generate_204", 204),
};
