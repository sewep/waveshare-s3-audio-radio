# Kopia fabrycznego flasha

Pełny zrzut 16 MB z płytki Waveshare ESP32-S3-AUDIO-Board (SKU 32184), wykonany 2026-10-21, zanim cokolwiek zostało wgrane.

| Plik | Co to |
|---|---|
| `factory-xiaozhi-2.4.2-full16MB.bin` | cały flash od `0x0` do `0x1000000` (poza gitem, patrz `.gitignore`) |
| `SHA256SUMS` | suma kontrolna pliku `.bin` |
| `efuse-summary.txt` | stan eFuse w chwili zrzutu (tylko do wglądu) |
| `restore-factory.ps1` | skrypt przywracający |

Zrzut sprawdzony `esptool verify-flash` — zgodny z chipem. Flash encryption i secure boot są wyłączone, więc obraz wgrywa się 1:1. NVS w kopii nie zawiera danych Wi-Fi (urządzenie nie było jeszcze skonfigurowane).

## Przywracanie

```powershell
.\backup\restore-factory.ps1            # domyślnie COM8
.\backup\restore-factory.ps1 -Port COM5
```

Albo ręcznie:

```powershell
python -m esptool --port COM8 -b 921600 write-flash 0x0 backup/factory-xiaozhi-2.4.2-full16MB.bin
```

Jeśli płytka nie łączy się z esptool (np. własny program zawiesza USB), wejdź w tryb pobierania: przytrzymaj **BOOT**, naciśnij i puść **RESET**, puść **BOOT** — i spróbuj ponownie.

**Plik `.bin` jest tylko na tym dysku.** Skopiuj go też w inne miejsce (np. NAS), bo git go nie przechowuje.
