#include "leds.h"

#include <Adafruit_NeoPixel.h>

namespace leds {
namespace {

constexpr uint32_t FRAME_MS = 30;
constexpr uint32_t BOOT_SPIN_MS = 1500;
constexpr uint32_t BOOT_FLASH_MS = 120;  // trzy błyski R, G, B po spinie
constexpr int LOW_BATTERY_ON = 10;       // poniżej: czerwone pulsowanie
constexpr int LOW_BATTERY_OFF = 13;      // histereza, żeby nie migało na granicy

const char* const MODE_IDS[] = {"off", "status", "color", "breathe", "rainbow", "comet", "vu"};

Adafruit_NeoPixel* strip = nullptr;
int ledCount = 0;
uint32_t bootStartMs = 0;

volatile Mode curMode = Mode::Rainbow;
volatile uint32_t curColor = 0xFF6A00;
volatile uint8_t curBrightness = 60;
volatile State curState = State::Connecting;
volatile uint8_t vuInput = 0;
volatile int batteryPercent = -1;
bool lowBattery = false;

// VU: wygładzony poziom i ruchomy „sufit”, żeby pierścień reagował przy każdej głośności.
float vuSmooth = 0;
float vuPeak = 64;

uint32_t scale(uint32_t rgb, float k) {
  k = constrain(k, 0.0f, 1.0f) * curBrightness / 255.0f;
  uint8_t r = ((rgb >> 16) & 0xFF) * k;
  uint8_t g = ((rgb >> 8) & 0xFF) * k;
  uint8_t b = (rgb & 0xFF) * k;
  return Adafruit_NeoPixel::Color(r, g, b);
}

uint32_t hue(uint16_t h, float k = 1.0f) {
  return scale(Adafruit_NeoPixel::gamma32(Adafruit_NeoPixel::ColorHSV(h)), k);
}

float wave(uint32_t t, uint32_t periodMs) {  // 0..1..0
  return 0.5f - 0.5f * cosf(2 * PI * (t % periodMs) / periodMs);
}

void fill(uint32_t c) {
  for (int i = 0; i < ledCount; i++) strip->setPixelColor(i, c);
}

void comet(uint32_t t, uint32_t rgb, uint32_t periodMs) {
  float head = (float)(t % periodMs) / periodMs * ledCount;
  for (int i = 0; i < ledCount; i++) {
    float d = head - i;
    if (d < 0) d += ledCount;
    strip->setPixelColor(i, d < 3.0f ? scale(rgb, 1.0f - d / 3.0f) : 0);
  }
}

// Zwraca true, gdy animacja startowa jeszcze trwa.
bool renderBoot(uint32_t t) {
  uint32_t e = t - bootStartMs;
  if (e < BOOT_SPIN_MS) {
    float env = sinf(PI * e / BOOT_SPIN_MS);  // płynne rozjaśnienie i wygaszenie
    for (int i = 0; i < ledCount; i++)
      strip->setPixelColor(i, hue(e * 60 + i * 65536 / ledCount, env));
    return true;
  }
  e -= BOOT_SPIN_MS;
  static const uint32_t flashes[] = {0xFF0000, 0x00FF00, 0x0000FF};
  int step = e / BOOT_FLASH_MS;  // parzyste = błysk, nieparzyste = przerwa
  if (step >= 6) return false;
  fill(step % 2 == 0 ? scale(flashes[step / 2], 1.0f) : 0);
  return true;
}

void renderVu() {
  float in = curState == State::Playing ? vuInput : 0;
  vuSmooth = in > vuSmooth ? in : vuSmooth * 0.85f + in * 0.15f;  // szybki atak, wolne opadanie
  vuPeak = max(vuPeak * 0.995f, max(vuSmooth, 64.0f));
  float lit = vuSmooth / vuPeak * ledCount;
  for (int i = 0; i < ledCount; i++) {
    float k = constrain(lit - i, 0.0f, 1.0f);
    uint16_t h = 21845 - 21845 * i / max(1, ledCount - 1);  // zielony -> czerwony
    strip->setPixelColor(i, k > 0 ? hue(h, k) : 0);
  }
}

void renderMode(uint32_t t) {
  switch (curMode) {
    case Mode::Off: fill(0); break;
    case Mode::Status:
      if (curState == State::Playing) {
        fill(scale(0x00FF20, 0.35f));
      } else {
        fill(0);
        strip->setPixelColor(0, scale(0xFF8000, 0.35f));
      }
      break;
    case Mode::Color: fill(scale(curColor, 1.0f)); break;
    case Mode::Breathe: fill(scale(curColor, 0.1f + 0.9f * wave(t, 4000))); break;
    case Mode::Rainbow:
      for (int i = 0; i < ledCount; i++) strip->setPixelColor(i, hue(t * 12 + i * 65536 / ledCount));
      break;
    case Mode::Comet: comet(t, curColor, 1200); break;
    case Mode::Vu: renderVu(); break;
    default: fill(0);
  }
}

void render(uint32_t t) {
  if (renderBoot(t)) return;
  if (curMode == Mode::Off) return fill(0);

  int bat = batteryPercent;
  if (bat >= 0) {
    if (bat < LOW_BATTERY_ON) lowBattery = true;
    else if (bat > LOW_BATTERY_OFF) lowBattery = false;
  }
  if (lowBattery) return fill(scale(0xFF0000, 0.05f + 0.95f * wave(t, 1200)));

  switch (curState) {
    case State::Connecting:
    case State::Loading: return comet(t, 0x0040FF, 1000);
    case State::Setup: return fill(scale(0x0040FF, 0.1f + 0.9f * wave(t, 2000)));
    default: return renderMode(t);
  }
}

void task(void*) {
  for (;;) {
    render(millis());
    strip->show();
    vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
  }
}

}  // namespace

const char* modeId(Mode m) { return MODE_IDS[(int)m < (int)Mode::Count ? (int)m : 0]; }

bool modeFromId(const String& id, Mode& out) {
  for (int i = 0; i < (int)Mode::Count; i++) {
    if (id == MODE_IDS[i]) {
      out = (Mode)i;
      return true;
    }
  }
  return false;
}

void begin(int pin, int count) {
  ledCount = count;
  strip = new Adafruit_NeoPixel(count, pin, NEO_GRB + NEO_KHZ800);
  strip->begin();
  strip->clear();
  strip->show();
  bootStartMs = millis();
  xTaskCreatePinnedToCore(task, "leds", 4096, nullptr, 1, nullptr, 0);
}

void setMode(Mode m) { curMode = m; }
Mode mode() { return curMode; }
void setColor(uint32_t rgb) { curColor = rgb & 0xFFFFFF; }
uint32_t color() { return curColor; }
void setBrightness(uint8_t b) { curBrightness = b; }
uint8_t brightness() { return curBrightness; }
void setState(State s) { curState = s; }
void setVuLevel(uint8_t level) { vuInput = level; }
void setBatteryPercent(int percent) { batteryPercent = percent; }

}  // namespace leds
