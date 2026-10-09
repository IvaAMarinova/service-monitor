#pragma once

#include <stdint.h>

// Debounced active-LOW push button. wasPressed() reports each press once.
class Button {
public:
    explicit Button(int8_t pin) : pin_(pin) {}
    void begin();
    void update();
    bool wasPressed();

private:
    int8_t pin_;
    bool stableLevel_ = true;  // HIGH = released
    bool lastReading_ = true;
    uint32_t lastChangeMs_ = 0;
    bool pressedEvent_ = false;
};
