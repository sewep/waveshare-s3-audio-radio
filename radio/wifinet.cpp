#include "wifinet.h"

#include <Preferences.h>
#include <WiFi.h>
#include <algorithm>
#include <time.h>

namespace wifinet {
namespace {

// Siła sygnału: dobra ≥ −70 dBm, słaba do −82 dBm, poniżej za słaba (próba dopiero na końcu).
constexpr int GOOD_RSSI = -70;
constexpr int POOR_RSSI = -82;
constexpr int RECENT_BONUS_DB = 8;      // przewaga, jakiej inna sieć potrzebuje nad ostatnio używaną
#ifdef WIFINET_TEST  // próba na płytce w dobrym zasięgu: każdy sygnał uchodzi za słaby
constexpr int CHECK_AFTER_FAST = -30;
constexpr int ROAM_BELOW = -30;
constexpr uint32_t ROAM_MIN_UPTIME_MS = 40000;
constexpr uint32_t ROAM_COOLDOWN_MS = 60000;
#else
constexpr int CHECK_AFTER_FAST = -75;   // po szybkim połączeniu słabiej = rozejrzyj się za lepszą
constexpr int ROAM_BELOW = -80;         // w pracy: średnia z 30 s poniżej = szukaj lepszej sieci
constexpr uint32_t ROAM_MIN_UPTIME_MS = 120000;
constexpr uint32_t ROAM_COOLDOWN_MS = 300000;
#endif
constexpr int ROAM_GAIN_DB = 10;        // ...mocniejszej co najmniej o tyle
constexpr uint32_t FAST_TIMEOUT_MS = 6000;
constexpr uint32_t TRY_TIMEOUT_MS = 8000;
constexpr uint32_t HIDDEN_TIMEOUT_MS = 10000;
constexpr uint32_t SCAN_MS_PER_CHANNEL = 120;
constexpr int AVG_SAMPLES = 30;  // co 1 s

std::vector<Network> nets;
struct Seen {
  String ssid;
  int rssi;
};
std::vector<Seen> seen;  // ostatnie skanowanie: najsilniejszy punkt każdej zapamiętanej sieci
bool scannedOnce = false;
String connected, choice, failed;
bool connectedIsStatic = false;
uint32_t connectTime = 0;
bool stamped = false;

int samples[AVG_SAMPLES];
int sampleCount = 0, sampleNext = 0;
uint32_t lastSampleMs = 0, lastRoamMs = 0;
bool roamScanning = false;

bool validText(const String& s) { return s.indexOf('\t') < 0 && s.indexOf('\n') < 0; }

String bssidHex(const uint8_t* b) {
  char s[13];
  snprintf(s, sizeof(s), "%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
  return s;
}

// Wiersz: ssid, hasło, ipcfg, ukryta (0/1), bssid (12 cyfr szesnastkowych), kanał, czas ostatniego użycia.
String serialize() {
  String out;
  for (const Network& n : nets) {
    out += n.ssid + '\t' + n.pass + '\t' + n.ipcfg + '\t' + (n.hidden ? '1' : '0') + '\t' + bssidHex(n.bssid) + '\t' +
           String(n.channel) + '\t' + String(n.lastUsed) + '\n';
  }
  return out;
}

void parse(const String& data) {
  nets.clear();
  int start = 0;
  while (start < (int)data.length() && (int)nets.size() < MAX_NETWORKS) {
    int nl = data.indexOf('\n', start);
    String line = data.substring(start, nl < 0 ? data.length() : nl);
    start = nl < 0 ? data.length() : nl + 1;
    String f[7];
    int pos = 0, i = 0;
    for (; i < 7; i++) {
      int tab = line.indexOf('\t', pos);
      f[i] = line.substring(pos, tab < 0 ? line.length() : tab);
      if (tab < 0) break;
      pos = tab + 1;
    }
    if (f[0].isEmpty()) continue;
    Network n;
    n.ssid = f[0];
    n.pass = f[1];
    n.ipcfg = f[2];
    n.hidden = f[3] == "1";
    if (f[4].length() == 12)
      for (int k = 0; k < 6; k++) n.bssid[k] = strtoul(f[4].substring(k * 2, k * 2 + 2).c_str(), nullptr, 16);
    n.channel = f[5].toInt();
    n.lastUsed = strtoul(f[6].c_str(), nullptr, 10);
    nets.push_back(n);
  }
}

// Dwie pierwsze sieci także w kluczach sprzed listy: starsza wersja radia (po powrocie przez OTA)
// czyta tylko je i bez nich nie połączyłaby się z siecią, więc nie mogłaby się już zaktualizować.
void syncLegacy() {
  static const char* const KEYS[2][3] = {{"ssid", "pass", "ipcfg"}, {"ssid2", "pass2", "ipcfg2"}};
  Preferences p;
  p.begin("radio", false);
  for (int i = 0; i < 2; i++) {
    String v[3];
    if (i < (int)nets.size()) {
      v[0] = nets[i].ssid;
      v[1] = nets[i].pass;
      v[2] = nets[i].ipcfg;
    }
    for (int k = 0; k < 3; k++) {
      if (!v[k].isEmpty()) {
        if (p.getString(KEYS[i][k], "") != v[k]) p.putString(KEYS[i][k], v[k]);
      } else if (p.isKey(KEYS[i][k])) {
        p.remove(KEYS[i][k]);
      }
    }
  }
  p.end();
}

void save() {
  Preferences p;
  p.begin("wifi", false);
  p.putString("list", serialize());
  p.end();
  syncLegacy();
}

void moveToFront(int i) {
  if (i <= 0) return;
  Network n = nets[i];
  nets.erase(nets.begin() + i);
  nets.insert(nets.begin(), n);
}

// Łączenie z jedną siecią; z kanałem i BSSID radio nie skanuje, tylko pyta wprost ten punkt.
bool attempt(const Network& n, int channel, const uint8_t* bssid, uint32_t timeoutMs) {
  IpConfig c;
  if (parseIpConfig(n.ipcfg, c)) WiFi.config(c.ip, c.gw, c.mask, c.dns);
  else WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);  // DHCP
  Serial.printf("Łączę z Wi-Fi \"%s\"%s%s", n.ssid.c_str(), bssid ? (" (kanał " + String(channel) + ")").c_str() : "",
                n.ipcfg.isEmpty() ? "" : " (stały adres)");
  uint32_t start = millis();
  WiFi.begin(n.ssid.c_str(), n.pass.c_str(), bssid ? channel : 0, bssid);
  wl_status_t st = WiFi.status();
  while ((st = WiFi.status()) != WL_CONNECTED && millis() - start < timeoutMs) {
    // Nieudane uwierzytelnienie albo brak punktu: od razu dalej, bez czekania do końca limitu.
    if (millis() - start > 500 && (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL)) break;
    delay(50);
  }
  Serial.printf(" %s po %lu ms\n", st == WL_CONNECTED ? "OK" : st == WL_CONNECT_FAILED ? "odmowa" : st == WL_NO_SSID_AVAIL ? "brak sieci" : "za długo",
                (unsigned long)(millis() - start));
  if (st == WL_CONNECTED) return true;
  WiFi.disconnect();
  delay(50);
  return false;
}

struct Candidate {
  int index;
  int rssi;  // 0 = ukryta, niewidoczna w skanowaniu
  uint8_t bssid[6];
  int channel;
};

int scoreOf(const Candidate& c) { return c.rssi + (c.index == 0 ? RECENT_BONUS_DB : 0); }

int classOf(const Candidate& c) {
  if (c.rssi == 0) return 3;  // ukryte na końcu
  if (c.rssi < POOR_RSSI) return 2;
  return scoreOf(c) >= GOOD_RSSI ? 0 : 1;
}

// Kandydaci z bieżącego wyniku skanowania: każda zapamiętana sieć w zasięgu z najsilniejszym
// punktem dostępowym (sieć z kilkoma punktami, np. mesh, ma kilka wpisów o tej samej nazwie).
std::vector<Candidate> candidates(int count) {
  std::vector<Candidate> out;
  for (int i = 0; i < (int)nets.size(); i++) {
    Candidate c{i, 0, {0}, 0};
    int best = -1;
    for (int k = 0; k < count; k++)
      if (WiFi.SSID(k) == nets[i].ssid && (best < 0 || WiFi.RSSI(k) > WiFi.RSSI(best))) best = k;
    if (best >= 0) {
      c.rssi = WiFi.RSSI(best);
      if (c.rssi == 0) c.rssi = -1;
      memcpy(c.bssid, WiFi.BSSID(best), 6);
      c.channel = WiFi.channel(best);
    } else if (!nets[i].hidden) {
      continue;
    }
    out.push_back(c);
  }
  std::stable_sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
    if (classOf(a) != classOf(b)) return classOf(a) < classOf(b);
    return scoreOf(a) > scoreOf(b);
  });
  return out;
}

int scanNow() {
  uint32_t t = millis();
  int n = WiFi.scanNetworks(false, false, false, SCAN_MS_PER_CHANNEL);
  if (n < 0) n = 0;
  noteScan();
  Serial.printf("Skanowanie Wi-Fi: %d sieci w %lu ms\n", n, (unsigned long)(millis() - t));
  return n;
}

// Po połączeniu: zapamiętaj punkt dostępowy, przesuń sieć na początek listy.
void finish(int index) {
  Network& n = nets[index];
  memcpy(n.bssid, WiFi.BSSID(), 6);
  n.channel = WiFi.channel();
  connected = n.ssid;
  connectedIsStatic = !n.ipcfg.isEmpty();
  moveToFront(index);
  save();
}

String dbm(int rssi) { return String(rssi) + " dBm"; }

}  // namespace

void begin() {
  Preferences p;
  p.begin("wifi", false);
  bool have = p.isKey("list");
  if (have) parse(p.getString("list", ""));
  p.end();
  if (!have) {  // pierwsza wersja z listą: przenieś sieć główną i zapasową
    Preferences r;
    r.begin("radio", true);
    static const char* const KEYS[2][3] = {{"ssid", "pass", "ipcfg"}, {"ssid2", "pass2", "ipcfg2"}};
    for (int i = 0; i < 2; i++) {
      if (!r.isKey(KEYS[i][0])) continue;
      Network n;
      n.ssid = r.getString(KEYS[i][0], "");
      n.pass = r.isKey(KEYS[i][1]) ? r.getString(KEYS[i][1], "") : "";
      n.ipcfg = r.isKey(KEYS[i][2]) ? r.getString(KEYS[i][2], "") : "";
      if (!n.ssid.isEmpty() && find(n.ssid) < 0) nets.push_back(n);
    }
    r.end();
    Serial.printf("Wi-Fi: przeniesiono %d sieci do listy\n", (int)nets.size());
    save();
  }
  Preferences q;
  q.begin("wifi", false);
  if (q.isKey("roam")) {  // powód przejścia na inną sieć przed restartem
    choice = q.getString("roam", "");
    q.remove("roam");
  }
  q.end();
}

const std::vector<Network>& networks() { return nets; }

int find(const String& ssid) {
  for (int i = 0; i < (int)nets.size(); i++)
    if (nets[i].ssid == ssid) return i;
  return -1;
}

bool empty() { return nets.empty(); }

bool remember(const String& ssid, const String& pass, bool hidden, String& err) {
  if (!validText(ssid) || !validText(pass)) {
    err = "niedozwolony znak w nazwie albo hasle";
    return false;
  }
  int i = find(ssid);
  if (i < 0) {
    if ((int)nets.size() >= MAX_NETWORKS) {
      err = "radio pamieta juz 8 sieci, usun ktoras";
      return false;
    }
    Network n;
    n.ssid = ssid;
    nets.insert(nets.begin(), n);
    i = 0;
  }
  nets[i].pass = pass;
  nets[i].hidden = hidden;
  moveToFront(i);
  save();
  return true;
}

bool forget(const String& ssid) {
  int i = find(ssid);
  if (i < 0) return false;
  nets.erase(nets.begin() + i);
  save();
  return true;
}

bool setIpConfig(const String& ssid, const String& cfg) {
  int i = find(ssid);
  if (i < 0) return false;
  nets[i].ipcfg = cfg;
  save();
  return true;
}

String password(const String& ssid) {
  int i = find(ssid);
  return i < 0 ? String() : nets[i].pass;
}

bool parseIpConfig(const String& s, IpConfig& c) {
  int a = s.indexOf(' '), b = s.indexOf(' ', a + 1), d = s.indexOf(' ', b + 1);
  if (a < 0 || b < 0 || d < 0) return false;
  return c.ip.fromString(s.substring(0, a)) && c.mask.fromString(s.substring(a + 1, b)) &&
         c.gw.fromString(s.substring(b + 1, d)) && c.dns.fromString(s.substring(d + 1));
}

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

bool connect() {
  if (nets.empty()) return false;
  uint32_t t0 = millis();
  WiFi.setAutoReconnect(false);  // nieudana próba nie ma się ponawiać w tle w trakcie następnej
  bool roamed = !choice.isEmpty();
  bool ok = false;

  // 1. Ostatnio używana sieć wprost, bez skanowania.
  if (nets[0].channel && attempt(nets[0], nets[0].channel, nets[0].bssid, FAST_TIMEOUT_MS)) {
    int rssi = WiFi.RSSI();
    ok = true;
    if (!roamed) choice = "ostatnio używana, " + dbm(rssi);
    if (rssi < CHECK_AFTER_FAST) {
      // Słaby sygnał: może radio przeniesiono bliżej innej znanej sieci.
      int n = scanNow();
      for (const Candidate& c : candidates(n)) {
        if (c.index == 0 || c.rssi == 0 || c.rssi < rssi + RECENT_BONUS_DB) continue;
        Network prev = nets[0];
        WiFi.disconnect();
        if (attempt(nets[c.index], c.channel, c.bssid, TRY_TIMEOUT_MS)) {
          choice = nets[c.index].ssid + " zamiast " + prev.ssid + ": o " + String(c.rssi - rssi) + " dB mocniejsza";
          finish(c.index);
          connectTime = millis() - t0;
          WiFi.setAutoReconnect(true);
          return true;
        }
        if (!attempt(prev, prev.channel, prev.bssid, FAST_TIMEOUT_MS)) ok = false;
        break;
      }
      if (ok && !roamed) choice = "ostatnio używana, słaby sygnał " + dbm(rssi);
    }
    if (ok) finish(0);
  }

  // 2. Skanowanie i sieci w zasięgu od najlepszej.
  if (!ok) {
    int n = scanNow();
    std::vector<Candidate> list = candidates(n);
    // Sieć dopiero zapisana na stronie (jeszcze nigdy nie połączona) idzie pierwsza bez względu na
    // sygnał: użytkownik kazał się z nią połączyć, a przy złym haśle strona pokaże ostrzeżenie.
    bool fresh = nets[0].channel == 0;
    if (fresh)
      for (size_t k = 1; k < list.size(); k++)
        if (list[k].index == 0) std::rotate(list.begin(), list.begin() + k, list.begin() + k + 1);
    for (const Candidate& c : list) {
      Network& net = nets[c.index];
      bool hiddenTry = c.rssi == 0;
      if (attempt(net, c.channel, hiddenTry ? nullptr : c.bssid, hiddenTry ? HIDDEN_TIMEOUT_MS : TRY_TIMEOUT_MS)) {
        int rssi = WiFi.RSSI();
        if (c.index == 0) {
          choice = (fresh ? "nowo zapisana, " : "ostatnio używana, ") + dbm(rssi);
        } else {
          int topRssi = lastRssi(nets[0].ssid);
          choice = net.ssid + (topRssi ? " zamiast " + nets[0].ssid + ": " + dbm(rssi) + " wobec " + dbm(topRssi)
                                       : ", " + dbm(rssi) + " (" + nets[0].ssid + " poza zasięgiem)");
        }
        if (rssi < POOR_RSSI) choice += "; bardzo słaby sygnał";
        finish(c.index);
        ok = true;
        break;
      }
      if (failed.isEmpty() && !hiddenTry) failed = net.ssid;
    }
  }
  connectTime = millis() - t0;
  Serial.printf("Wi-Fi: %s w %lu ms (%s)\n", ok ? "połączono" : "brak znanej sieci", (unsigned long)connectTime,
                ok ? choice.c_str() : "hotspot");
  if (ok) WiFi.setAutoReconnect(true);
  return ok;
}

String connectedSsid() { return connected; }
bool connectedStatic() { return connectedIsStatic; }
String lastChoice() { return choice; }
String lastFailed() { return failed == connected ? String() : failed; }
uint32_t connectMs() { return connectTime; }

void noteScan() {
  int n = WiFi.scanComplete();
  if (n < 0) return;
  seen.clear();
  scannedOnce = true;
  for (const Network& net : nets) {
    int best = 0;
    for (int k = 0; k < n; k++)
      if (WiFi.SSID(k) == net.ssid && (best == 0 || WiFi.RSSI(k) > best)) best = WiFi.RSSI(k);
    if (best) seen.push_back({net.ssid, best});
  }
}

int lastRssi(const String& ssid) {
  for (const Seen& s : seen)
    if (s.ssid == ssid) return s.rssi;
  return 0;
}

bool scanned() { return scannedOnce; }

int averageRssi() {
  if (!sampleCount) return 0;
  long sum = 0;
  for (int i = 0; i < sampleCount; i++) sum += samples[i];
  return sum / sampleCount;
}

bool tick(bool playing, bool trouble) {
  uint32_t now = millis();
  if (!WiFi.isConnected()) return false;

  // Czas ostatniego użycia sieci: dopiero gdy NTP poda zegar, raz na start.
  if (!stamped && !nets.empty() && nets[0].ssid == connected && time(nullptr) > 1700000000) {
    stamped = true;
    nets[0].lastUsed = time(nullptr);
    save();
  }

  if (now - lastSampleMs >= 1000) {
    lastSampleMs = now;
    samples[sampleNext] = WiFi.RSSI();
    sampleNext = (sampleNext + 1) % AVG_SAMPLES;
    if (sampleCount < AVG_SAMPLES) sampleCount++;
  }

  if (roamScanning) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return false;
    roamScanning = false;
    if (n < 0) return false;  // wynik zabrało skanowanie ze strony albo błąd
    noteScan();
    int avg = averageRssi();
    for (const Candidate& c : candidates(n)) {
      Network& net = nets[c.index];
      if (net.ssid == connected || c.rssi == 0 || c.rssi < POOR_RSSI || c.rssi < avg + ROAM_GAIN_DB) continue;
      String reason = net.ssid + " zamiast " + connected + ": " + dbm(c.rssi) + " wobec średnio " + dbm(avg) +
                      (playing ? ", strumień się rwał" : "");
      Serial.printf("Wi-Fi: przechodzę na %s\n", reason.c_str());
      memcpy(net.bssid, c.bssid, 6);
      net.channel = c.channel;
      moveToFront(c.index);
      save();
      Preferences p;
      p.begin("wifi", false);
      p.putString("roam", reason);
      p.end();
      WiFi.scanDelete();
      return true;
    }
    WiFi.scanDelete();
    Serial.printf("Wi-Fi: słaby sygnał (%d dBm), ale lepszej znanej sieci brak\n", avg);
    return false;
  }

  int avg = averageRssi();
  if (now < ROAM_MIN_UPTIME_MS || sampleCount < AVG_SAMPLES || avg >= ROAM_BELOW || (playing && !trouble)) return false;
  if (lastRoamMs && now - lastRoamMs < ROAM_COOLDOWN_MS) return false;
  if (nets.size() < 2) return false;
  lastRoamMs = now;
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return false;
  Serial.printf("Wi-Fi: średnio %d dBm, szukam lepszej znanej sieci\n", avg);
  WiFi.scanNetworks(true, false, false, SCAN_MS_PER_CHANNEL);
  roamScanning = true;
  return false;
}

}  // namespace wifinet
