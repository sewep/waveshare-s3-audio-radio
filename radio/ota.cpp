#include "ota.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <vector>

#include "version.h"

String jsonEscape(const String& s);  // radio.ino

namespace ota {
namespace {

constexpr uint32_t VERIFY_AFTER_MS = 30000;
constexpr size_t MAX_RELEASES = 8;

struct Release {
  String version;
  int proto = 0;
  size_t size = 0;
  String sha256;
  String url;
};

// Listę i błąd pisze zadanie w tle, gdy state() to Checking/Installing; strona czyta je tylko
// poza tymi stanami.
volatile State st = State::Idle;
std::vector<Release> releases;
String error;
String lastResult;  // wynik poprzedniej aktualizacji, pokazywany na stronie
bool lastOk = true;
Release target;
volatile size_t written = 0;
bool waitForAudio = false;
bool pendingVerify = false;

// Wersja x.y.z z opcjonalnym -przedwydaniem i +budową. false = to nie jest taki numer (np. "dev").
bool parseVersion(const String& v, int num[3], String& pre) {
  String core = v;
  int plus = core.indexOf('+');
  if (plus >= 0) core = core.substring(0, plus);
  int dash = core.indexOf('-');
  pre = dash >= 0 ? core.substring(dash + 1) : "";
  if (dash >= 0) core = core.substring(0, dash);
  int part = 0, start = 0;
  for (int i = 0; i <= (int)core.length(); i++) {
    if (i < (int)core.length() && isDigit(core[i])) continue;
    if (i == start || part > 2 || (i < (int)core.length() && core[i] != '.')) return false;
    num[part++] = core.substring(start, i).toInt();
    start = i + 1;
  }
  return part == 3;
}

// -1/0/1 jak a wobec b; 2 = nie da się porównać.
int compareVersions(const String& a, const String& b) {
  int x[3], y[3];
  String pa, pb;
  if (!parseVersion(a, x, pa) || !parseVersion(b, y, pb)) return 2;
  for (int i = 0; i < 3; i++)
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  if (pa == pb) return 0;
  if (pa.isEmpty()) return 1;  // 1.2.0 jest nowsze niż 1.2.0-beta
  if (pb.isEmpty()) return -1;
  return pa < pb ? -1 : 1;
}

bool isHex64(const String& s) {
  if (s.length() != 64) return false;
  for (char c : s)
    if (!isxdigit(c)) return false;
  return true;
}

// Linia versions.tsv: wersja, ota, rozmiar, sha256, adres. Adres musi wskazywać wydanie tego
// repozytorium, a rozmiar mieścić się w partycji OTA.
bool parseLine(const String& line, Release& r) {
  String f[5];
  int start = 0;
  for (int i = 0; i < 5; i++) {
    int tab = line.indexOf('\t', start);
    if (tab < 0 && i < 4) return false;
    f[i] = tab < 0 ? line.substring(start) : line.substring(start, tab);
    f[i].trim();
    start = tab + 1;
  }
  int num[3];
  String pre;
  const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
  r.version = f[0];
  r.proto = f[1].toInt();
  r.size = f[2].toInt();
  r.sha256 = f[3];
  r.url = f[4];
  r.sha256.toLowerCase();
  return parseVersion(r.version, num, pre) && r.version.length() <= 32 && r.proto >= PROTOCOL &&
         r.size >= 64 * 1024 && slot && r.size <= slot->size && isHex64(r.sha256) &&
         r.url.startsWith(String(REPO_URL) + "/releases/download/") && r.url.length() < 200;
}

void startHttp(HTTPClient& http, NetworkClientSecure& tls, const String& url) {
  tls.useBuiltinCACertBundle();  // pobieramy program, więc certyfikat GitHuba musi się zgadzać
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // GitHub przekierowuje na serwer plików
  http.setTimeout(20000);
  http.setReuse(false);
  if (waitForAudio) vTaskDelay(pdMS_TO_TICKS(1500));  // zadanie audio zamyka strumień i zwalnia pamięć TLS
  http.begin(tls, url);
}

String httpError(HTTPClient& http, NetworkClientSecure& tls, int code) {
  if (code > 0) return code == 404 ? String("nie ma takiego pliku w wydaniu (404)") : "serwer odpowiedział kodem " + String(code);
  String e = "brak połączenia z GitHubem (" + http.errorToString(code) + ")";
  char buf[80];
  if (tls.lastError(buf, sizeof(buf))) e += String(", TLS: ") + buf;
  return e;
}

void checkTask(void*) {
  HTTPClient http;
  NetworkClientSecure tls;
  startHttp(http, tls, String(REPO_URL) + "/releases/latest/download/versions.tsv");
  int code = http.GET();
  std::vector<Release> list;
  if (code != HTTP_CODE_OK) {
    error = code == 404 ? String("najnowsze wydanie na GitHubie nie ma jeszcze listy wersji (jest starsze niż aktualizacja przez sieć)")
                        : httpError(http, tls, code);
  } else {
    String body = http.getString();
    int start = 0;
    while (start < (int)body.length() && list.size() < MAX_RELEASES) {
      int nl = body.indexOf('\n', start);
      String line = nl < 0 ? body.substring(start) : body.substring(start, nl);
      start = nl < 0 ? body.length() : nl + 1;
      line.trim();
      Release r;
      if (line.isEmpty() || line[0] == '#') continue;
      if (parseLine(line, r)) list.push_back(r);
      else Serial.printf("OTA: pomijam wiersz „%s”\n", line.c_str());
    }
    if (list.empty()) error = "lista wersji jest pusta";
  }
  http.end();
  releases = list;
  Serial.printf("OTA: %s\n", list.empty() ? error.c_str() : (String(list.size()) + " wersji, najnowsza " + list[0].version).c_str());
  st = list.empty() ? State::Error : State::Checked;
  vTaskDelete(nullptr);
}

void installTask(void*) {
  HTTPClient http;
  NetworkClientSecure tls;
  Serial.printf("OTA: instaluję %s z %s\n", target.version.c_str(), target.url.c_str());
  startHttp(http, tls, target.url);
  int code = http.GET();
  bool ok = false;
  if (code != HTTP_CODE_OK) {
    error = httpError(http, tls, code);
  } else if (http.getSize() != (int)target.size) {
    error = "plik ma inny rozmiar niż na liście wersji";
  } else if (!Update.begin(target.size) || !Update.setSHA256(target.sha256.c_str())) {
    error = String("nie da się zacząć zapisu: ") + Update.errorString();
  } else {
    NetworkClient* in = http.getStreamPtr();
    static uint8_t buf[4096];
    uint32_t lastData = millis();
    while (written < target.size) {
      size_t avail = in->available();
      if (avail) {
        size_t n = in->readBytes(buf, min(min(avail, sizeof(buf)), target.size - written));
        if (n && Update.write(buf, n) != n) {
          error = String("błąd zapisu: ") + Update.errorString();
          break;
        }
        written += n;
        lastData = millis();
      } else if (!http.connected()) {
        error = "połączenie zerwane w trakcie pobierania";
        break;
      } else if (millis() - lastData > 20000) {
        error = "serwer przestał wysyłać dane";
        break;
      } else {
        vTaskDelay(pdMS_TO_TICKS(5));
      }
    }
    if (written < target.size) {
      Update.abort();
    } else if (!Update.end()) {  // sprawdza SHA-256 i obraz, ustawia partycję startową
      error = Update.getError() == UPDATE_ERROR_SHA256 ? String("suma SHA-256 się nie zgadza") : String("zły obraz: ") + Update.errorString();
    } else {
      ok = true;
    }
  }
  http.end();
  if (ok) {
    Preferences p;
    p.begin("ota", false);
    p.putString("from", FW_VERSION);
    p.putString("to", target.version);
    p.end();
    Serial.printf("OTA: wgrane %s, restart\n", target.version.c_str());
    st = State::Restarting;
    vTaskDelay(pdMS_TO_TICKS(1500));  // strona zdąży pokazać, że gotowe
    ESP.restart();
  }
  Serial.printf("OTA: nieudane: %s\n", error.c_str());
  st = State::Error;
  vTaskDelete(nullptr);
}

bool spawn(TaskFunction_t fn, const char* name) {
  // Duży stos: uzgadnianie TLS ze sprawdzaniem certyfikatu. Rdzeń 0, z dala od zadania audio.
  if (xTaskCreatePinnedToCore(fn, name, 12288, nullptr, 1, nullptr, 0) == pdPASS) return true;
  error = "brak pamięci na zadanie";
  st = State::Error;
  return false;
}

}  // namespace

void begin() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  pendingVerify = running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;

  Preferences p;
  p.begin("ota", false);
  String from = p.getString("from", ""), to = p.getString("to", "");
  if (!to.isEmpty()) {
    lastOk = to == FW_VERSION;
    int cmp = compareVersions(to, from);
    lastResult = !lastOk  ? "Wersja " + to + " nie uruchomiła się poprawnie, radio wróciło do " + FW_VERSION + "."
                 : cmp < 0 ? "Przywrócono starszą wersję " + to + " (była " + from + ")."
                 : cmp > 0 ? "Zaktualizowano z " + from + " do " + to + "."
                           : "Zmieniono wersję z " + from + " na " + to + ".";
    Serial.printf("OTA: %s\n", lastResult.c_str());
    // Niepotwierdzona wersja zostawia wpis: jeśli bootloader ją wycofa, poprzednia pokaże, co się stało.
    if (!pendingVerify) {
      p.remove("from");
      p.remove("to");
    }
  }
  p.end();
  Serial.printf("Partycja %s%s\n", running ? running->label : "?", pendingVerify ? ", nowa wersja czeka na potwierdzenie" : "");
}

// Arduino domyślnie potwierdza nową wersję od razu przy starcie; tu dopiero, gdy działa sieć.
extern "C" bool verifyRollbackLater() { return true; }

void tick(bool online) {
  if (!pendingVerify || !online || millis() < VERIFY_AFTER_MS) return;
  pendingVerify = false;
  esp_ota_mark_app_valid_cancel_rollback();
  Preferences p;
  p.begin("ota", false);
  p.remove("from");
  p.remove("to");
  p.end();
  Serial.println("OTA: nowa wersja działa, potwierdzona");
}

bool startCheck(bool waitAudio) {
  if (busy()) return false;
  error = "";
  waitForAudio = waitAudio;
  st = State::Checking;
  return spawn(checkTask, "otaCheck");
}

bool startInstall(const String& version, bool waitAudio, String& err) {
  if (busy()) {
    err = "trwa sprawdzanie albo aktualizacja";
    return false;
  }
  for (const Release& r : releases) {
    if (r.version != version) continue;
    target = r;
    written = 0;
    error = "";
    waitForAudio = waitAudio;
    st = State::Installing;
    if (spawn(installTask, "otaInstall")) return true;
    err = error;
    return false;
  }
  err = "tej wersji nie ma na liście; sprawdź wersje jeszcze raz";
  return false;
}

State state() { return st; }

bool busy() { return st == State::Checking || st == State::Installing || st == State::Restarting; }

String statusJson() {
  static const char* NAMES[] = {"idle", "checking", "checked", "installing", "restarting", "error"};
  State s = st;
  String json = String("{\"version\":\"") + jsonEscape(FW_VERSION) + "\",\"state\":\"" + NAMES[(int)s] + "\"";
  json += ",\"pending\":" + String(pendingVerify ? "true" : "false");
  json += ",\"last\":\"" + jsonEscape(lastResult) + "\",\"lastOk\":" + (lastOk ? "true" : "false");
  if (s == State::Error) json += ",\"error\":\"" + jsonEscape(error) + "\"";
  if (s == State::Installing || s == State::Restarting)
    json += ",\"target\":\"" + jsonEscape(target.version) + "\",\"done\":" + String(written) + ",\"size\":" + String(target.size);
  json += ",\"releases\":[";
  if (s != State::Checking && s != State::Installing) {
    for (size_t i = 0; i < releases.size(); i++) {
      int cmp = compareVersions(releases[i].version, FW_VERSION);
      json += String(i ? "," : "") + "{\"v\":\"" + jsonEscape(releases[i].version) + "\",\"size\":" + String(releases[i].size) +
              ",\"cmp\":" + (cmp == 2 ? String("null") : String(cmp)) + ",\"latest\":" + (i == 0 ? "true" : "false") + "}";
    }
  }
  return json + "]}";
}

}  // namespace ota
