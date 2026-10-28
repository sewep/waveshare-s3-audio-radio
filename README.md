# waveshare-s3-audio-radio

<p align="center"><img src="docs/img/radio-hero.svg" alt="Animated drawing of the radio from the web interface: a wooden radio under a night sky with a glowing LED ring around the speaker" width="100%"></p>

Internet radio and other projects for the **Waveshare ESP32-S3-AUDIO-Board** (SKU 32184, sold as "AI Smart Speaker"): ESP32-S3R8, ES8311 speaker codec, ES7210 microphone array, 7 RGB LEDs, three buttons.

> The web interface, code comments and docs are in Polish. Interfejs, komentarze i dokumentacja są po polsku.

## What's inside

| Folder | What it is |
|---|---|
| [radio/](radio/) | Arduino sketch: Wi-Fi internet radio controlled from a browser |
| [halloween/](halloween/) | CC0 scream and laugh sounds for the radio's Halloween mode |
| [xiaozhi-esp32/](xiaozhi-esp32/) | Submodule: the [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) AI voice assistant (the factory firmware), pinned to v2.5.2 |
| [scripts/build-xiaozhi.ps1](scripts/build-xiaozhi.ps1) | Builds xiaozhi with the Polish UI and voice prompts (or any other language) |
| [backup/](backup/) | How to dump and restore the factory flash |
| [tools/](tools/) | Generator for the spoken station numbers (offline Piper TTS) |
| [docs/projekty.md](docs/projekty.md) | Project log and ideas |

## Radio features

- MP3, AAC and HLS streams (BBC, Polskie Radio), up to 99 stations with favourites, 36 preset stations
- Web interface at `http://radio.local`: playback, station list, alarm, lights, Wi-Fi, status and Halloween tabs
- Spoken station number (1–40) when switching, synthesized offline and built into the firmware
- Alarm clock with NTP time, weekday schedule, LED sunrise, rising volume and a beep fallback when the stream fails
- Sleep timer, tone control (bass / mid / treble), recently played titles
- Clap twice to play or stop. The detector subtracts the speaker's own output, because the speaker sits on the same board as the microphones
- More than 20 LED effects, including a music visualizer and FFT spectrum
- Up to 8 remembered Wi-Fi networks, each with optional static IP. The radio connects straight to the last one used and scans only when that fails, picking the known network with the best signal. If the signal stays very weak while the music keeps breaking up, it moves to a stronger known network. Setup hotspot `Radio-XXXX` at `http://192.168.4.1`
- Updates over Wi-Fi from GitHub releases (Status tab), including going back to an earlier version
- Halloween mode: hide the board in a mask, and when someone speaks nearby it plays a scream, with the LEDs glowing like eyes in time with the sound
- Buttons work like Bluetooth headphones: Key1 / Key3 change the volume, hold them to change the station, Key2 is play/stop

## Building the radio

You need [arduino-cli](https://arduino.github.io/arduino-cli/). The board core and library versions are pinned in [radio/sketch.yaml](radio/sketch.yaml) (esp32 core 3.3.12, ESP32-audioI2S 4.0.0, Adafruit NeoPixel 1.15.5).

```bash
arduino-cli compile --profile audio-board radio
arduino-cli upload --profile audio-board radio --port COM8
```

Replace `COM8` with your port. On the first start the radio opens the `Radio-XXXX` hotspot. Connect to it, pick your network, and the radio is then at `http://radio.local`.

The partition scheme is `app3M_fat9M_16MB`: two 3 MB app slots for OTA updates and a 9 MB FFat partition. The FFat partition holds the station list, the spoken numbers and the Halloween sound.

### Prebuilt firmware

Each [release](https://github.com/sewep/waveshare-s3-audio-radio/releases) has ready-to-flash files built by GitHub Actions:

| File | What it is |
|---|---|
| `radio-full.bin` | Bootloader, partition table and app. Flash it at `0x0` on a new board |
| `radio-app.bin` | The app alone, for updates (OTA) |
| `firmware.json` | Version, sizes and SHA-256 of both files |
| `versions.tsv` | The versions the radio offers for updates: this release and a few earlier ones |

```bash
python -m esptool --chip esp32s3 --port COM8 write-flash 0x0 radio-full.bin
```

`radio-full.bin` stops before the FFat partition, so flashing it keeps your stations and files. The latest files are always at `https://github.com/sewep/waveshare-s3-audio-radio/releases/latest/download/<file>`.

### Updates over Wi-Fi

The **Aktualizacje** card in the Status tab (Stan) checks the latest release, installs a newer version, or goes back to an earlier one. The list only shows versions that can update themselves, so going back never strands the radio on a version without updates. The radio checks the SHA-256 of the download, writes it to the spare OTA slot and restarts. The new version is kept only after it has been connected to Wi-Fi for 30 seconds; if it restarts before that, the bootloader goes back to the previous version. Version 1.0.0 has no updates over Wi-Fi, so it needs one flash over USB.

To publish a release, push a version tag. A tag with a hyphen, for example `v1.1.0-beta.1`, is marked as a pre-release.

```bash
git tag v1.0.0
git push origin v1.0.0
```

## Building xiaozhi in Polish

Requires ESP-IDF ≥ 6.0.1 (tested with 6.1 in `C:\esp\esp-idf-v6.1`).

```powershell
git submodule update --init
.\scripts\build-xiaozhi.ps1                # pl-PL
.\scripts\build-xiaozhi.ps1 -Language en-US
.\scripts\build-xiaozhi.ps1 -Flash         # build and flash
```

ESP-IDF fails when the project path contains a space, so the script copies the sources to `C:\esp\work\xiaozhi-esp32` and builds there.

## Before flashing anything

Make a full copy of the factory flash first. The steps are in [backup/README.md](backup/README.md). The flash dump itself is not in this repository.

## Hardware notes

Things worth knowing about this board that the schematics don't make obvious:

- The speaker amplifier is enabled by bit 0 of port 1 on the TCA9555 expander (I2C 0x20). Buttons Key1–Key3 are bits 1–3 of the same port, active low.
- The 7 WS2812 LEDs on GPIO38 use RGB order (`NEO_RGB`), not the usual GRB. xiaozhi assumes 6.
- ES7210 MIC3 is a loopback of the speaker (AEC reference), not a microphone.
- Battery voltage measurement needs a 0 Ω jumper soldered on GPIO1, which disables the camera.

More hardware details and library workarounds (ESP32-audioI2S 4.0.0 bugs with short Ogg and MP3 files) are in [CLAUDE.md](CLAUDE.md).

## License

The code in this repository is licensed under [GPL-3.0](LICENSE), because the radio links against the GPL-3.0 library ESP32-audioI2S.

Third-party content:

- Halloween sounds: CC0, from OpenGameArt (authors listed in [halloween/README.md](halloween/README.md))
- Spoken station numbers in `radio/station_numbers.h`: generated with [Piper](https://github.com/rhasspy/piper) using the `pl_PL-gosia-medium` voice (trained on a CC0 dataset)
- xiaozhi-esp32: MIT, included as a submodule
