#include "mics.h"

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>

#include "board.h"

namespace mics {
namespace {

constexpr int SAMPLE_RATE = 48000;  // jak wyjście biblioteki audio (setOutputSampleRate)
constexpr int WINDOW_FRAMES = SAMPLE_RATE / 10;  // 100 ms
constexpr uint32_t AWAKE_MS = 5000;
constexpr uint32_t STALE_MS = 500;  // starsze wyniki = brak danych (np. stoją zegary I2S)

i2s_chan_handle_t rx = nullptr;
volatile uint32_t wantedMs = 0;
volatile uint32_t updatedMs = 0;
volatile float rms[COUNT] = {SILENCE_DB, SILENCE_DB};
volatile float peak[COUNT] = {SILENCE_DB, SILENCE_DB};

float toDb(float x) { return x > 1e-6f ? 20 * log10f(x) : SILENCE_DB; }

// Klaśnięcie: szczyt w bloku 5 ms wyraźnie ponad tłem (średnim szczytem z ok. 1,5 s, więc
// muzyka z głośnika podnosi próg), który w ciągu 80 ms opada o 12 dB. Dłuższe dźwięki
// (szczekanie, mowa, uderzenie w muzyce z pogłosem) nie opadają tak szybko. Dwa klaśnięcia
// 150–800 ms od siebie = polecenie.
constexpr int CLAP_BLOCK = SAMPLE_RATE / 200;  // 5 ms
constexpr float CLAP_DECAY_DB = 12;
constexpr int CLAP_DECAY_BLOCKS = 16;     // 80 ms
constexpr int CLAP_LOCKOUT_BLOCKS = 20;   // 100 ms po klaśnięciu: pogłos
constexpr int DOUBLE_MIN_BLOCKS = 30;     // 150 ms
constexpr int DOUBLE_MAX_BLOCKS = 160;    // 800 ms
constexpr int DOUBLE_REST_BLOCKS = 300;   // 1,5 s przerwy po poleceniu
// Głośnik i mikrofony są na jednej płytce: przy głośności 14 muzyka daje w mikrofonach szczyty
// do −2 dBFS przy −17 dBFS na wyjściu (pomiar 2026-10-22). Gdy coś gra, klaśnięcie musi więc być
// głośniejsze, niż wyjaśnia to sygnał wysłany na głośnik: szczyt wyjścia z ostatnich 400 ms
// (z zapasem na opóźnienie bufora I2S) + sprzężenie głośnik → mikrofon + margines. Sprzężenie to
// największa różnica z ostatnich chwil (opada 0,5 dB/s), bo średnia przepuszczała rzadkie szczyty.
// Przy dużej głośności próg przekracza 0 dBFS i klaśnięcie po prostu nie działa.
constexpr int OUT_SLOTS = 8;  // po 50 ms
constexpr uint32_t OUT_SLOT_MS = 50;
constexpr float OUT_QUIET_DB = -60;  // ciszej = nic nie gra
constexpr float OUT_LEARN_DB = -50;  // sprzężenie uczone tylko przy wyraźnym graniu
constexpr float OUT_MARGIN_DB = 6;
constexpr float COUPLING_MIN_DB = 10, COUPLING_MAX_DB = 40;
constexpr float COUPLING_DECAY_DB = 0.0025f;  // na blok 5 ms = 0,5 dB/s
volatile float outSlotPeak[OUT_SLOTS];
volatile uint32_t outSlotId[OUT_SLOTS];
volatile float coupling = 15;  // mikrofon − wyjście w dB
// Start i zatrzymanie odtwarzania dają trzask w głośniku (~0 dBFS w mikrofonach), którego nie
// widać w próbkach wyjścia: przez chwilę po nich nic nie jest klaśnięciem.
volatile uint32_t suppressUntilMs = 0;

// Szczyt wyjścia z ostatnich 400 ms w dBFS.
float outputHoldDb() {
  uint32_t now = millis() / OUT_SLOT_MS;
  float m = 0;
  for (int i = 0; i < OUT_SLOTS; i++)
    if (now - outSlotId[i] < OUT_SLOTS) m = max(m, (float)outSlotPeak[i]);
  return toDb(m);
}

// Wzrost nad tło i minimalny szczyt dla czułości 1..3.
constexpr float CLAP_RISE_DB[] = {24, 18, 13};
constexpr float CLAP_MIN_DB[] = {-18, -26, -34};

volatile bool clapOn = false;
volatile uint8_t clapSens = 2;
volatile bool doubleClap = false;
volatile uint32_t claps = 0;
volatile float clapDb = SILENCE_DB;

struct ClapDetector {
  uint32_t block = 0;      // licznik bloków 5 ms
  float bg = -60;          // tło: średni szczyt bloku w dB
  int candidate = -1;      // bloki od początku kandydata; -1 = brak
  float candPeak = 0;
  bool waitQuiet = false;  // długi dźwięk: czekamy, aż ucichnie
  uint32_t lockoutUntil = 0;
  uint32_t lastClap = 0;   // blok poprzedniego klaśnięcia (0 = brak)

  void reset() {
    candidate = -1;
    waitQuiet = false;
    lastClap = 0;
  }

  void feed(float db) {
    block++;
    int s = constrain(clapSens, 1, 3) - 1;
    float rise = CLAP_RISE_DB[s];
    if (candidate >= 0) {
      candidate++;
      if (candidate <= 2) candPeak = max(candPeak, db);  // szczyt w pierwszych 10 ms
      if (candidate > 2 && db < candPeak - CLAP_DECAY_DB) {
        confirm();
        candidate = -1;
      } else if (candidate > CLAP_DECAY_BLOCKS) {
        candidate = -1;
        waitQuiet = true;
      }
      return;
    }
    if (waitQuiet) {
      if (db < bg + rise / 2) waitQuiet = false;
      else return;
    }
    if ((int32_t)(suppressUntilMs - millis()) > 0) {
      lastClap = 0;
      bg += (db - bg) * 0.0033f;
      return;
    }
    float out = outputHoldDb();
    bool playing = out > OUT_QUIET_DB;
    if (out > OUT_LEARN_DB) {
      float d = min(db - out, COUPLING_MAX_DB);
      coupling = d > coupling ? d : max(COUPLING_MIN_DB, coupling - COUPLING_DECAY_DB);
    }
    bool aboveSpeaker = !playing || db > out + coupling + OUT_MARGIN_DB;
    if (block >= lockoutUntil && db > bg + rise && db > CLAP_MIN_DB[s] && aboveSpeaker) {
      candidate = 0;
      candPeak = db;
      return;
    }
    bg += (db - bg) * 0.0033f;  // ok. 1,5 s
  }

  void confirm() {
    uint32_t at = block - candidate;
    Serial.printf("Klaśnięcie: szczyt %.0f dBFS, tło %.0f, opadło w %d ms, głośnik %.0f dBFS, sprzężenie %.0f dB\n", candPeak, bg,
                  candidate * 5, outputHoldDb(), (float)coupling);
    claps++;
    clapDb = candPeak;
    lockoutUntil = block + CLAP_LOCKOUT_BLOCKS;
    uint32_t gap = at - lastClap;
    if (lastClap && gap >= DOUBLE_MIN_BLOCKS && gap <= DOUBLE_MAX_BLOCKS) {
      doubleClap = true;
      lastClap = 0;
      lockoutUntil = block + DOUBLE_REST_BLOCKS;
    } else {
      lastClap = at;
    }
  }
};

void task(void*) {
  static int32_t buf[480 * 2];  // 10 ms, stereo
  bool running = false;
  float dc[COUNT] = {0, 0};
  double sum[COUNT] = {0, 0};
  float pk[COUNT] = {0, 0};
  int frames = 0;
  ClapDetector clap;
  float blockPeak = 0;
  int blockFrames = 0;
  for (;;) {
    bool want = awake() || clapOn;
    if (want != running) {
      running = want;
      running ? i2s_channel_enable(rx) : i2s_channel_disable(rx);
      frames = 0;
      for (int c = 0; c < COUNT; c++) sum[c] = pk[c] = 0;
      clap.reset();
    }
    if (!running) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    size_t got = 0;
    if (i2s_channel_read(rx, buf, sizeof(buf), &got, pdMS_TO_TICKS(100)) != ESP_OK || got == 0) continue;
    int n = got / (sizeof(int32_t) * COUNT);
    for (int i = 0; i < n; i++) {
      for (int c = 0; c < COUNT; c++) {
        float s = buf[i * COUNT + c] / 2147483648.0f;
        dc[c] += (s - dc[c]) * 0.001f;  // resztki składowej stałej mimo filtru w ES7210
        s -= dc[c];
        sum[c] += s * s;
        pk[c] = max(pk[c], fabsf(s));
        blockPeak = max(blockPeak, fabsf(s));
      }
      if (++blockFrames == CLAP_BLOCK) {
        if (clapOn) clap.feed(toDb(blockPeak));
        blockPeak = 0;
        blockFrames = 0;
      }
    }
    frames += n;
    if (frames >= WINDOW_FRAMES) {
      for (int c = 0; c < COUNT; c++) {
        rms[c] = toDb(sqrt(sum[c] / frames));
        peak[c] = toDb(pk[c]);
        sum[c] = pk[c] = 0;
      }
      frames = 0;
      updatedMs = millis();
    }
  }
}

}  // namespace

bool begin() {
  if (!board::beginMics()) return false;

  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_SLAVE);
  chan.dma_desc_num = 4;
  chan.dma_frame_num = 480;
  if (i2s_new_channel(&chan, nullptr, &rx) != ESP_OK) return false;
  i2s_std_config_t cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {.mclk = I2S_GPIO_UNUSED,
                   .bclk = (gpio_num_t)board::I2S_BCLK,
                   .ws = (gpio_num_t)board::I2S_LRCK,
                   .dout = I2S_GPIO_UNUSED,
                   .din = (gpio_num_t)board::I2S_DIN,
                   .invert_flags = {}},
  };
  if (i2s_channel_init_std_mode(rx, &cfg) != ESP_OK) return false;
  // Sterownik ustawił BCLK i WS jako wejścia I2S1; wracają na nie wyjścia zegarów I2S0, a sygnał
  // trafia do I2S1 przez matrycę GPIO (pin może być naraz wyjściem jednego i wejściem drugiego).
  gpio_set_direction((gpio_num_t)board::I2S_BCLK, GPIO_MODE_INPUT_OUTPUT);
  esp_rom_gpio_connect_out_signal(board::I2S_BCLK, I2S0O_BCK_OUT_IDX, false, false);
  gpio_set_direction((gpio_num_t)board::I2S_LRCK, GPIO_MODE_INPUT_OUTPUT);
  esp_rom_gpio_connect_out_signal(board::I2S_LRCK, I2S0O_WS_OUT_IDX, false, false);

  xTaskCreatePinnedToCore(task, "mics", 4096, nullptr, 1, nullptr, 0);
  return true;
}

void keepAwake() { wantedMs = millis(); }
bool awake() { return wantedMs && millis() - wantedMs < AWAKE_MS; }

void setClapDetection(bool on, uint8_t sensitivity) {
  clapSens = constrain(sensitivity, 1, 3);
  clapOn = on;
  doubleClap = false;
}

bool takeDoubleClap() {
  if (!doubleClap) return false;
  doubleClap = false;
  return true;
}

void feedOutput(const int32_t* buff, int samples) {
  if (!clapOn) return;
  float pk = 0;
  for (int i = 0; i < samples; i++) pk = max(pk, fabsf(buff[i] / 2147483648.0f));
  uint32_t id = millis() / OUT_SLOT_MS;
  int slot = id % OUT_SLOTS;
  if (outSlotId[slot] != id) {
    outSlotId[slot] = id;
    outSlotPeak[slot] = pk;
  } else if (pk > outSlotPeak[slot]) {
    outSlotPeak[slot] = pk;
  }
}

void suppress(uint32_t ms) { suppressUntilMs = millis() + ms; }

float clapNeedDb() {
  float out = outputHoldDb();
  float need = CLAP_MIN_DB[constrain(clapSens, 1, 3) - 1];
  return out > OUT_QUIET_DB ? max(need, out + coupling + OUT_MARGIN_DB) : need;
}

uint32_t clapCount() { return claps; }
float lastClapDb() { return clapDb; }

float rmsDb(int mic) {
  return mic >= 0 && mic < COUNT && millis() - updatedMs < STALE_MS ? rms[mic] : SILENCE_DB;
}

float peakDb(int mic) {
  return mic >= 0 && mic < COUNT && millis() - updatedMs < STALE_MS ? peak[mic] : SILENCE_DB;
}

}  // namespace mics
