#include "wifi_manager.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <string.h>

#include "config.h"

// secrets.h is optional: credentials are normally set over USB and kept in NVS.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

namespace {

constexpr const char* kPrefsNamespace = "wifi";

// Last logged disconnect reason; reset so a new attempt logs its reason again.
volatile uint8_t gLastDisconnectReason = 0;

}  // namespace

const char* wifiStateName(WifiState state) {
    switch (state) {
        case WifiState::NotConfigured: return "Not set up";
        case WifiState::Connecting: return "Connecting";
        case WifiState::Connected: return "Connected";
        case WifiState::Disconnected: return "No Wi-Fi";
    }
    return "?";
}

void WifiManager::loadCredentials() {
    ssid_[0] = '\0';
    password_[0] = '\0';

    Preferences prefs;
    // Read-write so a missing namespace (first boot) is created, not an error.
    if (prefs.begin(kPrefsNamespace, false)) {
        if (prefs.isKey("ssid")) prefs.getString("ssid", ssid_, sizeof(ssid_));
        if (prefs.isKey("pass")) prefs.getString("pass", password_, sizeof(password_));
        prefs.end();
    }
    if (ssid_[0] != '\0') {
        Serial.printf("[wifi] using saved network \"%s\"\n", ssid_);
        return;
    }

    password_[0] = '\0';
    strlcpy(ssid_, WIFI_SSID, sizeof(ssid_));
    strlcpy(password_, WIFI_PASSWORD, sizeof(password_));
    if (ssid_[0] != '\0') Serial.printf("[wifi] using built-in network \"%s\"\n", ssid_);
    else Serial.println("[wifi] not set up; connect over USB and send credentials (type 'help')");
}

// (Re)start the connection state machine with the current credentials.
void WifiManager::restart() {
    if (scanning_) {
        WiFi.scanDelete();
        scanning_ = false;
    }
    scannedThisOutage_ = false;
    gLastDisconnectReason = 0;
    WiFi.disconnect(false, false);

    const uint32_t now = millis();
    disconnectedSinceMs_ = now;
    lastAttemptMs_ = now;
    if (ssid_[0] == '\0') {
        state_ = WifiState::NotConfigured;
        return;
    }
    state_ = WifiState::Connecting;
    startConnect(now);
}

bool WifiManager::setCredentials(const char* ssid, const char* password) {
    const size_t ssidLen = strlen(ssid);
    const size_t passLen = strlen(password);
    if (ssidLen == 0 || ssidLen > kMaxSsidLen) return false;
    // WPA2 passphrases are 8..63 characters; empty means an open network.
    if (passLen > kMaxPasswordLen || (passLen > 0 && passLen < 8)) return false;

    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) return false;
    const bool ok = prefs.putString("ssid", ssid) == ssidLen && prefs.putString("pass", password) == passLen;
    prefs.end();
    if (!ok) return false;

    strlcpy(ssid_, ssid, sizeof(ssid_));
    strlcpy(password_, password, sizeof(password_));
    Serial.printf("[wifi] saved network \"%s\"\n", ssid_);
    credentialsChanged_ = true;
    restart();
    return true;
}

void WifiManager::clearCredentials() {
    Preferences prefs;
    if (prefs.begin(kPrefsNamespace, false)) {
        prefs.clear();
        prefs.end();
    }
    Serial.println("[wifi] saved network erased");
    loadCredentials();
    credentialsChanged_ = true;
    restart();
}

bool WifiManager::consumeCredentialsChanged() {
    const bool changed = credentialsChanged_;
    credentialsChanged_ = false;
    return changed;
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
            const uint8_t reason = info.wifi_sta_disconnected.reason;
            if (reason == gLastDisconnectReason) return;
            gLastDisconnectReason = reason;
            Serial.printf("[wifi] disconnect reason %u: %s\n", reason,
                          WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)));
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    loadCredentials();
    restart();
}

void WifiManager::startConnect(uint32_t now) {
    Serial.printf("[wifi] connecting to \"%s\"\n", ssid_);
    WiFi.disconnect(false, false);
    WiFi.begin(ssid_, password_[0] != '\0' ? password_ : nullptr);
    lastAttemptMs_ = now;
}

void WifiManager::onConnected() {
    char ip[16];
    formatIp(ip, sizeof(ip));
    Serial.printf("[wifi] connected, ip=%s rssi=%d dBm\n", ip, WiFi.RSSI());
    state_ = WifiState::Connected;
    scannedThisOutage_ = false;
    gLastDisconnectReason = 0;
}

void WifiManager::loop() {
    if (state_ == WifiState::NotConfigured) return;

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

    Serial.printf("[wifi] %d networks visible (looking for \"%s\"):\n", count, ssid_);
    for (int16_t i = 0; i < count; ++i) {
        const String name = WiFi.SSID(i);
        Serial.printf("[wifi]   \"%s\" ch%d %d dBm%s\n", name.length() ? name.c_str() : "(hidden)",
                      WiFi.channel(i), WiFi.RSSI(i), name == ssid_ ? "  <-- match" : "");
    }
    WiFi.scanDelete();
    return true;
}

int WifiManager::rssi() const { return isConnected() ? WiFi.RSSI() : 0; }

void WifiManager::formatIp(char* buf, size_t len) const {
    if (!isConnected() && WiFi.status() != WL_CONNECTED) {
        snprintf(buf, len, "-");
        return;
    }
    const IPAddress ip = WiFi.localIP();
    snprintf(buf, len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}
