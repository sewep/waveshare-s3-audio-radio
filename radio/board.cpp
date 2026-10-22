#include "board.h"

#include <Wire.h>

namespace board {
namespace {

constexpr uint8_t TCA9555_ADDR = 0x20;
constexpr uint8_t TCA9555_INPUT_PORT1 = 0x01;
constexpr uint8_t TCA9555_OUTPUT_PORT1 = 0x03;
constexpr uint8_t TCA9555_CONFIG_PORT1 = 0x07;
constexpr uint8_t AMP_ENABLE_BIT = 0x01;  // EXIO8 = port 1, bit 0

constexpr uint8_t ES8311_ADDR = 0x18;
constexpr uint8_t ES8311_CHIP_ID = 0x83;

constexpr uint8_t ES7210_ADDR = 0x40;
constexpr uint8_t ES7210_MIC_GAIN = 0x0A;  // 30 dB, krok 3 dB (jak domyślnie w esp_codec_dev)

bool writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

int readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

bool initEs8311() {
  int id = readReg(ES8311_ADDR, 0xFD);
  if (id != ES8311_CHIP_ID) {
    log_e("ES8311 nie odpowiada (ID=0x%02X)", id);
    return false;
  }

  // Sekwencja jak w sterowniku es8311 z ESP-IDF: tryb slave, MCLK z pinu.
  static const uint8_t init[][2] = {
      {0x44, 0x08}, {0x44, 0x08},  // odporność I2C na zakłócenia
      {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10}, {0x16, 0x24}, {0x04, 0x10},
      {0x05, 0x00}, {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F}, {0x11, 0x7F},
      {0x00, 0x80},  // CSM on, slave
      {0x01, 0x3F},  // wszystkie zegary, MCLK z pinu
      // dzielniki dla MCLK 12.288 MHz / 48 kHz (te same dla każdego fs przy 256 * fs)
      {0x02, 0x00}, {0x05, 0x00}, {0x03, 0x10}, {0x04, 0x10},
      {0x07, 0x00}, {0x08, 0xFF}, {0x06, 0x03},
      {0x13, 0x10},
      {0x09, 0x0C}, {0x0A, 0x0C},  // I2S, 16 bit
      // zasilanie części analogowej i DAC
      {0x17, 0xBF}, {0x0E, 0x02}, {0x12, 0x00}, {0x14, 0x1A}, {0x0D, 0x01},
      {0x15, 0x40}, {0x37, 0x08}, {0x45, 0x00},
      {0x32, 0xBF},  // głośność DAC 0 dB; głośnością steruje biblioteka audio
      {0x31, 0x00},  // DAC bez wyciszenia
  };
  for (auto& r : init) {
    if (!writeReg(ES8311_ADDR, r[0], r[1])) {
      log_e("ES8311: zapis rejestru 0x%02X nie powiódł się", r[0]);
      return false;
    }
  }
  return true;
}

bool updateReg(uint8_t addr, uint8_t reg, uint8_t mask, uint8_t val) {
  int v = readReg(addr, reg);
  return v >= 0 && writeReg(addr, reg, (v & ~mask) | (val & mask));
}

// Włącza MIC1 i MIC2, wyłącza MIC3 (na tej płytce to pętla zwrotna z głośnika dla AEC) i MIC4.
bool es7210SelectMics() {
  bool ok = true;
  for (uint8_t r = 0x43; r <= 0x46; r++) ok &= updateReg(ES7210_ADDR, r, 0x10, 0x00);
  ok &= writeReg(ES7210_ADDR, 0x4B, 0xFF) && writeReg(ES7210_ADDR, 0x4C, 0xFF);
  ok &= updateReg(ES7210_ADDR, 0x01, 0x0B, 0x00);  // zegary ADC1/ADC2
  ok &= writeReg(ES7210_ADDR, 0x4B, 0x00);         // bias, ADC i PGA dla MIC1/MIC2
  for (uint8_t r = 0x43; r <= 0x44; r++) ok &= updateReg(ES7210_ADDR, r, 0x1F, 0x10 | ES7210_MIC_GAIN);
  ok &= writeReg(ES7210_ADDR, 0x12, 0x00);  // bez TDM: MIC1/MIC2 na SDOUT1
  return ok;
}

// Sekwencja jak w sterowniku es7210 z esp_codec_dev (używa go xiaozhi): open, set_fs, enable.
bool initEs7210() {
  static const uint8_t open[][2] = {
      {0x00, 0xFF}, {0x00, 0x41},  // reset
      {0x01, 0x3F},                // zegary wyłączone na czas konfiguracji
      {0x09, 0x30}, {0x0A, 0x30},  // czasy stanów przy starcie
      {0x23, 0x2A}, {0x22, 0x0A}, {0x20, 0x0A}, {0x21, 0x2A},  // filtry górnoprzepustowe
  };
  for (auto& r : open) {
    if (!writeReg(ES7210_ADDR, r[0], r[1])) {
      log_e("ES7210 nie odpowiada (rejestr 0x%02X)", r[0]);
      return false;
    }
  }
  bool ok = updateReg(ES7210_ADDR, 0x08, 0x01, 0x00);  // slave
  static const uint8_t analog[][2] = {
      {0x40, 0x43},                // zasilanie części analogowej, VDDA 3,3 V
      {0x41, 0x70}, {0x42, 0x70},  // bias mikrofonów 2,87 V
      {0x07, 0x20}, {0x02, 0xC1},  // OSR i dzielnik ADC dla MCLK = 256 * fs
  };
  for (auto& r : analog) ok &= writeReg(ES7210_ADDR, r[0], r[1]);
  ok &= es7210SelectMics();
  ok &= updateReg(ES7210_ADDR, 0x11, 0xE3, 0x80);  // słowo 32 bit, format I2S
  // start
  int clockOff = readReg(ES7210_ADDR, 0x01);
  ok &= clockOff >= 0 && writeReg(ES7210_ADDR, 0x01, clockOff);
  ok &= writeReg(ES7210_ADDR, 0x06, 0x00) && writeReg(ES7210_ADDR, 0x40, 0x43);
  for (uint8_t r = 0x47; r <= 0x4A; r++) ok &= writeReg(ES7210_ADDR, r, 0x08);
  ok &= es7210SelectMics();
  ok &= writeReg(ES7210_ADDR, 0x40, 0x43) && writeReg(ES7210_ADDR, 0x00, 0x71) && writeReg(ES7210_ADDR, 0x00, 0x41);
  if (!ok) log_e("ES7210: konfiguracja nie powiodła się");
  return ok;
}

}  // namespace

void setAmplifier(bool on) {
  int out = readReg(TCA9555_ADDR, TCA9555_OUTPUT_PORT1);
  int cfg = readReg(TCA9555_ADDR, TCA9555_CONFIG_PORT1);
  if (out < 0 || cfg < 0) {
    log_e("TCA9555 nie odpowiada");
    return;
  }
  out = on ? (out | AMP_ENABLE_BIT) : (out & ~AMP_ENABLE_BIT);
  writeReg(TCA9555_ADDR, TCA9555_OUTPUT_PORT1, out);
  writeReg(TCA9555_ADDR, TCA9555_CONFIG_PORT1, cfg & ~AMP_ENABLE_BIT);  // 0 = wyjście
}

uint8_t readKeys() {
  int in = readReg(TCA9555_ADDR, TCA9555_INPUT_PORT1);
  if (in < 0) return 0;
  return ~in & (KEY1 | KEY2 | KEY3);
}

int batteryPercent() {
  if (!BATTERY_ADC_ENABLED) return -1;
  // Przybliżona krzywa rozładowania ogniwa Li-ion/Li-Po (mV -> %).
  static const int curve[][2] = {{3300, 0}, {3600, 10}, {3700, 25}, {3800, 50}, {3950, 75}, {4200, 100}};
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(BATTERY_ADC_PIN);
  mv = mv / 8 * 3;  // dzielnik 200k/100k
  if (mv <= (uint32_t)curve[0][0]) return 0;
  for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
    if (mv <= (uint32_t)curve[i][0]) {
      return map(mv, curve[i - 1][0], curve[i][0], curve[i - 1][1], curve[i][1]);
    }
  }
  return 100;
}

bool begin() {
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  setAmplifier(true);
  return initEs8311();
}

bool beginMics() { return initEs7210(); }

}  // namespace board
