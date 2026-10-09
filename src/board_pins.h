#pragma once

#include <stdint.h>

// LILYGO T-Display S3 pin map.
// Source: Xinyuan-LilyGO/T-Display-S3, examples/factory/pin_config.h
namespace BoardPins {

// Must be driven HIGH to power the LCD (and keep it on when running on battery)
constexpr int8_t POWER_ON = 15;

// ST7789 over 8-bit i8080 parallel bus
constexpr int8_t LCD_BACKLIGHT = 38;
constexpr int8_t LCD_D0 = 39;
constexpr int8_t LCD_D1 = 40;
constexpr int8_t LCD_D2 = 41;
constexpr int8_t LCD_D3 = 42;
constexpr int8_t LCD_D4 = 45;
constexpr int8_t LCD_D5 = 46;
constexpr int8_t LCD_D6 = 47;
constexpr int8_t LCD_D7 = 48;
constexpr int8_t LCD_RESET = 5;
constexpr int8_t LCD_CS = 6;
constexpr int8_t LCD_DC = 7;
constexpr int8_t LCD_WR = 8;
constexpr int8_t LCD_RD = 9;

// Buttons, active LOW. BUTTON_1 is the BOOT button (GPIO0).
constexpr int8_t BUTTON_1 = 0;
constexpr int8_t BUTTON_2 = 14;

}  // namespace BoardPins
