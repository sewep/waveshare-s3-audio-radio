# Projekty na ESP32-S3-AUDIO-Board

Kolejka pomysłów na płytkę. Przed wgraniem czegokolwiek: kopia fabryczna jest w [backup/](../backup/README.md), polski xiaozhi buduje się skryptem [scripts/build-xiaozhi.ps1](../scripts/build-xiaozhi.ps1).

## 1. Radio internetowe sterowane z przeglądarki — działa (2026-10-21)

Kod: [radio/](../radio/) (Arduino, ESP32-audioI2S 4.0.0, rdzeń esp32 3.3.12).

- Zrobione: odtwarzanie MP3/AAC na ES8311, strona WWW pod `http://radio.local` (graj/stop, głośność, lista stacji z dodawaniem i usuwaniem), konfiguracja Wi-Fi przez hotspot `Radio-XXXX`, pamięć ostatniej stacji i głośności, ponowne łączenie po zerwaniu strumienia, przyciski: Key1 głośniej (przytrzymanie = ściszanie), Key2 graj/stop, Key3 następna stacja (przytrzymanie = poprzednia), BOOT graj/stop i reset Wi-Fi (3 s).
- Sprawdzone na sprzęcie: konfiguracja Wi-Fi przez hotspot, dźwięk z głośnika, strona pod `radio.local`.
- Pomysły na dalej: rezerwacja DHCP na routerze, płytka czyta swój adres głosem po starcie, kolory pierścienia LED (GPIO38, 6 diod WS2812) zależnie od stanu, sortowanie stacji.

## 2. Wykrywanie szczekania psa (i rozpoznawanie, który to pies)

Cel: płytka nasłuchuje, wykrywa szczekanie, a docelowo rozpoznaje konkretnego psa.

- **Etap 1 — detekcja szczekania na płytce.** Model klasyfikacji dźwięku (np. Edge Impulse: okno ~1 s, cechy MFE/MFCC, mała sieć CNN, eksport do biblioteki Arduino/ESP-IDF). Klasy: `szczekanie`, `tło`, ewentualnie inne hałasy domowe. Zdarzenie wysyłane do Home Assistant / MQTT.
- **Etap 2 — identyfikacja psa.** Zbiór zamknięty (znane psy: „nasz pies”, „pies sąsiada”, „inny”) jest realny jako dodatkowe klasy w tym samym modelu, jeśli zbierze się po kilkadziesiąt–kilkaset nagrań każdego psa. Dokładniejszy wariant: płytka wykrywa szczekanie i wysyła kilkusekundowy klip na serwer w domu (QNAP), gdzie model embeddingów audio (np. YAMNet/PANNs) + klasyfikator porównuje go z nagraniami wzorcowymi.
- **Dane.** Najważniejsza część: nagrania z tej samej płytki i tego samego miejsca (te same mikrofony i akustyka). Potrzebny prosty firmware do nagrywania klipów (na PC przez Wi-Fi albo przez USB) i oznaczania ich.
- **Ryzyka.** Fałszywe alarmy od podobnych dźwięków (TV, inne psy), różne odległości od mikrofonu; rozpoznawanie psa, którego model nigdy nie słyszał, jest znacznie trudniejsze niż wybór spośród znanych.
