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
- Dodane 2026-10-22: straszak na Halloween (zakładka z dynią): radio schowane w masce czeka na głos ponad progiem, gra krzyk z pliku MP3 (pobrany z adresu albo wgrany ze strony), a diody świecą jak oczy w rytm dźwięku. Sprawdzone na płytce: wgrywanie i pobieranie (http, https, odrzucanie nie-MP3, plik 0,5 s z ramką Info bez restartu), pełny cykl czuwanie → krzyk → odpoczynek → czuwanie; wygląd oczu w masce do obejrzenia.
- Pomysły na dalej: kilka plików na zmianę dla straszaka, drzemka budzika, zapowiedzi numerów 41–99 (trzeba wygenerować Piperem), odcinki podcastów z RSS, rezerwacja DHCP na routerze, płytka czyta swój adres głosem po starcie, sortowanie stacji.

## 2. Wykrywanie szczekania psa (i rozpoznawanie, który to pies)

Cel: płytka nasłuchuje, wykrywa szczekanie, a docelowo rozpoznaje konkretnego psa.

- **Etap 1 — detekcja szczekania na płytce.** Model klasyfikacji dźwięku (np. Edge Impulse: okno ~1 s, cechy MFE/MFCC, mała sieć CNN, eksport do biblioteki Arduino/ESP-IDF). Klasy: `szczekanie`, `tło`, ewentualnie inne hałasy domowe. Zdarzenie wysyłane do Home Assistant / MQTT.
- **Etap 2 — identyfikacja psa.** Zbiór zamknięty (znane psy: „nasz pies”, „pies sąsiada”, „inny”) jest realny jako dodatkowe klasy w tym samym modelu, jeśli zbierze się po kilkadziesiąt–kilkaset nagrań każdego psa. Dokładniejszy wariant: płytka wykrywa szczekanie i wysyła kilkusekundowy klip na serwer w domu (QNAP), gdzie model embeddingów audio (np. YAMNet/PANNs) + klasyfikator porównuje go z nagraniami wzorcowymi.
- **Dane.** Najważniejsza część: nagrania z tej samej płytki i tego samego miejsca (te same mikrofony i akustyka). Potrzebny prosty firmware do nagrywania klipów (na PC przez Wi-Fi albo przez USB) i oznaczania ich.
- **Ryzyka.** Fałszywe alarmy od podobnych dźwięków (TV, inne psy), różne odległości od mikrofonu; rozpoznawanie psa, którego model nigdy nie słyszał, jest znacznie trudniejsze niż wybór spośród znanych.

## 3. Radio: wyszukiwarka stacji z radio-browser.info — działa (2026-10-29)

Sprawdzone na płytce: wyszukiwanie (jazz w PL: 7 stacji, „BBC Radio” na świecie: 36, w tym HLS), rozpoznanie stacji już zapisanych, dodanie i granie w radiu z tytułem. Odsłuch ▶ w przeglądarce nie sprawdzony automatycznie (w panelu bez dźwięku).

Cel: w zakładce Stacje pole wyszukiwania (nazwa, kraj, gatunek), wyniki z kodekiem, bitrate i przyciskiem „Dodaj” (oraz „Posłuchaj” bez zapisu na liście).

- **Bez zmian w firmware na wyszukiwanie.** API wysyła `Access-Control-Allow-Origin: *` (sprawdzone 2026-10-29), więc stronę radia może pytać przeglądarka, a do radia trafia tylko gotowa stacja przez istniejące API dodawania. Strona z `http://radio.local` może wołać API po https.
- **API:** `https://<serwer>.api.radio-browser.info/json/stations/search?name=…&countrycode=PL&tag=…&hidebroken=true&order=clickcount&reverse=true&limit=30`; listę serwerów daje `all.api.radio-browser.info/json/servers` (lustrzane, losować jeden). Do strumienia brać `url_resolved`. Przy dodaniu stacji wypada wywołać `/json/url/<stationuuid>` (licznik kliknięć, prośba autorów API).
- **Filtry:** tylko kodeki, które gra ESP32-audioI2S (MP3, AAC/AAC+, także HLS); stacje z bitrate > 320 kb/s pomijać albo oznaczać.
- **Ograniczenie:** lista stacji to najwyżej 99 pozycji; przy pełnej liście komunikat zamiast cichej odmowy.

## 4. Radio: komendy głosowe bez chmury (ESP-SR)

Cel: „Hi ESP” + komenda („next station”, „volume up”, „stop”) działające także w trakcie grania.

- **Co daje rdzeń esp32 3.3.12** (sprawdzone w `libraries/ESP_SR` i `esp32s3-libs/3.3.12/sdkconfig`): słowo wybudzające wkompilowane na sztywno — tylko `wn9_hiesp` („Hi ESP”), komendy po angielsku (MultiNet7 quant) podawane jako tekst. Inne słowo albo język wymagają przebudowania bibliotek rdzenia (esp32-arduino-lib-builder). Polskie komendy zapisane fonetycznie po angielsku — do sprawdzenia, raczej zawodne.
- **Dźwięk wejściowy:** ESP-SR chce 16 kHz, 16 bit; mikrofony czyta teraz I2S1 jako slave przy 48 kHz, 32 bit (zegar z biblioteki audio). Zamiast `ESP_SR.begin(i2s…)` użyć niższego `sr_start()` z `esp32-hal-sr.h` z własnym `fill_cb`: decymacja 48→16 kHz z `mics.cpp`, układ kanałów np. `"MMR"` (input_format: M = mikrofon, R = odniesienie z głośnika, N = nieużywany).
- **Usuwanie echa (AEC):** bez niego muzyka zagłuszy komendy (głośnik daje w mikrofonach do −2 dBFS przy głośności 14). Odniesienie najlepiej z MIC3 (na tej płytce pętla z głośnika, tak robi xiaozhi) — wymaga przejścia ES7210 i I2S1 na TDM z 3–4 kanałami. Zastępczo próbki wyjścia z `audio_process_i2s()`, ale opóźnienie DMA (~85 ms) musi być stałe.
- **Partycje:** ESP-SR czyta modele z partycji `model` (`srmodels.bin`). Schemat rdzenia `esp_sr_16` ma te same sloty aplikacji co obecny `app3M_fat9M_16MB` (0x10000 i 0x310000 po 3 MB), a FAT dzieli na 6 MB danych + 3,9 MB modeli od 0xC10000. Zmiany tablicy partycji nie da się zrobić przez OTA: jednorazowe wgranie przez USB, sformatowanie FFat od nowa (najpierw eksport listy stacji). Aplikacja powinna sprawdzać obecność partycji `model` w czasie pracy, żeby ten sam program działał też na płytkach ze starym układem (bez komend głosowych) i dalej aktualizował się przez OTA. Sprawdzić, czy `arduino-cli --profile` z własnym `partitions.csv` wygeneruje i wgra `srmodels.bin` (w `boards.txt` robi to tylko schemat `esp_sr_16`), oraz czy `radio-full.bin` w Actions ma zawierać modele.
- **Ryzyka:** obciążenie CPU (AFE + WakeNet na rdzeniu 0 obok Wi-Fi, dekodowanie na rdzeniu 1 — mierzyć przerwy w `/api/diag`), PSRAM (modele i bufory AFE) obok 640 KB bufora strumienia; konflikt z klaskaniem i trybem Halloween (wspólne mikrofony).
