#pragma once

#include "service_monitor.h"

// Small HTTP server for editing the service list over Wi-Fi:
//
//   GET  /                 editor page (open http://service-monitor.local/)
//   GET  /services         current list as text, one service per line
//   PUT  /services         replace the list (same text format; POST also works)
//   POST /services/reset   go back to the firmware defaults
//
// If OTA_PASSWORD is set in secrets.h, requests need basic auth
// (user "admin", that password).
class WebConfig {
public:
    explicit WebConfig(ServiceMonitor& monitor) : monitor_(monitor) {}
    void begin();  // call once the network is up
    void loop();

private:
    ServiceMonitor& monitor_;
    bool started_ = false;
};
