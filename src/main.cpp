// Service Monitor for LILYGO T-Display S3
//
// Core 1 (Arduino loop): Wi-Fi state machine, OTA, buttons, LVGL rendering.
// Core 0 (svc-monitor task): HTTP(S) checks, results shared via a mutex-guarded snapshot.

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>

#include "board_pins.h"
#include "button.h"
#include "config.h"
#include "display_manager.h"
#include "ota_manager.h"
#include "service_monitor.h"
#include "time_manager.h"
#include "wifi_manager.h"

namespace {

enum class BootPhase : uint8_t {
    ConnectingWifi,  // splash: "connecting to wifi..."
    SyncingTime,     // splash: "syncing time..."
    NoWifi,          // splash: "No Wi-Fi" for a moment, then dashboard
    Running,
};

DisplayManager display;
WifiManager wifi;
ServiceMonitor monitor;
OtaManager ota;
TimeManager timeManager;
Button screenButton(BoardPins::BUTTON_1);
Button refreshButton(BoardPins::BUTTON_2);

BootPhase bootPhase = BootPhase::ConnectingWifi;
uint32_t phaseStartMs = 0;
bool networkServicesStarted = false;
bool otaActive = false;
uint32_t otaErrorAtMs = 0;

bool refreshPending = false;
uint32_t refreshRequestedAtCycle = 0;

uint32_t lastUiUpdateMs = 0;
uint32_t lowHeapSinceMs = 0;

MonitorSnapshot snapshot;  // static storage: avoids a large copy on the loop stack
DeviceStatus deviceStatus;

void enterPhase(BootPhase phase) {
    bootPhase = phase;
    phaseStartMs = millis();
    switch (phase) {
        case BootPhase::ConnectingWifi:
            display.showSplash("connecting to wifi...", wifi.ssid(), SplashTone::Normal);
            break;
        case BootPhase::SyncingTime:
            display.showSplash("syncing time...", nullptr, SplashTone::Normal);
            break;
        case BootPhase::NoWifi:
            display.showSplash("no wifi", "retrying in background", SplashTone::Error);
            break;
        case BootPhase::Running:
            Serial.println("[app] dashboard");
            display.showDashboard();
            break;
    }
}

void updateBootPhase() {
    const uint32_t inPhase = millis() - phaseStartMs;
    switch (bootPhase) {
        case BootPhase::ConnectingWifi:
            if (wifi.isConnected()) enterPhase(BootPhase::SyncingTime);
            else if (wifi.state() == WifiState::Disconnected) enterPhase(BootPhase::NoWifi);
            break;
        case BootPhase::SyncingTime:
            if (timeManager.isSynced() || inPhase >= BOOT_TIME_SYNC_WAIT_MS) enterPhase(BootPhase::Running);
            break;
        case BootPhase::NoWifi:
            if (wifi.isConnected()) enterPhase(BootPhase::SyncingTime);
            else if (inPhase >= BOOT_NO_WIFI_SHOW_MS) enterPhase(BootPhase::Running);
            break;
        case BootPhase::Running:
            break;
    }
}

// NTP and OTA need a live network stack; start them on the first connection.
void startNetworkServicesOnce() {
    if (networkServicesStarted || !wifi.isConnected()) return;
    networkServicesStarted = true;
    timeManager.begin();
    ota.begin();
}

void onOtaEvent(OtaEvent event, uint8_t percent, const char* message) {
    static uint8_t lastShown = 255;
    switch (event) {
        case OtaEvent::Start:
            otaActive = true;
            lastShown = 255;
            monitor.setPaused(true);
            display.showOta(0, nullptr);
            display.tick();
            break;
        case OtaEvent::Progress:
            // handle() blocks during the upload, so render from here — but only on change.
            if (percent != lastShown) {
                lastShown = percent;
                display.showOta(percent, nullptr);
                display.tick();
            }
            break;
        case OtaEvent::End:
            display.showOta(100, "Rebooting...");
            display.tick();
            break;
        case OtaEvent::Error:
            display.showOta(0, message);
            display.tick();
            otaErrorAtMs = millis();
            break;
    }
}

void handleOtaErrorRecovery() {
    if (!otaActive || otaErrorAtMs == 0 || millis() - otaErrorAtMs < 3000) return;
    otaActive = false;
    otaErrorAtMs = 0;
    monitor.setPaused(false);
    display.leaveOta();
}

void handleButtons() {
    screenButton.update();
    refreshButton.update();
    if (otaActive) return;

    if (screenButton.wasPressed()) {
        if (bootPhase != BootPhase::Running) enterPhase(BootPhase::Running);
        else display.nextScreen();
    }

    if (refreshButton.wasPressed()) {
        Serial.println("[app] refresh requested");
        refreshPending = wifi.isConnected();
        refreshRequestedAtCycle = snapshot.cycleCount;
        monitor.requestRefresh();
    }
}

void collectDeviceStatus(DeviceStatus& out) {
    out.wifiState = wifi.state();
    out.ssid = wifi.ssid();
    wifi.formatIp(out.ip, sizeof(out.ip));
    out.rssi = wifi.rssi();
    out.uptimeSeconds = static_cast<uint64_t>(esp_timer_get_time() / 1000000);
    out.freeHeap = ESP.getFreeHeap();
    out.minFreeHeap = ESP.getMinFreeHeap();
    out.timeSynced = timeManager.isSynced();
    out.refreshPending = refreshPending;
}

void refreshUi() {
    const uint32_t now = millis();
    if (now - lastUiUpdateMs < UI_REFRESH_MS) return;
    lastUiUpdateMs = now;

    monitor.getSnapshot(snapshot);
    if (refreshPending && (snapshot.cycleCount != refreshRequestedAtCycle || !wifi.isConnected())) {
        refreshPending = false;
    }
    collectDeviceStatus(deviceStatus);
    display.update(snapshot, deviceStatus);
}

// Safety net for a device that runs for months: reboot only if internal heap
// stays critically low for a sustained period.
void checkHeapHealth() {
    const uint32_t now = millis();
    if (ESP.getFreeHeap() >= LOW_HEAP_RESTART_BYTES) {
        lowHeapSinceMs = 0;
        return;
    }
    if (lowHeapSinceMs == 0) {
        lowHeapSinceMs = now;
        Serial.printf("[app] low heap: %lu bytes\n", static_cast<unsigned long>(ESP.getFreeHeap()));
    } else if (now - lowHeapSinceMs >= LOW_HEAP_GRACE_MS) {
        Serial.println("[app] heap critically low for too long, restarting");
        Serial.flush();
        ESP.restart();
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
    // On a USB charger nobody drains the CDC buffer; drop logs instead of
    // stalling up to 100 ms per write.
    Serial.setTxTimeoutMs(0);
#endif
    delay(200);  // let USB CDC enumerate so early logs are not lost
    Serial.printf("\n[app] Service Monitor v%s, %u services\n", FIRMWARE_VERSION,
                  static_cast<unsigned>(SERVICE_COUNT));

    if (!display.begin()) Serial.println("[app] continuing without display");

    screenButton.begin();
    refreshButton.begin();

    wifi.begin();
    enterPhase(BootPhase::ConnectingWifi);
    display.tick();

    ota.setCallback(onOtaEvent);
    if (!monitor.begin()) Serial.println("[app] service monitor failed to start");

    // Arduino feeds the loop task watchdog after every loop() iteration.
    enableLoopWDT();
}

void loop() {
    wifi.loop();
    startNetworkServicesOnce();
    ota.loop();
    timeManager.loop();

    handleButtons();
    handleOtaErrorRecovery();

    if (!otaActive) {
        updateBootPhase();
        refreshUi();
        checkHeapHealth();
    }

    display.tick();
    delay(5);
}
