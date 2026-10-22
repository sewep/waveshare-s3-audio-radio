# Smart spiker — eksperymenty z Waveshare ESP32-S3-AUDIO-Board

Repozytorium na własne projekty na płytce Waveshare ESP32-S3-AUDIO-Board (SKU 32184, „AI Smart Speaker”).

Kolejka zaplanowanych projektów: [docs/projekty.md](docs/projekty.md).

## Płytka

- ESP32-S3R8 (QFN56, rev v0.2), 240 MHz dual core, 8 MB PSRAM (AP 3.3 V), 16 MB flash QIO.
- Podłączona przez natywne USB (USB-Serial/JTAG, VID 303A PID 1001), u użytkownika jako **COM8**.
- Audio: kodek ES8311 (głośnik, DAC) + ES7210 (mikrofony, 4 kanały TDM), I2S.
- Złącza LCD (firmware fabryczny używa sterownika JD9853) i kamery DVP — kamera niepodłączona.
- Brak flash encryption i secure boot.

## Fabryczny firmware i kopia

- Fabrycznie: xiaozhi-esp32 2.4.2 (ESP-IDF 6.0.2), układ partycji: nvs `0x9000`, otadata `0xd000`, phy_init `0xf000`, ota_0 `0x20000`, ota_1 `0x410000`, assets `0x800000` (8 MB).
- Pełna kopia flasha i skrypt przywracania: [backup/README.md](backup/README.md). Przed zapisem czegokolwiek na płytkę kopia musi istnieć.

## Własny build xiaozhi (wersja PL)

- Źródła: podmoduł [xiaozhi-esp32/](xiaozhi-esp32/) (github.com/78/xiaozhi-esp32), przypięty do tagu v2.5.2. Płytka w projekcie: `waveshare/esp32-s3-audio-board`.
- ESP-IDF 6.1 w `C:\esp\esp-idf-v6.1` (narzędzia w `%USERPROFILE%\.espressif`); xiaozhi 2.5.x wymaga IDF ≥ 6.0.1.
- Budowanie: `.\scripts\build-xiaozhi.ps1` (domyślnie `pl-PL`, `-Language en-US` itd., `-Flash` od razu wgrywa). Wynik: `firmware/xiaozhi-<wersja>-<język>-esp32-s3-audio-board.bin` — pełny obraz od `0x0` z partycją assets.
- **ESP-IDF nie działa ze spacją w ścieżce** („Smart spiker”): `esp_audio_codec` i `esp_image_effects` podają linkerowi `-L <ścieżka>` bez cudzysłowów. `subst` i junction nie pomagają, bo `idf.py` i `build.py` robią `realpath`. Dlatego skrypt kopiuje źródła robocopy `/MIR` do `C:\esp\work\xiaozhi-esp32` i buduje tam. Zmiany w kodzie rób w podkatalogu `xiaozhi-esp32/`, nie w kopii roboczej.
- Język interfejsu i komunikatów głosowych jest wkompilowany (`CONFIG_LANGUAGE_*`); język rozmowy ustawia się na serwerze xiaozhi.me. Słowo wybudzające: domyślne „Nǐ hǎo Xiǎozhì” (`wn9_nihaoxiaozhi_tts`), inne modele: `python scripts/build.py --list-wake-words`.

## Radio (Arduino)

- Szkic w [radio/](radio/), profil `audio-board` w `radio/sketch.yaml` przypina rdzeń esp32 3.3.12 i ESP32-audioI2S 4.0.0 oraz port COM8.
- Budowanie i wgrywanie: `arduino-cli compile --profile audio-board radio`, `arduino-cli upload --profile audio-board radio` (arduino-cli w `C:\Program Files\Arduino CLI`). Arduino nie ma problemu ze spacją w ścieżce.
- Sprzęt audio: wzmacniacz głośnika włącza bit 0 portu 1 ekspandera TCA9555 (I2C 0x20, EXIO8); ES8311 (0x18) jako slave z MCLK z GPIO12. Biblioteka audio wysyła stale 48 kHz (`setOutputSampleRate(SR_48000)`), więc kodek konfiguruje się raz na MCLK = 256·fs.
- Lista stacji (do 99) w pliku `/stations.tsv` na FFat, zapis przez `/stations.tmp` i zmianę nazwy; stara lista z Preferences przenosi się sama przy starcie. Strumienie HLS (`.m3u8`, BBC, Polskie Radio) biblioteka gra.
- Wi-Fi: dwie zapamiętane sieci w Preferences (`ssid`/`pass`/`ipcfg` = główna, `ssid2`/`pass2`/`ipcfg2` = zapasowa; `ipcfg` = „ip maska brama dns”, brak klucza = DHCP, stały adres wędruje razem z siecią). Zapis nowej sieci (strona, zakładka Wi-Fi) robi z niej główną, a poprzednią zapasową; przy starcie radio próbuje obu (po 20 s), a ta, która zadziała, zostaje główną. Przytrzymanie BOOT niczego nie kasuje: ustawia flagę `setupOnce` i restartuje do hotspotu `Radio-XXXX`; hotspot bez gości przez 5 min = restart i ponowna próba zapisanych sieci. Strona `INDEX_HTML` służy w obu trybach (w hotspocie widać tylko zakładkę Wi-Fi). Skanowanie sieci jest asynchroniczne (`/api/scan` zwraca `{"scanning":true}`, aż wyniki będą gotowe), bo blokujące wstrzymywało stronę na ~3 s.
- Mikrofony (`radio/mics.cpp`): ES7210 (I2C 0x40) w zwykłym I2S, 32 bit, MIC1 = lewy, MIC2 = prawy kanał, wzmocnienie 30 dB; MIC3 to na tej płytce pętla z głośnika (referencja AEC w xiaozhi), wyłączony. Czyta je I2S1 jako slave na pinach BCLK 13 / WS 14 biblioteki audio (DIN 15). Sterownik I2S ustawia te piny jako wejścia, więc po inicjalizacji trzeba przywrócić na nich wyjścia I2S0 (`esp_rom_gpio_connect_out_signal`, `I2S0O_BCK_OUT_IDX` / `I2S0O_WS_OUT_IDX`). Pomiar (i I2S1) działa tylko 5 s po ostatnim `/api/levels` albo cały czas, gdy włączone jest klaskanie.
- Klaskanie (dwa klaśnięcia 150–800 ms od siebie = graj/stop): głośnik i mikrofony są na jednej płytce, przy głośności 14 muzyka daje w mikrofonach szczyty do −2 dBFS. Dlatego `audio_process_i2s()` podaje próbki wyjścia do `mics::feedOutput()`, a klaśnięcie musi przebić szczyt wyjścia z 400 ms + sprzężenie (maksimum z ostatnich chwil, opada 0,5 dB/s) + 6 dB; przy dużej głośności nie działa wcale. Start i zatrzymanie odtwarzania dają trzask ~0 dBFS niewidoczny w próbkach wyjścia, więc `sendAudio()` i pierwsza zdekodowana ramka wyłączają wykrywanie na 1,5 s (`mics::suppress`). Każde wykrycie idzie do logu szeregowego (szczyt, tło, głośnik, sprzężenie).
- Czas z NTP (`configTzTime`, strefa CET/CEST) po połączeniu z Wi-Fi. Budzik (`alOn`, `alH`, `alM`, `alDays` = maska bitów `tm_wday`, 0 = jednorazowy, `alSt`, `alVol`, `alRamp`, `alSun` w Preferences): świt na diodach (`leds::setSunrise`, ponad trybem), stacja z głośnością rosnącą od 1, a gdy po 30 s nic nie gra, pikanie z `/beep2.wav` (WAV 16 kHz generowany przy starcie, puszczany co 2,2 s, bo biblioteka nie zapętla plików). `play()`, `stop()` i `setVolume()` kończą budzik, więc budzik ustawia swój stan dopiero po `play()`.
- Halloween (`radio/scare.cpp`, zakładka 🎃): głos w mikrofonach ponad progiem przez 200 ms = `/scare.mp3` z FFat (pobrany w osobnym zadaniu z adresu albo wgrany ze strony, do 3 MB), oczy = `leds::setScare` z jasnością ze szczytu wyjścia w `audio_process_i2s()`; po krzyku odpoczynek, żeby echo nie straszyło. Włączony tryb przetrwa restart i wtedy radio nie wznawia grania. Plik: znacznik Xing/Info w pierwszej ramce jest zerowany (obejście błędu biblioteki z plikami < 1 s), sprawdzana synchronizacja ramki MP3 (za ID3v2). Drugie połączenie TLS obok strumienia https nie mieści się w RAM („SSL - Memory allocation failed”), więc pobieranie https zatrzymuje radio. Pobieranie przez `HTTPClient::writeToStream`, bo surowy strumień zawiera nagłówki kawałków przy `Transfer-Encoding: chunked`.
- Ulubione stacje: trzecia kolumna `*` w `/stations.tsv`. Barwa dźwięku: `audio.setTone()` (półki 500 Hz / 6 kHz, szczyt 1,8 kHz, ±12 dB), wołane z zadania audio poleceniem `AudioCmd::Tone`; wartości w `msg.arg` jako bajty, bo `sendAudio()` kopiuje napis, a 0 dB to bajt 0. Historia tytułów (20 ostatnich) tylko w RAM.
- Poziom na głośniku liczy `audio_process_i2s()` (słaby symbol biblioteki, wywoływany po regulacji głośności). Krzywa głośności biblioteki: dB = −112t³ + 172t² − 60, t = głośność/21. Zmierzone (2026-10-22): pokój ok. −70 dBFS w mikrofonach, muzyka na głośności 7 podnosi je do ok. −61 dBFS — głośnik wyraźnie słychać w mikrofonach.
- Zapowiedź numeru stacji: `radio/station_numbers.h` jest generowany (nie edytuj ręcznie) przez `tools/gen_station_numbers.py` lokalnym Piperem (pip: piper-tts, soundfile, numpy, scipy + model głosu z huggingface.co/rhasspy/piper-voices). Nagrania MP3 16 kHz mono, ok. 270 KB na 40 liczebników. Przy starcie trafiają na partycję FFat (`/n/<nr>.mp3`, przepisywane, gdy zmieni się CRC), bo biblioteka audio gra lokalne pliki tylko przez `connecttoFS`. Połączenie ze strumieniem po zapowiedzi zleca `loop()` po `evt_eof` i dopiero po długości nagrania (`eof` = zdekodowane, nie odtworzone; `connecttohost` czyści bufor wyjściowy).
- Błędy ESP32-audioI2S 4.0.0, które trzeba omijać:
  - pliki Ogg (Opus/Vorbis) < 64 KB: `getLastGranulePosition()` czeka 3 s w `audio.loop()` — dlatego nagrania są w MP3;
  - MP3 z ramką Xing/Info krótszy niż 1 s: dzielenie przez zero w `read_ID3_Header` (restart w pętli) — generator usuwa tę ramkę;
  - plik włączony zaraz po przerwaniu strumienia w trakcie buforowania potrafi się zawiesić — przed `connecttoFS` jest `stopSong()`, a w `loop()` strażnik: brak „stream ready” po 1,5 s = ponowienie, potem pominięcie zapowiedzi.
- Wszystkie wywołania biblioteki audio idą z osobnego zadania (`audioTask`, rdzeń 1, ten sam priorytet co `loop()`), sterowanego kolejką `AudioMsg`. `connecttohost` blokuje na DNS i SSL (1–3 s), a wyższy priorytet zagładzał stronę WWW przez ~0,6 s liczenia SSL.
- Przyciski Key1–Key3 (schemat: Extend_IO10–12) to wejścia TCA9555 port 1, bity 1–3, aktywne stanem niskim (podciąganie 10 kΩ). Extend_IOn ze schematu = pin n−1 ekspandera, więc PA_CTRL (Extend_IO9) = EXIO8 z kodu xiaozhi.
- Diody: 7× WS2812 w jednym łańcuchu na GPIO38 (xiaozhi zakłada 6), kolejność kolorów RGB (`NEO_RGB`, nie typowe dla WS2812 `NEO_GRB`). Animacje w osobnym zadaniu FreeRTOS (`radio/leds.cpp`), więc działają, gdy `setup()` czeka na Wi-Fi.
- Bateria: dzielnik 200k/100k (ADC = ⅓ napięcia) na BAT_ADC, fabrycznie niepodłączony z GPIO1 — trzeba wlutować zworkę 0R, a kamera przestaje działać. Ładowarka ETA6098 sygnalizuje stan tylko diodą, ESP32 go nie widzi.
- W rdzeniu 3.x `WiFi.macAddress()` tuż po `WiFi.mode()` zwraca zera — MAC czytaj przez `esp_read_mac()`.

## Narzędzia i pułapki

- esptool zainstalowany przez `pip --user`, nie ma go w PATH — wywołuj `python -m esptool` / `python -m espefuse`.
- Reset przez pyserial (DTR/RTS) nie działa niezawodnie; do restartu używaj `python -m esptool --port COM8 --after hard-reset chip-id`.
- Po resecie port USB znika i wraca, więc pierwsze ~0,5 s logu startowego przepada. Monitor portu musi się ponownie łączyć.
- Logi fabrycznego firmware'u idą na 115200, prefiks ESP-IDF (`I (ms) TAG:`).
