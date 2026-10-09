#include "wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h — copy include/secrets.example.h to include/secrets.h and set your Wi-Fi credentials."
#endif

const char* wifiStateName(WifiState state) {
    switch (state) {
        case WifiState::Connecting: return "Connecting";
        case WifiState::Connected: return "Connected";
        case WifiState::Disconnected: return "No Wi-Fi";
    }
    return "?";
}

void WifiManager::begin() {
    WiFi.persistent(false);       // don't write credentials to NVS on every begin()
    WiFi.setHostname(OTA_HOSTNAME);  // must precede WiFi.mode()
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);         // mains powered; modem sleep would inflate latency readings
    WiFi.setAutoReconnect(true);

    // Log why the station dropped/failed (e.g. NO_AP_FOUND = SSID not visible on
    // 2.4 GHz, AUTH_FAIL/HANDSHAKE_TIMEOUT = wrong password). Only on change.
    WiFi.onEvent(
        [](WiFiEvent_t, WiFiEventInfo_t info) {
            static uint8_t lastReason = 0;
            const uint8_t reason = info.wifi_sta_disconnected.reason;
            if (reason == lastReason) return;
            lastReason = reason;
            Serial.printf("[wifi] disconnect reason %u: %s\n", reason,
                          WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)));
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    const uint32_t now = millis();
    disconnectedSinceMs_ = now;
    startConnect(now);
}

void WifiManager::startConnect(uint32_t now) {
    Serial.printf("[wifi] connecting to \"%s\"\n", WIFI_SSID);
    WiFi.disconnect(false, false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastAttemptMs_ = now;
}

void WifiManager::onConnected() {
    char ip[16];
    formatIp(ip, sizeof(ip));
    Serial.printf("[wifi] connected, ip=%s rssi=%d dBm\n", ip, WiFi.RSSI());
    state_ = WifiState::Connected;
    scannedThisOutage_ = false;
}

void WifiManager::loop() {
    const uint32_t now = millis();
    const bool linkUp = WiFi.status() == WL_CONNECTED;

    if (linkUp) {
        if (state_ != WifiState::Connected) onConnected();
        return;
    }

    if (state_ == WifiState::Connected) {
        Serial.println("[wifi] connection lost, reconnecting");
        state_ = WifiState::Connecting;
        disconnectedSinceMs_ = now;
        lastAttemptMs_ = now;  // give the driver's auto-reconnect a chance first
    }

    const uint32_t downFor = now - disconnectedSinceMs_;

    if (state_ == WifiState::Connecting && downFor >= WIFI_CONNECT_TIMEOUT_MS) {
        Serial.println("[wifi] not connected yet, retrying in background");
        state_ = WifiState::Disconnected;
        startDiagnosticScan();
    }

    if (scanning_) {
        if (!finishDiagnosticScan()) return;  // still scanning
        startConnect(now);
    }

    if (now - lastAttemptMs_ >= WIFI_RETRY_INTERVAL_MS) {
        startConnect(now);
    }

    if (downFor >= WIFI_REBOOT_AFTER_MS) {
        Serial.printf("[wifi] no connection for %lu min, restarting\n",
                      static_cast<unsigned long>(downFor / 60000));
        Serial.flush();
        delay(100);
        ESP.restart();
    }
}

// Once per outage, list the 2.4 GHz networks the radio can see. Makes SSID
// typos, 5 GHz-only networks and curly-apostrophe hotspot names obvious.
void WifiManager::startDiagnosticScan() {
    if (scannedThisOutage_) return;
    scannedThisOutage_ = true;
    WiFi.disconnect(false, false);
    if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
        Serial.println("[wifi] scan failed to start");
        return;
    }
    scanning_ = true;
}

bool WifiManager::finishDiagnosticScan() {
    const int16_t count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) return false;

    scanning_ = false;
    if (count < 0) {
        Serial.println("[wifi] scan failed");
        return true;
    }

    Serial.printf("[wifi] %d networks visible (looking for \"%s\"):\n", count, WIFI_SSID);
    for (int16_t i = 0; i < count; ++i) {
        const String name = WiFi.SSID(i);
        Serial.printf("[wifi]   \"%s\" ch%d %d dBm%s\n", name.length() ? name.c_str() : "(hidden)",
                      WiFi.channel(i), WiFi.RSSI(i), name == WIFI_SSID ? "  <-- match" : "");
    }
    WiFi.scanDelete();
    return true;
}

const char* WifiManager::ssid() const { return WIFI_SSID; }

int WifiManager::rssi() const { return isConnected() ? WiFi.RSSI() : 0; }

void WifiManager::formatIp(char* buf, size_t len) const {
    if (!isConnected() && WiFi.status() != WL_CONNECTED) {
        snprintf(buf, len, "-");
        return;
    }
    const IPAddress ip = WiFi.localIP();
    snprintf(buf, len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}
