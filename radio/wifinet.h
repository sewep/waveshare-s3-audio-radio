#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include <vector>

// Zapamiętane sieci Wi-Fi (do 8) i wybór sieci przy starcie oraz w trakcie pracy.
//
// Start: najpierw ostatnio używana sieć wprost, z zapamiętanym punktem dostępowym (BSSID) i kanałem,
// bez skanowania — w domu tyle samo czasu co zwykłe łączenie. Gdy się nie uda, jedno szybkie
// skanowanie i próby tylko z sieciami w zasięgu, od najlepszej (klasa sygnału, potem ostatnio
// używana; inna wygrywa z nią przewagą ≥ 8 dB). Brak znanej sieci w zasięgu = od razu hotspot.
// Po szybkim połączeniu ze słabym sygnałem radio sprawdza, czy w pobliżu nie ma lepszej znanej sieci.
// W pracy: gdy sygnał długo jest bardzo słaby, a strumień ma kłopoty (albo radio nie gra), skanowanie
// w tle i przejście (przez restart) na zapamiętaną sieć mocniejszą o ≥ 10 dB.
//
// Lista leży w Preferences "wifi" (klucz "list", jeden napis zapisywany naraz). Dwie pierwsze sieci
// są też w starych kluczach ssid/ssid2 przestrzeni "radio", więc wersja sprzed listy (powrót do
// starszej wersji przez OTA) dalej się łączy.
namespace wifinet {

constexpr int MAX_NETWORKS = 8;

struct IpConfig {
  IPAddress ip, mask, gw, dns;
};

struct Network {
  String ssid, pass;
  String ipcfg;  // "ip maska brama dns" albo pusty = DHCP
  bool hidden = false;
  uint8_t bssid[6] = {0};
  uint8_t channel = 0;  // 0 = nieznany punkt dostępowy (sieć jeszcze nie łączona)
  uint32_t lastUsed = 0;  // czas uniksowy ostatniego połączenia, 0 = nieznany
};

// Wczytuje listę; przy pierwszym starcie przenosi sieci ze starych kluczy.
void begin();

const std::vector<Network>& networks();
int find(const String& ssid);
bool empty();

// Zapis nowej sieci (albo nowego hasła): trafia na początek listy, stały adres zostaje przy sieci.
// false + błąd w err, np. pełna lista.
bool remember(const String& ssid, const String& pass, bool hidden, String& err);
bool forget(const String& ssid);
bool setIpConfig(const String& ssid, const String& cfg);
String password(const String& ssid);

bool parseIpConfig(const String& s, IpConfig& c);
const char* checkIpConfig(const IpConfig& c);  // nullptr = poprawny

// Łączenie przy starcie. true = połączono; opis wyboru w lastChoice(), nieudane sieci w lastFailed().
bool connect();
String connectedSsid();
bool connectedStatic();
String lastChoice();   // np. „ostatnio używana, −58 dBm” albo „Biuro zamiast Dom: o 12 dB mocniejsza”
String lastFailed();   // sieć w zasięgu, z którą nie udało się połączyć (złe hasło?), albo pusty
uint32_t connectMs();  // czas łączenia przy starcie

// Wołaj w loop(). playing/trouble: radio gra / strumień ma kłopoty (przerwy, ponowne łączenie).
// Zwraca true, gdy radio ma się zrestartować, żeby przejść na lepszą sieć.
bool tick(bool playing, bool trouble);

// Ostatnio widziana siła sygnału zapamiętanych sieci (ze skanowania przy starcie albo ze strony).
void noteScan();  // po skanowaniu WiFi.scanNetworks: zapamiętuje RSSI widocznych sieci
int lastRssi(const String& ssid);  // 0 = nie widziano
int averageRssi();                 // średnia z ostatnich 30 s, 0 = brak pomiarów
bool scanned();                    // było już skanowanie (lastRssi 0 znaczy wtedy „poza zasięgiem”)

}  // namespace wifinet
