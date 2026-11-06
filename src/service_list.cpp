#include "service_list.h"

#include <Arduino.h>
#include <Preferences.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr const char* kPrefsNamespace = "services";
constexpr const char* kPrefsKey = "list";
constexpr size_t kMaxFields = 4;
constexpr size_t kTextBufferSize = MAX_SERVICES * 200 + 1;

// Shared scratch buffer for (de)serialising; only used from the loop task.
char gText[kTextBufferSize];

// Trims [begin, end) in place and returns the new begin.
char* trim(char* begin, char* end) {
    while (begin < end && isspace(static_cast<unsigned char>(*begin))) ++begin;
    while (end > begin && isspace(static_cast<unsigned char>(end[-1]))) --end;
    *end = '\0';
    return begin;
}

}  // namespace

bool parseServiceLine(const char* line, ServiceEntry& out, const char** error) {
    char buf[256];
    if (strlen(line) >= sizeof(buf)) {
        *error = "line too long";
        return false;
    }
    strlcpy(buf, line, sizeof(buf));

    char* fields[kMaxFields] = {};
    size_t count = 0;
    char* start = buf;
    for (char* p = buf;; ++p) {
        if (*p == '|' || *p == '\0') {
            const bool last = *p == '\0';
            if (count == kMaxFields) {
                *error = "too many '|' fields";
                return false;
            }
            fields[count++] = trim(start, p);
            if (last) break;
            start = p + 1;
        }
    }

    if (count < 3) {
        *error = "expected: type | name | url [| code]";
        return false;
    }

    const char* type = fields[0];
    const char* name = fields[1];
    const char* url = fields[2];

    if (strcasecmp(type, "http") == 0) out.type = CheckType::Http;
    else if (strcasecmp(type, "statuspage") == 0) out.type = CheckType::StatusPage;
    else {
        *error = "type must be 'http' or 'statuspage'";
        return false;
    }

    if (name[0] == '\0' || strlen(name) >= sizeof(out.name)) {
        *error = "name must be 1-23 characters";
        return false;
    }
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        *error = "url must start with http:// or https://";
        return false;
    }
    if (strlen(url) >= sizeof(out.url)) {
        *error = "url too long (max 159 characters)";
        return false;
    }

    out.expectedCode = 0;
    if (count == 4 && fields[3][0] != '\0') {
        if (out.type != CheckType::Http) {
            *error = "only http checks take a status code";
            return false;
        }
        char* endp = nullptr;
        const long code = strtol(fields[3], &endp, 10);
        if (*endp != '\0' || code < 100 || code > 599) {
            *error = "status code must be 100-599";
            return false;
        }
        out.expectedCode = static_cast<int>(code);
    }

    strlcpy(out.name, name, sizeof(out.name));
    strlcpy(out.url, url, sizeof(out.url));
    return true;
}

bool parseServiceText(const char* text, ServiceList& out, char* err, size_t errLen) {
    out.count = 0;
    size_t lineNo = 0;
    const char* p = text;

    while (*p != '\0') {
        const char* eol = strchr(p, '\n');
        const size_t len = eol != nullptr ? static_cast<size_t>(eol - p) : strlen(p);
        ++lineNo;

        char line[256];
        if (len >= sizeof(line)) {
            snprintf(err, errLen, "line %u: line too long", static_cast<unsigned>(lineNo));
            return false;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        const char* content = trim(line, line + len);

        if (content[0] != '\0' && content[0] != '#') {
            if (out.count == MAX_SERVICES) {
                snprintf(err, errLen, "line %u: at most %u services", static_cast<unsigned>(lineNo),
                         static_cast<unsigned>(MAX_SERVICES));
                return false;
            }
            const char* reason = nullptr;
            if (!parseServiceLine(content, out.items[out.count], &reason)) {
                snprintf(err, errLen, "line %u: %s", static_cast<unsigned>(lineNo), reason);
                return false;
            }
            ++out.count;
        }

        if (eol == nullptr) break;
        p = eol + 1;
    }

    if (out.count == 0) {
        snprintf(err, errLen, "add at least one service");
        return false;
    }
    return true;
}

void formatServiceLine(const ServiceEntry& entry, char* buf, size_t len) {
    if (entry.type == CheckType::StatusPage) {
        snprintf(buf, len, "statuspage | %s | %s", entry.name, entry.url);
    } else if (entry.expectedCode != 0) {
        snprintf(buf, len, "http | %s | %s | %d", entry.name, entry.url, entry.expectedCode);
    } else {
        snprintf(buf, len, "http | %s | %s", entry.name, entry.url);
    }
}

bool formatServiceText(const ServiceList& list, char* buf, size_t len) {
    size_t used = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < list.count; ++i) {
        char line[220];
        formatServiceLine(list.items[i], line, sizeof(line));
        const int n = snprintf(buf + used, len - used, "%s\n", line);
        if (n < 0 || used + n >= len) return false;
        used += n;
    }
    return true;
}

void loadDefaultServices(ServiceList& out) {
    out.count = 0;
    for (size_t i = 0; i < DEFAULT_SERVICE_COUNT; ++i) {
        ServiceEntry& e = out.items[out.count++];
        strlcpy(e.name, SERVICES[i].name, sizeof(e.name));
        strlcpy(e.url, SERVICES[i].url, sizeof(e.url));
        e.type = SERVICES[i].type;
        e.expectedCode = SERVICES[i].expectedCode;
    }
}

void loadServices(ServiceList& out) {
    Preferences prefs;
    size_t len = 0;
    if (prefs.begin(kPrefsNamespace, false)) {
        if (prefs.isKey(kPrefsKey)) len = prefs.getBytes(kPrefsKey, gText, sizeof(gText) - 1);
        prefs.end();
    }
    gText[len] = '\0';

    char err[64];
    if (len > 0 && parseServiceText(gText, out, err, sizeof(err))) {
        Serial.printf("[services] loaded %u saved services\n", static_cast<unsigned>(out.count));
        return;
    }
    if (len > 0) Serial.printf("[services] saved list invalid (%s), using defaults\n", err);
    loadDefaultServices(out);
}

bool saveServices(const ServiceList& list) {
    if (!formatServiceText(list, gText, sizeof(gText))) return false;
    const size_t len = strlen(gText);

    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) return false;
    const bool ok = prefs.putBytes(kPrefsKey, gText, len) == len;
    prefs.end();
    Serial.printf("[services] %s %u services\n", ok ? "saved" : "failed to save", static_cast<unsigned>(list.count));
    return ok;
}

void eraseSavedServices() {
    Preferences prefs;
    if (prefs.begin(kPrefsNamespace, false)) {
        prefs.clear();
        prefs.end();
    }
    Serial.println("[services] saved list erased, using defaults");
}
