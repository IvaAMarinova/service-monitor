#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "service_monitor.h"
#include "wifi_manager.h"

// Everything besides service results that the UI shows. Filled in by main.
struct DeviceStatus {
    WifiState wifiState = WifiState::Connecting;
    const char* ssid = "";
    char ip[16] = "-";
    int rssi = 0;
    uint64_t uptimeSeconds = 0;
    uint32_t freeHeap = 0;
    uint32_t minFreeHeap = 0;
    bool timeSynced = false;
    bool refreshPending = false;  // manual refresh requested, cycle not finished yet
};

enum class SplashTone : uint8_t { Normal, Error };

// Owns the panel driver and all LVGL objects. Must only be used from the loop task.
class DisplayManager {
public:
    bool begin();
    void tick();  // run LVGL timers/rendering; call every loop iteration

    void showSplash(const char* status, const char* detail, SplashTone tone);
    void showDashboard();
    void nextScreen();
    bool isOnSplash() const { return current_ == splash_.frame.screen; }

    void update(const MonitorSnapshot& snapshot, const DeviceStatus& device);

    void showOta(uint8_t percent, const char* message);
    void leaveOta();

private:
    // A screen with a rounded outline and labels set into its top border.
    struct Frame {
        lv_obj_t* screen;
        lv_obj_t* title;   // top-left
        lv_obj_t* center;  // top-middle (clock / Wi-Fi state)
        lv_obj_t* right;   // top-right
        lv_obj_t* bottomLeft;
        lv_obj_t* bottomRight;
    };

    struct ServiceRow {
        lv_obj_t* dot;
        lv_obj_t* name;
        lv_obj_t* value;
    };

    enum DeviceField : uint8_t {
        FIELD_WIFI,
        FIELD_IP,
        FIELD_RSSI,
        FIELD_UPTIME,
        FIELD_HEAP,
        FIELD_CHECKS,
        FIELD_LAST_REFRESH,
        FIELD_COUNT
    };

    static constexpr size_t MAX_VISIBLE_ROWS = 5;
    static constexpr size_t ROW_COUNT = SERVICE_COUNT < MAX_VISIBLE_ROWS ? SERVICE_COUNT : MAX_VISIBLE_ROWS;

    static Frame makeFrame(const char* title, const char* right);
    void buildSplash();
    void buildServices();
    void buildDevice();
    void buildOta();
    void load(lv_obj_t* screen);

    void updateStatusLabel(lv_obj_t* label, const DeviceStatus& device);
    void updateServicesFooter(const MonitorSnapshot& snapshot, const DeviceStatus& device);
    void updateServiceRows(const MonitorSnapshot& snapshot);
    void updateServiceRow(ServiceRow& row, const ServiceConfig& config, const ServiceStatus& status);
    void updateDevice(const MonitorSnapshot& snapshot, const DeviceStatus& device);

    struct {
        Frame frame;
        lv_obj_t* status;
        lv_obj_t* detail;
    } splash_{};

    struct {
        Frame frame;
        ServiceRow rows[ROW_COUNT];
    } services_{};

    struct {
        Frame frame;
        lv_obj_t* values[FIELD_COUNT];
    } device_{};

    struct {
        Frame frame;
        lv_obj_t* bar;
        lv_obj_t* percent;
        lv_obj_t* message;
    } ota_{};

    lv_obj_t* current_ = nullptr;
    lv_obj_t* beforeOta_ = nullptr;
};
