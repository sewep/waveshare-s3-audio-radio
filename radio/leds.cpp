#include "leds.h"

#include <Adafruit_NeoPixel.h>

namespace leds {
namespace {

constexpr uint32_t FRAME_MS = 30;
constexpr uint32_t BOOT_SPIN_MS = 1500;
constexpr uint32_t BOOT_FLASH_MS = 120;  // trzy błyski R, G, B po spinie
constexpr int LOW_BATTERY_ON = 10;       // poniżej: czerwone pulsowanie
constexpr int LOW_BATTERY_OFF = 13;      // histereza, żeby nie migało na granicy

const char* const MODE_IDS[] = {"off",     "status", "color",  "breathe", "rainbow", "comet", "vu",
                                "fire",    "sparkle", "police", "rcomet", "wave",    "pulse", "disco"};
static_assert(sizeof(MODE_IDS) / sizeof(MODE_IDS[0]) == (size_t)Mode::Count, "MODE_IDS a enum Mode");
constexpr int MAX_LEDS = 16;

Adafruit_NeoPixel* strip = nullptr;
int ledCount = 0;
uint32_t bootStartMs = 0;

volatile Mode curMode = Mode::Rainbow;
volatile uint32_t curColor = 0xFF6A00;
volatile uint8_t curBrightness = 60;
volatile State curState = State::Connecting;
volatile uint8_t vuInput = 0;
volatile int batteryPercent = -1;
volatile bool darkPlaying = false;
bool lowBattery = false;

// Muzyka: biblioteka podaje pierwiastek z amplitudy, więc głośna, skompresowana muzyka (EDM)
// stoi prawie przy maksimum. Kwadrat przywraca dynamikę, a ruchome „dno” i „sufit” rozciągają
// to, co zostało, na cały pierścień.
float musFloor = 0;
float musCeil = 64;
float musLevel = 0;  // 0..1, szybki atak, wolne opadanie
bool beat = false;   // w tej klatce wypadło uderzenie
bool beatArmed = true;
uint32_t lastBeatMs = 0;

// Stan efektów.
float heat[MAX_LEDS];
float spark[MAX_LEDS];
uint16_t discoHue[MAX_LEDS];
uint16_t pulseHue = 0;
float pulse = 0;

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

void updateMusic(uint32_t t) {
  float in = curState == State::Playing ? vuInput : 0;
  float lin = in * in / 255.0f;
  musFloor = lin < musFloor ? lin : musFloor + (lin - musFloor) * 0.02f;  // ok. 1,5 s w górę
  musCeil = lin > musCeil ? lin : musCeil + (lin - musCeil) * 0.005f;     // ok. 6 s w dół
  float level = constrain((lin - musFloor) / max(musCeil - musFloor, 24.0f), 0.0f, 1.0f);
  musLevel = level > musLevel ? level : musLevel * 0.85f + level * 0.15f;
  // Uderzenie: skok powyżej 70% zakresu po spadku poniżej 35%, nie częściej niż co 180 ms.
  beat = false;
  if (level < 0.35f) {
    beatArmed = true;
  } else if (beatArmed && level > 0.7f && t - lastBeatMs > 180) {
    beat = true;
    beatArmed = false;
    lastBeatMs = t;
  }
}

void renderVu() {
  float lit = musLevel * ledCount;
  for (int i = 0; i < ledCount; i++) {
    float k = constrain(lit - i, 0.0f, 1.0f);
    uint16_t h = 21845 - 21845 * i / max(1, ledCount - 1);  // zielony -> czerwony
    strip->setPixelColor(i, k > 0 ? hue(h, k) : 0);
  }
}

void renderFire() {
  for (int i = 0; i < ledCount; i++) {
    heat[i] += (random(256) - heat[i]) * 0.25f;
    float h = heat[i] / 255.0f;
    uint8_t g = 20 + 90 * h * h;  // gorętsze = bardziej żółte
    strip->setPixelColor(i, scale(Adafruit_NeoPixel::Color(255, g, 0), 0.25f + 0.75f * h));
  }
}

void renderSparkle() {
  if (random(5) == 0) spark[random(ledCount)] = 1.0f;
  for (int i = 0; i < ledCount; i++) {
    spark[i] *= 0.82f;
    strip->setPixelColor(i, scale(curColor, 0.06f + 0.94f * spark[i]));
  }
}

void renderPolice(uint32_t t) {
  uint32_t p = t % 1000;
  bool red = p < 500;
  p %= 500;
  bool on = p < 70 || (p >= 140 && p < 210);  // podwójny błysk
  for (int i = 0; i < ledCount; i++) {
    bool side = i < ledCount / 2;
    strip->setPixelColor(i, on && side == red ? scale(red ? 0xFF0000 : 0x0000FF, 1.0f) : 0);
  }
}

void renderRainbowComet(uint32_t t) {
  float head = (float)(t % 1500) / 1500 * ledCount;
  for (int i = 0; i < ledCount; i++) {
    float d = head - i;
    if (d < 0) d += ledCount;
    strip->setPixelColor(i, d < 4.0f ? hue(t * 15 - (uint32_t)(d * 4000), 1.0f - d / 4.0f) : 0);
  }
}

void renderWave(uint32_t t) {
  for (int i = 0; i < ledCount; i++) {
    float s = 0.5f + 0.5f * sinf(2 * PI * ((float)i / ledCount - (float)(t % 2500) / 2500));
    strip->setPixelColor(i, scale(curColor, 0.05f + 0.95f * s * s));
  }
}

void renderPulse() {
  if (beat) {
    pulse = 1.0f;
    pulseHue += 9000;
  }
  pulse *= 0.88f;
  fill(hue(pulseHue, 0.05f + 0.95f * max(pulse, musLevel * 0.4f)));
}

void renderDisco() {
  if (beat)
    for (int i = 0; i < ledCount; i++) discoHue[i] = random(65536);
  for (int i = 0; i < ledCount; i++) strip->setPixelColor(i, hue(discoHue[i], 0.2f + 0.8f * musLevel));
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
    case Mode::Fire: renderFire(); break;
    case Mode::Sparkle: renderSparkle(); break;
    case Mode::Police: renderPolice(t); break;
    case Mode::RainbowComet: renderRainbowComet(t); break;
    case Mode::Wave: renderWave(t); break;
    case Mode::Pulse: renderPulse(); break;
    case Mode::Disco: renderDisco(); break;
    default: fill(0);
  }
}

void render(uint32_t t) {
  updateMusic(t);
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
    case State::Trouble: return comet(t, 0xFF5000, 1000);
    case State::Setup: return fill(scale(0x0040FF, 0.1f + 0.9f * wave(t, 2000)));
    case State::Playing: return darkPlaying ? fill(0) : renderMode(t);
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

bool usesMusic(Mode m) { return m == Mode::Vu || m == Mode::Pulse || m == Mode::Disco; }

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
  ledCount = min(count, MAX_LEDS);
  // Diody na tej płytce przyjmują kolory w kolejności RGB (z NEO_GRB czerwony i zielony były zamienione).
  strip = new Adafruit_NeoPixel(count, pin, NEO_RGB + NEO_KHZ800);
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
void setDarkWhilePlaying(bool on) { darkPlaying = on; }
bool darkWhilePlaying() { return darkPlaying; }
void setState(State s) { curState = s; }
void setVuLevel(uint8_t level) { vuInput = level; }
void setBatteryPercent(int percent) { batteryPercent = percent; }

}  // namespace leds
