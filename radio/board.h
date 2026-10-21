#pragma once
#include <Arduino.h>

// Waveshare ESP32-S3-AUDIO-Board (SKU 32184).
// Piny wzięte z xiaozhi-esp32/main/boards/waveshare/esp32-s3-audio-board/config.h.
namespace board {

constexpr int I2S_MCLK = 12;
constexpr int I2S_BCLK = 13;
constexpr int I2S_LRCK = 14;
constexpr int I2S_DOUT = 16;  // do ES8311 (głośnik)
constexpr int I2C_SDA = 11;
constexpr int I2C_SCL = 10;
constexpr int BOOT_BUTTON = 0;

// Przyciski użytkownika na ekspanderze TCA9555 (port 1), aktywne stanem niskim.
// Na schemacie: Key1 = Extend_IO10, Key2 = Extend_IO11, Key3 = Extend_IO12.
constexpr uint8_t KEY1 = 0x02;
constexpr uint8_t KEY2 = 0x04;
constexpr uint8_t KEY3 = 0x08;

// Inicjuje I2C, włącza wzmacniacz głośnika (TCA9555 EXIO8) i konfiguruje kodek ES8311
// jako slave I2S, 16 bit, 48 kHz przy MCLK = 256 * fs. Wymaga działającego MCLK,
// więc wywołuj po Audio::setPinout().
bool begin();

void setAmplifier(bool on);

// Maska wciśniętych przycisków KEY1..KEY3 (bit ustawiony = wciśnięty).
uint8_t readKeys();

}  // namespace board
