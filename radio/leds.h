#pragma once
#include <Arduino.h>
#include <vector>

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
  ColorOrgan,    // kolorofon: bas czerwony, średnie zielone, wysokie niebieskie (widmo z FFT)
  Spectrum,      // analizator widma: każda dioda to inne pasmo, od basu do wysokich
  Confetti,      // losowe kolorowe rozbłyski
  Aurora,        // zorza: płynące zielenie, błękity i fiolety
  Bounce,        // kula odbijająca się tam i z powrotem
  Twins,         // dwie komety w przeciwnych kierunkach
  Auto,          // pokaz: kolejne efekty co autoSeconds() sekund, z płynnym przejściem
  Count
};

// Stan urządzenia, który może nadpisać tryb (np. konfiguracja Wi-Fi).
// Loading = od wybrania stacji (z zapowiedzią numeru) do pierwszych dźwięków strumienia.
// Trouble = ładowanie lub granie z kłopotami (zerwany strumień, za wolne buforowanie).
enum class State : uint8_t { Connecting, Setup, Stopped, Loading, Trouble, Playing };

const char* modeId(Mode m);
// Czego tryb potrzebuje od biblioteki audio (liczenie poziomu i FFT tylko wtedy, gdy trzeba).
bool usesVu(Mode m);
bool usesSpectrum(Mode m);
// Efekt grany teraz w pokazie (Mode::Auto); w innych trybach to po prostu mode().
Mode currentEffect();
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
void setAutoSeconds(uint8_t s);  // czas jednego efektu w pokazie, 5..120 s
uint8_t autoSeconds();

void setVuLevel(uint8_t level);       // 0..255, z biblioteki audio
void setSpectrum(const std::vector<uint32_t>& bands);  // 15 pasm 0..255, z biblioteki audio
void setBatteryPercent(int percent);  // -1 = nieznany

// Świt przed budzikiem: 0..1 rozjaśnia pierścień od czerwieni do ciepłej bieli; -1 = wyłączony.
// Ma pierwszeństwo przed trybem i stanem (poza animacją startową i wskaźnikiem).
void setSunrise(float progress);

// Przez ms świeci count pierwszych diod w kolorze rgb (np. czas wyłącznika), ponad wszystkim.
void showIndicator(uint8_t count, uint32_t rgb, uint32_t ms);

}  // namespace leds
