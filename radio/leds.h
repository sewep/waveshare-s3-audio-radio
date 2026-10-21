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
  // Nowe tryby tylko na końcu: numer trybu jest zapisany w pamięci (Preferences).
  Fire,          // migotanie płomienia
  Sparkle,       // iskry w wybranym kolorze na przygaszonym tle
  Police,        // naprzemienne czerwone i niebieskie błyski
  RainbowComet,  // kometa zmieniająca kolor
  Wave,          // fala jasności krążąca w wybranym kolorze
  Pulse,         // błysk całego pierścienia na każde uderzenie, co uderzenie inny kolor
  Disco,         // na każde uderzenie diody losują nowe kolory
  Count
};

// Stan urządzenia, który może nadpisać tryb (np. konfiguracja Wi-Fi).
// Loading = od wybrania stacji (z zapowiedzią numeru) do pierwszych dźwięków strumienia.
// Trouble = ładowanie lub granie z kłopotami (zerwany strumień, za wolne buforowanie).
enum class State : uint8_t { Connecting, Setup, Stopped, Loading, Trouble, Playing };

const char* modeId(Mode m);
bool usesMusic(Mode m);  // tryb potrzebuje poziomu głośności z biblioteki audio
bool modeFromId(const String& id, Mode& out);

// Startuje zadanie animacji; od razu leci animacja startowa.
void begin(int pin, int count);

void setMode(Mode m);
Mode mode();
void setColor(uint32_t rgb);  // 0xRRGGBB
uint32_t color();
void setBrightness(uint8_t b);
uint8_t brightness();

// Wygaszenie diod tylko na czas grania muzyki; ładowanie, Wi-Fi, bateria i stop dalej świecą.
void setDarkWhilePlaying(bool on);
bool darkWhilePlaying();

void setState(State s);
void setVuLevel(uint8_t level);       // 0..255, z biblioteki audio
void setBatteryPercent(int percent);  // -1 = nieznany

}  // namespace leds
