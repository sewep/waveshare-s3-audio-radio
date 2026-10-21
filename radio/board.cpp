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

bool begin() {
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  setAmplifier(true);
  return initEs8311();
}

}  // namespace board
