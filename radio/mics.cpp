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

void task(void*) {
  static int32_t buf[480 * 2];  // 10 ms, stereo
  bool running = false;
  float dc[COUNT] = {0, 0};
  double sum[COUNT] = {0, 0};
  float pk[COUNT] = {0, 0};
  int frames = 0;
  for (;;) {
    bool want = awake();
    if (want != running) {
      running = want;
      running ? i2s_channel_enable(rx) : i2s_channel_disable(rx);
      frames = 0;
      for (int c = 0; c < COUNT; c++) sum[c] = pk[c] = 0;
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

float rmsDb(int mic) {
  return mic >= 0 && mic < COUNT && millis() - updatedMs < STALE_MS ? rms[mic] : SILENCE_DB;
}

float peakDb(int mic) {
  return mic >= 0 && mic < COUNT && millis() - updatedMs < STALE_MS ? peak[mic] : SILENCE_DB;
}

}  // namespace mics
