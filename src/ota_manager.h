#pragma once

#include <stdint.h>

enum class OtaEvent : uint8_t { Start, Progress, End, Error };

// Invoked on the loop task (from inside ArduinoOTA.handle()).
using OtaCallback = void (*)(OtaEvent event, uint8_t percent, const char* message);

class OtaManager {
public:
    void setCallback(OtaCallback callback) { callback_ = callback; }
    void begin();  // call once Wi-Fi is up
    void loop();
    bool isStarted() const { return started_; }

private:
    static void notify(OtaEvent event, uint8_t percent, const char* message);

    static OtaCallback callback_;
    bool started_ = false;
};
