#pragma once
#include <Arduino.h>

// Plik dźwięku do trybu Halloween (/scare.mp3 na FFat): pobierany z adresu http/https w osobnym
// zadaniu albo wgrywany ze strony. Plik z sieci gra od razu, bez czekania na DNS i SSL.
namespace scare {

constexpr const char* FILE_PATH = "/scare.mp3";
constexpr size_t MAX_SIZE = 3 * 1024 * 1024;

enum class Download : uint8_t { Idle, Busy, Ok, Error };

// Zapamiętuje, czy plik już jest. Wołaj po zamontowaniu FFat.
void begin();

// Pobiera plik w tle; stan przez downloadState(). false = pobieranie już trwa.
bool startDownload(const String& url);
Download downloadState();
String lastError();

// Wgrywanie ze strony (WebServer upload): kawałki po kolei, na końcu finishUpload().
void uploadStart();
void uploadWrite(const uint8_t* data, size_t len);
bool uploadFinish();  // false = błąd, opis w lastError()
void uploadAbort();

size_t fileSize();  // 0 = brak pliku
bool busy();        // trwa pobieranie lub wgrywanie: nie odtwarzać

}  // namespace scare
