#pragma once
#include <Arduino.h>

// Diody WS2812 na płytce. Animacje liczy osobne zadanie FreeRTOS, więc działają też wtedy,
// gdy pętla główna czeka (łączenie z Wi-Fi, otwieranie strumienia).
namespace leds {

enum class Mode : uint8_t {
  Off,      // całkowicie wyłączone (także ostrzeżenia), poza animacją po włączeniu zasilania
  Status,   // przygaszony stan: zielony gdy gra, jedna bursztynowa dioda gdy stop
  Color,    // stały kolor
  Breathe,  // oddech w wybranym kolorze
  Rainbow,  // obracająca się tęcza
  Comet,    // kometa krążąca w wybranym kolorze
  Vu,       // wizualizer głośności muzyki
  Count
};

// Stan urządzenia, który może nadpisać tryb (np. konfiguracja Wi-Fi).
// Loading = od wybrania stacji (z zapowiedzią numeru) do pierwszych dźwięków strumienia.
enum class State : uint8_t { Connecting, Setup, Stopped, Loading, Playing };

const char* modeId(Mode m);
bool modeFromId(const String& id, Mode& out);

// Startuje zadanie animacji; od razu leci animacja startowa.
void begin(int pin, int count);

void setMode(Mode m);
Mode mode();
void setColor(uint32_t rgb);  // 0xRRGGBB
uint32_t color();
void setBrightness(uint8_t b);
uint8_t brightness();

void setState(State s);
void setVuLevel(uint8_t level);       // 0..255, z biblioteki audio
void setBatteryPercent(int percent);  // -1 = nieznany

}  // namespace leds
