#pragma once

#include <stddef.h>
#include <stdint.h>

// Monitored endpoints live in include/services.h.
#include "services.h"

constexpr size_t SERVICE_COUNT = sizeof(SERVICES) / sizeof(SERVICES[0]);
static_assert(SERVICE_COUNT > 0, "Configure at least one service in services.h");
static_assert(SERVICE_COUNT <= 16, "Too many services for one check cycle");

// ---------------------------------------------------------------------------
// Service checks
// ---------------------------------------------------------------------------
constexpr uint32_t CHECK_INTERVAL_MS   = 10 * 1000;  // start-to-start interval between check cycles
constexpr uint32_t HTTP_TIMEOUT_MS     = 4000;       // applied to connect, TLS handshake and response separately
// Every check opens a fresh TLS connection, and the ESP32's handshake alone
// takes ~600-800 ms, so this sits above that baseline.
constexpr uint32_t DEGRADED_LATENCY_MS = 1500;       // slower than this => DEGRADED
constexpr size_t   STATUSPAGE_MAX_BODY = 4096;       // status.json is ~250 bytes; refuse anything huge

// Show DOWN/DEGRADED services at the top of the list (keeps problems visible
// when there are more services than fit on screen).
constexpr bool SORT_SERVICES_BY_SEVERITY = true;

// ---------------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------------
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15 * 1000;       // after this the UI shows "No Wi-Fi"
constexpr uint32_t WIFI_RETRY_INTERVAL_MS  = 15 * 1000;       // re-issue WiFi.begin() while disconnected
constexpr uint32_t WIFI_REBOOT_AFTER_MS    = 30 * 60 * 1000;  // restart after this long without Wi-Fi

// ---------------------------------------------------------------------------
// Network identity / OTA
// ---------------------------------------------------------------------------
#define OTA_HOSTNAME "service-monitor"   // also the DHCP hostname; OTA target is <hostname>.local
constexpr uint16_t OTA_PORT = 3232;

// ---------------------------------------------------------------------------
// Time (POSIX TZ string handles DST automatically)
// ---------------------------------------------------------------------------
#define NTP_SERVER   "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define TZ_INFO      "EET-2EEST,M3.5.0/3,M10.5.0/4"   // Europe/Sofia

// ---------------------------------------------------------------------------
// UI / misc
// ---------------------------------------------------------------------------
// Landscape 320x170. 1 or 3 — swap if the picture is upside down for how
// your board sits on the desk (they differ in which side the USB-C port is on).
constexpr uint8_t DISPLAY_ROTATION = 1;

constexpr uint32_t UI_REFRESH_MS         = 250;    // how often widgets are refreshed from state
constexpr uint32_t BOOT_TIME_SYNC_WAIT_MS = 4000;  // max wait for NTP on the splash screen
constexpr uint32_t BOOT_NO_WIFI_SHOW_MS  = 3000;   // how long the "No Wi-Fi" splash stays up
constexpr uint32_t BUTTON_DEBOUNCE_MS    = 30;

// Restart if free internal heap stays below this (leak/fragmentation safety net).
constexpr uint32_t LOW_HEAP_RESTART_BYTES = 16 * 1024;
constexpr uint32_t LOW_HEAP_GRACE_MS      = 60 * 1000;

#define FIRMWARE_VERSION "1.0.0"
