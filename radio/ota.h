#pragma once
#include <Arduino.h>

// Aktualizacja z wydań na GitHubie (OTA). Najnowsze wydanie ma plik versions.tsv z listą kilku
// ostatnich wersji, które same potrafią się aktualizować (pole ota ≥ 1). Radio pobiera tę listę,
// a wybraną wersję wgrywa do wolnej partycji OTA, sprawdza SHA-256 i uruchamia się od nowa.
// Nowa wersja zostaje na stałe dopiero, gdy połączy się z Wi-Fi i popracuje 30 s; restart wcześniej
// (zawieszenie, brak sieci) = bootloader wraca do poprzedniej.
namespace ota {

// Wersja mechanizmu aktualizacji. GitHub Actions czyta ją stąd do firmware.json i versions.tsv;
// radio instaluje tylko wydania z ota ≥ 1, czyli takie, które potem same pobiorą następne.
constexpr int PROTOCOL = 1;

constexpr const char* REPO_URL = "https://github.com/sewep/waveshare-s3-audio-radio";

enum class State : uint8_t { Idle, Checking, Checked, Installing, Restarting, Error };

// Wołaj w setup() po prefs: stan partycji, wynik poprzedniej aktualizacji.
void begin();
// Wołaj w loop(); online = połączone z Wi-Fi (potwierdza nową wersję po 30 s).
void tick(bool online);

// Pobiera listę wersji w tle. waitAudio = radio właśnie milknie i zwalnia pamięć TLS.
bool startCheck(bool waitAudio);
// Instaluje wersję z ostatnio pobranej listy; po sukcesie radio samo się restartuje.
bool startInstall(const String& version, bool waitAudio, String& error);

State state();
bool busy();  // trwa sprawdzanie, instalacja albo restart: radio nie gra
String statusJson();

}  // namespace ota
