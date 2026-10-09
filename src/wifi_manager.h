#pragma once

#include <stddef.h>
#include <stdint.h>

enum class WifiState : uint8_t {
    Connecting,    // first attempt or reconnecting after a drop
    Connected,
    Disconnected,  // gave up waiting (UI shows "No Wi-Fi"); retries continue
};

const char* wifiStateName(WifiState state);

// Non-blocking Wi-Fi station manager. Call loop() frequently.
class WifiManager {
public:
    void begin();
    void loop();

    WifiState state() const { return state_; }
    bool isConnected() const { return state_ == WifiState::Connected; }
    const char* ssid() const;
    int rssi() const;
    void formatIp(char* buf, size_t len) const;

private:
    void startConnect(uint32_t now);
    void onConnected();
    void startDiagnosticScan();
    bool finishDiagnosticScan();  // true once the scan is done (or failed)

    WifiState state_ = WifiState::Connecting;
    bool scanning_ = false;
    bool scannedThisOutage_ = false;
    uint32_t lastAttemptMs_ = 0;
    uint32_t disconnectedSinceMs_ = 0;
};
