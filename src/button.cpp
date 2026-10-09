#include "button.h"

#include <Arduino.h>

#include "config.h"

void Button::begin() {
    pinMode(pin_, INPUT_PULLUP);
    stableLevel_ = lastReading_ = digitalRead(pin_);
    lastChangeMs_ = millis();
}

void Button::update() {
    const bool reading = digitalRead(pin_);
    const uint32_t now = millis();

    if (reading != lastReading_) {
        lastReading_ = reading;
        lastChangeMs_ = now;
        return;
    }

    if (reading != stableLevel_ && now - lastChangeMs_ >= BUTTON_DEBOUNCE_MS) {
        stableLevel_ = reading;
        if (stableLevel_ == LOW) pressedEvent_ = true;
    }
}

bool Button::wasPressed() {
    const bool pressed = pressedEvent_;
    pressedEvent_ = false;
    return pressed;
}
