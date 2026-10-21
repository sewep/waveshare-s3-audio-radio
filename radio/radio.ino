// Radio internetowe na Waveshare ESP32-S3-AUDIO-Board.
// Sterowanie przez przeglądarkę: http://radio.local
//
// Przyciski (jak w słuchawkach Bluetooth):
//   Key1  krótko = ciszej,  przytrzymanie = poprzednia stacja
//   Key2  krótko = graj/stop, przytrzymanie = wygaś/przywróć diody na czas grania
//   Key3  krótko = głośniej, przytrzymanie = następna stacja
//   BOOT  krótko = graj/stop, przytrzymanie 3 s = zapomnij Wi-Fi i uruchom konfigurację
// Przy starcie i zmianie stacji radio wypowiada jej numer (nagrania 1..40 wbudowane w firmware,
// przy starcie zapisywane na partycję FFat). Od wybrania stacji do pierwszych dźwięków
// diody pokazują niebieską kometę.
// Bez zapisanej sieci (albo gdy połączenie się nie uda) radio wystawia hotspot "Radio-XXXX",
// a konfiguracja Wi-Fi jest pod http://192.168.4.1.

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
static const uint32_t WIFI_RESET_PRESS_MS = 3000;
static const uint32_t KEY_POLL_MS = 20;
static const uint32_t BATTERY_POLL_MS = 10000;
static const size_t MAX_STATIONS = 40;

struct Station {
  String name;
  String url;
};

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
};

Audio audio;
WebServer server(80);
DNSServer dns;
Preferences prefs;

std::vector<Station> stations;
int currentStation = 0;
bool playing = false;  // stan zamierzony przez użytkownika
uint8_t volume = 8;    // 0..21
String streamTitle;
bool apMode = false;
uint32_t lastStreamOkMs = 0;
int battery = -1;

bool announceEnabled = true;  // wypowiadanie numeru stacji
bool numbersReady = false;    // nagrania liczebników są na FFat
bool announcing = false;      // trwa zapowiedź numeru
bool streamPending = false;   // zapowiedź skończona, czas połączyć ze strumieniem
bool loading = false;         // stacja wybrana, ale jeszcze nie gra

// ---------- trwałe ustawienia ----------

void saveStations() {
  String data;
  for (auto& s : stations) data += s.name + '\t' + s.url + '\n';
  prefs.putString("stations", data);
}

void loadStations() {
  stations.clear();
  String data = prefs.getString("stations", "");
  int pos = 0;
  while (pos < (int)data.length()) {
    int tab = data.indexOf('\t', pos);
    int nl = data.indexOf('\n', pos);
    if (tab < 0 || nl < 0 || tab > nl) break;
    stations.push_back({data.substring(pos, tab), data.substring(tab + 1, nl)});
    pos = nl + 1;
  }
  if (stations.empty()) {
    for (auto& s : DEFAULT_STATIONS) stations.push_back(s);
    saveStations();
  }
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
}

void setLedsDarkWhilePlaying(bool on) {
  leds::setDarkWhilePlaying(on);
  prefs.putBool("ledDark", on);
  Serial.printf("Diody podczas grania: %s\n", on ? "wygaszone" : "włączone");
}

void applyLedMode(leds::Mode m) {
  leds::setMode(m);
  audio.settings.VU_LEVEL = (m == leds::Mode::Vu);  // poziom liczony tylko, gdy potrzebny
}

// ---------- odtwarzanie ----------

// Nagrania liczebników trafiają na FFat tylko wtedy, gdy zmieniła się ich wersja (CRC).
void prepareNumberClips() {
  if (!FFat.begin(true)) {
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
      snprintf(path, sizeof(path), "/n/%d.ogg", i + 1);
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

void connectStream() {
  const Station& s = stations[currentStation];
  Serial.printf("Gram: %d. %s (%s)\n", currentStation + 1, s.name.c_str(), s.url.c_str());
  audio.connecttohost(s.url.c_str());
  lastStreamOkMs = millis();
}

// announce = false przy ponownym łączeniu po zerwaniu strumienia.
void startStation(int index, bool announce) {
  if (index < 0 || index >= (int)stations.size()) return;
  currentStation = index;
  playing = true;
  loading = true;
  streamPending = false;
  streamTitle = "";
  announcing = false;
  if (announce && announceEnabled && numbersReady && index < STATION_NUMBER_COUNT) {
    char path[16];
    snprintf(path, sizeof(path), "/n/%d.ogg", index + 1);
    announcing = audio.connecttoFS(FFat, path);
  }
  if (!announcing) connectStream();
  lastStreamOkMs = millis();
  saveState();
}

void play(int index) { startStation(index, true); }

void stop() {
  playing = false;
  loading = announcing = streamPending = false;
  streamTitle = "";
  audio.stopSong();
  saveState();
}

void setVolume(int v) {
  volume = constrain(v, 0, 21);
  audio.setVolume(volume);
  saveState();
}

void audioInfo(Audio::msg_t m) {
  if (m.e == Audio::evt_vu) {  // [lewy, prawy, szczyt L, szczyt P]; za częste, żeby logować
    if (m.vec1.size() >= 2) leds::setVuLevel(max(m.vec1[0], m.vec1[1]));
    return;
  }
  if (m.e == Audio::evt_eof && announcing) {
    announcing = false;
    streamPending = true;  // połączenie w loop(), nie wewnątrz callbacku biblioteki
  }
  // Pierwsza ramka strumienia zdekodowana = gra; koniec ładowania.
  bool decoding = m.e == Audio::evt_bitrate ||
                  (m.e == Audio::evt_info && m.msg && strncmp(m.msg, "SampleRate", 10) == 0);
  if (decoding && !announcing) loading = false;
  if (m.e == Audio::evt_streamtitle && m.msg) streamTitle = m.msg;
  Serial.printf("%s: %s\n", m.s ? m.s : "", m.msg ? m.msg : "");
}

// Gdy strumień się urwie, a użytkownik chce słuchać, łączymy ponownie.
void keepStreamAlive() {
  if (streamPending) {
    streamPending = false;
    if (playing) connectStream();
    return;
  }
  if (!playing || apMode) return;
  if (audio.isRunning()) {
    lastStreamOkMs = millis();
  } else if (millis() - lastStreamOkMs > RECONNECT_DELAY_MS) {
    Serial.println("Strumień przerwany, łączę ponownie");
    startStation(currentStation, false);
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
         ",\"dark\":" + (leds::darkWhilePlaying() ? "true" : "false") + "}";
}

void handleStatus() {
  String json = "{\"playing\":" + String(playing ? "true" : "false") +
                ",\"station\":" + currentStation +
                ",\"volume\":" + volume +
                ",\"title\":\"" + jsonEscape(streamTitle) + "\"" +
                ",\"ip\":\"" + WiFi.localIP().toString() + "\"" +
                ",\"rssi\":" + WiFi.RSSI() +
                ",\"host\":\"" + HOSTNAME + ".local\"" +
                ",\"battery\":" + battery +
                ",\"loading\":" + (loading ? "true" : "false") +
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

void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ',';
    json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" + WiFi.RSSI(i) + "}";
  }
  json += ']';
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void handleSaveWifi() {
  String ssid = server.arg("ssid");
  if (ssid.isEmpty()) return sendError("wybierz siec");
  prefs.putString("ssid", ssid);
  prefs.putString("pass", server.arg("pass"));
  server.send(200, "text/html; charset=utf-8",
              "<meta charset=utf-8><p>Zapisano. Radio uruchamia się ponownie – za chwilę otwórz "
              "<a href='http://radio.local'>http://radio.local</a>.</p>");
  delay(500);
  ESP.restart();
}

void setupRoutes() {
  if (apMode) {
    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", SETUP_HTML); });
    server.on("/api/scan", HTTP_GET, handleScan);
    server.on("/api/wifi", HTTP_POST, handleSaveWifi);
    // captive portal: każdy adres prowadzi do strony konfiguracji
    server.onNotFound([] {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    });
  } else {
    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/stations", HTTP_GET, handleStations);
    server.on("/api/play", HTTP_POST, handlePlay);
    server.on("/api/stop", HTTP_POST, [] { stop(); sendOk(); });
    server.on("/api/volume", HTTP_POST, [] { setVolume(server.arg("v").toInt()); sendOk(); });
    server.on("/api/stations/add", HTTP_POST, handleAddStation);
    server.on("/api/stations/delete", HTTP_POST, handleDeleteStation);
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

bool connectWifi() {
  String ssid = prefs.getString("ssid", "");
  if (ssid.isEmpty()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(ssid.c_str(), prefs.getString("pass", "").c_str());
  Serial.printf("Łączę z Wi-Fi \"%s\"", ssid.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFi.setSleep(false);  // płynniejszy strumień
  WiFi.setAutoReconnect(true);
  return true;
}

void startAccessPoint() {
  apMode = true;
  WiFi.mode(WIFI_AP_STA);  // STA potrzebne do skanowania sieci
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  char apName[20];
  snprintf(apName, sizeof(apName), "Radio-%02X%02X", mac[4], mac[5]);
  WiFi.softAP(apName);
  dns.start(53, "*", WiFi.softAPIP());
  Serial.printf("Tryb konfiguracji: połącz się z \"%s\" i otwórz http://%s\n", apName,
                WiFi.softAPIP().toString().c_str());
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

Button bootButton(WIFI_RESET_PRESS_MS);
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
    Serial.println("Zapominam Wi-Fi i uruchamiam konfigurację");
    prefs.remove("ssid");
    prefs.remove("pass");
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

void updateLeds() {
  if (apMode) leds::setState(leds::State::Setup);
  else if (!playing) leds::setState(leds::State::Stopped);
  else leds::setState(loading ? leds::State::Loading : leds::State::Playing);

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
  delay(300);
  Serial.println("\n=== Radio internetowe ===");
  pinMode(board::BOOT_BUTTON, INPUT_PULLUP);

  prefs.begin("radio", false);
  loadStations();
  currentStation = constrain(prefs.getInt("station", 0), 0, (int)stations.size() - 1);
  volume = prefs.getUChar("volume", 8);
  bool resume = prefs.getBool("playing", true);
  announceEnabled = prefs.getBool("announce", true);

  leds::begin(board::LED_PIN, board::LED_COUNT);  // od razu animacja startowa
  leds::setColor(prefs.getUInt("ledColor", 0xFF6A00));
  leds::setBrightness(prefs.getUChar("ledBright", 60));
  leds::setDarkWhilePlaying(prefs.getBool("ledDark", false));
  uint8_t ledMode = prefs.getUChar("ledMode", (uint8_t)leds::Mode::Rainbow);
  applyLedMode(ledMode < (uint8_t)leds::Mode::Count ? (leds::Mode)ledMode : leds::Mode::Rainbow);
  leds::setState(leds::State::Connecting);

  Audio::audio_info_callback = audioInfo;
  audio.setPinout(board::I2S_BCLK, board::I2S_LRCK, board::I2S_DOUT, board::I2S_MCLK);
  audio.setOutputSampleRate(Audio::SR_48000);  // kodek skonfigurowany na stałe 48 kHz
  audio.setVolume(volume);
  if (!board::begin()) Serial.println("Błąd inicjalizacji kodeka ES8311 – nie będzie dźwięku");
  prepareNumberClips();

  if (connectWifi()) {
    Serial.printf("Połączono, IP: %s\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin(HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("Strona: http://%s.local\n", HOSTNAME);
    }
    setupRoutes();
    if (resume) play(currentStation);
  } else {
    startAccessPoint();
    setupRoutes();
  }
}

void loop() {
  audio.loop();
  server.handleClient();
  if (apMode) dns.processNextRequest();
  handleButtons();
  keepStreamAlive();
  updateLeds();
  vTaskDelay(1);
}
