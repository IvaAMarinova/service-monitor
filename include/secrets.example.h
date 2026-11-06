#pragma once

// OPTIONAL. The firmware builds without this file: Wi-Fi is normally set over
// USB with setup/index.html and stored on the board.
//
// Copy to include/secrets.h (git-ignored) only if you want built-in values.
// Credentials saved over USB always take priority over these.
//
// Don't leave your own Wi-Fi password here when flashing a board you give away:
// it is compiled into the firmware.

// #define WIFI_SSID     "your-wifi-ssid"
// #define WIFI_PASSWORD "your-wifi-password"

// Optional password for OTA uploads and the service editor web page
// (user "admin"). If set, export the same value as SERVICE_MONITOR_OTA_PASSWORD
// before an OTA upload or tools/push-services.sh.
// #define OTA_PASSWORD  ""
