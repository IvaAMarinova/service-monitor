#pragma once

#include <stddef.h>

#include "config.h"

// Runtime list of monitored services. Saved in NVS; the list in
// include/services.h is the factory default used until one is saved.
//
// Text format (one service per line, used over USB, HTTP and in files):
//
//   statuspage | Claude | https://status.claude.com
//   http       | Google | https://www.google.com/generate_204 | 204
//   http       | My API | https://api.example.com/health
//
// Blank lines and lines starting with '#' are ignored.

struct ServiceEntry {
    char name[24];
    char url[160];
    CheckType type;
    int expectedCode;  // Http only; 0 = any 2xx/3xx is healthy
};

struct ServiceList {
    size_t count = 0;
    ServiceEntry items[MAX_SERVICES];
};

// Parses one non-comment line. On failure returns false and sets *error.
bool parseServiceLine(const char* line, ServiceEntry& out, const char** error);

// Parses a whole document. On failure writes "line N: reason" into err.
bool parseServiceText(const char* text, ServiceList& out, char* err, size_t errLen);

void formatServiceLine(const ServiceEntry& entry, char* buf, size_t len);

// Writes every line (with '\n') into buf. Returns false if it didn't fit.
bool formatServiceText(const ServiceList& list, char* buf, size_t len);

void loadDefaultServices(ServiceList& out);

// Saved list if there is one, otherwise the defaults.
void loadServices(ServiceList& out);
bool saveServices(const ServiceList& list);
void eraseSavedServices();
