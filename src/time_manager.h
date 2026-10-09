#pragma once

#include <stddef.h>
#include <time.h>

// NTP + timezone handling. Safe to query before (or without) a successful sync.
class TimeManager {
public:
    void begin();  // call once Wi-Fi is up
    void loop();   // logs the first successful sync
    bool isSynced() const;
    // Formats local time with strftime; writes `fallback` when time is unknown.
    void format(char* buf, size_t len, const char* fmt, const char* fallback) const;
    static void formatEpoch(time_t epoch, char* buf, size_t len, const char* fmt);

private:
    bool started_ = false;
    bool reportedSync_ = false;
};
