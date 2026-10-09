#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "board_pins.h"

// LovyanGFX device for the T-Display S3 panel.
// Values mirror LILYGO's factory example: ST7789, 170x320 visible area inside
// 240x320 controller RAM (35 px gap on the short axis), colour inversion on,
// 16 MHz write clock. Rotation 0 = portrait, USB-C at the bottom.
class LGFX_TDisplayS3 : public lgfx::LGFX_Device {
public:
    static constexpr int WIDTH = 170;
    static constexpr int HEIGHT = 320;

    LGFX_TDisplayS3() {
        {
            auto cfg = bus_.config();
            cfg.freq_write = 16000000;
            cfg.pin_wr = BoardPins::LCD_WR;
            cfg.pin_rd = BoardPins::LCD_RD;
            cfg.pin_rs = BoardPins::LCD_DC;
            cfg.pin_d0 = BoardPins::LCD_D0;
            cfg.pin_d1 = BoardPins::LCD_D1;
            cfg.pin_d2 = BoardPins::LCD_D2;
            cfg.pin_d3 = BoardPins::LCD_D3;
            cfg.pin_d4 = BoardPins::LCD_D4;
            cfg.pin_d5 = BoardPins::LCD_D5;
            cfg.pin_d6 = BoardPins::LCD_D6;
            cfg.pin_d7 = BoardPins::LCD_D7;
            bus_.config(cfg);
            panel_.setBus(&bus_);
        }
        {
            auto cfg = panel_.config();
            cfg.pin_cs = BoardPins::LCD_CS;
            cfg.pin_rst = BoardPins::LCD_RESET;
            cfg.pin_busy = -1;
            cfg.panel_width = WIDTH;
            cfg.panel_height = HEIGHT;
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            cfg.offset_x = 35;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = true;
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            panel_.config(cfg);
        }
        // Backlight is switched with a plain GPIO (see DisplayManager) — the board's
        // AW9364 LED driver uses pulse-count dimming, not PWM.
        setPanel(&panel_);
    }

private:
    lgfx::Bus_Parallel8 bus_;
    lgfx::Panel_ST7789 panel_;
};
