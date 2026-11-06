#include "serial_console.h"

#include <Arduino.h>
#include <string.h>

#include "config.h"

namespace {

// "cmd" or "cmd <arg>": returns the argument (possibly empty) or nullptr if
// the line is a different command. The argument is kept verbatim.
const char* matchCommand(const char* line, const char* cmd) {
    const size_t n = strlen(cmd);
    if (strncasecmp(line, cmd, n) != 0) return nullptr;
    if (line[n] == '\0') return line + n;
    if (line[n] == ' ') return line + n + 1;
    return nullptr;
}

}  // namespace

void SerialConsole::loop() {
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0) break;

#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
        // Someone is on the other end: let replies wait briefly instead of
        // being dropped (setup() uses a zero timeout for headless operation).
        if (!hostSeen_) {
            hostSeen_ = true;
            Serial.setTxTimeoutMs(50);
        }
#endif

        if (c == '\r' || c == '\n') {
            if (overflow_) Serial.println("[setup] error line too long");
            else if (lineLen_ > 0) {
                line_[lineLen_] = '\0';
                handleLine(line_);
            }
            lineLen_ = 0;
            overflow_ = false;
        } else if (lineLen_ < sizeof(line_) - 1) {
            line_[lineLen_++] = static_cast<char>(c);
        } else {
            overflow_ = true;
        }
    }
}

void SerialConsole::handleLine(char* line) {
    const char* arg;

    if ((arg = matchCommand(line, "ssid")) != nullptr) {
        if (*arg == '\0' || strlen(arg) > WifiManager::kMaxSsidLen) {
            Serial.println("[setup] error network name must be 1-32 characters");
            return;
        }
        strlcpy(pendingSsid_, arg, sizeof(pendingSsid_));
        haveSsid_ = true;
        pendingPass_[0] = '\0';
        Serial.printf("[setup] ok ssid \"%s\"\n", pendingSsid_);
    } else if ((arg = matchCommand(line, "pass")) != nullptr) {
        const size_t len = strlen(arg);
        if (len > WifiManager::kMaxPasswordLen || (len > 0 && len < 8)) {
            Serial.println("[setup] error password must be 8-63 characters (or empty for open networks)");
            return;
        }
        strlcpy(pendingPass_, arg, sizeof(pendingPass_));
        Serial.printf("[setup] ok password set (%u characters)\n", static_cast<unsigned>(len));
    } else if (matchCommand(line, "save") != nullptr) {
        if (!haveSsid_) {
            Serial.println("[setup] error send 'ssid <name>' first");
            return;
        }
        if (!wifi_.setCredentials(pendingSsid_, pendingPass_)) {
            Serial.println("[setup] error could not save credentials");
            return;
        }
        haveSsid_ = false;
        memset(pendingPass_, 0, sizeof(pendingPass_));
        Serial.println("[setup] ok saved, connecting");
    } else if (matchCommand(line, "forget") != nullptr) {
        wifi_.clearCredentials();
        Serial.println("[setup] ok forgotten");
    } else if ((arg = matchCommand(line, "svc")) != nullptr) {
        handleServiceCommand(arg);
    } else if (matchCommand(line, "status") != nullptr) {
        printStatus();
    } else if (matchCommand(line, "help") != nullptr || strcmp(line, "?") == 0) {
        printHelp();
    } else {
        Serial.println("[setup] error unknown command, type 'help'");
    }
}

void SerialConsole::handleServiceCommand(const char* args) {
    const char* arg;
    static ServiceList current;  // ~3 KB; keep it off the loop task's stack

    if (matchCommand(args, "list") != nullptr) {
        monitor_.getServices(current);
        for (size_t i = 0; i < current.count; ++i) {
            char text[220];
            formatServiceLine(current.items[i], text, sizeof(text));
            Serial.printf("[setup] svc %s\n", text);
        }
        Serial.printf("[setup] ok %u services\n", static_cast<unsigned>(current.count));
    } else if (matchCommand(args, "clear") != nullptr) {
        pendingServices_.count = 0;
        Serial.println("[setup] ok cleared");
    } else if ((arg = matchCommand(args, "add")) != nullptr) {
        if (pendingServices_.count >= MAX_SERVICES) {
            Serial.printf("[setup] error at most %u services\n", static_cast<unsigned>(MAX_SERVICES));
            return;
        }
        const char* reason = nullptr;
        if (!parseServiceLine(arg, pendingServices_.items[pendingServices_.count], &reason)) {
            Serial.printf("[setup] error %s\n", reason);
            return;
        }
        ++pendingServices_.count;
        Serial.printf("[setup] ok added %u\n", static_cast<unsigned>(pendingServices_.count));
    } else if (matchCommand(args, "save") != nullptr) {
        if (pendingServices_.count == 0) {
            Serial.println("[setup] error add at least one service first");
            return;
        }
        if (!saveServices(pendingServices_)) {
            Serial.println("[setup] error could not save services");
            return;
        }
        monitor_.setServices(pendingServices_);
        Serial.printf("[setup] ok saved %u services\n", static_cast<unsigned>(pendingServices_.count));
        pendingServices_.count = 0;
    } else if (matchCommand(args, "defaults") != nullptr) {
        eraseSavedServices();
        loadDefaultServices(current);
        monitor_.setServices(current);
        Serial.printf("[setup] ok restored %u default services\n", static_cast<unsigned>(current.count));
    } else {
        Serial.println("[setup] error use: svc list | clear | add <line> | save | defaults");
    }
}

void SerialConsole::printStatus() {
    char ip[16];
    wifi_.formatIp(ip, sizeof(ip));
    Serial.printf("[setup] ok status v%s wifi=\"%s\" network=\"%s\" ip=%s\n", FIRMWARE_VERSION,
                  wifiStateName(wifi_.state()), wifi_.ssid(), ip);
}

void SerialConsole::printHelp() {
    Serial.println("[setup] Wi-Fi setup commands (2.4 GHz networks only):");
    Serial.println("[setup]   ssid <network name>");
    Serial.println("[setup]   pass <password>   (just 'pass' for an open network)");
    Serial.println("[setup]   save              store and connect");
    Serial.println("[setup]   status | forget | help");
    Serial.println("[setup] Services:");
    Serial.println("[setup]   svc list | svc clear | svc save | svc defaults");
    Serial.println("[setup]   svc add http | Name | https://host/path [| code]");
    Serial.println("[setup]   svc add statuspage | Name | https://status.example.com");
}
