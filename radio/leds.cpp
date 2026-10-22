#include "leds.h"

#include <Adafruit_NeoPixel.h>

namespace leds {
namespace {

constexpr uint32_t FRAME_MS = 30;
constexpr uint32_t BOOT_SPIN_MS = 1500;
constexpr uint32_t BOOT_FLASH_MS = 120;  // trzy błyski R, G, B po spinie
constexpr int LOW_BATTERY_ON = 10;       // poniżej: czerwone pulsowanie
constexpr int LOW_BATTERY_OFF = 13;      // histereza, żeby nie migało na granicy

const char* const MODE_IDS[] = {"off",        "status",   "color",    "breathe", "rainbow", "comet",
                                "vu",         "fire",     "sparkle",  "police",  "rcomet",  "wave",
                                "pulse",      "disco",    "colororgan", "spectrum", "confetti", "aurora",
                                "bounce",     "twins",    "auto"};
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

// Ruchome „dno” i „sufit” sygnału: wynik 0..1 rozciągnięty na to, co akurat gra, więc efekty
// reagują tak samo przy cichej i przy mocno skompresowanej muzyce.
struct Env {
  float floor = 0;
  float ceil = 64;
  float level = 0;  // wygładzony: szybki atak, wolne opadanie

  float update(float x, float minSpan) {  // zwraca poziom bez wygładzania
    floor = x < floor ? x : floor + (x - floor) * 0.02f;  // ok. 1,5 s w górę
    ceil = x > ceil ? x : ceil + (x - ceil) * 0.005f;     // ok. 6 s w dół
    float v = constrain((x - floor) / max(ceil - floor, minSpan), 0.0f, 1.0f);
    level = v > level ? v : level * 0.85f + v * 0.15f;
    return v;
  }
};

// Głośność: biblioteka podaje pierwiastek z amplitudy, więc głośna, skompresowana muzyka (EDM)
// stoi prawie przy maksimum; kwadrat przywraca dynamikę. Dno to minimum z ostatnich ok. 0,6 s
// (jedno uderzenie), sufit to maksimum z ok. 2 s, więc pasek przechodzi cały zakres przy każdym
// uderzeniu, nawet gdy muzyka jest stale głośna. Druga potęga rozciąga dół paska. Dobrane na
// poziomach nagranych z radia (RauteMusik Club, 2026-10-22): wcześniej dolne diody prawie nigdy
// nie gasły, teraz liczba świecących rozkłada się równo od 0 do 7.
constexpr int MUSIC_LO_FRAMES = 20;  // 0,6 s przy klatce 30 ms
constexpr int MUSIC_HI_FRAMES = 66;  // 2 s
float musicHist[MUSIC_HI_FRAMES];
int musicPos = 0;
float musicLevel = 0;  // 0..1, to pokazują efekty
bool beat = false;     // w tej klatce wypadło uderzenie
bool beatArmed = true;
uint32_t lastBeatMs = 0;

// Widmo: 15 pasm z FFT biblioteki (skala decybelowa 0..255), od ok. 90 Hz do 18 kHz.
constexpr int SPECTRUM_BANDS = 15;
volatile uint8_t spectrum[SPECTRUM_BANDS];
Env organ[3];             // kolorofon: bas, średnie, wysokie
Env specLed[MAX_LEDS];    // analizator: pasma przypisane kolejnym diodom

// Stan efektów.
float heat[MAX_LEDS];
float spark[MAX_LEDS];
uint16_t discoHue[MAX_LEDS];
uint16_t pulseHue = 0;
float pulse = 0;
uint16_t confettiHue[MAX_LEDS];
float confettiK[MAX_LEDS];

// Pokaz: kolejność efektów i płynne przejście między nimi.
const Mode AUTO_LIST[] = {Mode::Rainbow, Mode::Pulse,    Mode::RainbowComet, Mode::ColorOrgan,
                          Mode::Fire,    Mode::Disco,    Mode::Wave,         Mode::Spectrum,
                          Mode::Confetti, Mode::Vu,      Mode::Aurora,       Mode::Sparkle,
                          Mode::Bounce,  Mode::Twins,    Mode::Breathe,      Mode::Comet};
constexpr int AUTO_COUNT = sizeof(AUTO_LIST) / sizeof(AUTO_LIST[0]);
constexpr uint32_t AUTO_FADE_MS = 1000;
volatile uint8_t autoSec = 15;
volatile int autoIdx = 0;
int autoPrev = -1;  // efekt, z którego trwa przejście; -1 = bez przejścia
uint32_t autoStartMs = 0;
bool autoRunning = false;

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

void addPixel(int i, uint32_t c) {  // dodawanie kolorów z nasyceniem (nakładające się efekty)
  uint32_t o = strip->getPixelColor(i);
  strip->setPixelColor(i, min<uint32_t>(255, ((o >> 16) & 0xFF) + ((c >> 16) & 0xFF)),
                       min<uint32_t>(255, ((o >> 8) & 0xFF) + ((c >> 8) & 0xFF)),
                       min<uint32_t>(255, (o & 0xFF) + (c & 0xFF)));
}

float bandAverage(int from, int to) {  // pasma [from, to)
  if (curState != State::Playing) return 0;
  float sum = 0;
  for (int b = from; b < to; b++) sum += spectrum[b];
  return sum / max(1, to - from);
}

void updateSpectrum() {
  organ[0].update(bandAverage(0, 3), 30);   // ok. 90–600 Hz
  organ[1].update(bandAverage(3, 8), 30);   // ok. 0,7–3,8 kHz
  organ[2].update(bandAverage(8, 13), 30);  // ok. 3,9–14 kHz
  for (int i = 0; i < ledCount; i++) {
    int from = i * 14 / ledCount;  // pasmo 15 (18 kHz) prawie puste, pomijamy
    specLed[i].update(bandAverage(from, max(from + 1, (i + 1) * 14 / ledCount)), 30);
  }
}

void updateMusic(uint32_t t) {
  float in = curState == State::Playing ? vuInput : 0;
  float x = in * in / 255.0f;
  musicHist[musicPos] = x;
  musicPos = (musicPos + 1) % MUSIC_HI_FRAMES;
  float lo = x, hi = x;
  for (int k = 0; k < MUSIC_HI_FRAMES; k++) {
    float h = musicHist[(musicPos - 1 - k + MUSIC_HI_FRAMES) % MUSIC_HI_FRAMES];
    if (k < MUSIC_LO_FRAMES) lo = min(lo, h);
    hi = max(hi, h);
  }
  // Ostatni czynnik wygasza pasek przy ciszy i bardzo cichym graniu.
  float level = constrain((x - lo) / max(hi - lo, 24.0f), 0.0f, 1.0f) * min(1.0f, x / 24.0f);
  float shown = level * level;
  musicLevel = shown > musicLevel ? shown : musicLevel * 0.7f + shown * 0.3f;
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
  float lit = musicLevel * ledCount;
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
  fill(hue(pulseHue, 0.05f + 0.95f * max(pulse, musicLevel * 0.4f)));
}

void renderDisco() {
  if (beat)
    for (int i = 0; i < ledCount; i++) discoHue[i] = random(65536);
  for (int i = 0; i < ledCount; i++) strip->setPixelColor(i, hue(discoHue[i], 0.2f + 0.8f * musicLevel));
}

// Jak żarówki w dawnym kolorofonie: czerwone, zielone i niebieskie na przemian.
void renderColorOrgan() {
  static const uint32_t lamps[3] = {0xFF0000, 0x00FF00, 0x0000FF};
  for (int i = 0; i < ledCount; i++) {
    float k = organ[i % 3].level;
    strip->setPixelColor(i, scale(lamps[i % 3], 0.02f + 0.98f * k * k));
  }
}

void renderSpectrum() {
  for (int i = 0; i < ledCount; i++) {
    uint16_t h = 52000L * i / max(1, ledCount - 1);  // bas czerwony ... wysokie fioletowe
    strip->setPixelColor(i, hue(h, specLed[i].level));
  }
}

void renderConfetti() {
  if (random(3) == 0) {
    int i = random(ledCount);
    confettiHue[i] = random(65536);
    confettiK[i] = 1.0f;
  }
  for (int i = 0; i < ledCount; i++) {
    confettiK[i] *= 0.9f;
    strip->setPixelColor(i, hue(confettiHue[i], confettiK[i]));
  }
}

void renderAurora(uint32_t t) {
  float s = t / 1000.0f;
  for (int i = 0; i < ledCount; i++) {
    uint16_t h = 34000 + 12000 * sinf(s * 0.35f + i * 0.9f);  // zieleń ... fiolet
    float k = (0.5f + 0.5f * sinf(s * 0.6f + i * 1.7f)) * (0.5f + 0.5f * sinf(s * 0.37f - i * 0.6f));
    strip->setPixelColor(i, hue(h, 0.1f + 0.9f * k));
  }
}

void renderBounce(uint32_t t) {
  float p = (float)(t % 1800) / 1800;
  float pos = (p < 0.5f ? p * 2 : 2 - p * 2) * (ledCount - 1);
  for (int i = 0; i < ledCount; i++)
    strip->setPixelColor(i, hue(t * 6, max(0.0f, 1.0f - fabsf(i - pos) / 1.5f)));
}

void renderTwins(uint32_t t) {
  fill(0);
  float head = (float)(t % 2000) / 2000 * ledCount;
  uint16_t h = t * 10;
  for (int i = 0; i < ledCount; i++) {
    float d1 = head - i;  // kometa zgodnie z numeracją diod
    if (d1 < 0) d1 += ledCount;
    float d2 = i - (ledCount - head);  // i druga w przeciwną stronę
    if (d2 < 0) d2 += ledCount;
    if (d1 < 3.0f) addPixel(i, hue(h, 1.0f - d1 / 3.0f));
    if (d2 < 3.0f) addPixel(i, hue(h + 32768, 1.0f - d2 / 3.0f));
  }
}

void renderEffect(Mode m, uint32_t t) {
  switch (m) {
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
    case Mode::ColorOrgan: renderColorOrgan(); break;
    case Mode::Spectrum: renderSpectrum(); break;
    case Mode::Confetti: renderConfetti(); break;
    case Mode::Aurora: renderAurora(t); break;
    case Mode::Bounce: renderBounce(t); break;
    case Mode::Twins: renderTwins(t); break;
    default: fill(0);
  }
}

void renderAuto(uint32_t t) {
  if (!autoRunning) {
    autoRunning = true;
    autoPrev = -1;
    autoStartMs = t;
  }
  if (t - autoStartMs >= autoSec * 1000UL) {
    autoPrev = autoIdx;
    autoIdx = (autoIdx + 1) % AUTO_COUNT;
    autoStartMs = t;
  }
  uint32_t e = t - autoStartMs;
  if (autoPrev < 0 || e >= AUTO_FADE_MS) return renderEffect(AUTO_LIST[autoIdx], t);
  // Przejście: poprzedni efekt gaśnie, następny się rozjaśnia.
  uint32_t from[MAX_LEDS];
  renderEffect(AUTO_LIST[autoPrev], t);
  for (int i = 0; i < ledCount; i++) from[i] = strip->getPixelColor(i);
  renderEffect(AUTO_LIST[autoIdx], t);
  float a = (float)e / AUTO_FADE_MS;
  for (int i = 0; i < ledCount; i++) {
    uint32_t to = strip->getPixelColor(i);
    auto mix = [&](int shift) {
      return (uint8_t)(((from[i] >> shift) & 0xFF) * (1 - a) + ((to >> shift) & 0xFF) * a);
    };
    strip->setPixelColor(i, mix(16), mix(8), mix(0));
  }
}

void renderMode(uint32_t t) {
  if (curMode == Mode::Auto) return renderAuto(t);
  autoRunning = false;
  renderEffect(curMode, t);
}

void render(uint32_t t) {
  updateMusic(t);
  updateSpectrum();
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

bool usesVu(Mode m) { return m == Mode::Vu || m == Mode::Pulse || m == Mode::Disco || m == Mode::Auto; }
bool usesSpectrum(Mode m) { return m == Mode::ColorOrgan || m == Mode::Spectrum || m == Mode::Auto; }
Mode currentEffect() { return curMode == Mode::Auto ? AUTO_LIST[autoIdx] : (Mode)curMode; }

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
void setAutoSeconds(uint8_t s) { autoSec = constrain(s, 5, 120); }
uint8_t autoSeconds() { return autoSec; }
void setVuLevel(uint8_t level) { vuInput = level; }
void setSpectrum(const std::vector<uint32_t>& bands) {
  for (int b = 0; b < SPECTRUM_BANDS && b < (int)bands.size(); b++) spectrum[b] = min<uint32_t>(bands[b], 255);
}
void setBatteryPercent(int percent) { batteryPercent = percent; }

}  // namespace leds
