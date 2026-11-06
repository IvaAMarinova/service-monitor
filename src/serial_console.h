#pragma once

#include <stddef.h>

#include "service_list.h"
#include "service_monitor.h"
#include "wifi_manager.h"

// Line-based setup commands over the USB serial port, used by setup/index.html
// (Web Serial) or by any serial terminal at 115200 baud:
//
//   ssid <network name>    rest of the line, spaces allowed
//   pass <password>        rest of the line; "pass" alone = open network
//   save                   store in flash and connect
//   status | forget | help
//
//   svc list               current services, one "[setup] svc <line>" each
//   svc clear              start a new list
//   svc add <line>         e.g. "svc add http | My API | https://x/health | 200"
//   svc save               store the new list and start checking it
//   svc defaults           go back to the list built into the firmware
//
// Replies start with "[setup] ok" or "[setup] error".
class SerialConsole {
public:
    SerialConsole(WifiManager& wifi, ServiceMonitor& monitor) : wifi_(wifi), monitor_(monitor) {}
    void loop();

private:
    void handleLine(char* line);
    void printStatus();
    void printHelp();
    void handleServiceCommand(const char* args);

    WifiManager& wifi_;
    ServiceMonitor& monitor_;
    char line_[256] = "";
    size_t lineLen_ = 0;
    bool overflow_ = false;
    bool hostSeen_ = false;

    char pendingSsid_[WifiManager::kMaxSsidLen + 1] = "";
    char pendingPass_[WifiManager::kMaxPasswordLen + 1] = "";
    bool haveSsid_ = false;

    ServiceList pendingServices_;
};
