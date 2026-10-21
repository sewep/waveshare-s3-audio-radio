// Radio internetowe na Waveshare ESP32-S3-AUDIO-Board.
// Sterowanie przez przeglądarkę: http://radio.local
//
// Przycisk BOOT: krótko = graj/stop, przytrzymanie 3 s = zapomnij Wi-Fi i uruchom konfigurację.
// Bez zapisanej sieci (albo gdy połączenie się nie uda) radio wystawia hotspot "Radio-XXXX",
// a konfiguracja Wi-Fi jest pod http://192.168.4.1.

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <vector>

#include "Audio.h"
#include "board.h"
#include "web_page.h"

static const char* HOSTNAME = "radio";
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static const uint32_t RECONNECT_DELAY_MS = 5000;
static const uint32_t LONG_PRESS_MS = 3000;
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

// ---------- odtwarzanie ----------

void play(int index) {
  if (index < 0 || index >= (int)stations.size()) return;
  currentStation = index;
  playing = true;
  streamTitle = "";
  Serial.printf("Gram: %s (%s)\n", stations[index].name.c_str(), stations[index].url.c_str());
  audio.connecttohost(stations[index].url.c_str());
  lastStreamOkMs = millis();
  saveState();
}

void stop() {
  playing = false;
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
  if (m.e == Audio::evt_streamtitle && m.msg) streamTitle = m.msg;
  Serial.printf("%s: %s\n", m.s ? m.s : "", m.msg ? m.msg : "");
}

// Gdy strumień się urwie, a użytkownik chce słuchać, łączymy ponownie.
void keepStreamAlive() {
  if (!playing || apMode) return;
  if (audio.isRunning()) {
    lastStreamOkMs = millis();
  } else if (millis() - lastStreamOkMs > RECONNECT_DELAY_MS) {
    Serial.println("Strumień przerwany, łączę ponownie");
    play(currentStation);
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

void handleStatus() {
  String json = "{\"playing\":" + String(playing ? "true" : "false") +
                ",\"station\":" + currentStation +
                ",\"volume\":" + volume +
                ",\"title\":\"" + jsonEscape(streamTitle) + "\"" +
                ",\"ip\":\"" + WiFi.localIP().toString() + "\"" +
                ",\"rssi\":" + WiFi.RSSI() +
                ",\"host\":\"" + HOSTNAME + ".local\"}";
  server.send(200, "application/json", json);
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

// ---------- przycisk BOOT ----------

void handleButton() {
  static bool wasPressed = false;
  static uint32_t pressedAt = 0;
  static bool longHandled = false;
  bool pressed = digitalRead(board::BOOT_BUTTON) == LOW;
  uint32_t now = millis();

  if (pressed && !wasPressed) {
    pressedAt = now;
    longHandled = false;
  } else if (pressed && !longHandled && now - pressedAt > LONG_PRESS_MS) {
    longHandled = true;
    Serial.println("Zapominam Wi-Fi i uruchamiam konfigurację");
    prefs.remove("ssid");
    prefs.remove("pass");
    delay(200);
    ESP.restart();
  } else if (!pressed && wasPressed && !longHandled && now - pressedAt > 30) {
    if (!apMode) playing ? stop() : play(currentStation);
  }
  wasPressed = pressed;
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

  Audio::audio_info_callback = audioInfo;
  audio.setPinout(board::I2S_BCLK, board::I2S_LRCK, board::I2S_DOUT, board::I2S_MCLK);
  audio.setOutputSampleRate(Audio::SR_48000);  // kodek skonfigurowany na stałe 48 kHz
  audio.setVolume(volume);
  if (!board::begin()) Serial.println("Błąd inicjalizacji kodeka ES8311 – nie będzie dźwięku");

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
  handleButton();
  keepStreamAlive();
  vTaskDelay(1);
}
