#pragma once

#include <stddef.h>
#include <stdint.h>

enum class WifiState : uint8_t {
    NotConfigured,  // no credentials yet; waiting for USB setup
    Connecting,     // first attempt or reconnecting after a drop
    Connected,
    Disconnected,   // gave up waiting (UI shows "No Wi-Fi"); retries continue
};

const char* wifiStateName(WifiState state);

// Non-blocking Wi-Fi station manager. Call loop() frequently.
//
// Credentials come from NVS (set over USB, see SerialConsole). If none are
// stored, the optional WIFI_SSID / WIFI_PASSWORD from secrets.h are used.
class WifiManager {
public:
    static constexpr size_t kMaxSsidLen = 32;
    static constexpr size_t kMaxPasswordLen = 63;

    void begin();
    void loop();

    // Saves to NVS and reconnects. Returns false if the values are invalid.
    bool setCredentials(const char* ssid, const char* password);
    // Erases stored credentials; falls back to secrets.h if present.
    void clearCredentials();
    // True once after credentials were changed (set or cleared).
    bool consumeCredentialsChanged();

    WifiState state() const { return state_; }
    bool isConnected() const { return state_ == WifiState::Connected; }
    const char* ssid() const { return ssid_; }
    int rssi() const;
    void formatIp(char* buf, size_t len) const;

private:
    void loadCredentials();
    void restart();
    void startConnect(uint32_t now);
    void onConnected();
    void startDiagnosticScan();
    bool finishDiagnosticScan();  // true once the scan is done (or failed)

    char ssid_[kMaxSsidLen + 1] = "";
    char password_[kMaxPasswordLen + 1] = "";

    WifiState state_ = WifiState::Connecting;
    bool credentialsChanged_ = false;
    bool scanning_ = false;
    bool scannedThisOutage_ = false;
    uint32_t lastAttemptMs_ = 0;
    uint32_t disconnectedSinceMs_ = 0;
};
