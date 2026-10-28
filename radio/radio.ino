// Radio internetowe na Waveshare ESP32-S3-AUDIO-Board.
// Sterowanie przez przeglądarkę: http://radio.local
//
// Przyciski (jak w słuchawkach Bluetooth):
//   Key1  krótko = ciszej,  przytrzymanie = poprzednia stacja
//   Key2  krótko = graj/stop, dwa razy szybko = następna ulubiona stacja,
//         przytrzymanie = wygaś/przywróć diody na czas grania
//   Key3  krótko = głośniej, przytrzymanie = następna stacja
//   Key1 + Key3 razem = wyłącznik czasowy 30 → 60 → 90 min → wyłączony
//   BOOT  krótko = następny tryb diod, przytrzymanie 3 s = tryb konfiguracji Wi-Fi (zapisane sieci zostają),
//         w trybie konfiguracji przytrzymanie 3 s = powrót bez zmian
// Przy starcie i zmianie stacji radio wypowiada jej numer (nagrania 1..40 wbudowane w firmware,
// przy starcie zapisywane na partycję FFat; stacje 41..99 grają bez zapowiedzi). Od wybrania stacji do pierwszych dźwięków
// diody pokazują niebieską kometę.
// Bez zapisanej sieci (albo gdy połączenie się nie uda) radio wystawia hotspot "Radio-XXXX",
// a konfiguracja Wi-Fi jest pod http://192.168.4.1. Sieć można też zmienić na stronie (zakładka Wi-Fi).
// Radio pamięta dwie sieci: ostatnio użytą i poprzednią, więc literówka w haśle nowej sieci
// nie odcina go od internetu. Każda z nich może mieć stały adres IP zamiast DHCP.
// Budzik (czas z NTP): świt na diodach przed czasem, stacja z narastającą głośnością, a gdy
// stacja nie zagra w 30 s, pikanie z pliku. Wyłącznik czasowy wycisza muzykę przez ostatnią minutę.
// Dwa klaśnięcia włączają i zatrzymują radio (mics.cpp; muzyka z głośnika podnosi próg).
// Zakładka Radio pokazuje też barwę dźwięku (filtry biblioteki) i ostatnio grane tytuły (w RAM).
// Tryb Halloween (zakładka z dynią): gdy mikrofony usłyszą głos ponad progiem, radio gra plik
// z FFat (pobrany z adresu albo wgrany ze strony), a diody świecą w rytm dźwięku jak oczy maski.
// Zakładka Stan pokazuje dane strumienia, Wi-Fi i systemu oraz poziomy dźwięku: wyjście na
// głośnik (po regulacji głośności) i oba mikrofony.

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <FFat.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <time.h>
#include <vector>

#include "Audio.h"
#include "board.h"
#include "leds.h"
#include "mics.h"
#include "scare.h"
#include "station_numbers.h"
#include "version.h"
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
static const uint32_t STREAM_STATS_MS = 1000;
static const uint32_t LEVELS_AWAKE_MS = 5000;  // pomiar wyjścia trwa, póki strona pyta o poziomy
static const uint32_t LEVELS_STALE_MS = 500;
static const uint32_t KEY_POLL_MS = 20;
static const uint32_t BATTERY_POLL_MS = 10000;
static const size_t MAX_STATIONS = 99;  // zapowiedź numeru tylko do STATION_NUMBER_COUNT
static const char* STATIONS_FILE = "/stations.tsv";
static const char* STATIONS_TMP_FILE = "/stations.tmp";
static const char* TIME_ZONE = "CET-1CEST,M3.5.0,M10.5.0/3";  // Polska, z czasem letnim
static const uint32_t DOUBLE_CLICK_MS = 350;
static const uint32_t SLEEP_FADE_MS = 60000;      // ostatnia minuta wyłącznika: muzyka cichnie
static const uint32_t ALARM_FALLBACK_MS = 30000;  // budzik bez muzyki po 30 s = sygnał z pliku
static const uint32_t ALARM_MAX_MS = 60 * 60 * 1000UL;  // nietknięty budzik gra najwyżej godzinę
static const uint32_t SUNRISE_HOLD_MS = 30 * 60 * 1000UL;
static const uint32_t BEEP_REPEAT_MS = 2200;
static const char* BEEP_FILE = "/beep2.wav";  // nowa nazwa = nowy plik po zmianie dźwięku
static const size_t HISTORY_SIZE = 20;
static const uint32_t CLAP_SUPPRESS_MS = 1500;  // trzask głośnika przy starcie / zatrzymaniu
static const uint32_t SCARE_HOLD_MS = 200;       // głos musi trwać, żeby trzask czy stuk nie straszył
static const uint32_t SCARE_TAIL_MS = 1500;      // po eof dźwięk jeszcze leci z bufora
static const uint32_t SCARE_MAX_MS = 120000;

struct Station {
  String name;
  String url;
  bool fav = false;  // ulubiona: przełącza ją podwójne kliknięcie Key2
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
bool wifiStatic = false;   // bieżąca sieć ma stały adres IP
uint32_t restartAtMs = 0;  // zaplanowany restart (0 = brak)

// Zapamiętana sieć: 0 = główna (klucze ssid, pass, ipcfg), 1 = zapasowa (ssid2, pass2, ipcfg2).
// ipcfg = "ip maska brama dns" albo pusty napis (DHCP).
struct WifiSlot {
  String ssid, pass, ipcfg;
};

struct IpConfig {
  IPAddress ip, mask, gw, dns;
};
uint32_t lastStreamOkMs = 0;
int battery = -1;

int8_t tone3[3] = {0, 0, 0};  // barwa dźwięku: bas, środek, wysokie, -12..+12 dB

// Budzik. days: bit = dzień tygodnia (0 = niedziela, jak tm_wday); 0 = jednorazowo.
struct AlarmConfig {
  bool on = false;
  uint8_t hour = 7, minute = 0, days = 0x3E;  // pon–pt
  int station = -1;                           // -1 = ostatnio słuchana
  uint8_t volume = 8;
  uint8_t rampMin = 5;      // narastanie głośności
  uint8_t sunriseMin = 10;  // świt na diodach przed budzikiem; 0 = bez
};
AlarmConfig alarmCfg;
// Budzik, który właśnie dzwoni: narastanie głośności, świt i sygnał zastępczy.
struct AlarmRun {
  bool ramping = false;  // głośność rośnie
  bool active = false;   // budzik gra i nikt go jeszcze nie dotknął
  bool beeping = false;  // stacja nie ruszyła: sygnał z pliku
  bool sunrise = false;  // świt trwa (przed budzikiem i po nim)
  uint32_t startMs = 0, beepMs = 0, sunriseUntilMs = 0;
  uint8_t fromVol = 1;
  int32_t firedKey = -1;  // minuta, w której budzik już zadzwonił
};
AlarmRun alarmRun;

uint32_t sleepEndMs = 0;     // wyłącznik czasowy; 0 = brak
uint8_t sleepFadeVol = 255;  // ostatnia głośność wysłana przy wyciszaniu

bool clapEnabled = false;
uint8_t clapSensitivity = 2;

// Halloween: głos przy masce = krzyk z pliku i oczy w rytm dźwięku.
struct ScareConfig {
  bool on = false;
  String url;
  int8_t thresholdDb = -45;  // średni poziom mikrofonu (100 ms), od którego głos straszy
  uint8_t volume = 18;
  uint32_t color = 0xFF0000;
  uint8_t cooldownSec = 10;
  bool glow = true;  // oczy żarzą się w ciszy
  bool restPulse = true;  // oczy pulsują w odpoczynku: widać, kiedy straszak znów nasłuchuje
};
ScareConfig scareCfg;
enum class ScareState : uint8_t { Off, Armed, Screaming, Cooldown, Paused, NoFile };
ScareState scareState = ScareState::Off;
volatile bool scareEof = false;  // zadanie audio: plik zdekodowany do końca
uint32_t scareStartMs = 0, scareEofMs = 0, scareCoolUntilMs = 0, scareAboveSinceMs = 0;
uint32_t scareCount = 0;
volatile uint8_t scareEyePeak = 0;  // najjaśniejszy błysk oczu w ostatnim krzyku, 0..255

// Ostatnio grane: tytuły z metadanych strumienia, tylko w RAM. Pisze zadanie audio, czyta strona.
struct HistoryItem {
  time_t at;  // 0 = czas nieznany
  String station, title;
};
std::vector<HistoryItem> history;  // najstarszy pierwszy; chroni titleLock

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
enum class AudioCmd : uint8_t { PlayUrl, PlayFile, Stop, Volume, Tone };

struct AudioMsg {
  AudioCmd cmd;
  uint8_t value;
  char arg[400];
};

QueueHandle_t audioQueue;

// Tytuł utworu i nagłówki ICY ustawia zadanie audio, a czyta serwer WWW w loop().
SemaphoreHandle_t titleLock;
String streamTitle;
String icyName, icyDescription, icyGenre, icyUrl, streamHost;
String playingName;          // nazwa wybranej stacji dla historii tytułów
bool uploadResult = false;   // wynik ostatniego wgrania pliku Halloween
uint32_t historySeq = 0;     // rośnie z każdym nowym tytułem; strona wie, kiedy pobrać listę

// Liczby o strumieniu do zakładki Stan; zadanie audio odświeża je co sekundę (tylko ono
// woła bibliotekę). Rozerwany odczyt w serwerze WWW niczego nie psuje.
struct StreamStats {
  const char* codec = "";
  uint32_t bitrate = 0, sampleRate = 0, bufFilled = 0, bufSize = 0;
  uint8_t channels = 0, bits = 0;
};
StreamStats streamStats;
volatile uint32_t playStartMs = 0;    // pierwsza zdekodowana ramka bieżącej stacji
volatile uint16_t streamLosses = 0;   // zerwania strumienia od wybrania stacji

// Poziom na wyjściu (po regulacji głośności), liczony w audio_process_i2s z zadania audio.
volatile uint32_t levelsWantedMs = 0;
volatile uint32_t outUpdatedMs = 0;
volatile float outRmsDb = mics::SILENCE_DB, outPeakDb = mics::SILENCE_DB;

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

bool timeValid() { return time(nullptr) > 1700000000; }  // NTP już odpowiedział

// Zapisuje tytuł w historii; pomija puste, powtórzone i równe nazwie stacji. Z zadania audio.
void addHistory(const char* t) {
  String title = t;
  title.trim();
  if (title.isEmpty()) return;
  xSemaphoreTake(titleLock, portMAX_DELAY);
  String name = playingName;
  if (!title.equalsIgnoreCase(name) && (history.empty() || history.back().title != title)) {
    if (history.size() >= HISTORY_SIZE) history.erase(history.begin());
    history.push_back({timeValid() ? time(nullptr) : 0, name, title});
    historySeq++;
  }
  xSemaphoreGive(titleLock);
}

void setStreamInfo(String& field, const char* value) {
  xSemaphoreTake(titleLock, portMAX_DELAY);
  field = value;
  xSemaphoreGive(titleLock);
}

String streamInfo(const String& field) {
  xSemaphoreTake(titleLock, portMAX_DELAY);
  String v = field;
  xSemaphoreGive(titleLock);
  return v;
}

// ---------- trwałe ustawienia ----------

// Lista stacji leży w pliku na FFat: w Preferences napis ma limit 4000 bajtów, a to za mało
// na kilkadziesiąt stacji z długimi adresami (np. BBC). Zapis przez plik tymczasowy, żeby
// zanik zasilania w trakcie nie zostawił uciętej listy.
void saveStations() {
  String data;
  for (auto& s : stations) data += s.name + '\t' + s.url + (s.fav ? "\t*" : "") + '\n';
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
    Station st{data.substring(pos, tab), data.substring(tab + 1, nl)};
    int tab2 = st.url.indexOf('\t');  // trzecia kolumna "*" = ulubiona
    if (tab2 >= 0) {
      st.fav = st.url.substring(tab2 + 1) == "*";
      st.url.remove(tab2);
    }
    stations.push_back(st);
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
  if (cmd == AudioCmd::PlayUrl || cmd == AudioCmd::PlayFile || cmd == AudioCmd::Stop) mics::suppress(CLAP_SUPPRESS_MS);
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
    case AudioCmd::Tone: audio.setTone((int8_t)msg.arg[0], (int8_t)msg.arg[1], (int8_t)msg.arg[2]); break;
  }
}

void updateStreamStats() {
  StreamStats s;
  if (audioRunning) {
    s.codec = audio.getCodecname();
    s.bitrate = audio.getBitRate();
    s.sampleRate = audio.getSampleRate();
    s.channels = audio.getChannels();
    s.bits = audio.getBitsPerSample();
  }
  s.bufFilled = audio.inBufferFilled();
  s.bufSize = s.bufFilled + audio.inBufferFree();
  streamStats = s;
}

bool levelsWanted() { return levelsWantedMs && millis() - levelsWantedMs < LEVELS_AWAKE_MS; }

// Wołane przez bibliotekę audio (zadanie audio) dla każdej porcji próbek tuż przed I2S, czyli
// po regulacji głośności: to, co dostaje kodek głośnika.
void audio_process_i2s(int32_t* buff, int16_t validSamples, bool* continueI2S) {
  static double sum = 0;
  static float peak = 0;
  static int count = 0;
  mics::feedOutput(buff, validSamples);
  if (scareState == ScareState::Screaming) {
    float pk = 0;
    for (int i = 0; i < validSamples; i++) pk = max(pk, fabsf(buff[i] / 2147483648.0f));
    // Próbki są już ściszone krzywą biblioteki (dB = −112t³ + 172t² − 60, t = głośność/21); bez
    // jej odjęcia oczy przy cichym testowaniu (np. głośność 5 = −52 dB) w ogóle się nie zapalały.
    float t = scareCfg.volume / 21.0f;
    float db = pk > 1e-6f ? 20 * log10f(pk) - max(-60.0f, -112 * t * t * t + 172 * t * t - 60) : -120;
    uint8_t eye = constrain((db + 32) / 28, 0.0f, 1.0f) * 255;  // −32..−4 dBFS pliku
    leds::setScareLevel(eye);
    if (eye > scareEyePeak) scareEyePeak = eye;
  }
  if (!levelsWanted()) return;
  for (int i = 0; i < validSamples; i++) {
    float s = buff[i] / 2147483648.0f;
    sum += s * s;
    peak = max(peak, fabsf(s));
  }
  count += validSamples;
  if (count >= 48000 / 10 * 2) {  // 100 ms stereo
    float rms = sqrt(sum / count);
    outRmsDb = rms > 1e-6f ? 20 * log10f(rms) : mics::SILENCE_DB;
    outPeakDb = peak > 1e-6f ? 20 * log10f(peak) : mics::SILENCE_DB;
    outUpdatedMs = millis();
    sum = peak = count = 0;
  }
}

// Najdłuższy czas z ostatnich 5–10 s (w µs), do diagnozy zacinania w zakładce Stan.
struct MaxWindow {
  volatile uint32_t cur = 0, prev = 0, startMs = 0;
  void add(uint32_t us) {
    if (millis() - startMs > 5000) {
      prev = cur;
      cur = 0;
      startMs = millis();
    }
    if (us > cur) cur = us;
  }
  uint32_t value() const { return max(cur, prev); }
};
MaxWindow audioGap, audioLoopTime, mainLoopTime, webTime;
// Bufor DMA biblioteki to 16 × 256 ramek = ok. 85 ms; dłuższa przerwa zadania audio = słyszalna dziura.
static const uint32_t AUDIO_STALL_US = 40000;
volatile uint16_t audioStalls = 0;      // przerwy zadania audio > 40 ms od startu
volatile uint16_t slowStreamCount = 0;  // „slow stream” z biblioteki od startu (sieć nie nadąża)

void audioTask(void*) {
  static AudioMsg batch[8];
  uint32_t lastStats = 0, lastLoopUs = 0;
  bool wasSteady = false;  // poprzedni obieg był zwykłym graniem
  for (;;) {
    int n = 0;
    while (n < 8 && xQueueReceive(audioQueue, &batch[n], 0) == pdTRUE) n++;
    if (n) {
      audioBusy = true;
      // Przy szybkim przełączaniu stacji liczy się tylko ostatnie graj/stop z paczki.
      auto playback = [](AudioCmd c) { return c == AudioCmd::PlayUrl || c == AudioCmd::PlayFile || c == AudioCmd::Stop; };
      int lastPlayback = -1;
      for (int i = 0; i < n; i++)
        if (playback(batch[i].cmd)) lastPlayback = i;
      for (int i = 0; i < n; i++)
        if (!playback(batch[i].cmd) || i == lastPlayback) runAudioCommand(batch[i]);
      audioBusy = false;
    }
    uint32_t t0 = micros();
    // Łączenie ze stacją (polecenie, ładowanie, zapowiedź) blokuje z natury na 0,4–1 s (DNS, SSL),
    // ale wtedy stara stacja już nie gra; przestój liczy się tylko między dwoma obiegami zwykłego grania.
    bool steady = audioRunning && !n && !loading && !announcing;
    if (lastLoopUs) {
      uint32_t gap = t0 - lastLoopUs;
      audioGap.add(gap);
      if (gap > AUDIO_STALL_US && steady && wasSteady) {
        audioStalls++;
        Serial.printf("Zadanie audio stało %lu ms\n", (unsigned long)(gap / 1000));
      }
    }
    wasSteady = steady;
    lastLoopUs = t0;
    audio.loop();
    audioLoopTime.add(micros() - t0);
    audioRunning = audio.isRunning();
    if (millis() - lastStats >= STREAM_STATS_MS) {
      lastStats = millis();
      updateStreamStats();
    }
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
  setStreamInfo(playingName, stations[index].name.c_str());
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

void play(int index) {
  alarmDismiss();
  for (String* f : {&icyName, &icyDescription, &icyGenre, &icyUrl, &streamHost}) setStreamInfo(*f, "");
  streamLosses = 0;
  playStartMs = 0;
  startStation(index, true);
}

void stop() {
  alarmDismiss();
  sleepEndMs = 0;
  playing = false;
  loading = announcing = streamPending = false;
  setTitle("");
  sendAudio(AudioCmd::Stop, "", 0);
  saveState();
}

void setVolume(int v) {
  alarmDismiss();
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
  if (m.e == Audio::evt_eof && m.msg && strstr(m.msg, scare::FILE_PATH + 1)) scareEof = true;
  if (m.e == Audio::evt_eof && announcing && m.msg && strstr(m.msg, announceFile)) {
    announcing = false;
    streamPending = true;  // połączenie zleca loop()
  }
  // Pierwsza ramka strumienia zdekodowana = gra; koniec ładowania.
  bool decoding = m.e == Audio::evt_bitrate ||
                  (m.e == Audio::evt_info && m.msg && strncmp(m.msg, "SampleRate", 10) == 0);
  if (decoding && !announcing) {
    if (loading) mics::suppress(CLAP_SUPPRESS_MS);  // początek dźwięku po ładowaniu
    loading = false;
    if (!playStartMs) playStartMs = millis();
  }
  if (m.e == Audio::evt_info && m.msg && strcmp(m.msg, "slow stream") == 0) {
    slowStreamMs = millis();
    slowStreamCount++;
  }
  // Biblioteka sama łączy się ponownie; do pierwszej ramki to znów ładowanie, tyle że z kłopotem.
  if (m.e == Audio::evt_info && m.msg && strcmp(m.msg, "Stream lost") == 0) {
    loading = true;
    streamTrouble = true;
    streamLosses++;
  }
  if (m.e == Audio::evt_streamtitle && m.msg) {
    setTitle(m.msg);
    if (!announcing) addHistory(m.msg);
  }
  if (m.msg && !announcing) {
    if (m.e == Audio::evt_name) setStreamInfo(icyName, m.msg);
    if (m.e == Audio::evt_icydescription) setStreamInfo(icyDescription, m.msg);
    if (m.e == Audio::evt_genre) setStreamInfo(icyGenre, m.msg);
    if (m.e == Audio::evt_icyurl) setStreamInfo(icyUrl, m.msg);
    if (m.e == Audio::evt_lasthost) setStreamInfo(streamHost, m.msg);  // adres po przekierowaniach
  }
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

// ---------- barwa dźwięku ----------

void applyTone() {
  AudioMsg msg{AudioCmd::Tone, 0, ""};
  for (int i = 0; i < 3; i++) msg.arg[i] = tone3[i];  // sendAudio kopiuje napis, a 0 dB to bajt 0
  if (xQueueSend(audioQueue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) Serial.println("Kolejka audio pełna");
}

// ---------- budzik ----------
// Świt zaczyna się sunriseMin minut przed budzikiem. O czasie radio włącza stację z głośnością
// rosnącą od 1 do ustawionej przez rampMin minut. Gdy po 30 s nie gra (brak internetu, martwa
// stacja), zamiast niej pika sygnał z pliku. Każde dotknięcie radia (przycisk, strona,
// klaśnięcie) kończy narastanie i świt; nietknięty budzik gra najwyżej godzinę.

void saveAlarm() {
  prefs.putBool("alOn", alarmCfg.on);
  prefs.putUChar("alH", alarmCfg.hour);
  prefs.putUChar("alM", alarmCfg.minute);
  prefs.putUChar("alDays", alarmCfg.days);
  prefs.putInt("alSt", alarmCfg.station);
  prefs.putUChar("alVol", alarmCfg.volume);
  prefs.putUChar("alRamp", alarmCfg.rampMin);
  prefs.putUChar("alSun", alarmCfg.sunriseMin);
}

void loadAlarm() {
  AlarmConfig d;
  alarmCfg.on = prefs.getBool("alOn", d.on);
  alarmCfg.hour = min<uint8_t>(prefs.getUChar("alH", d.hour), 23);
  alarmCfg.minute = min<uint8_t>(prefs.getUChar("alM", d.minute), 59);
  alarmCfg.days = prefs.getUChar("alDays", d.days) & 0x7F;
  alarmCfg.station = prefs.getInt("alSt", d.station);
  alarmCfg.volume = constrain(prefs.getUChar("alVol", d.volume), 1, 21);
  alarmCfg.rampMin = min<uint8_t>(prefs.getUChar("alRamp", d.rampMin), 30);
  alarmCfg.sunriseMin = min<uint8_t>(prefs.getUChar("alSun", d.sunriseMin), 30);
}

// Użytkownik coś zrobił: budzik przestaje sam podkręcać głośność, sygnał milknie, świt gaśnie.
void alarmDismiss() {
  if (alarmRun.beeping) sendAudio(AudioCmd::Stop, "", 0);
  alarmRun.ramping = alarmRun.active = alarmRun.beeping = false;
  alarmRun.sunrise = false;
  alarmRun.sunriseUntilMs = 0;
  leds::setSunrise(-1);
}

bool alarmRinging() { return alarmRun.active || alarmRun.beeping; }

// Sekundy do najbliższego budzika albo -1 (wyłączony, czas nieznany).
long secondsToAlarm() {
  if (!alarmCfg.on || !timeValid()) return -1;
  time_t now = time(nullptr);
  tm today;
  localtime_r(&now, &today);
  for (int d = 0; d <= 7; d++) {
    tm c = today;
    c.tm_mday += d;
    c.tm_hour = alarmCfg.hour;
    c.tm_min = alarmCfg.minute;
    c.tm_sec = 0;
    c.tm_isdst = -1;
    time_t t = mktime(&c);  // poprawia datę i ustawia tm_wday
    if (t <= now) continue;
    if (alarmCfg.days && !(alarmCfg.days & (1 << c.tm_wday))) continue;
    return t - now;
  }
  return -1;
}

void alarmFire() {
  Serial.printf("Budzik %02u:%02u\n", alarmCfg.hour, alarmCfg.minute);
  if (!alarmCfg.days) {  // jednorazowy
    alarmCfg.on = false;
    saveAlarm();
  }
  bool sunrise = alarmCfg.sunriseMin > 0;
  if (!playing && !stations.empty()) {
    int st = alarmCfg.station >= 0 && alarmCfg.station < (int)stations.size() ? alarmCfg.station : currentStation;
    play(st);  // kończy poprzedni budzik, więc stan ustawiamy dopiero potem
    volume = alarmCfg.rampMin ? 1 : alarmCfg.volume;
    sendAudio(AudioCmd::Volume, "", volume);
    alarmRun.active = true;
    alarmRun.ramping = alarmCfg.rampMin > 0;
    alarmRun.startMs = millis();
    alarmRun.fromVol = volume;
  }
  if (sunrise) {
    alarmRun.sunrise = true;
    alarmRun.sunriseUntilMs = millis() + SUNRISE_HOLD_MS;
  }
}

void startBeeping() {
  Serial.println("Budzik: stacja nie gra, włączam sygnał");
  playing = false;  // bez ponownego łączenia ze stacją
  loading = announcing = streamPending = false;
  alarmRun.beeping = true;
  alarmRun.beepMs = 0;
  if (volume < alarmCfg.volume) {
    volume = alarmCfg.volume;
    sendAudio(AudioCmd::Volume, "", volume);
  }
  alarmRun.ramping = false;
}

// Co sekundę: czy pora budzić, świt, narastanie głośności, sygnał zastępczy.
void tickAlarm() {
  static uint32_t lastTick = 0;
  static bool inSunriseWindow = false;
  uint32_t now = millis();

  if (alarmRun.beeping && (alarmRun.beepMs == 0 || now - alarmRun.beepMs >= BEEP_REPEAT_MS)) {
    alarmRun.beepMs = now;
    sendAudio(AudioCmd::PlayFile, BEEP_FILE, 0);
  }
  if (now - lastTick < 1000) return;
  lastTick = now;

  if (alarmRun.active || alarmRun.beeping) {
    uint32_t ran = now - alarmRun.startMs;
    if (ran > ALARM_MAX_MS || (alarmRun.beeping && ran > ALARM_MAX_MS / 6)) {
      Serial.println("Budzik: nikt nie wyłączył, koniec");
      stop();
      return;
    }
    if (alarmRun.active && !alarmRun.beeping && playing && !playStartMs && ran > ALARM_FALLBACK_MS) startBeeping();
  }
  if (alarmRun.ramping) {
    uint32_t dur = alarmCfg.rampMin * 60000UL, ran = now - alarmRun.startMs;
    uint8_t v = ran >= dur ? alarmCfg.volume : alarmRun.fromVol + (alarmCfg.volume - alarmRun.fromVol) * ran / dur;
    if (ran >= dur) alarmRun.ramping = false;
    if (v != volume) {
      volume = v;
      sendAudio(AudioCmd::Volume, "", volume);
      if (!alarmRun.ramping) saveState();
    }
  }

  if (timeValid()) {
    time_t t = time(nullptr);
    tm lt;
    localtime_r(&t, &lt);
    int32_t key = ((lt.tm_year * 366 + lt.tm_yday) * 24 + lt.tm_hour) * 60 + lt.tm_min;
    bool today = !alarmCfg.days || (alarmCfg.days & (1 << lt.tm_wday));
    if (alarmCfg.on && today && lt.tm_hour == alarmCfg.hour && lt.tm_min == alarmCfg.minute && key != alarmRun.firedKey) {
      alarmRun.firedKey = key;
      alarmFire();
    }
    // Świt przed budzikiem: na wejściu w okno włącza się od nowa (nawet gdy poprzedni
    // ktoś zgasił), a zgaszony w oknie już nie wraca.
    long left = alarmCfg.sunriseMin && !apMode ? secondsToAlarm() : -1;
    bool window = left > 0 && left <= alarmCfg.sunriseMin * 60L;
    if (window && !inSunriseWindow) alarmRun.sunrise = true;
    inSunriseWindow = window;
    if (window && alarmRun.sunrise) {
      leds::setSunrise(1.0f - (float)left / (alarmCfg.sunriseMin * 60.0f));
      return;
    }
  }
  bool hold = alarmRun.sunrise && alarmRun.sunriseUntilMs && (int32_t)(alarmRun.sunriseUntilMs - now) > 0;
  if (!hold) alarmRun.sunrise = false;
  leds::setSunrise(hold ? 1.0f : -1.0f);
}

// Krótki sygnał budzika (WAV 16 kHz mono): cztery piknięcia i chwila ciszy.
void prepareBeep() {
  if (!fsReady) return;
  if (FFat.exists("/beep1.wav")) FFat.remove("/beep1.wav");  // cichsza wersja
  if (FFat.exists(BEEP_FILE)) return;
  const uint32_t rate = 16000, samples = rate;  // 1 s
  File f = FFat.open(BEEP_FILE, "w");
  if (!f) return;
  auto u32 = [&](uint32_t v) { f.write((const uint8_t*)&v, 4); };
  auto u16 = [&](uint16_t v) { f.write((const uint8_t*)&v, 2); };
  f.write((const uint8_t*)"RIFF", 4);
  u32(36 + samples * 2);
  f.write((const uint8_t*)"WAVEfmt ", 8);
  u32(16);
  u16(1);  // PCM
  u16(1);  // mono
  u32(rate);
  u32(rate * 2);
  u16(2);
  u16(16);
  f.write((const uint8_t*)"data", 4);
  u32(samples * 2);
  int16_t buf[400];  // 25 ms
  for (uint32_t i = 0; i < samples; i += 400) {
    for (int k = 0; k < 400; k++) {
      uint32_t n = i + k, inBeep = n % (rate / 5);  // co 200 ms
      bool on = n < rate * 4 / 5 && inBeep < rate / 10;  // 100 ms piknięcia
      float env = min(1.0f, min(inBeep, rate / 10 - inBeep) / 80.0f);  // 5 ms łagodnie, bez trzasków
      buf[k] = on ? (int16_t)(28000 * env * sinf(2 * PI * 1000 * n / rate)) : 0;
    }
    f.write((const uint8_t*)buf, sizeof(buf));
  }
  f.close();
}

// ---------- wyłącznik czasowy ----------

void setSleep(int minutes) {
  if (sleepFadeVol != 255) sendAudio(AudioCmd::Volume, "", volume);  // przerwane wyciszanie
  sleepFadeVol = 255;
  if (minutes <= 0) {
    sleepEndMs = 0;
    Serial.println("Wyłącznik czasowy: wyłączony");
    return;
  }
  if (!playing) play(currentStation);
  sleepEndMs = (millis() + minutes * 60000UL) | 1;
  Serial.printf("Wyłącznik czasowy: %d min\n", minutes);
}

long sleepSecondsLeft() { return sleepEndMs ? max(0L, (long)(int32_t)(sleepEndMs - millis()) / 1000) : 0; }

void tickSleep() {
  if (!sleepEndMs) return;
  int32_t left = sleepEndMs - millis();
  if (left <= 0) {
    Serial.println("Wyłącznik czasowy: stop");
    stop();
    sendAudio(AudioCmd::Volume, "", volume);  // następne granie z normalną głośnością
    sleepFadeVol = 255;
    return;
  }
  if (left < (int32_t)SLEEP_FADE_MS) {
    uint8_t v = ((uint32_t)volume * left + SLEEP_FADE_MS - 1) / SLEEP_FADE_MS;
    if (v != sleepFadeVol) {
      sleepFadeVol = v;
      sendAudio(AudioCmd::Volume, "", v);
    }
  }
}

// Key1 + Key3 razem: 30 → 60 → 90 min → wyłączony. Diody pokazują czas (jedna = 15 min).
void cycleSleep() {
  long left = sleepSecondsLeft() / 60;
  int next = !sleepEndMs ? 30 : left < 30 ? 60 : left < 60 ? 90 : 0;
  setSleep(next);
  if (next) leds::showIndicator(next / 15, 0x8040FF, 2500);
  else leds::showIndicator(1, 0xFF0000, 1000);
}

// ---------- Halloween ----------

void saveScare() {
  prefs.putBool("hwOn", scareCfg.on);
  prefPut("hwUrl", scareCfg.url);
  prefs.putChar("hwThr", scareCfg.thresholdDb);
  prefs.putUChar("hwVol", scareCfg.volume);
  prefs.putUInt("hwColor", scareCfg.color);
  prefs.putUChar("hwCool", scareCfg.cooldownSec);
  prefs.putBool("hwGlow", scareCfg.glow);
  prefs.putBool("hwRest", scareCfg.restPulse);
}

void loadScare() {
  ScareConfig d;
  scareCfg.on = prefs.getBool("hwOn", d.on);
  scareCfg.url = prefString("hwUrl");
  scareCfg.thresholdDb = constrain(prefs.getChar("hwThr", d.thresholdDb), -80, -5);
  scareCfg.volume = constrain(prefs.getUChar("hwVol", d.volume), 1, 21);
  scareCfg.color = prefs.getUInt("hwColor", d.color) & 0xFFFFFF;
  scareCfg.cooldownSec = constrain(prefs.getUChar("hwCool", d.cooldownSec), 3, 120);
  scareCfg.glow = prefs.getBool("hwGlow", d.glow);
  scareCfg.restPulse = prefs.getBool("hwRest", d.restPulse);
}

void applyScareLeds() { leds::setScare(scareCfg.on || scareState == ScareState::Screaming, scareCfg.color, scareCfg.glow); }

// Koniec krzyku: radio wraca do swojej głośności, mikrofony odpoczywają (głośnik ich nie straszy).
void finishScare() {
  sendAudio(AudioCmd::Volume, "", volume);
  leds::setScareLevel(0);
  scareState = scareCfg.on ? ScareState::Cooldown : ScareState::Off;
  scareCoolUntilMs = millis() + scareCfg.cooldownSec * 1000UL;
  scareAboveSinceMs = 0;
  applyScareLeds();
}

bool fireScare() {
  if (!scare::fileSize() || scare::busy() || playing) return false;
  Serial.println("Halloween: krzyk!");
  scareState = ScareState::Screaming;
  scareEof = false;
  scareEofMs = 0;
  scareStartMs = millis();
  scareEyePeak = 0;
  scareCount++;
  applyScareLeds();
  sendAudio(AudioCmd::Volume, "", scareCfg.volume);
  sendAudio(AudioCmd::PlayFile, scare::FILE_PATH, 0);
  return true;
}

void setScareEnabled(bool on) {
  if (on == scareCfg.on) return;
  scareCfg.on = on;
  if (on) {
    if (playing) stop();  // muzyka w mikrofonach straszyłaby bez przerwy
    scareState = ScareState::Cooldown;
    scareCoolUntilMs = millis() + 2000;  // chwila na odejście od maski
  } else if (scareState == ScareState::Screaming) {
    sendAudio(AudioCmd::Stop, "", 0);
    finishScare();
  } else {
    scareState = ScareState::Off;
  }
  mics::setListening(on);
  applyScareLeds();
  Serial.printf("Halloween: %s\n", on ? "włączony" : "wyłączony");
}

void tickScare() {
  uint32_t now = millis();
  leds::setScareResting(scareCfg.restPulse && scareState == ScareState::Cooldown &&
                        (int32_t)(scareCoolUntilMs - now) > 0);
  if (scareState == ScareState::Screaming) {
    if (playing) return finishScare();  // ktoś włączył radio: krzyk już przerwany
    if (scareEof && !scareEofMs) scareEofMs = now | 1;
    if ((scareEofMs && now - scareEofMs > SCARE_TAIL_MS) || now - scareStartMs > SCARE_MAX_MS) {
      if (!scareEofMs) sendAudio(AudioCmd::Stop, "", 0);
      finishScare();
    }
    return;
  }
  if (!scareCfg.on) return;
  if (playing) scareState = ScareState::Paused;
  else if (!scare::fileSize() || scare::busy()) scareState = ScareState::NoFile;
  else if (scareState == ScareState::Cooldown && (int32_t)(scareCoolUntilMs - now) > 0) return;
  else scareState = ScareState::Armed;
  if (scareState != ScareState::Armed) {
    scareAboveSinceMs = 0;
    return;
  }
  float level = max(mics::rmsDb(0), mics::rmsDb(1));
  if (level < scareCfg.thresholdDb) scareAboveSinceMs = 0;
  else if (!scareAboveSinceMs) scareAboveSinceMs = now | 1;
  else if (now - scareAboveSinceMs >= SCARE_HOLD_MS) fireScare();
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

String alarmJson() {
  return String(",\"alarm\":{\"on\":") + (alarmCfg.on ? "true" : "false") + ",\"h\":" + alarmCfg.hour +
         ",\"m\":" + alarmCfg.minute + ",\"days\":" + alarmCfg.days + ",\"station\":" + alarmCfg.station +
         ",\"vol\":" + alarmCfg.volume + ",\"ramp\":" + alarmCfg.rampMin + ",\"sunrise\":" + alarmCfg.sunriseMin +
         ",\"next\":" + secondsToAlarm() + ",\"ringing\":" + (alarmRinging() ? "true" : "false") +
         ",\"beeping\":" + (alarmRun.beeping ? "true" : "false") + "}";
}

String scareJson() {
  static const char* STATES[] = {"off", "armed", "scream", "cool", "paused", "nofile"};
  const char* dl[] = {"idle", "busy", "ok", "error"};
  char color[8];
  snprintf(color, sizeof(color), "#%06lx", (unsigned long)scareCfg.color);
  long cool = scareState == ScareState::Cooldown ? max(0L, (long)(int32_t)(scareCoolUntilMs - millis()) / 1000) : 0;
  return String(",\"hw\":{\"on\":") + (scareCfg.on ? "true" : "false") + ",\"state\":\"" + STATES[(int)scareState] +
         "\",\"cool\":" + cool + ",\"count\":" + scareCount + ",\"eye\":" + scareEyePeak + ",\"thr\":" + (int)scareCfg.thresholdDb +
         ",\"vol\":" + scareCfg.volume + ",\"color\":\"" + color + "\",\"cooldown\":" + scareCfg.cooldownSec +
         ",\"glow\":" + (scareCfg.glow ? "true" : "false") + ",\"rest\":" + (scareCfg.restPulse ? "true" : "false") +
         ",\"url\":\"" + jsonEscape(scareCfg.url) +
         "\",\"file\":" + scare::fileSize() + ",\"dl\":\"" + dl[(int)scare::downloadState()] +
         "\",\"err\":\"" + jsonEscape(scare::lastError()) + "\"}";
}

String clockJson() {
  if (!timeValid()) return ",\"clock\":\"\"";
  time_t t = time(nullptr);
  tm lt;
  localtime_r(&t, &lt);
  char buf[48];
  snprintf(buf, sizeof(buf), ",\"clock\":\"%02d:%02d\",\"wday\":%d", lt.tm_hour, lt.tm_min, lt.tm_wday);
  return buf;
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
                ",\"announce\":" + (announceEnabled ? "true" : "false") + ledJson() + alarmJson() + clockJson() +
                ",\"sleep\":" + sleepSecondsLeft() + ",\"tone\":[" + (int)tone3[0] + "," + (int)tone3[1] + "," + (int)tone3[2] + "]" +
                ",\"clap\":{\"on\":" + (clapEnabled ? "true" : "false") + ",\"sens\":" + clapSensitivity +
                ",\"count\":" + mics::clapCount() + ",\"db\":" + String(mics::lastClapDb(), 0) +
                ",\"need\":" + String(mics::clapNeedDb(), 0) + "}" +
                ",\"hist\":" + historySeq + scareJson() + "}";
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
            jsonEscape(stations[i].url) + "\",\"fav\":" + (stations[i].fav ? "true" : "false") + "}";
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
  if (alarmCfg.station == i) alarmCfg.station = -1;
  else if (alarmCfg.station > i) alarmCfg.station--;
  saveAlarm();
  saveStations();
  saveState();
  sendOk();
}

// Zastępuje listę domyślną; stacje dodane przez użytkownika przepadają.
void handleResetStations() {
  if (playing) stop();
  setDefaultStations();
  currentStation = 0;
  alarmCfg.station = -1;
  saveAlarm();
  saveState();
  sendOk();
}

void handleFavourite() {
  int i = server.arg("i").toInt();
  if (i < 0 || i >= (int)stations.size()) return sendError("zly numer stacji");
  stations[i].fav = server.arg("on") == "1";
  saveStations();
  sendOk();
}

void handleTone() {
  static const char* KEYS[] = {"toneB", "toneM", "toneT"};
  static const char* ARGS[] = {"b", "m", "t"};
  for (int i = 0; i < 3; i++) {
    if (!server.hasArg(ARGS[i])) continue;
    tone3[i] = constrain(server.arg(ARGS[i]).toInt(), -12, 12);
    prefs.putChar(KEYS[i], tone3[i]);
  }
  applyTone();
  sendOk();
}

void handleAlarm() {
  if (server.hasArg("on")) alarmCfg.on = server.arg("on") == "1";
  if (server.hasArg("h")) alarmCfg.hour = constrain(server.arg("h").toInt(), 0, 23);
  if (server.hasArg("m")) alarmCfg.minute = constrain(server.arg("m").toInt(), 0, 59);
  if (server.hasArg("days")) alarmCfg.days = server.arg("days").toInt() & 0x7F;
  if (server.hasArg("station")) alarmCfg.station = constrain(server.arg("station").toInt(), -1, (int)stations.size() - 1);
  if (server.hasArg("vol")) alarmCfg.volume = constrain(server.arg("vol").toInt(), 1, 21);
  if (server.hasArg("ramp")) alarmCfg.rampMin = constrain(server.arg("ramp").toInt(), 0, 30);
  if (server.hasArg("sunrise")) alarmCfg.sunriseMin = constrain(server.arg("sunrise").toInt(), 0, 30);
  saveAlarm();
  Serial.printf("Budzik: %s %02u:%02u dni %02x\n", alarmCfg.on ? "wł." : "wył.", alarmCfg.hour, alarmCfg.minute, alarmCfg.days);
  sendOk();
}

void handleClap() {
  if (server.hasArg("on")) clapEnabled = server.arg("on") == "1";
  if (server.hasArg("sens")) clapSensitivity = constrain(server.arg("sens").toInt(), 1, 3);
  prefs.putBool("clap", clapEnabled);
  prefs.putUChar("clapSens", clapSensitivity);
  mics::setClapDetection(clapEnabled, clapSensitivity);
  sendOk();
}

void handleScare() {
  if (server.hasArg("thr")) scareCfg.thresholdDb = constrain(server.arg("thr").toInt(), -80, -5);
  if (server.hasArg("vol")) scareCfg.volume = constrain(server.arg("vol").toInt(), 1, 21);
  if (server.hasArg("cooldown")) scareCfg.cooldownSec = constrain(server.arg("cooldown").toInt(), 3, 120);
  if (server.hasArg("glow")) scareCfg.glow = server.arg("glow") == "1";
  if (server.hasArg("rest")) scareCfg.restPulse = server.arg("rest") == "1";
  if (server.hasArg("color")) {
    String c = server.arg("color");
    if (c.startsWith("#")) c.remove(0, 1);
    if (c.length() != 6) return sendError("kolor w formacie #rrggbb");
    scareCfg.color = strtoul(c.c_str(), nullptr, 16);
  }
  if (server.hasArg("on")) setScareEnabled(server.arg("on") == "1");
  applyScareLeds();
  saveScare();
  sendOk();
}

void handleScareDownload() {
  String url = server.arg("url");
  url.trim();
  if (!(url.startsWith("http://") || url.startsWith("https://")) || url.length() > 390)
    return sendError("podaj adres http/https pliku MP3");
  if (scareState == ScareState::Screaming) return sendError("poczekaj, az skonczy krzyczec");
  scareCfg.url = url;
  saveScare();
  // Drugie połączenie TLS obok strumienia https nie mieści się w pamięci wewnętrznej
  // („SSL - Memory allocation failed”), więc radio milknie na czas pobierania.
  if (url.startsWith("https://") && playing) stop();
  if (!scare::startDownload(url)) return sendError("pobieranie juz trwa");
  sendOk();
}

void sendScareResult(bool ok) {
  if (ok) return sendOk();
  server.send(400, "application/json", "{\"error\":\"" + jsonEscape(scare::lastError()) + "\"}");
}

// Kawałki pliku z formularza; odpowiedź (sendScareResult) idzie po ostatnim.
void handleScareUpload() {
  HTTPUpload& up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      uploadResult = false;
      scare::uploadStart();
      break;
    case UPLOAD_FILE_WRITE: scare::uploadWrite(up.buf, up.currentSize); break;
    case UPLOAD_FILE_END:
      uploadResult = scare::uploadFinish();
      Serial.printf("Plik Halloween wgrany: %s\n", uploadResult ? "tak" : scare::lastError().c_str());
      break;
    case UPLOAD_FILE_ABORTED: scare::uploadAbort(); break;
    default: break;
  }
}

void handleHistory() {
  String json = "[";
  xSemaphoreTake(titleLock, portMAX_DELAY);
  for (int i = history.size() - 1; i >= 0; i--) {
    char at[8] = "";
    if (history[i].at) {
      tm lt;
      localtime_r(&history[i].at, &lt);
      snprintf(at, sizeof(at), "%02d:%02d", lt.tm_hour, lt.tm_min);
    }
    if (json.length() > 1) json += ',';
    json += String("{\"at\":\"") + at + "\",\"station\":\"" + jsonEscape(history[i].station) + "\",\"title\":\"" +
            jsonEscape(history[i].title) + "\"}";
  }
  xSemaphoreGive(titleLock);
  server.send(200, "application/json", json + "]");
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
  bool first = true;
  for (int i = 0; i < 2; i++) {
    WifiSlot s = loadSlot(i);
    if (s.ssid.isEmpty()) continue;
    if (!first) json += ',';
    first = false;
    json += "{\"ssid\":\"" + jsonEscape(s.ssid) + "\",\"ip\":\"" + s.ipcfg + "\"}";
  }
  json += String("],\"failed\":\"") + jsonEscape(wifiFailedSsid) + "\",\"hotspot\":\"" + hotspotSsid +
          "\",\"reason\":\"" + REASONS[(int)setupReason] + "\"";
  if (!apMode) {
    json += String(",\"static\":") + (wifiStatic ? "true" : "false") +
            ",\"ip\":\"" + WiFi.localIP().toString() + "\",\"mask\":\"" + WiFi.subnetMask().toString() +
            "\",\"gw\":\"" + WiFi.gatewayIP().toString() + "\",\"dns\":\"" + WiFi.dnsIP().toString() + "\"";
  }
  server.send(200, "application/json", json + "}");
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

// Stały adres albo DHCP dla zapamiętanej sieci; dla bieżącej sieci radio od razu się restartuje.
void handleWifiIp() {
  String ssid = server.arg("ssid");
  int slot = findSlot(ssid);
  if (slot < 0) return sendError("nie ma takiej sieci");
  WifiSlot s = loadSlot(slot);
  if (server.arg("mode") == "dhcp") {
    s.ipcfg = "";
  } else {
    String dns = server.arg("dns");
    if (dns.isEmpty()) dns = server.arg("gw");
    String cfg = server.arg("ip") + ' ' + server.arg("mask") + ' ' + server.arg("gw") + ' ' + dns;
    IpConfig c;
    if (!parseIpConfig(cfg, c)) return sendError("niepoprawny zapis adresu");
    if (const char* err = checkIpConfig(c)) return sendError(err);
    // Adres z innej podsieci odciąłby radio od strony (zostałby tylko hotspot przez BOOT).
    IPAddress here = WiFi.localIP(), mask = WiFi.subnetMask();
    if (ssid == wifiSsid && !apMode)
      for (int i = 0; i < 4; i++)
        if ((c.ip[i] & mask[i]) != (here[i] & mask[i])) return sendError("adres spoza obecnej sieci radia");
    s.ipcfg = cfg;
  }
  storeSlot(slot, s);
  Serial.printf("Sieć \"%s\": %s\n", ssid.c_str(), s.ipcfg.isEmpty() ? "DHCP" : s.ipcfg.c_str());
  sendOk();
  if (ssid == wifiSsid) restartAtMs = millis() + RESTART_DELAY_MS;
}

const char* resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "włączenie zasilania";
    case ESP_RST_EXT: return "przycisk RESET";
    case ESP_RST_SW: return "restart programowy";
    case ESP_RST_PANIC: return "błąd programu (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "spadek napięcia zasilania";
    case ESP_RST_USB: return "USB";
    case ESP_RST_DEEPSLEEP: return "wybudzenie";
    default: return "inny";
  }
}

String jsonField(const char* name, const String& value) { return String(",\"") + name + "\":\"" + jsonEscape(value) + "\""; }
String jsonField(const char* name, long value) { return String(",\"") + name + "\":" + value; }

void handleDiag() {
  StreamStats st = streamStats;
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  String url = streamInfo(streamHost);
  if (url.isEmpty() && currentStation < (int)stations.size()) url = stations[currentStation].url;

  String json = "{\"stream\":{\"playing\":" + String(playing && !loading ? "true" : "false") +
                jsonField("codec", st.codec) + jsonField("bitrate", st.bitrate) + jsonField("rate", st.sampleRate) +
                jsonField("channels", st.channels) + jsonField("bits", st.bits) + jsonField("buf", st.bufFilled) +
                jsonField("bufSize", st.bufSize) + jsonField("url", url) + jsonField("name", streamInfo(icyName)) +
                jsonField("desc", streamInfo(icyDescription)) + jsonField("genre", streamInfo(icyGenre)) +
                jsonField("web", streamInfo(icyUrl)) + jsonField("losses", streamLosses) +
                jsonField("since", playStartMs && playing ? (long)((millis() - playStartMs) / 1000) : -1L) + "}";
  json += ",\"wifi\":{\"static\":" + String(wifiStatic ? "true" : "false") + jsonField("ssid", wifiSsid) +
          jsonField("bssid", WiFi.BSSIDstr()) + jsonField("channel", WiFi.channel()) + jsonField("rssi", WiFi.RSSI()) +
          jsonField("ip", WiFi.localIP().toString()) + jsonField("mask", WiFi.subnetMask().toString()) +
          jsonField("gw", WiFi.gatewayIP().toString()) + jsonField("dns", WiFi.dnsIP().toString()) +
          jsonField("mac", macStr) + jsonField("host", String(HOSTNAME) + ".local") + "}";
  json += ",\"sys\":{\"uptime\":" + String(millis() / 1000) + jsonField("reset", resetReasonText()) +
          ",\"temp\":" + String(temperatureRead(), 1) + jsonField("heap", ESP.getFreeHeap()) +
          jsonField("heapMin", ESP.getMinFreeHeap()) + jsonField("psram", ESP.getFreePsram()) +
          jsonField("psramSize", ESP.getPsramSize()) + jsonField("cpu", ESP.getCpuFreqMHz()) +
          jsonField("flash", ESP.getFlashChipSize()) + jsonField("fs", fsReady ? (long)FFat.usedBytes() : -1L) +
          jsonField("fsSize", fsReady ? (long)FFat.totalBytes() : -1L) + jsonField("core", ESP_ARDUINO_VERSION_STR) + jsonField("fw", FW_VERSION) +
          jsonField("sdk", ESP.getSdkVersion()) + jsonField("build", __DATE__ " " __TIME__) +
          jsonField("audioGap", audioGap.value()) + jsonField("audioLoop", audioLoopTime.value()) +
          jsonField("mainLoop", mainLoopTime.value()) + jsonField("web", webTime.value()) +
          jsonField("stalls", audioStalls) + jsonField("slow", slowStreamCount) + "}}";
  server.send(200, "application/json", json);
}

void handleLevels() {
  levelsWantedMs = millis();
  mics::keepAwake();
  bool outFresh = millis() - outUpdatedMs < LEVELS_STALE_MS;
  char json[160];
  snprintf(json, sizeof(json), "{\"out\":[%.1f,%.1f],\"mic\":[[%.1f,%.1f],[%.1f,%.1f]]}",
           outFresh ? outRmsDb : mics::SILENCE_DB, outFresh ? outPeakDb : mics::SILENCE_DB,
           mics::rmsDb(0), mics::peakDb(0), mics::rmsDb(1), mics::peakDb(1));
  server.send(200, "application/json", json);
}

// Zapomina sieć, z którą radio nie jest teraz połączone.
void handleForgetWifi() {
  String ssid = server.arg("ssid");
  if (ssid == wifiSsid) return sendError("tej sieci radio teraz uzywa");
  int slot = findSlot(ssid);
  if (slot < 0) return sendError("nie ma takiej sieci");
  if (slot == 0) storeSlot(0, loadSlot(1));  // zapasowa zostaje jedyną
  storeSlot(1, WifiSlot());
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
  server.on("/api/wifi/ip", HTTP_POST, handleWifiIp);
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
    server.on("/api/diag", HTTP_GET, handleDiag);
    server.on("/api/levels", HTTP_GET, handleLevels);
    server.on("/api/stations", HTTP_GET, handleStations);
    server.on("/api/play", HTTP_POST, handlePlay);
    server.on("/api/stop", HTTP_POST, [] { stop(); sendOk(); });
    server.on("/api/volume", HTTP_POST, [] { setVolume(server.arg("v").toInt()); sendOk(); });
    server.on("/api/stations/add", HTTP_POST, handleAddStation);
    server.on("/api/stations/delete", HTTP_POST, handleDeleteStation);
    server.on("/api/stations/reset", HTTP_POST, handleResetStations);
    server.on("/api/led", HTTP_POST, handleLed);
    server.on("/api/stations/fav", HTTP_POST, handleFavourite);
    server.on("/api/tone", HTTP_POST, handleTone);
    server.on("/api/alarm", HTTP_POST, handleAlarm);
    server.on("/api/alarm/test", HTTP_POST, [] { sendOk(); alarmFire(); });
    server.on("/api/sleep", HTTP_POST, [] { setSleep(server.arg("min").toInt()); sendOk(); });
    server.on("/api/clap", HTTP_POST, handleClap);
    server.on("/api/history", HTTP_GET, handleHistory);
    server.on("/api/hw", HTTP_POST, handleScare);
    server.on("/api/hw/download", HTTP_POST, handleScareDownload);
    server.on("/api/hw/upload", HTTP_POST, [] { sendScareResult(uploadResult); }, handleScareUpload);
    server.on("/api/hw/test", HTTP_POST, [] {
      if (playing) return sendError("zatrzymaj radio");
      if (scareState == ScareState::Screaming) return sendOk();
      if (!fireScare()) return sendError("brak pliku z dzwiekiem");
      sendOk();
    });
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

// Dwie zapamiętane sieci (WifiSlot): główna i zapasowa. Zapisywana sieć zostaje główną,
// a dotychczasowa główna zapasową; stały adres IP zostaje przy swojej sieci.
static const char* const SLOT_KEYS[2][3] = {{"ssid", "pass", "ipcfg"}, {"ssid2", "pass2", "ipcfg2"}};

// Bez isKey() Preferences loguje błąd przy każdym braku klucza.
String prefString(const char* key) { return prefs.isKey(key) ? prefs.getString(key) : String(); }

void prefPut(const char* key, const String& value) {
  if (!value.isEmpty()) prefs.putString(key, value);
  else if (prefs.isKey(key)) prefs.remove(key);
}

WifiSlot loadSlot(int i) { return {prefString(SLOT_KEYS[i][0]), prefString(SLOT_KEYS[i][1]), prefString(SLOT_KEYS[i][2])}; }

void storeSlot(int i, const WifiSlot& s) {
  prefPut(SLOT_KEYS[i][0], s.ssid);
  prefPut(SLOT_KEYS[i][1], s.pass);
  prefPut(SLOT_KEYS[i][2], s.ipcfg);
}

int findSlot(const String& ssid) {
  for (int i = 0; i < 2; i++)
    if (!ssid.isEmpty() && prefString(SLOT_KEYS[i][0]) == ssid) return i;
  return -1;
}

void rememberWifi(const String& ssid, const String& pass) {
  WifiSlot main = loadSlot(0), spare = loadSlot(1);
  WifiSlot next{ssid, pass, ""};
  int known = findSlot(ssid);
  if (known >= 0) next.ipcfg = loadSlot(known).ipcfg;
  if (main.ssid != ssid) {
    if (!main.ssid.isEmpty()) spare = main;
    else if (spare.ssid == ssid) spare = WifiSlot();
  }
  storeSlot(0, next);
  storeSlot(1, spare);
}

String savedWifiPassword(const String& ssid) {
  int slot = findSlot(ssid);
  return slot < 0 ? String() : loadSlot(slot).pass;
}

bool hasSavedWifi() { return !prefString("ssid").isEmpty() || !prefString("ssid2").isEmpty(); }

bool parseIpConfig(const String& s, IpConfig& c) {
  int a = s.indexOf(' '), b = s.indexOf(' ', a + 1), d = s.indexOf(' ', b + 1);
  if (a < 0 || b < 0 || d < 0) return false;
  return c.ip.fromString(s.substring(0, a)) && c.mask.fromString(s.substring(a + 1, b)) &&
         c.gw.fromString(s.substring(b + 1, d)) && c.dns.fromString(s.substring(d + 1));
}

// nullptr = adres pasuje do maski i bramy.
const char* checkIpConfig(const IpConfig& c) {
  auto u = [](const IPAddress& a) { return (uint32_t)a[0] << 24 | a[1] << 16 | a[2] << 8 | a[3]; };
  uint32_t mask = u(c.mask), ip = u(c.ip);
  if (mask == 0 || (~mask & (~mask + 1)) != 0) return "niepoprawna maska";
  if ((ip & mask) != (u(c.gw) & mask)) return "brama spoza sieci tego adresu";
  if ((ip & ~mask) == 0 || (ip & ~mask) == ~mask) return "to adres sieci albo rozgloszeniowy";
  if (ip == u(c.gw)) return "adres taki sam jak brama";
  if (u(c.dns) == 0) return "niepoprawny DNS";
  return nullptr;
}

bool tryWifi(const WifiSlot& s) {
  IpConfig c;
  if (parseIpConfig(s.ipcfg, c)) WiFi.config(c.ip, c.gw, c.mask, c.dns);
  else WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);  // DHCP
  WiFi.begin(s.ssid.c_str(), s.pass.c_str());
  Serial.printf("Łączę z Wi-Fi \"%s\"%s", s.ssid.c_str(), s.ipcfg.isEmpty() ? "" : " (stały adres)");
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
  WifiSlot main = loadSlot(0), spare = loadSlot(1);
  if (main.ssid.isEmpty() && spare.ssid.isEmpty()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  if (!main.ssid.isEmpty() && tryWifi(main)) {
    wifiSsid = main.ssid;
    wifiStatic = !main.ipcfg.isEmpty();
  } else if (!spare.ssid.isEmpty() && tryWifi(spare)) {
    wifiSsid = spare.ssid;
    wifiStatic = !spare.ipcfg.isEmpty();
    wifiFailedSsid = main.ssid;
    rememberWifi(spare.ssid, spare.pass);
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

enum class ButtonEvent { None, Click, DoubleClick, LongPress, Repeat };

// Rozpoznaje krótkie kliknięcie (po puszczeniu), przytrzymanie i opcjonalne powtarzanie.
// Z doubleMs > 0 także podwójne kliknięcie; pojedyncze przychodzi wtedy z opóźnieniem doubleMs.
struct Button {
  uint32_t longPressMs;
  uint32_t repeatMs;  // 0 = bez powtarzania
  uint32_t doubleMs;  // 0 = bez podwójnego kliknięcia
  bool wasPressed = false;
  bool longFired = false;  // także: naciśnięcie zużyte (np. w kombinacji przycisków)
  bool clickPending = false;
  uint32_t pressedAt = 0;
  uint32_t lastRepeatAt = 0;
  uint32_t clickAt = 0;

  Button(uint32_t longMs, uint32_t repMs = 0, uint32_t dblMs = 0) : longPressMs(longMs), repeatMs(repMs), doubleMs(dblMs) {}

  // Naciśnięcie obsłużone inaczej: puszczenie nie da kliknięcia ani przytrzymania.
  void consume() {
    longFired = true;
    clickPending = false;
  }

  ButtonEvent update(bool pressed, uint32_t now) {
    ButtonEvent ev = ButtonEvent::None;
    if (pressed && !wasPressed) {
      pressedAt = now;
      longFired = false;
    } else if (pressed && !longFired && now - pressedAt >= longPressMs) {
      longFired = true;
      clickPending = false;
      lastRepeatAt = now;
      ev = ButtonEvent::LongPress;
    } else if (pressed && longFired && repeatMs && now - lastRepeatAt >= repeatMs) {
      lastRepeatAt = now;
      ev = ButtonEvent::Repeat;
    } else if (!pressed && wasPressed && !longFired && now - pressedAt > 30) {
      if (!doubleMs) {
        ev = ButtonEvent::Click;
      } else if (clickPending && now - clickAt <= doubleMs) {
        clickPending = false;
        ev = ButtonEvent::DoubleClick;
      } else {
        clickPending = true;
        clickAt = now;
      }
    } else if (!pressed && !wasPressed && clickPending && now - clickAt > doubleMs) {
      clickPending = false;
      ev = ButtonEvent::Click;
    }
    wasPressed = pressed;
    return ev;
  }
};

Button bootButton(WIFI_SETUP_PRESS_MS);
// Układ jak w słuchawkach Bluetooth: zewnętrzne krótko = głośność, długo = stacja.
Button key1(800);
Button key2(1000, 0, DOUBLE_CLICK_MS);
Button key3(800);

// Sygnał budzika też jest „graniem”: graj/stop go wycisza, zamiast włączać stację.
void togglePlay() { playing || alarmRun.beeping ? stop() : play(currentStation); }

// Następna ulubiona stacja po bieżącej (w kółko).
void nextFavourite() {
  int n = stations.size();
  for (int k = 1; k <= n; k++) {
    int i = (currentStation + k) % n;
    if (stations[i].fav) return play(i);
  }
  Serial.println("Brak ulubionych stacji");
  leds::showIndicator(1, 0xFF0000, 800);
}

// Kolejność trybów jak na stronie (zakładka Światło), a nie jak w enum Mode, do którego nowe
// tryby dopisuje się na końcu.
static const leds::Mode LED_MODE_CYCLE[] = {
    leds::Mode::Off,     leds::Mode::Status,       leds::Mode::Color,    leds::Mode::Breathe,
    leds::Mode::Rainbow, leds::Mode::Comet,        leds::Mode::Vu,       leds::Mode::Pulse,
    leds::Mode::Disco,   leds::Mode::RainbowComet, leds::Mode::Wave,     leds::Mode::Sparkle,
    leds::Mode::Fire,    leds::Mode::Police,       leds::Mode::ColorOrgan, leds::Mode::Spectrum,
    leds::Mode::Confetti, leds::Mode::Aurora,      leds::Mode::Bounce,   leds::Mode::Twins,
    leds::Mode::Auto};
static_assert(sizeof(LED_MODE_CYCLE) / sizeof(LED_MODE_CYCLE[0]) == (size_t)leds::Mode::Count,
              "LED_MODE_CYCLE musi mieć każdy tryb");

void nextLedMode() {
  int n = sizeof(LED_MODE_CYCLE) / sizeof(LED_MODE_CYCLE[0]), i = 0;
  while (i < n && LED_MODE_CYCLE[i] != leds::mode()) i++;
  leds::Mode m = LED_MODE_CYCLE[(i + 1) % n];
  applyLedMode(m);
  // Zmiana trybu przyciskiem ma być widać, więc zdejmuje wygaszenie na czas grania.
  if (leds::darkWhilePlaying()) setLedsDarkWhilePlaying(false);
  saveLeds();
  Serial.printf("Tryb diod: %s\n", leds::modeId(m));
}

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
  // Sygnał budzika wycisza dowolny przycisk; to naciśnięcie nie robi nic więcej.
  if (boot == ButtonEvent::Click && alarmRun.beeping) return stop();
  if (boot == ButtonEvent::Click) nextLedMode();

  static uint32_t lastPoll = 0;
  if (now - lastPoll < KEY_POLL_MS) return;
  lastPoll = now;
  uint8_t keys = board::readKeys();

  ButtonEvent e1 = key1.update(keys & board::KEY1, now);
  ButtonEvent e2 = key2.update(keys & board::KEY2, now);
  ButtonEvent e3 = key3.update(keys & board::KEY3, now);
  // Key1 i Key3 razem = wyłącznik czasowy; puszczenie nie zmienia już głośności ani stacji.
  if ((keys & board::KEY1) && (keys & board::KEY3) && !(key1.longFired && key3.longFired)) {
    key1.consume();
    key3.consume();
    e1 = e3 = ButtonEvent::None;
    cycleSleep();
  }
  if (alarmRun.beeping && (e1 != ButtonEvent::None || e2 != ButtonEvent::None || e3 != ButtonEvent::None)) return stop();

  switch (e1) {
    case ButtonEvent::Click: changeVolume(-1); break;
    case ButtonEvent::LongPress: switchStation(-1); break;
    default: break;
  }
  switch (e2) {
    case ButtonEvent::Click: togglePlay(); break;
    case ButtonEvent::DoubleClick: nextFavourite(); break;
    case ButtonEvent::LongPress: setLedsDarkWhilePlaying(!leds::darkWhilePlaying()); break;
    default: break;
  }
  switch (e3) {
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
  Serial.println("\n=== Radio internetowe " FW_VERSION " ===");
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
  tone3[0] = constrain(prefs.getChar("toneB", 0), -12, 12);
  tone3[1] = constrain(prefs.getChar("toneM", 0), -12, 12);
  tone3[2] = constrain(prefs.getChar("toneT", 0), -12, 12);
  loadAlarm();
  clapEnabled = prefs.getBool("clap", false);
  clapSensitivity = constrain(prefs.getUChar("clapSens", 2), 1, 3);
  loadScare();
  if (scareCfg.on) resume = false;  // w masce radio nie gra, tylko czeka na gości

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
  audio.setTone(tone3[0], tone3[1], tone3[2]);
  if (!board::begin()) Serial.println("Błąd inicjalizacji kodeka ES8311 – nie będzie dźwięku");
  if (!mics::begin()) Serial.println("Błąd inicjalizacji mikrofonów (ES7210 / I2S1)");
  prepareNumberClips();
  prepareBeep();
  scare::begin();
  if (scareCfg.on) {
    scareState = ScareState::Cooldown;
    scareCoolUntilMs = millis() + 3000;
    mics::setListening(true);
  }
  applyScareLeds();

  audioQueue = xQueueCreate(8, sizeof(AudioMsg));
  // Rdzeń 1 i ten sam priorytet co loop(): FreeRTOS dzieli między nie czas, więc liczenie SSL
  // przy łączeniu ze stacją (ok. 0,6 s) nie zagładza strony WWW; duży bufor audio kryje przerwy.
  // Duży stos: uzgadnianie SSL dzieje się w tym zadaniu.
  xTaskCreatePinnedToCore(audioTask, "audio", 16384, nullptr, 1, nullptr, 1);

  if (!setupOnce && connectWifi()) {
    Serial.printf("Połączono, IP: %s\n", WiFi.localIP().toString().c_str());
    configTzTime(TIME_ZONE, "pool.ntp.org", "time.google.com");
    mics::setClapDetection(clapEnabled, clapSensitivity);
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
  uint32_t t0 = micros();
  server.handleClient();
  webTime.add(micros() - t0);
  if (apMode) dns.processNextRequest();
  checkSetupIdle();
  if (restartAtMs && (int32_t)(millis() - restartAtMs) >= 0) ESP.restart();
  handleButtons();
  if (!apMode && mics::takeDoubleClap()) {
    Serial.println("Podwójne klaśnięcie");
    togglePlay();
  }
  keepStreamAlive();
  tickAlarm();
  tickSleep();
  tickScare();
  updateLeds();
  mainLoopTime.add(micros() - t0);
  vTaskDelay(1);
}
