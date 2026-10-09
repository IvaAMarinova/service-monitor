#include "service_monitor.h"

#include <HTTPClient.h>
#include <WiFi.h>

namespace {

constexpr uint32_t TASK_STACK_BYTES = 16 * 1024;  // mbedTLS handshake runs on this stack
constexpr UBaseType_t TASK_PRIORITY = 1;
constexpr BaseType_t TASK_CORE = 0;               // UI/loop runs on core 1
constexpr uint32_t WAIT_FOR_NETWORK_MS = 500;
constexpr time_t MIN_VALID_EPOCH = 1700000000;    // anything earlier means NTP not synced

time_t wallClockOrZero() {
    const time_t now = time(nullptr);
    return now >= MIN_VALID_EPOCH ? now : 0;
}

class LockGuard {
public:
    explicit LockGuard(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~LockGuard() { xSemaphoreGive(m_); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    SemaphoreHandle_t m_;
};

}  // namespace

const char* serviceStateName(ServiceState state) {
    switch (state) {
        case ServiceState::Unknown: return "UNKNOWN";
        case ServiceState::Healthy: return "HEALTHY";
        case ServiceState::Degraded: return "DEGRADED";
        case ServiceState::Down: return "DOWN";
    }
    return "?";
}

bool ServiceMonitor::begin() {
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) return false;

    // Status monitoring only needs reachability + status code, so certificates
    // are not validated. See README if you need strict TLS.
    secureClient_.setInsecure();
    secureClient_.setHandshakeTimeout((HTTP_TIMEOUT_MS + 999) / 1000);

    const BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "svc-monitor", TASK_STACK_BYTES, this,
                                                  TASK_PRIORITY, &task_, TASK_CORE);
    if (ok != pdPASS) {
        Serial.println("[monitor] failed to create task");
        return false;
    }
    Serial.printf("[monitor] %u services, interval %lu ms, timeout %lu ms\n",
                  static_cast<unsigned>(SERVICE_COUNT), static_cast<unsigned long>(CHECK_INTERVAL_MS),
                  static_cast<unsigned long>(HTTP_TIMEOUT_MS));
    return true;
}

void ServiceMonitor::requestRefresh() {
    if (task_ != nullptr) xTaskNotifyGive(task_);
}

void ServiceMonitor::setPaused(bool paused) { paused_ = paused; }

void ServiceMonitor::getSnapshot(MonitorSnapshot& out) const {
    LockGuard lock(mutex_);
    out = state_;
}

void ServiceMonitor::taskEntry(void* arg) { static_cast<ServiceMonitor*>(arg)->run(); }

bool ServiceMonitor::canCheck() const { return !paused_ && WiFi.status() == WL_CONNECTED; }

void ServiceMonitor::run() {
    bool hasRun = false;
    uint32_t lastCycleStartMs = 0;

    for (;;) {
        if (!canCheck()) {
            // Wake early on a refresh request; it is consumed and simply retried.
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WAIT_FOR_NETWORK_MS));
            continue;
        }

        if (hasRun) {
            const uint32_t elapsed = millis() - lastCycleStartMs;
            if (elapsed < CHECK_INTERVAL_MS) {
                // Sleeps until the interval elapses or requestRefresh() is called.
                if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CHECK_INTERVAL_MS - elapsed)) > 0) {
                    Serial.println("[monitor] manual refresh");
                }
                if (!canCheck()) continue;
            }
        }

        hasRun = true;
        lastCycleStartMs = millis();
        runCycle();
    }
}

void ServiceMonitor::runCycle() {
    {
        LockGuard lock(mutex_);
        state_.cycleInProgress = true;
    }

    for (size_t i = 0; i < SERVICE_COUNT; ++i) {
        if (!canCheck()) break;  // Wi-Fi dropped or OTA started mid-cycle

        ServiceStatus result;
        checkService(SERVICES[i], result);

        LockGuard lock(mutex_);
        state_.services[i] = result;
        state_.totalChecks++;
    }

    // Drop any refresh requested while this cycle was already running.
    ulTaskNotifyTake(pdTRUE, 0);

    LockGuard lock(mutex_);
    state_.cycleInProgress = false;
    state_.cycleCount++;
    state_.hasCompletedCycle = true;
    state_.lastCycleEndMs = millis();
    state_.lastCycleEndEpoch = wallClockOrZero();
}

void ServiceMonitor::checkService(const ServiceConfig& service, ServiceStatus& result) {
    const bool isStatusPage = service.type == CheckType::StatusPage;

    char url[192];
    if (isStatusPage) {
        size_t baseLen = strlen(service.url);
        if (baseLen > 0 && service.url[baseLen - 1] == '/') --baseLen;
        snprintf(url, sizeof(url), "%.*s/api/v2/status.json", static_cast<int>(baseLen), service.url);
    } else {
        snprintf(url, sizeof(url), "%s", service.url);
    }

    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);  // 3xx counts as healthy
    http.setUserAgent("ServiceMonitor/" FIRMWARE_VERSION " (ESP32-S3)");

    const bool isHttps = strncmp(url, "https://", 8) == 0;
    const bool configured = isHttps ? http.begin(secureClient_, url) : http.begin(plainClient_, url);

    result.checkedAtMs = millis();
    result.checkedAtEpoch = wallClockOrZero();

    if (!configured) {
        result.state = ServiceState::Down;
        result.httpCode = 0;
        snprintf(result.badge, sizeof(result.badge), "BAD URL");
        snprintf(result.error, sizeof(result.error), "Invalid URL");
        Serial.printf("[check] %-16s DOWN     invalid url \"%s\"\n", service.name, url);
        return;
    }

    const uint32_t start = millis();
    const int code = http.GET();  // returns after the status line + headers
    const uint32_t latency = millis() - start;

    if (isStatusPage && code == 200) {
        result.httpCode = code;
        result.latencyMs = latency;
        const int size = http.getSize();  // -1 when chunked
        if (size > static_cast<int>(STATUSPAGE_MAX_BODY)) {
            result.state = ServiceState::Degraded;
            snprintf(result.badge, sizeof(result.badge), "?");
            snprintf(result.error, sizeof(result.error), "Status response too large");
        } else {
            const String body = http.getString();
            classifyStatusPage(body.c_str(), result);
        }
    } else {
        // Plain checks only need headers; end() discards any body.
        classifyHttp(code, latency, isStatusPage ? 200 : service.expectedCode, result);
    }
    http.end();

    Serial.printf("[check] %-16s %-8s code=%d %lums%s%s\n", service.name, serviceStateName(result.state), code,
                  static_cast<unsigned long>(latency), result.error[0] ? " " : "", result.error);
}

void ServiceMonitor::classifyHttp(int httpCode, uint32_t latencyMs, int expectedCode, ServiceStatus& result) {
    result.httpCode = httpCode;
    result.latencyMs = latencyMs;
    result.badge[0] = '\0';
    result.error[0] = '\0';

    if (httpCode <= 0) {
        result.state = ServiceState::Down;
        snprintf(result.badge, sizeof(result.badge), "DOWN");
        switch (httpCode) {
            // HTTPClient reports every connect failure (DNS, TCP, TLS) as -1.
            case HTTPC_ERROR_CONNECTION_REFUSED: snprintf(result.error, sizeof(result.error), "Connect failed"); break;
            case HTTPC_ERROR_READ_TIMEOUT: snprintf(result.error, sizeof(result.error), "Timeout"); break;
            case HTTPC_ERROR_CONNECTION_LOST: snprintf(result.error, sizeof(result.error), "Connection lost"); break;
            default:
                snprintf(result.error, sizeof(result.error), "%s", HTTPClient::errorToString(httpCode).c_str());
                break;
        }
        return;
    }

    const bool matches = expectedCode != 0 ? httpCode == expectedCode : (httpCode >= 200 && httpCode < 400);
    if (matches) {
        const bool slow = latencyMs > DEGRADED_LATENCY_MS;
        result.state = slow ? ServiceState::Degraded : ServiceState::Healthy;
        if (slow) snprintf(result.error, sizeof(result.error), "Slow response");
        return;
    }

    // Reachable but answered with the wrong status: 5xx is down, anything else degraded.
    result.state = httpCode >= 500 ? ServiceState::Down : ServiceState::Degraded;
    snprintf(result.badge, sizeof(result.badge), "%d", httpCode);
    if (expectedCode != 0) snprintf(result.error, sizeof(result.error), "HTTP %d, expected %d", httpCode, expectedCode);
    else snprintf(result.error, sizeof(result.error), "HTTP %d", httpCode);
}

namespace {

// Copies the string value of "key" from a flat JSON document. Good enough for
// Statuspage's status.json; avoids pulling in a JSON library.
bool findJsonString(const char* json, const char* key, char* out, size_t outLen) {
    char needle[24];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (p == nullptr) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == ':') ++p;
    if (*p != '"') return false;
    ++p;
    size_t n = 0;
    while (*p != '\0' && *p != '"' && n + 1 < outLen) out[n++] = *p++;
    out[n] = '\0';
    return n > 0;
}

}  // namespace

void ServiceMonitor::classifyStatusPage(const char* body, ServiceStatus& result) {
    result.badge[0] = '\0';
    result.error[0] = '\0';

    char indicator[16];
    if (!findJsonString(body, "indicator", indicator, sizeof(indicator))) {
        result.state = ServiceState::Degraded;
        snprintf(result.badge, sizeof(result.badge), "?");
        snprintf(result.error, sizeof(result.error), "No status in response");
        return;
    }
    findJsonString(body, "description", result.error, sizeof(result.error));

    if (strcmp(indicator, "none") == 0) {
        result.state = ServiceState::Healthy;
        result.error[0] = '\0';
    } else if (strcmp(indicator, "minor") == 0) {
        result.state = ServiceState::Degraded;
        snprintf(result.badge, sizeof(result.badge), "minor");
    } else if (strcmp(indicator, "maintenance") == 0) {
        result.state = ServiceState::Degraded;
        snprintf(result.badge, sizeof(result.badge), "maint");
    } else if (strcmp(indicator, "major") == 0) {
        result.state = ServiceState::Down;
        snprintf(result.badge, sizeof(result.badge), "major");
    } else if (strcmp(indicator, "critical") == 0) {
        result.state = ServiceState::Down;
        snprintf(result.badge, sizeof(result.badge), "critical");
    } else {
        result.state = ServiceState::Degraded;
        snprintf(result.badge, sizeof(result.badge), "%.9s", indicator);
    }
}
