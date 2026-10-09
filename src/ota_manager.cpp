#include "ota_manager.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "secrets.h"

OtaCallback OtaManager::callback_ = nullptr;

namespace {

const char* otaErrorText(ota_error_t error) {
    switch (error) {
        case OTA_AUTH_ERROR: return "Auth failed";
        case OTA_BEGIN_ERROR: return "Begin failed";
        case OTA_CONNECT_ERROR: return "Connect failed";
        case OTA_RECEIVE_ERROR: return "Receive failed";
        case OTA_END_ERROR: return "End failed";
    }
    return "Unknown error";
}

}  // namespace

void OtaManager::notify(OtaEvent event, uint8_t percent, const char* message) {
    if (callback_ != nullptr) callback_(event, percent, message);
}

void OtaManager::begin() {
    if (started_) return;

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPort(OTA_PORT);
    if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        Serial.printf("[ota] start (%s)\n", ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
        notify(OtaEvent::Start, 0, nullptr);
    });

    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        static uint8_t lastLogged = 255;
        const uint8_t percent = total > 0 ? static_cast<uint8_t>((done * 100ULL) / total) : 0;
        // The upload runs inside handle() on the loop task, so keep its watchdog fed.
        esp_task_wdt_reset();
        if (percent / 10 != lastLogged / 10 || lastLogged == 255) {
            Serial.printf("[ota] %u%%\n", percent);
            lastLogged = percent;
        }
        notify(OtaEvent::Progress, percent, nullptr);
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("[ota] done, rebooting");
        notify(OtaEvent::End, 100, nullptr);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        const char* text = otaErrorText(error);
        Serial.printf("[ota] error %u: %s\n", static_cast<unsigned>(error), text);
        notify(OtaEvent::Error, 0, text);
    });

    ArduinoOTA.begin();
    started_ = true;
    Serial.printf("[ota] ready at %s.local:%u%s\n", OTA_HOSTNAME, OTA_PORT,
                  strlen(OTA_PASSWORD) > 0 ? " (password protected)" : "");
}

void OtaManager::loop() {
    if (started_) ArduinoOTA.handle();
}
