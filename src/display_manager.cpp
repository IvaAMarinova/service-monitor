#include "display_manager.h"

#include <Arduino.h>
#include <string.h>

#include "board_pins.h"
#include "lgfx_tdisplay_s3.h"
#include "time_manager.h"

// JetBrains Mono Bold (SIL OFL), ASCII only — see src/fonts/
LV_FONT_DECLARE(mono_bold_14);
LV_FONT_DECLARE(mono_bold_16);
LV_FONT_DECLARE(mono_bold_18);

namespace {

LGFX_TDisplayS3 tft;

// Landscape 320x170
constexpr lv_coord_t SCREEN_W = LGFX_TDisplayS3::HEIGHT;
constexpr lv_coord_t SCREEN_H = LGFX_TDisplayS3::WIDTH;

// Outline: the top edge runs through the middle of the border labels.
constexpr lv_coord_t FRAME_X = 4;
constexpr lv_coord_t FRAME_Y = 10;
constexpr lv_coord_t FRAME_W = SCREEN_W - 2 * FRAME_X;
constexpr lv_coord_t FRAME_H = SCREEN_H - FRAME_Y - 10;  // bottom edge also carries labels
constexpr lv_coord_t FRAME_RADIUS = 10;
constexpr lv_coord_t BORDER_LABEL_INSET = 12;  // from the frame's side to a border label

// Service rows
constexpr lv_coord_t ROWS_Y = 21;
constexpr lv_coord_t ROW_PITCH = 26;
constexpr lv_coord_t DOT_X = FRAME_X + 14;
constexpr lv_coord_t DOT_SIZE = 10;
constexpr lv_coord_t NAME_X = DOT_X + DOT_SIZE + 10;
constexpr lv_coord_t VALUE_RIGHT = FRAME_X + 14;  // distance from the screen's right edge

// Device rows
constexpr lv_coord_t KV_Y = 21;
constexpr lv_coord_t KV_PITCH = 18;
constexpr lv_coord_t KV_KEY_X = FRAME_X + 14;
constexpr lv_coord_t KV_VALUE_X = KV_KEY_X + 80;

constexpr size_t DRAW_BUF_LINES = 24;
lv_color_t drawPixels[SCREEN_W * DRAW_BUF_LINES];
lv_disp_draw_buf_t drawBuf;
lv_disp_drv_t dispDrv;

// Green-phosphor terminal palette
namespace color {
constexpr uint32_t BG = 0x000000;
constexpr uint32_t FRAME = 0x2BFF6E;  // outline + titles
constexpr uint32_t TEXT = 0xCFFFDC;   // service names (green-tinted white)
constexpr uint32_t VALUE = 0x4FE3FF;  // latency (cyan)
constexpr uint32_t MUTED = 0x3FA162;  // secondary text
constexpr uint32_t DIM = 0x22573A;    // unknown / placeholders
constexpr uint32_t GREEN = 0x2BFF6E;
constexpr uint32_t YELLOW = 0xFFB000;  // amber
constexpr uint32_t RED = 0xFF3B5C;
}  // namespace color

void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* pixels) {
    const uint32_t w = area->x2 - area->x1 + 1;
    const uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.writePixels(reinterpret_cast<lgfx::rgb565_t*>(&pixels->full), w * h);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

uint32_t stateColor(ServiceState state) {
    switch (state) {
        case ServiceState::Healthy: return color::GREEN;
        case ServiceState::Degraded: return color::YELLOW;
        case ServiceState::Down: return color::RED;
        case ServiceState::Unknown: break;
    }
    return color::DIM;
}

// Lower rank = shown first
uint8_t severityRank(ServiceState state) {
    switch (state) {
        case ServiceState::Down: return 0;
        case ServiceState::Degraded: return 1;
        default: return 2;
    }
}

// --- small LVGL helpers; setters skip no-op updates to avoid needless redraws ---

void setText(lv_obj_t* label, const char* text) {
    const char* current = lv_label_get_text(label);
    if (current == nullptr || strcmp(current, text) != 0) lv_label_set_text(label, text);
}

void setTextColor(lv_obj_t* obj, uint32_t hex) {
    const lv_color_t c = lv_color_hex(hex);
    if (lv_obj_get_style_text_color(obj, LV_PART_MAIN).full != c.full) {
        lv_obj_set_style_text_color(obj, c, LV_PART_MAIN);
    }
}

void setBgColor(lv_obj_t* obj, uint32_t hex) {
    const lv_color_t c = lv_color_hex(hex);
    if (lv_obj_get_style_bg_color(obj, LV_PART_MAIN).full != c.full) {
        lv_obj_set_style_bg_color(obj, c, LV_PART_MAIN);
    }
}

lv_obj_t* makeScreen() {
    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(color::BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    return screen;
}

lv_obj_t* makeBox(lv_obj_t* parent, uint32_t bg, lv_coord_t radius) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, radius, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return box;
}

lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t hex, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(hex), 0);
    lv_label_set_text(label, text);
    return label;
}

// A label that sits on the frame's top edge, masking the line behind it.
lv_obj_t* makeBorderLabel(lv_obj_t* screen, const lv_font_t* font, uint32_t hex, const char* text,
                          lv_align_t align, lv_coord_t x, lv_coord_t edgeY = FRAME_Y) {
    lv_obj_t* label = makeLabel(screen, font, hex, text);
    lv_obj_set_style_bg_color(label, lv_color_hex(color::BG), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(label, 4, 0);
    lv_obj_align(label, align, x, edgeY - lv_font_get_line_height(font) / 2);
    return label;
}

void formatAge(uint32_t seconds, char* buf, size_t len) {
    if (seconds < 60) snprintf(buf, len, "%lus", static_cast<unsigned long>(seconds));
    else if (seconds < 3600) snprintf(buf, len, "%lum", static_cast<unsigned long>(seconds / 60));
    else snprintf(buf, len, "%luh", static_cast<unsigned long>(seconds / 3600));
}

void formatUptime(uint64_t seconds, char* buf, size_t len) {
    const unsigned long days = seconds / 86400;
    const unsigned hours = (seconds / 3600) % 24;
    const unsigned minutes = (seconds / 60) % 60;
    if (days > 0) snprintf(buf, len, "%lud %02uh %02um", days, hours, minutes);
    else snprintf(buf, len, "%02u:%02u:%02u", hours, minutes, static_cast<unsigned>(seconds % 60));
}

}  // namespace

bool DisplayManager::begin() {
    // Panel power rail + backlight off until the first frame is drawn.
    pinMode(BoardPins::POWER_ON, OUTPUT);
    digitalWrite(BoardPins::POWER_ON, HIGH);
    pinMode(BoardPins::LCD_BACKLIGHT, OUTPUT);
    digitalWrite(BoardPins::LCD_BACKLIGHT, LOW);

    if (!tft.init()) {
        Serial.println("[display] panel init failed");
        return false;
    }
    tft.setRotation(DISPLAY_ROTATION);
    tft.fillScreen(TFT_BLACK);

    lv_init();
    lv_disp_draw_buf_init(&drawBuf, drawPixels, nullptr, SCREEN_W * DRAW_BUF_LINES);
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = SCREEN_W;
    dispDrv.ver_res = SCREEN_H;
    dispDrv.flush_cb = flushCb;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_t* disp = lv_disp_drv_register(&dispDrv);
    lv_theme_t* theme = lv_theme_default_init(disp, lv_color_hex(color::FRAME), lv_color_hex(color::RED), true,
                                              &mono_bold_16);
    lv_disp_set_theme(disp, theme);

    buildSplash();
    buildServices();
    buildDevice();
    buildOta();
    load(splash_.frame.screen);

    lv_timer_handler();
    digitalWrite(BoardPins::LCD_BACKLIGHT, HIGH);
    Serial.printf("[display] %dx%d ready (rotation %u)\n", SCREEN_W, SCREEN_H, DISPLAY_ROTATION);
    return true;
}

void DisplayManager::tick() { lv_timer_handler(); }

void DisplayManager::load(lv_obj_t* screen) {
    if (current_ == screen) return;
    lv_scr_load(screen);
    current_ = screen;
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------

DisplayManager::Frame DisplayManager::makeFrame(const char* title, const char* right) {
    Frame frame{};
    frame.screen = makeScreen();

    lv_obj_t* outline = lv_obj_create(frame.screen);
    lv_obj_remove_style_all(outline);
    lv_obj_set_pos(outline, FRAME_X, FRAME_Y);
    lv_obj_set_size(outline, FRAME_W, FRAME_H);
    lv_obj_set_style_radius(outline, FRAME_RADIUS, 0);
    lv_obj_set_style_border_width(outline, 2, 0);
    lv_obj_set_style_border_color(outline, lv_color_hex(color::FRAME), 0);
    lv_obj_set_style_border_opa(outline, LV_OPA_COVER, 0);
    lv_obj_clear_flag(outline, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    frame.title = makeBorderLabel(frame.screen, &mono_bold_16, color::FRAME, title, LV_ALIGN_TOP_LEFT,
                                  FRAME_X + BORDER_LABEL_INSET);
    frame.center = makeBorderLabel(frame.screen, &mono_bold_14, color::MUTED, "", LV_ALIGN_TOP_MID, 0);
    frame.right = makeBorderLabel(frame.screen, &mono_bold_16, color::FRAME, right, LV_ALIGN_TOP_RIGHT,
                                  -(FRAME_X + BORDER_LABEL_INSET));

    constexpr lv_coord_t bottomEdge = FRAME_Y + FRAME_H - 1;
    frame.bottomLeft = makeBorderLabel(frame.screen, &mono_bold_14, color::MUTED, "", LV_ALIGN_TOP_LEFT,
                                       FRAME_X + BORDER_LABEL_INSET, bottomEdge);
    frame.bottomRight = makeBorderLabel(frame.screen, &mono_bold_14, color::MUTED, "", LV_ALIGN_TOP_RIGHT,
                                        -(FRAME_X + BORDER_LABEL_INSET), bottomEdge);
    return frame;
}

void DisplayManager::buildSplash() {
    splash_.frame = makeFrame("service-monitor", "boot");

    splash_.status = makeLabel(splash_.frame.screen, &mono_bold_18, color::TEXT, "");
    lv_obj_set_width(splash_.status, FRAME_W - 24);
    lv_obj_set_style_text_align(splash_.status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(splash_.status, LV_ALIGN_CENTER, 0, 0);

    splash_.detail = makeLabel(splash_.frame.screen, &mono_bold_14, color::MUTED, "");
    lv_obj_set_width(splash_.detail, FRAME_W - 24);
    lv_label_set_long_mode(splash_.detail, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(splash_.detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(splash_.detail, LV_ALIGN_CENTER, 0, 26);

    setText(splash_.frame.bottomLeft, "v" FIRMWARE_VERSION);
}

void DisplayManager::buildServices() {
    services_.frame = makeFrame("~/services", "status");
    lv_obj_t* screen = services_.frame.screen;

    for (size_t i = 0; i < ROW_COUNT; ++i) {
        ServiceRow& row = services_.rows[i];
        const lv_coord_t y = ROWS_Y + i * ROW_PITCH;
        const lv_coord_t textY = y + (ROW_PITCH - lv_font_get_line_height(&mono_bold_18)) / 2;

        row.dot = makeBox(screen, color::DIM, LV_RADIUS_CIRCLE);
        lv_obj_set_size(row.dot, DOT_SIZE, DOT_SIZE);
        lv_obj_set_pos(row.dot, DOT_X, y + (ROW_PITCH - DOT_SIZE) / 2);

        row.name = makeLabel(screen, &mono_bold_18, color::TEXT, SERVICES[i].name);
        lv_label_set_long_mode(row.name, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(row.name, NAME_X, textY);

        row.value = makeLabel(screen, &mono_bold_18, color::DIM, "...");
        lv_obj_align(row.value, LV_ALIGN_TOP_RIGHT, -VALUE_RIGHT, textY);
    }
}

void DisplayManager::buildDevice() {
    static const char* const kKeys[FIELD_COUNT] = {"wifi", "ip", "rssi", "uptime", "heap", "checks", "polled"};

    device_.frame = makeFrame("~/device", "sysinfo");
    lv_obj_t* screen = device_.frame.screen;

    for (uint8_t i = 0; i < FIELD_COUNT; ++i) {
        const lv_coord_t y = KV_Y + i * KV_PITCH;

        lv_obj_t* key = makeLabel(screen, &mono_bold_14, color::MUTED, kKeys[i]);
        lv_obj_set_pos(key, KV_KEY_X, y);

        lv_obj_t* value = makeLabel(screen, &mono_bold_14, color::TEXT, "-");
        lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
        lv_obj_set_width(value, SCREEN_W - KV_VALUE_X - VALUE_RIGHT);
        lv_obj_set_pos(value, KV_VALUE_X, y);
        device_.values[i] = value;
    }

    setText(device_.frame.bottomLeft, OTA_HOSTNAME);
    setText(device_.frame.bottomRight, "v" FIRMWARE_VERSION);
}

void DisplayManager::buildOta() {
    ota_.frame = makeFrame("~/ota", "flashing");

    ota_.percent = makeLabel(ota_.frame.screen, &mono_bold_18, color::TEXT, "0%");
    lv_obj_align(ota_.percent, LV_ALIGN_CENTER, 0, -14);

    ota_.bar = lv_bar_create(ota_.frame.screen);
    lv_obj_remove_style_all(ota_.bar);
    lv_obj_set_size(ota_.bar, FRAME_W - 60, 8);
    lv_obj_set_style_bg_color(ota_.bar, lv_color_hex(0x1C1C24), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ota_.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(ota_.bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ota_.bar, lv_color_hex(color::FRAME), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ota_.bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(ota_.bar, 4, LV_PART_INDICATOR);
    lv_bar_set_range(ota_.bar, 0, 100);
    lv_obj_align(ota_.bar, LV_ALIGN_CENTER, 0, 12);

    ota_.message = makeLabel(ota_.frame.screen, &mono_bold_14, color::MUTED, "do not unplug");
    lv_obj_align(ota_.message, LV_ALIGN_CENTER, 0, 38);
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void DisplayManager::showSplash(const char* status, const char* detail, SplashTone tone) {
    char line[48];
    snprintf(line, sizeof(line), "> %s", status);
    setText(splash_.status, line);
    setTextColor(splash_.status, tone == SplashTone::Error ? color::RED : color::FRAME);
    setText(splash_.detail, detail != nullptr ? detail : "");
    load(splash_.frame.screen);
}

void DisplayManager::showDashboard() { load(services_.frame.screen); }

void DisplayManager::nextScreen() {
    if (current_ == ota_.frame.screen) return;
    load(current_ == services_.frame.screen ? device_.frame.screen : services_.frame.screen);
}

void DisplayManager::showOta(uint8_t percent, const char* message) {
    if (current_ != ota_.frame.screen) beforeOta_ = current_;
    lv_bar_set_value(ota_.bar, percent, LV_ANIM_OFF);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", percent);
    setText(ota_.percent, buf);
    setText(ota_.message, message != nullptr ? message : "do not unplug");
    load(ota_.frame.screen);
}

void DisplayManager::leaveOta() {
    if (current_ != ota_.frame.screen) return;
    load(beforeOta_ != nullptr ? beforeOta_ : services_.frame.screen);
}

// ---------------------------------------------------------------------------
// Data binding
// ---------------------------------------------------------------------------

void DisplayManager::update(const MonitorSnapshot& snapshot, const DeviceStatus& device) {
    if (current_ == services_.frame.screen) {
        updateStatusLabel(services_.frame.center, device);
        updateServiceRows(snapshot);
        updateServicesFooter(snapshot, device);
    } else if (current_ == device_.frame.screen) {
        updateStatusLabel(device_.frame.center, device);
        updateDevice(snapshot, device);
    }
}

// Middle of the top border: clock normally, otherwise what needs attention.
void DisplayManager::updateStatusLabel(lv_obj_t* label, const DeviceStatus& device) {
    char buf[12] = "--:--";
    uint32_t hex = color::MUTED;

    if (device.wifiState == WifiState::Disconnected) {
        snprintf(buf, sizeof(buf), "no wifi");
        hex = color::RED;
    } else if (device.wifiState == WifiState::Connecting) {
        snprintf(buf, sizeof(buf), "wifi...");
        hex = color::YELLOW;
    } else if (device.refreshPending) {
        snprintf(buf, sizeof(buf), "refresh");
        hex = color::FRAME;
    } else if (device.timeSynced) {
        TimeManager::formatEpoch(time(nullptr), buf, sizeof(buf), "%H:%M");
    }

    setText(label, buf);
    setTextColor(label, hex);
}

// Bottom border: "> polled 4s ago_" on the left, signal strength on the right.
void DisplayManager::updateServicesFooter(const MonitorSnapshot& snapshot, const DeviceStatus& device) {
    char buf[32];
    // Rows beyond the screen are the least severe ones (list is sorted by severity).
    constexpr size_t hidden = SERVICE_COUNT - ROW_COUNT;

    if (device.refreshPending || (!snapshot.hasCompletedCycle && device.wifiState == WifiState::Connected)) {
        snprintf(buf, sizeof(buf), "> polling..._");
    } else if (!snapshot.hasCompletedCycle) {
        snprintf(buf, sizeof(buf), "> offline_");
    } else {
        char age[8];
        formatAge((millis() - snapshot.lastCycleEndMs) / 1000, age, sizeof(age));
        if (hidden > 0) snprintf(buf, sizeof(buf), "> %s ago | +%u more_", age, static_cast<unsigned>(hidden));
        else snprintf(buf, sizeof(buf), "> polled %s ago_", age);
    }
    setText(services_.frame.bottomLeft, buf);

    if (device.wifiState == WifiState::Connected) snprintf(buf, sizeof(buf), "wifi %ddBm", device.rssi);
    else snprintf(buf, sizeof(buf), "wifi --");
    setText(services_.frame.bottomRight, buf);
    setTextColor(services_.frame.bottomRight,
                 device.wifiState == WifiState::Connected ? color::MUTED : color::RED);
}

void DisplayManager::updateServiceRows(const MonitorSnapshot& snapshot) {
    size_t order[SERVICE_COUNT];
    for (size_t i = 0; i < SERVICE_COUNT; ++i) order[i] = i;

    if (SORT_SERVICES_BY_SEVERITY) {
        // Stable insertion sort keeps config order within the same severity.
        for (size_t i = 1; i < SERVICE_COUNT; ++i) {
            const size_t idx = order[i];
            const uint8_t rank = severityRank(snapshot.services[idx].state);
            size_t j = i;
            while (j > 0 && severityRank(snapshot.services[order[j - 1]].state) > rank) {
                order[j] = order[j - 1];
                --j;
            }
            order[j] = idx;
        }
    }

    for (size_t i = 0; i < ROW_COUNT; ++i) {
        updateServiceRow(services_.rows[i], SERVICES[order[i]], snapshot.services[order[i]]);
    }
}

void DisplayManager::updateServiceRow(ServiceRow& row, const ServiceConfig& config, const ServiceStatus& status) {
    char value[12];
    uint32_t valueColor = color::VALUE;

    if (status.state == ServiceState::Unknown) {
        snprintf(value, sizeof(value), "...");
        valueColor = color::DIM;
    } else {
        // Status word: "ok", or what's wrong ("minor", "503", "DOWN", "slow").
        if (status.badge[0] != '\0') snprintf(value, sizeof(value), "%s", status.badge);
        else snprintf(value, sizeof(value), "%s", status.state == ServiceState::Healthy ? "ok" : "slow");
        valueColor = stateColor(status.state);
    }

    setBgColor(row.dot, stateColor(status.state));
    setText(row.value, value);
    setTextColor(row.value, valueColor);
    setText(row.name, config.name);

    // Give the name whatever width the value leaves free.
    lv_point_t valueSize;
    lv_txt_get_size(&valueSize, value, &mono_bold_18, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const lv_coord_t nameWidth = SCREEN_W - VALUE_RIGHT - valueSize.x - 12 - NAME_X;
    if (lv_obj_get_style_width(row.name, LV_PART_MAIN) != nameWidth) lv_obj_set_width(row.name, nameWidth);
}

void DisplayManager::updateDevice(const MonitorSnapshot& snapshot, const DeviceStatus& device) {
    char buf[40];
    const bool connected = device.wifiState == WifiState::Connected;

    if (connected) snprintf(buf, sizeof(buf), "%s", device.ssid);
    else snprintf(buf, sizeof(buf), "%s [%s]", device.ssid, device.wifiState == WifiState::Connecting ? "connecting" : "offline");
    setText(device_.values[FIELD_WIFI], buf);
    setTextColor(device_.values[FIELD_WIFI],
                 connected ? color::GREEN : (device.wifiState == WifiState::Connecting ? color::YELLOW : color::RED));

    setText(device_.values[FIELD_IP], connected ? device.ip : "-");

    if (connected) snprintf(buf, sizeof(buf), "%d dBm", device.rssi);
    else snprintf(buf, sizeof(buf), "-");
    setText(device_.values[FIELD_RSSI], buf);

    formatUptime(device.uptimeSeconds, buf, sizeof(buf));
    setText(device_.values[FIELD_UPTIME], buf);

    snprintf(buf, sizeof(buf), "%luK free / %luK min", static_cast<unsigned long>(device.freeHeap / 1024),
             static_cast<unsigned long>(device.minFreeHeap / 1024));
    setText(device_.values[FIELD_HEAP], buf);

    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(snapshot.totalChecks));
    setText(device_.values[FIELD_CHECKS], buf);

    if (!snapshot.hasCompletedCycle) {
        snprintf(buf, sizeof(buf), "never");
    } else {
        char age[8];
        formatAge((millis() - snapshot.lastCycleEndMs) / 1000, age, sizeof(age));
        if (snapshot.lastCycleEndEpoch != 0) {
            char clock[10];
            TimeManager::formatEpoch(snapshot.lastCycleEndEpoch, clock, sizeof(clock), "%H:%M:%S");
            snprintf(buf, sizeof(buf), "%s (%s ago)", clock, age);
        } else {
            snprintf(buf, sizeof(buf), "%s ago", age);
        }
    }
    setText(device_.values[FIELD_LAST_REFRESH], buf);
}
