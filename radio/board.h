#pragma once
#include <Arduino.h>

// Waveshare ESP32-S3-AUDIO-Board (SKU 32184).
// Piny wzięte z xiaozhi-esp32/main/boards/waveshare/esp32-s3-audio-board/config.h.
namespace board {

constexpr int I2S_MCLK = 12;
constexpr int I2S_BCLK = 13;
constexpr int I2S_LRCK = 14;
constexpr int I2S_DOUT = 16;  // do ES8311 (głośnik)
constexpr int I2S_DIN = 15;   // z ES7210 (mikrofony)
constexpr int I2C_SDA = 11;
constexpr int I2C_SCL = 10;
constexpr int BOOT_BUTTON = 0;

// Łańcuch 7 diod WS2812 (na schemacie U9, U19, U14..U10).
constexpr int LED_PIN = 38;
constexpr int LED_COUNT = 7;

// Pomiar baterii: dzielnik 200k/100k (ADC widzi 1/3 napięcia) na GPIO1. Fabrycznie NIE jest
// podłączony — trzeba wlutować zworkę 0R przy GPIO1 (BAT_ADC), a kamera przestaje wtedy działać.
// Po przelutowaniu ustaw na true.
constexpr bool BATTERY_ADC_ENABLED = false;
constexpr int BATTERY_ADC_PIN = 1;

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

// Konfiguruje ES7210 (mikrofony) jako slave I2S: MIC1 w lewym, MIC2 w prawym kanale, słowo 32 bit,
// wzmocnienie 30 dB. Zegary daje I2S biblioteki audio (64 takty BCLK na ramkę), więc zwykły
// format I2S pasuje bez TDM. Wołaj po begin().
bool beginMics();

// Maska wciśniętych przycisków KEY1..KEY3 (bit ustawiony = wciśnięty).
uint8_t readKeys();

// Poziom naładowania 0..100 albo -1, gdy pomiar baterii jest niedostępny.
int batteryPercent();

}  // namespace board
