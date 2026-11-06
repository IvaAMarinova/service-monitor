#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <time.h>

#include "config.h"
#include "service_list.h"

enum class ServiceState : uint8_t {
    Unknown,  // not checked yet
    Healthy,
    Degraded,
    Down,
};

const char* serviceStateName(ServiceState state);

struct ServiceStatus {
    ServiceState state = ServiceState::Unknown;
    int httpCode = 0;            // >0 HTTP status, <0 HTTPClient error code
    uint32_t latencyMs = 0;
    uint32_t checkedAtMs = 0;    // millis() of the last check
    time_t checkedAtEpoch = 0;   // wall clock of the last check, 0 if NTP not synced
    char badge[10] = "";         // UI status word when not healthy ("minor", "503", "DOWN"); empty = "ok"/"slow"
    char error[48] = "";         // human-readable detail for logs
};

// Plain-data copy of everything the UI needs; safe to read without locks.
struct MonitorSnapshot {
    size_t serviceCount = 0;
    char names[MAX_SERVICES][sizeof(ServiceEntry::name)] = {};
    ServiceStatus services[MAX_SERVICES];
    uint32_t totalChecks = 0;
    uint32_t cycleCount = 0;
    bool hasCompletedCycle = false;
    uint32_t lastCycleEndMs = 0;
    time_t lastCycleEndEpoch = 0;
    bool cycleInProgress = false;
};

// Runs HTTP(S) checks on a dedicated FreeRTOS task (core 0) so that slow or
// failing services never block the UI loop on core 1.
class ServiceMonitor {
public:
    bool begin(const ServiceList& services);
    // Replaces the list (results reset) and starts a new cycle right away.
    void setServices(const ServiceList& services);
    void getServices(ServiceList& out) const;
    void requestRefresh();
    void setPaused(bool paused);  // used while an OTA update is running
    void getSnapshot(MonitorSnapshot& out) const;

private:
    static void taskEntry(void* arg);
    void run();
    bool canCheck() const;
    void runCycle();
    void checkService(const ServiceEntry& service, ServiceStatus& result);
    static void classifyHttp(int httpCode, uint32_t latencyMs, int expectedCode, ServiceStatus& result);
    static void classifyStatusPage(const char* body, ServiceStatus& result);

    mutable SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    volatile bool paused_ = false;
    MonitorSnapshot state_;
    ServiceList services_;      // guarded by mutex_
    uint32_t listVersion_ = 0;  // bumped by setServices(); stale results are dropped

    // Reused across checks; HTTPClient::end() stops them after each request.
    WiFiClient plainClient_;
    WiFiClientSecure secureClient_;
};
