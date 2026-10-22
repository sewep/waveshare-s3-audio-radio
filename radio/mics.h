#pragma once
#include <Arduino.h>

// Mikrofony płytki (ES7210: MIC1 i MIC2). Próbki czyta I2S1 jako slave na tych samych pinach
// BCLK/WS co I2S0 biblioteki audio, więc działa tylko, gdy biblioteka podaje zegary (cały czas
// po Audio::setPinout()). Poziomy liczy osobne zadanie, ale tylko gdy ktoś je ogląda.
namespace mics {

constexpr int COUNT = 2;
constexpr float SILENCE_DB = -120;

// Konfiguruje ES7210 i I2S1, startuje zadanie pomiaru. Wołaj po board::begin() i Audio::setPinout().
bool begin();

// Ktoś ogląda poziomy: mierz przez najbliższe 5 s (potem zadanie śpi, a I2S1 stoi).
void keepAwake();
bool awake();

// Średni (RMS) i szczytowy poziom z ostatnich 100 ms w dBFS; SILENCE_DB = brak danych.
float rmsDb(int mic);
float peakDb(int mic);

}  // namespace mics
