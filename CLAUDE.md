# Smart spiker — eksperymenty z Waveshare ESP32-S3-AUDIO-Board

Repozytorium na własne projekty na płytce Waveshare ESP32-S3-AUDIO-Board (SKU 32184, „AI Smart Speaker”).

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

## Narzędzia i pułapki

- esptool zainstalowany przez `pip --user`, nie ma go w PATH — wywołuj `python -m esptool` / `python -m espefuse`.
- Reset przez pyserial (DTR/RTS) nie działa niezawodnie; do restartu używaj `python -m esptool --port COM8 --after hard-reset chip-id`.
- Po resecie port USB znika i wraca, więc pierwsze ~0,5 s logu startowego przepada. Monitor portu musi się ponownie łączyć.
- Logi fabrycznego firmware'u idą na 115200, prefiks ESP-IDF (`I (ms) TAG:`).
