#include "time_manager.h"

#include <Arduino.h>

#include "config.h"

namespace {
constexpr time_t MIN_VALID_EPOCH = 1700000000;  // 2023-11-14
}

void TimeManager::begin() {
    if (started_) return;
    configTzTime(TZ_INFO, NTP_SERVER, NTP_SERVER_2);
    started_ = true;
    Serial.printf("[time] NTP %s / %s, TZ %s\n", NTP_SERVER, NTP_SERVER_2, TZ_INFO);
}

void TimeManager::loop() {
    if (!started_ || reportedSync_ || !isSynced()) return;
    reportedSync_ = true;
    char buf[32];
    format(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", "");
    Serial.printf("[time] synchronized: %s\n", buf);
}

bool TimeManager::isSynced() const { return time(nullptr) >= MIN_VALID_EPOCH; }

void TimeManager::format(char* buf, size_t len, const char* fmt, const char* fallback) const {
    if (!isSynced()) {
        snprintf(buf, len, "%s", fallback);
        return;
    }
    formatEpoch(time(nullptr), buf, len, fmt);
}

void TimeManager::formatEpoch(time_t epoch, char* buf, size_t len, const char* fmt) {
    struct tm local;
    localtime_r(&epoch, &local);
    strftime(buf, len, fmt, &local);
}
