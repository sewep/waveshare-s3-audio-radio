# Projekty na ESP32-S3-AUDIO-Board

Kolejka pomysłów na płytkę. Przed wgraniem czegokolwiek: kopia fabryczna jest w [backup/](../backup/README.md), polski xiaozhi buduje się skryptem [scripts/build-xiaozhi.ps1](../scripts/build-xiaozhi.ps1).

## 1. Radio internetowe sterowane z przeglądarki — działa (2026-10-21)

Kod: [radio/](../radio/) (Arduino, ESP32-audioI2S 4.0.0, rdzeń esp32 3.3.12).

- Zrobione: odtwarzanie MP3/AAC na ES8311, strona WWW pod `http://radio.local` z zakładkami Radio / Stacje / Podświetlenie / Pomoc (graj/stop, poprzednia/następna, głośność, lista stacji z dodawaniem i usuwaniem, instrukcja i opis przycisków, dane urządzenia), konfiguracja Wi-Fi przez hotspot `Radio-XXXX`, pamięć ostatniej stacji i głośności, ponowne łączenie po zerwaniu strumienia, przyciski jak w słuchawkach BT: Key1 ciszej (przytrzymanie = poprzednia stacja), Key2 graj/stop (przytrzymanie = wygaszenie diod na czas grania), Key3 głośniej (przytrzymanie = następna stacja), BOOT graj/stop i reset Wi-Fi (3 s).
- Lista stacji do 99 pozycji w pliku `/stations.tsv` na FFat (wcześniej Preferences, limit 4000 bajtów na napis); domyślnie 36 stacji: polskie, dance/EDM/workout, BBC (HLS), rozmowy PL/EN. Przycisk „Przywróć domyślne stacje”.
- Zapowiedź numeru stacji (1–40) przy starcie i zmianie stacji, offline: nagrania MP3 z lokalnej syntezy Piper (głos pl_PL-gosia-medium) wbudowane w firmware, przełącznik w zakładce Stacje.
- Odtwarzanie w osobnym zadaniu FreeRTOS: strona i przyciski odpowiadają także podczas łączenia ze stacją (test 2026-10-21: 95/95 zapytań, maks. 389 ms przy przełączaniu stacji co 4 s; wcześniej przerwy po 3 s). Od wybrania stacji do pierwszych dźwięków diody pokazują niebieską kometę.
- Sprawdzone na sprzęcie: konfiguracja Wi-Fi przez hotspot, dźwięk z głośnika, strona pod `radio.local`.
- Diody (7× WS2812 na GPIO38): animacja tęczy i błyski R-G-B po włączeniu zasilania; tryby wybierane na stronie: wyłączone, status, stały kolor, oddech, tęcza, kometa, wizualizer muzyki, puls i disco (na uderzenia), kolorofon i widmo (z FFT biblioteki, `settings.SPECTRUM`), tęczowa kometa, fala, iskry, konfetti, zorza, piłka, dwie komety, ogień, policja oraz pokaz (efekty po kolei co 5–60 s, przejście 1 s); kolor i jasność. Wizualizer bierze poziom z biblioteki (pierwiastek amplitudy), podnosi go do kwadratu i rozciąga między ruchomym dnem a sufitem, bo inaczej skompresowana muzyka trzymała pierścień prawie na maksimum. Niebieska kometa = łączenie z Wi-Fi, niebieski oddech = tryb konfiguracji, pomarańczowa kometa = kłopot ze strumieniem (ładowanie > 8 s, ponowne łączenie, „slow stream”/„Stream lost” z biblioteki).
- Bateria: czerwone pulsowanie poniżej 10% jest w kodzie, ale wyłączone (`BATTERY_ADC_ENABLED` w `board.h`) — płytka fabrycznie nie mierzy napięcia baterii; wymaga wlutowania zworki 0R przy GPIO1, co wyłącza kamerę.
- Dodane 2026-10-22: budzik (dni tygodnia albo jednorazowy, świt na diodach, narastanie głośności, pikanie, gdy stacja nie zagra), wyłącznik czasowy (strona albo Key1 + Key3, wyciszanie w ostatniej minucie), barwa dźwięku (bas / środek / wysokie i gotowe ustawienia), ostatnio grane tytuły z linkiem do wyszukiwania, ulubione stacje (gwiazdka, przyciski na stronie, podwójne kliknięcie Key2), dwa klaśnięcia = graj/stop. Sprawdzone na płytce: NTP, historia, wyłącznik, narastanie i pikanie budzika; klaskanie sprawdzone tylko pod kątem fałszywych wykryć (4 min, cisza i głośności 5–14: zero), prawdziwe klaśnięcia do przetestowania.
- Pomysły na dalej: drzemka budzika, zapowiedzi numerów 41–99 (trzeba wygenerować Piperem), odcinki podcastów z RSS, rezerwacja DHCP na routerze, płytka czyta swój adres głosem po starcie, sortowanie stacji.

## 2. Wykrywanie szczekania psa (i rozpoznawanie, który to pies)

Cel: płytka nasłuchuje, wykrywa szczekanie, a docelowo rozpoznaje konkretnego psa.

- **Etap 1 — detekcja szczekania na płytce.** Model klasyfikacji dźwięku (np. Edge Impulse: okno ~1 s, cechy MFE/MFCC, mała sieć CNN, eksport do biblioteki Arduino/ESP-IDF). Klasy: `szczekanie`, `tło`, ewentualnie inne hałasy domowe. Zdarzenie wysyłane do Home Assistant / MQTT.
- **Etap 2 — identyfikacja psa.** Zbiór zamknięty (znane psy: „nasz pies”, „pies sąsiada”, „inny”) jest realny jako dodatkowe klasy w tym samym modelu, jeśli zbierze się po kilkadziesiąt–kilkaset nagrań każdego psa. Dokładniejszy wariant: płytka wykrywa szczekanie i wysyła kilkusekundowy klip na serwer w domu (QNAP), gdzie model embeddingów audio (np. YAMNet/PANNs) + klasyfikator porównuje go z nagraniami wzorcowymi.
- **Dane.** Najważniejsza część: nagrania z tej samej płytki i tego samego miejsca (te same mikrofony i akustyka). Potrzebny prosty firmware do nagrywania klipów (na PC przez Wi-Fi albo przez USB) i oznaczania ich.
- **Ryzyka.** Fałszywe alarmy od podobnych dźwięków (TV, inne psy), różne odległości od mikrofonu; rozpoznawanie psa, którego model nigdy nie słyszał, jest znacznie trudniejsze niż wybór spośród znanych.
