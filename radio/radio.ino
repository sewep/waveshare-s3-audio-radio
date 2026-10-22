// Radio internetowe na Waveshare ESP32-S3-AUDIO-Board.
// Sterowanie przez przeglądarkę: http://radio.local
//
// Przyciski (jak w słuchawkach Bluetooth):
//   Key1  krótko = ciszej,  przytrzymanie = poprzednia stacja
//   Key2  krótko = graj/stop, przytrzymanie = wygaś/przywróć diody na czas grania
//   Key3  krótko = głośniej, przytrzymanie = następna stacja
//   BOOT  krótko = graj/stop, przytrzymanie 3 s = tryb konfiguracji Wi-Fi (zapisane sieci zostają),
//         w trybie konfiguracji przytrzymanie 3 s = powrót bez zmian
// Przy starcie i zmianie stacji radio wypowiada jej numer (nagrania 1..40 wbudowane w firmware,
// przy starcie zapisywane na partycję FFat; stacje 41..99 grają bez zapowiedzi). Od wybrania stacji do pierwszych dźwięków
// diody pokazują niebieską kometę.
// Bez zapisanej sieci (albo gdy połączenie się nie uda) radio wystawia hotspot "Radio-XXXX",
// a konfiguracja Wi-Fi jest pod http://192.168.4.1. Sieć można też zmienić na stronie (zakładka Wi-Fi).
// Radio pamięta dwie sieci: ostatnio użytą i poprzednią, więc literówka w haśle nowej sieci
// nie odcina go od internetu.

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <FFat.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <vector>

#include "Audio.h"
#include "board.h"
#include "leds.h"
#include "station_numbers.h"
#include "web_page.h"

static const char* HOSTNAME = "radio";
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static const uint32_t RECONNECT_DELAY_MS = 5000;
static const uint32_t ANNOUNCE_STALL_MS = 1500;
static const uint32_t LOADING_SLOW_MS = 8000;      // dłuższe ładowanie = kłopot (pomarańczowa kometa)
static const uint32_t SLOW_STREAM_SHOW_MS = 3000;  // „slow stream” przychodzi co 2 s, póki bufor pusty
static const uint32_t WIFI_SETUP_PRESS_MS = 3000;
static const uint32_t SETUP_IDLE_RESTART_MS = 5 * 60 * 1000;  // hotspot bez gości = ponowna próba Wi-Fi
static const uint32_t RESTART_DELAY_MS = 1000;  // czas na wysłanie odpowiedzi przed restartem
static const uint32_t KEY_POLL_MS = 20;
static const uint32_t BATTERY_POLL_MS = 10000;
static const size_t MAX_STATIONS = 99;  // zapowiedź numeru tylko do STATION_NUMBER_COUNT
static const char* STATIONS_FILE = "/stations.tsv";
static const char* STATIONS_TMP_FILE = "/stations.tmp";

struct Station {
  String name;
  String url;
};

#define BBC_HLS \
  "http://a.files.bbci.co.uk/ms6/live/3441A116-B12E-4D2F-ACA8-C1984642FA4B/audio/simulcast/hls/nonuk/pc_hd_abr_v2/ak/"

// Domyślna lista; sprawdzone, że strumienie odpowiadają (2026-10-21).
static const Station DEFAULT_STATIONS[] = {
    {"RMF FM", "https://rs102-krk.rmfstream.pl/rmf_fm"},
    {"Radio ZET", "http://zet-net-01.cdn.eurozet.pl:8400/"},
    {"Radio Nowy Świat", "https://go-audio.toya.net.pl/63214"},
    {"Antyradio", "https://an.cdn.eurozet.pl/ant-web.mp3"},
    {"Chillizet", "https://ch.cdn.eurozet.pl/chi-net.mp3"},
    {"Radio Złote Przeboje", "https://radiostream.pl/tuba8904-1.mp3"},
    {"ESKA ROCK", "https://ic2.smcdn.pl/5380-1.mp3"},
    {"VOX FM", "https://ic2.smcdn.pl/3990-1.mp3"},
    {"RMF MAXX", "https://rs9-krk2-cyfronet.rmfstream.pl/RMFMAXXX48"},
    // Elektronika i trening (sprawdzone 2026-10-22).
    {"RMF Dance", "https://rs102-krk.rmfstream.pl/rmf_dance"},
    {"RMF Fitness", "https://rs102-krk.rmfstream.pl/rmf_fitness"},
    {"ZET Dance", "http://pl01.cdn.eurozet.pl/ZETDAN.mp3"},
    {"Sunshine Live", "http://stream.sunshine-live.de/live/mp3-192/stream.sunshine-live.de/"},
    {"Sunshine Live Workout", "http://stream.sunshine-live.de/workout/mp3-192/stream.sunshine-live.de/"},
    {"Technolovers EDM", "https://stream.technolovers.fm/edm"},
    {"I Love 2 Dance", "https://streams.ilovemusic.de/iloveradio2.mp3"},
    {"Dance Wave!", "https://dancewave.online/dance.mp3"},
    {"RauteMusik Workout", "https://workout-high.rautemusik.fm/"},
    {"Antenne Bayern Workout", "http://mp3channels.webradio.antenne.de/workout-hits"},
    {"FFH Workout", "http://streams.ffh.de/ffhchannels/mp3/hqworkout.mp3"},
    {"Fitness Radio", "https://stream.mixadance.fm/fitness"},
    {"Intense Radio", "https://secure.live-streams.nl/main"},
    {"RauteMusik Club", "https://club-high.rautemusik.fm/"},
    {"RauteMusik House", "https://house-high.rautemusik.fm/"},
    {"1.FM Amsterdam Trance", "http://strm112.1.fm/atr_mobile_mp3"},
    // BBC i rozmowy (sprawdzone na radiu 2026-10-22; .m3u8 to HLS, biblioteka je obsługuje).
    {"BBC World Service", "https://stream.live.vc.bbcmedia.co.uk/bbc_world_service"},
    {"BBC Radio 1", BBC_HLS "bbc_radio_one.m3u8"},
    {"BBC Radio 1 Dance", BBC_HLS "bbc_radio_one_dance.m3u8"},
    {"BBC Radio 2", BBC_HLS "bbc_radio_two.m3u8"},
    {"BBC Radio 4", BBC_HLS "bbc_radio_fourfm.m3u8"},
    {"TOK FM", "https://radiostream.pl/tuba10-1.mp3"},
    {"Radio 357", "https://stream.radio357.pl/"},
    {"Polskie Radio 24", "https://stream15.polskieradio.pl/pr24/pr24.sdp/playlist.m3u8"},
    {"NPR", "https://npr-ice.streamguys1.com/live.mp3"},
    {"LBC News", "http://media-ice.musicradio.com/LBCUKMP3Low"},
    {"Times Radio", "https://timesradio.wireless.radio/stream"},
};

Audio audio;
WebServer server(80);
DNSServer dns;
Preferences prefs;

std::vector<Station> stations;
int currentStation = 0;
bool playing = false;  // stan zamierzony przez użytkownika
uint8_t volume = 8;    // 0..21
bool apMode = false;
// Dlaczego radio jest w trybie konfiguracji (zadeklarowane przed funkcjami ze względu na prototypy Arduino).
enum class SetupReason : uint8_t { NoNetwork, ConnectFailed, Button };
SetupReason setupReason = SetupReason::NoNetwork;
String wifiSsid;           // sieć, z którą radio jest połączone
String hotspotSsid;        // nazwa hotspotu w trybie konfiguracji
String wifiFailedSsid;     // sieć główna, która przy tym starcie nie odpowiedziała (radio wzięło zapasową)
uint32_t restartAtMs = 0;  // zaplanowany restart (0 = brak)
uint32_t lastStreamOkMs = 0;
int battery = -1;

bool announceEnabled = true;  // wypowiadanie numeru stacji
bool fsReady = false;         // partycja FFat zamontowana
bool numbersReady = false;    // nagrania liczebników są na FFat

// Flagi wspólne dla loop() i zadania audio.
volatile bool announcing = false;     // trwa zapowiedź numeru
volatile bool streamPending = false;  // zapowiedź skończona, czas połączyć ze strumieniem
volatile bool loading = false;        // stacja wybrana, ale jeszcze nie gra
volatile bool streamTrouble = false;  // obecne ładowanie idzie z kłopotami
volatile uint32_t slowStreamMs = 0;   // ostatnie „slow stream” z biblioteki (bufor pusty od 2 s)
uint32_t loadingStartMs = 0;
volatile bool audioRunning = false;   // biblioteka coś odtwarza
volatile bool audioBusy = false;      // zadanie audio wykonuje polecenie (np. łączy się ze stacją)
char announceFile[16] = "";           // nazwa pliku bieżącej zapowiedzi, np. "3.mp3"
volatile uint32_t announceStartMs = 0;  // kiedy zadanie audio otworzyło plik zapowiedzi
volatile bool announceReady = false;    // biblioteka zaczęła dekodować zapowiedź
bool announceRetried = false;

// Polecenia dla zadania audio (zadeklarowane przed funkcjami ze względu na prototypy Arduino).
enum class AudioCmd : uint8_t { PlayUrl, PlayFile, Stop, Volume };

struct AudioMsg {
  AudioCmd cmd;
  uint8_t value;
  char arg[400];
};

QueueHandle_t audioQueue;

// Tytuł utworu ustawia zadanie audio, a czyta serwer WWW w loop().
SemaphoreHandle_t titleLock;
String streamTitle;

void setTitle(const char* t) {
  xSemaphoreTake(titleLock, portMAX_DELAY);
  streamTitle = t;
  xSemaphoreGive(titleLock);
}

String title() {
  xSemaphoreTake(titleLock, portMAX_DELAY);
  String t = streamTitle;
  xSemaphoreGive(titleLock);
  return t;
}

// ---------- trwałe ustawienia ----------

// Lista stacji leży w pliku na FFat: w Preferences napis ma limit 4000 bajtów, a to za mało
// na kilkadziesiąt stacji z długimi adresami (np. BBC). Zapis przez plik tymczasowy, żeby
// zanik zasilania w trakcie nie zostawił uciętej listy.
void saveStations() {
  String data;
  for (auto& s : stations) data += s.name + '\t' + s.url + '\n';
  if (fsReady) {
    File f = FFat.open(STATIONS_TMP_FILE, "w");
    bool ok = f && f.print(data) == data.length();
    if (f) f.close();
    if (ok && (!FFat.exists(STATIONS_FILE) || FFat.remove(STATIONS_FILE)) &&
        FFat.rename(STATIONS_TMP_FILE, STATIONS_FILE)) {
      if (prefs.isKey("stations")) prefs.remove("stations");  // lista sprzed przeniesienia na FFat
      return;
    }
    Serial.println("Błąd zapisu listy stacji na FFat");
  }
  if (data.length() < 4000) prefs.putString("stations", data);  // awaryjnie, bez FFat
}

void loadStations() {
  stations.clear();
  String data;
  bool fromFile = false;
  if (fsReady) {
    File f = FFat.open(STATIONS_FILE, "r");
    if (f) {
      data = f.readString();
      f.close();
      fromFile = true;
    }
  }
  if (!fromFile) data = prefs.getString("stations", "");
  int pos = 0;
  while (pos < (int)data.length() && stations.size() < MAX_STATIONS) {
    int tab = data.indexOf('\t', pos);
    int nl = data.indexOf('\n', pos);
    if (tab < 0 || nl < 0 || tab > nl) break;
    stations.push_back({data.substring(pos, tab), data.substring(tab + 1, nl)});
    pos = nl + 1;
  }
  if (stations.empty()) setDefaultStations();
  else if (!fromFile && fsReady) saveStations();  // przeniesienie z Preferences na FFat
}

void setDefaultStations() {
  stations.assign(std::begin(DEFAULT_STATIONS), std::end(DEFAULT_STATIONS));
  saveStations();
}

void saveState() {
  prefs.putInt("station", currentStation);
  prefs.putBool("playing", playing);
  prefs.putUChar("volume", volume);
}

void saveLeds() {
  prefs.putUChar("ledMode", (uint8_t)leds::mode());
  prefs.putUInt("ledColor", leds::color());
  prefs.putUChar("ledBright", leds::brightness());
  prefs.putUChar("ledAuto", leds::autoSeconds());
}

void setLedsDarkWhilePlaying(bool on) {
  leds::setDarkWhilePlaying(on);
  prefs.putBool("ledDark", on);
  Serial.printf("Diody podczas grania: %s\n", on ? "wygaszone" : "włączone");
}

void applyLedMode(leds::Mode m) {
  leds::setMode(m);
  // poziom i widmo liczone tylko, gdy tryb ich potrzebuje
  audio.settings.VU_LEVEL = leds::usesVu(m);
  audio.settings.SPECTRUM = leds::usesSpectrum(m);
}

// ---------- odtwarzanie ----------

// Nagrania liczebników trafiają na FFat tylko wtedy, gdy zmieniła się ich wersja (CRC).
void prepareNumberClips() {
  if (!fsReady) {
    Serial.println("FFat niedostępny – bez zapowiedzi numerów");
    return;
  }
  char crc[12];
  snprintf(crc, sizeof(crc), "%08lx", (unsigned long)STATION_NUMBER_CLIPS_CRC);
  File f = FFat.open("/n/crc", "r");
  bool upToDate = f && f.readString() == crc;
  if (f) f.close();
  if (!upToDate) {
    Serial.println("Zapisuję nagrania numerów stacji na FFat");
    FFat.mkdir("/n");
    for (int i = 0; i < STATION_NUMBER_COUNT; i++) {
      char path[16];
      snprintf(path, sizeof(path), "/n/%d.ogg", i + 1);  // nagrania z wcześniejszej wersji
      if (FFat.exists(path)) FFat.remove(path);
      snprintf(path, sizeof(path), "/n/%d%s", i + 1, STATION_NUMBER_EXT);
      File out = FFat.open(path, "w");
      if (!out || out.write(STATION_NUMBER_CLIPS[i].data, STATION_NUMBER_CLIPS[i].len) != STATION_NUMBER_CLIPS[i].len) {
        Serial.printf("Błąd zapisu %s\n", path);
        return;
      }
      out.close();
    }
    File marker = FFat.open("/n/crc", "w");
    marker.print(crc);
    marker.close();
  }
  numbersReady = true;
}

// ---------- zadanie audio ----------
// Łączenie ze stacją (DNS, SSL) trwa nawet kilka sekund. Robi to osobne zadanie, żeby strona
// i przyciski w loop() nie czekały. loop() wysyła polecenia przez kolejkę (AudioMsg); wszystkie
// wywołania biblioteki audio (poza ustawieniami w setup()) idą z tego zadania.

void sendAudio(AudioCmd cmd, const char* arg, uint8_t value) {
  AudioMsg msg{cmd, value, ""};
  strlcpy(msg.arg, arg, sizeof(msg.arg));
  if (xQueueSend(audioQueue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) Serial.println("Kolejka audio pełna");
}

void runAudioCommand(const AudioMsg& msg) {
  switch (msg.cmd) {
    case AudioCmd::PlayUrl: audio.connecttohost(msg.arg); break;
    case AudioCmd::PlayFile:
      // Bez jawnego stopSong() zapowiedź włączona w trakcie buforowania strumienia bywała
      // zamykana od razu, bez zdarzenia eof.
      audio.stopSong();
      if (audio.connecttoFS(FFat, msg.arg)) {
        announceStartMs = millis();
      } else {  // brak nagrania: od razu strumień
        announcing = false;
        streamPending = true;
      }
      break;
    case AudioCmd::Stop: audio.stopSong(); break;
    case AudioCmd::Volume: audio.setVolume(msg.value); break;
  }
}

void audioTask(void*) {
  static AudioMsg batch[8];
  for (;;) {
    int n = 0;
    while (n < 8 && xQueueReceive(audioQueue, &batch[n], 0) == pdTRUE) n++;
    if (n) {
      audioBusy = true;
      // Przy szybkim przełączaniu stacji liczy się tylko ostatnie graj/stop z paczki.
      int lastPlayback = -1;
      for (int i = 0; i < n; i++)
        if (batch[i].cmd != AudioCmd::Volume) lastPlayback = i;
      for (int i = 0; i < n; i++)
        if (batch[i].cmd == AudioCmd::Volume || i == lastPlayback) runAudioCommand(batch[i]);
      audioBusy = false;
    }
    audio.loop();
    audioRunning = audio.isRunning();
    vTaskDelay(1);
  }
}

// ---------- odtwarzanie ----------

void connectStream() {
  const Station& s = stations[currentStation];
  Serial.printf("Gram: %d. %s (%s)\n", currentStation + 1, s.name.c_str(), s.url.c_str());
  sendAudio(AudioCmd::PlayUrl, s.url.c_str(), 0);
  lastStreamOkMs = millis();
}

void sendAnnounce() {
  announceStartMs = 0;
  announceReady = false;
  char path[20];
  snprintf(path, sizeof(path), "/n/%s", announceFile);
  sendAudio(AudioCmd::PlayFile, path, 0);
}

// announce = false przy ponownym łączeniu po zerwaniu strumienia.
void startStation(int index, bool announce) {
  if (index < 0 || index >= (int)stations.size()) return;
  currentStation = index;
  playing = true;
  loading = true;
  loadingStartMs = millis();
  streamTrouble = false;
  streamPending = false;
  setTitle("");
  announcing = announce && announceEnabled && numbersReady && index < STATION_NUMBER_COUNT;
  if (announcing) {
    snprintf(announceFile, sizeof(announceFile), "%d%s", index + 1, STATION_NUMBER_EXT);
    announceRetried = false;
    sendAnnounce();
  } else {
    connectStream();
  }
  lastStreamOkMs = millis();
  saveState();
}

void play(int index) { startStation(index, true); }

void stop() {
  playing = false;
  loading = announcing = streamPending = false;
  setTitle("");
  sendAudio(AudioCmd::Stop, "", 0);
  saveState();
}

void setVolume(int v) {
  volume = constrain(v, 0, 21);
  sendAudio(AudioCmd::Volume, "", volume);
  saveState();
}

// Wywoływane z zadania audio.
void audioInfo(Audio::msg_t m) {
  if (m.e == Audio::evt_vu) {  // [lewy, prawy, szczyt L, szczyt P]; za częste, żeby logować
    if (m.vec1.size() >= 2) leds::setVuLevel(max(m.vec1[0], m.vec1[1]));
    return;
  }
  if (m.e == Audio::evt_spectrum) {  // co 50 ms, też za często, żeby logować
    leds::setSpectrum(m.vec1);
    return;
  }
  if (announcing && m.e == Audio::evt_info && m.msg && strcmp(m.msg, "stream ready") == 0) announceReady = true;
  // Koniec właśnie tej zapowiedzi (nie poprzedniej, przerwanej szybką zmianą stacji).
  if (m.e == Audio::evt_eof && announcing && m.msg && strstr(m.msg, announceFile)) {
    announcing = false;
    streamPending = true;  // połączenie zleca loop()
  }
  // Pierwsza ramka strumienia zdekodowana = gra; koniec ładowania.
  bool decoding = m.e == Audio::evt_bitrate ||
                  (m.e == Audio::evt_info && m.msg && strncmp(m.msg, "SampleRate", 10) == 0);
  if (decoding && !announcing) loading = false;
  if (m.e == Audio::evt_info && m.msg && strcmp(m.msg, "slow stream") == 0) slowStreamMs = millis();
  // Biblioteka sama łączy się ponownie; do pierwszej ramki to znów ładowanie, tyle że z kłopotem.
  if (m.e == Audio::evt_info && m.msg && strcmp(m.msg, "Stream lost") == 0) {
    loading = true;
    streamTrouble = true;
  }
  if (m.e == Audio::evt_streamtitle && m.msg) setTitle(m.msg);
  Serial.printf("%s: %s\n", m.s ? m.s : "", m.msg ? m.msg : "");
}

// Gdy strumień się urwie, a użytkownik chce słuchać, łączymy ponownie.
void keepStreamAlive() {
  // eof przychodzi, gdy plik jest zdekodowany, a nie odtworzony; połączenie ze stacją czyści
  // bufor wyjściowy, więc czekamy, aż zapowiedź naprawdę wybrzmi.
  if (streamPending && announceStartMs && currentStation < STATION_NUMBER_COUNT &&
      millis() - announceStartMs < STATION_NUMBER_CLIPS[currentStation].ms + 150)
    return;
  if (streamPending) {
    streamPending = false;
    if (playing) connectStream();
    return;
  }
  // Po przerwaniu strumienia w trakcie buforowania biblioteka potrafi zawiesić odczyt pliku
  // zapowiedzi (resztki danych w buforze). Wtedy jedna ponowna próba, a potem od razu stacja.
  if (announcing && !announceReady && announceStartMs && millis() - announceStartMs > ANNOUNCE_STALL_MS) {
    if (!announceRetried) {
      Serial.println("Zapowiedź nie ruszyła, ponawiam");
      announceRetried = true;
      streamTrouble = true;
      sendAnnounce();
    } else {
      Serial.println("Zapowiedź nie ruszyła, pomijam");
      announcing = false;
      connectStream();
    }
    return;
  }
  if (!playing || apMode) return;
  if (audioRunning || audioBusy || uxQueueMessagesWaiting(audioQueue)) {
    lastStreamOkMs = millis();
  } else if (millis() - lastStreamOkMs > RECONNECT_DELAY_MS) {
    Serial.println("Strumień przerwany, łączę ponownie");
    startStation(currentStation, false);
    streamTrouble = true;
  }
}

// ---------- JSON ----------

String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

// ---------- serwer WWW ----------

void sendOk() { server.send(200, "application/json", "{\"ok\":true}"); }

void sendError(const char* msg) {
  server.send(400, "application/json", String("{\"error\":\"") + msg + "\"}");
}

String ledJson() {
  char color[8];
  snprintf(color, sizeof(color), "#%06lx", (unsigned long)leds::color());
  return String(",\"led\":{\"mode\":\"") + leds::modeId(leds::mode()) + "\",\"color\":\"" + color +
         "\",\"brightness\":" + leds::brightness() +
         ",\"dark\":" + (leds::darkWhilePlaying() ? "true" : "false") +
         ",\"auto\":" + leds::autoSeconds() + ",\"now\":\"" + leds::modeId(leds::currentEffect()) + "\"}";
}

void handleStatus() {
  String json = "{\"playing\":" + String(playing ? "true" : "false") +
                ",\"station\":" + currentStation +
                ",\"volume\":" + volume +
                ",\"title\":\"" + jsonEscape(title()) + "\"" +
                ",\"ip\":\"" + WiFi.localIP().toString() + "\"" +
                ",\"rssi\":" + WiFi.RSSI() +
                ",\"ssid\":\"" + jsonEscape(wifiSsid) + "\"" +
                ",\"host\":\"" + HOSTNAME + ".local\"" +
                ",\"battery\":" + battery +
                ",\"loading\":" + (loading ? "true" : "false") +
                ",\"trouble\":" + (streamTroubleNow() ? "true" : "false") +
                ",\"announce\":" + (announceEnabled ? "true" : "false") + ledJson() + "}";
  server.send(200, "application/json", json);
}

void handleLed() {
  if (server.hasArg("mode")) {
    leds::Mode m;
    if (!leds::modeFromId(server.arg("mode"), m)) return sendError("nieznany tryb");
    applyLedMode(m);
  }
  if (server.hasArg("color")) {
    String c = server.arg("color");
    if (c.startsWith("#")) c.remove(0, 1);
    if (c.length() != 6) return sendError("kolor w formacie #rrggbb");
    leds::setColor(strtoul(c.c_str(), nullptr, 16));
  }
  if (server.hasArg("brightness")) leds::setBrightness(constrain(server.arg("brightness").toInt(), 0, 255));
  if (server.hasArg("dark")) setLedsDarkWhilePlaying(server.arg("dark") == "1");
  if (server.hasArg("auto")) leds::setAutoSeconds(constrain(server.arg("auto").toInt(), 5, 120));
  saveLeds();
  sendOk();
}

void handleStations() {
  String json = "[";
  for (size_t i = 0; i < stations.size(); i++) {
    if (i) json += ',';
    json += "{\"name\":\"" + jsonEscape(stations[i].name) + "\",\"url\":\"" +
            jsonEscape(stations[i].url) + "\"}";
  }
  json += ']';
  server.send(200, "application/json", json);
}

void handlePlay() {
  int i = server.hasArg("i") ? server.arg("i").toInt() : currentStation;
  if (i < 0 || i >= (int)stations.size()) return sendError("zly numer stacji");
  sendOk();  // odpowiedź przed connecttohost, które potrafi chwilę trwać
  play(i);
}

void handleAddStation() {
  String name = server.arg("name");
  String url = server.arg("url");
  name.trim();
  url.trim();
  if (name.isEmpty() || !(url.startsWith("http://") || url.startsWith("https://")))
    return sendError("podaj nazwe i adres http/https");
  if (stations.size() >= MAX_STATIONS) return sendError("lista jest pelna");
  name.replace("\t", " ");
  stations.push_back({name, url});
  saveStations();
  sendOk();
}

void handleDeleteStation() {
  int i = server.arg("i").toInt();
  if (i < 0 || i >= (int)stations.size()) return sendError("zly numer stacji");
  bool wasCurrent = (i == currentStation);
  stations.erase(stations.begin() + i);
  if (i < currentStation || currentStation >= (int)stations.size()) currentStation = max(0, currentStation - 1);
  if (wasCurrent && playing) stop();
  saveStations();
  saveState();
  sendOk();
}

// Zastępuje listę domyślną; stacje dodane przez użytkownika przepadają.
void handleResetStations() {
  if (playing) stop();
  setDefaultStations();
  currentStation = 0;
  saveState();
  sendOk();
}

// Skanowanie w tle: blokujące wstrzymałoby stronę, przyciski i diody na ok. 3 s. Strona pyta
// co chwilę, aż zamiast {"scanning":true} dostanie listę sieci.
void handleScan() {
  int n = WiFi.scanComplete();
  if (n < 0) {
    if (n != WIFI_SCAN_RUNNING) WiFi.scanNetworks(true);
    return server.send(200, "application/json", "{\"scanning\":true}");
  }
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ',';
    json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" + WiFi.RSSI(i) +
            ",\"lock\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  json += ']';
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void handleWifiInfo() {
  static const char* REASONS[] = {"none", "failed", "button"};
  String json = String("{\"ap\":") + (apMode ? "true" : "false") + ",\"connected\":\"" +
                jsonEscape(wifiSsid) + "\",\"saved\":[";
  String ssid = prefs.getString("ssid", ""), ssid2 = prefs.getString("ssid2", "");
  if (!ssid.isEmpty()) json += "\"" + jsonEscape(ssid) + "\"";
  if (!ssid2.isEmpty()) json += String(ssid.isEmpty() ? "" : ",") + "\"" + jsonEscape(ssid2) + "\"";
  json += String("],\"failed\":\"") + jsonEscape(wifiFailedSsid) + "\",\"hotspot\":\"" + hotspotSsid +
          "\",\"reason\":\"" + REASONS[(int)setupReason] + "\"}";
  server.send(200, "application/json", json);
}

void handleSaveWifi() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.isEmpty() || ssid.length() > 32) return sendError("podaj nazwe sieci (do 32 znakow)");
  if (pass.isEmpty() && server.arg("keep") == "1") pass = savedWifiPassword(ssid);
  if (!pass.isEmpty() && (pass.length() < 8 || pass.length() > 64)) return sendError("haslo Wi-Fi ma od 8 do 64 znakow");
  rememberWifi(ssid, pass);
  Serial.printf("Nowa sieć Wi-Fi \"%s\", restart\n", ssid.c_str());
  sendOk();
  restartAtMs = millis() + RESTART_DELAY_MS;
}

// Zapomina sieć, z którą radio nie jest teraz połączone.
void handleForgetWifi() {
  String ssid = server.arg("ssid");
  if (ssid == wifiSsid) return sendError("tej sieci radio teraz uzywa");
  if (ssid == prefs.getString("ssid2", "")) {
    prefs.remove("ssid2");
    prefs.remove("pass2");
  } else if (ssid == prefs.getString("ssid", "")) {  // zapasowa zostaje jedyną
    prefs.putString("ssid", prefs.getString("ssid2", ""));
    prefs.putString("pass", prefs.getString("pass2", ""));
    prefs.remove("ssid2");
    prefs.remove("pass2");
  } else {
    return sendError("nie ma takiej sieci");
  }
  sendOk();
}

void sendFavicon() {
  server.sendHeader("Cache-Control", "max-age=604800");  // tydzień; przeglądarka nie pyta co chwilę
  server.send_P(200, "image/svg+xml", FAVICON_SVG);
}

void setupRoutes() {
  server.on("/favicon.svg", HTTP_GET, sendFavicon);
  server.on("/favicon.ico", HTTP_GET, sendFavicon);
  // Ta sama strona w obu trybach; w trybie konfiguracji pokazuje tylko zakładkę Wi-Fi.
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_GET, handleWifiInfo);
  server.on("/api/wifi", HTTP_POST, handleSaveWifi);
  server.on("/api/wifi/forget", HTTP_POST, handleForgetWifi);
  if (apMode) {
    server.on("/api/wifi/cancel", HTTP_POST, [] {  // powrót do zapisanych sieci bez zmian
      sendOk();
      restartAtMs = millis() + RESTART_DELAY_MS;
    });
    // captive portal: każdy adres prowadzi do strony konfiguracji
    server.onNotFound([] {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    });
  } else {
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/stations", HTTP_GET, handleStations);
    server.on("/api/play", HTTP_POST, handlePlay);
    server.on("/api/stop", HTTP_POST, [] { stop(); sendOk(); });
    server.on("/api/volume", HTTP_POST, [] { setVolume(server.arg("v").toInt()); sendOk(); });
    server.on("/api/stations/add", HTTP_POST, handleAddStation);
    server.on("/api/stations/delete", HTTP_POST, handleDeleteStation);
    server.on("/api/stations/reset", HTTP_POST, handleResetStations);
    server.on("/api/led", HTTP_POST, handleLed);
    server.on("/api/announce", HTTP_POST, [] {
      announceEnabled = server.arg("on") == "1";
      prefs.putBool("announce", announceEnabled);
      sendOk();
    });
    server.onNotFound([] { server.send(404, "text/plain", "Nie ma takiej strony"); });
  }
  server.begin();
}

// ---------- Wi-Fi ----------

// Dwie zapamiętane sieci: główna (ssid/pass) i zapasowa (ssid2/pass2). Zapisywana sieć zostaje
// główną, a dotychczasowa główna zapasową.
void rememberWifi(const String& ssid, const String& pass) {
  String oldSsid = prefs.getString("ssid", "");
  if (oldSsid != ssid) {
    if (!oldSsid.isEmpty()) {
      prefs.putString("ssid2", oldSsid);
      prefs.putString("pass2", prefs.getString("pass", ""));
    } else if (prefs.getString("ssid2", "") == ssid) {
      prefs.remove("ssid2");
      prefs.remove("pass2");
    }
  }
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
}

String savedWifiPassword(const String& ssid) {
  if (ssid == prefs.getString("ssid", "")) return prefs.getString("pass", "");
  if (ssid == prefs.getString("ssid2", "")) return prefs.getString("pass2", "");
  return "";
}

bool hasSavedWifi() { return !prefs.getString("ssid", "").isEmpty() || !prefs.getString("ssid2", "").isEmpty(); }

bool tryWifi(const String& ssid, const String& pass) {
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.printf("Łączę z Wi-Fi \"%s\"", ssid.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect();
  return false;
}

// Najpierw sieć główna, potem zapasowa; ta, która zadziała, zostaje główną (przy przenoszeniu
// radia między dwoma miejscami tylko pierwszy start w nowym miejscu czeka na nieudaną próbę).
bool connectWifi() {
  String ssid = prefs.getString("ssid", ""), ssid2 = prefs.getString("ssid2", "");
  if (ssid.isEmpty() && ssid2.isEmpty()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  if (!ssid.isEmpty() && tryWifi(ssid, prefs.getString("pass", ""))) {
    wifiSsid = ssid;
  } else if (!ssid2.isEmpty() && tryWifi(ssid2, prefs.getString("pass2", ""))) {
    wifiSsid = ssid2;
    wifiFailedSsid = ssid;
    rememberWifi(ssid2, prefs.getString("pass2", ""));
  } else {
    return false;
  }
  WiFi.setSleep(false);  // płynniejszy strumień
  WiFi.setAutoReconnect(true);
  return true;
}

void startAccessPoint(SetupReason reason) {
  apMode = true;
  setupReason = reason;
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);  // STA potrzebne do skanowania sieci
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  char apName[20];
  snprintf(apName, sizeof(apName), "Radio-%02X%02X", mac[4], mac[5]);
  hotspotSsid = apName;
  WiFi.softAP(apName);
  dns.start(53, "*", WiFi.softAPIP());
  Serial.printf("Tryb konfiguracji: połącz się z \"%s\" i otwórz http://%s\n", apName,
                WiFi.softAPIP().toString().c_str());
}

// Hotspot po nieudanym łączeniu (np. router wstaje dłużej niż radio po zaniku prądu) albo
// włączony przyciskiem i zapomniany: gdy nikt nie jest z nim połączony przez 5 minut,
// radio uruchamia się ponownie i znów próbuje zapisanych sieci.
void checkSetupIdle() {
  static uint32_t lastCheck = 0, lastGuestMs = 0;
  uint32_t now = millis();
  if (!apMode || now - lastCheck < 1000) return;
  lastCheck = now;
  if (lastGuestMs == 0 || WiFi.softAPgetStationNum() > 0) lastGuestMs = now;
  else if (now - lastGuestMs > SETUP_IDLE_RESTART_MS && hasSavedWifi()) {
    Serial.println("Nikt nie konfiguruje Wi-Fi – ponawiam łączenie z zapisaną siecią");
    ESP.restart();
  }
}

// ---------- przyciski ----------

enum class ButtonEvent { None, Click, LongPress, Repeat };

// Rozpoznaje krótkie kliknięcie (po puszczeniu), przytrzymanie i opcjonalne powtarzanie.
struct Button {
  uint32_t longPressMs;
  uint32_t repeatMs;  // 0 = bez powtarzania
  bool wasPressed = false;
  bool longFired = false;
  uint32_t pressedAt = 0;
  uint32_t lastRepeatAt = 0;

  Button(uint32_t longMs, uint32_t repMs = 0) : longPressMs(longMs), repeatMs(repMs) {}

  ButtonEvent update(bool pressed, uint32_t now) {
    ButtonEvent ev = ButtonEvent::None;
    if (pressed && !wasPressed) {
      pressedAt = now;
      longFired = false;
    } else if (pressed && !longFired && now - pressedAt >= longPressMs) {
      longFired = true;
      lastRepeatAt = now;
      ev = ButtonEvent::LongPress;
    } else if (pressed && longFired && repeatMs && now - lastRepeatAt >= repeatMs) {
      lastRepeatAt = now;
      ev = ButtonEvent::Repeat;
    } else if (!pressed && wasPressed && !longFired && now - pressedAt > 30) {
      ev = ButtonEvent::Click;
    }
    wasPressed = pressed;
    return ev;
  }
};

Button bootButton(WIFI_SETUP_PRESS_MS);
// Układ jak w słuchawkach Bluetooth: zewnętrzne krótko = głośność, długo = stacja.
Button key1(800);
Button key2(1000);
Button key3(800);

void togglePlay() { playing ? stop() : play(currentStation); }

void changeVolume(int delta) {
  setVolume(volume + delta);
  Serial.printf("Głośność: %u\n", volume);
}

void switchStation(int delta) {
  int n = stations.size();
  if (n == 0) return;
  play((currentStation + delta + n) % n);
}

void handleButtons() {
  uint32_t now = millis();

  ButtonEvent boot = bootButton.update(digitalRead(board::BOOT_BUTTON) == LOW, now);
  if (boot == ButtonEvent::LongPress) {
    // Zapisane sieci zostają; flaga mówi następnemu startowi, żeby od razu wystawił hotspot.
    if (apMode) {
      Serial.println("Wychodzę z konfiguracji Wi-Fi bez zmian");
    } else {
      Serial.println("Uruchamiam konfigurację Wi-Fi (zapisane sieci zostają)");
      prefs.putBool("setupOnce", true);
    }
    delay(200);
    ESP.restart();
  }
  if (apMode) return;
  if (boot == ButtonEvent::Click) togglePlay();

  static uint32_t lastPoll = 0;
  if (now - lastPoll < KEY_POLL_MS) return;
  lastPoll = now;
  uint8_t keys = board::readKeys();

  switch (key1.update(keys & board::KEY1, now)) {
    case ButtonEvent::Click: changeVolume(-1); break;
    case ButtonEvent::LongPress: switchStation(-1); break;
    default: break;
  }
  switch (key2.update(keys & board::KEY2, now)) {
    case ButtonEvent::Click: togglePlay(); break;
    case ButtonEvent::LongPress: setLedsDarkWhilePlaying(!leds::darkWhilePlaying()); break;
    default: break;
  }
  switch (key3.update(keys & board::KEY3, now)) {
    case ButtonEvent::Click: changeVolume(+1); break;
    case ButtonEvent::LongPress: switchStation(+1); break;
    default: break;
  }
}

// ---------- diody ----------

bool streamTroubleNow() {
  bool starving = slowStreamMs && millis() - slowStreamMs < SLOW_STREAM_SHOW_MS;
  return playing && ((loading && streamTrouble) || starving);
}

void updateLeds() {
  if (apMode) leds::setState(leds::State::Setup);
  else if (!playing) leds::setState(leds::State::Stopped);
  else {
    if (loading && millis() - loadingStartMs > LOADING_SLOW_MS) streamTrouble = true;
    leds::setState(streamTroubleNow() ? leds::State::Trouble
                   : loading          ? leds::State::Loading
                                      : leds::State::Playing);
  }

  static uint32_t lastBatteryPoll = 0;
  if (board::BATTERY_ADC_ENABLED && (lastBatteryPoll == 0 || millis() - lastBatteryPoll > BATTERY_POLL_MS)) {
    lastBatteryPoll = millis();
    battery = board::batteryPercent();
    leds::setBatteryPercent(battery);
  }
}

// ---------- start ----------

void setup() {
  Serial.begin(115200);
  // Kabel w komputerze, ale nikt nie czyta portu: domyślnie każdy zapis czeka do 2 s, co
  // wstrzymywało zadanie audio (strumień nie ruszał). Bez czekania nadmiar logów przepada.
  Serial.setTxTimeoutMs(0);
  delay(300);
  Serial.println("\n=== Radio internetowe ===");
  pinMode(board::BOOT_BUTTON, INPUT_PULLUP);
  titleLock = xSemaphoreCreateMutex();

  prefs.begin("radio", false);
  fsReady = FFat.begin(true);
  loadStations();
  currentStation = constrain(prefs.getInt("station", 0), 0, (int)stations.size() - 1);
  volume = prefs.getUChar("volume", 8);
  bool resume = prefs.getBool("playing", true);
  bool setupOnce = prefs.getBool("setupOnce", false);  // przytrzymany BOOT przed restartem
  if (setupOnce) prefs.remove("setupOnce");
  announceEnabled = prefs.getBool("announce", true);

  leds::begin(board::LED_PIN, board::LED_COUNT);  // od razu animacja startowa
  leds::setColor(prefs.getUInt("ledColor", 0xFF6A00));
  leds::setBrightness(prefs.getUChar("ledBright", 60));
  leds::setDarkWhilePlaying(prefs.getBool("ledDark", false));
  leds::setAutoSeconds(prefs.getUChar("ledAuto", 15));
  uint8_t ledMode = prefs.getUChar("ledMode", (uint8_t)leds::Mode::Rainbow);
  applyLedMode(ledMode < (uint8_t)leds::Mode::Count ? (leds::Mode)ledMode : leds::Mode::Rainbow);
  leds::setState(leds::State::Connecting);

  Audio::audio_info_callback = audioInfo;
  audio.setPinout(board::I2S_BCLK, board::I2S_LRCK, board::I2S_DOUT, board::I2S_MCLK);
  audio.setOutputSampleRate(Audio::SR_48000);  // kodek skonfigurowany na stałe 48 kHz
  audio.setVolume(volume);
  if (!board::begin()) Serial.println("Błąd inicjalizacji kodeka ES8311 – nie będzie dźwięku");
  prepareNumberClips();

  audioQueue = xQueueCreate(8, sizeof(AudioMsg));
  // Rdzeń 1 i ten sam priorytet co loop(): FreeRTOS dzieli między nie czas, więc liczenie SSL
  // przy łączeniu ze stacją (ok. 0,6 s) nie zagładza strony WWW; duży bufor audio kryje przerwy.
  // Duży stos: uzgadnianie SSL dzieje się w tym zadaniu.
  xTaskCreatePinnedToCore(audioTask, "audio", 16384, nullptr, 1, nullptr, 1);

  if (!setupOnce && connectWifi()) {
    Serial.printf("Połączono, IP: %s\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin(HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("Strona: http://%s.local\n", HOSTNAME);
    }
    setupRoutes();
    if (resume) play(currentStation);
  } else {
    startAccessPoint(setupOnce        ? SetupReason::Button
                     : hasSavedWifi() ? SetupReason::ConnectFailed
                                      : SetupReason::NoNetwork);
    setupRoutes();
  }
}

void loop() {
  server.handleClient();
  if (apMode) dns.processNextRequest();
  checkSetupIdle();
  if (restartAtMs && (int32_t)(millis() - restartAtMs) >= 0) ESP.restart();
  handleButtons();
  keepStreamAlive();
  updateLeds();
  vTaskDelay(1);
}
