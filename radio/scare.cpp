#include "scare.h"

#include <FFat.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>

namespace scare {
namespace {

constexpr const char* TMP_PATH = "/scare.tmp";

volatile Download dlState = Download::Idle;
volatile bool uploading = false;
String error;  // pisze zadanie pobierania albo serwer WWW, nigdy naraz
String dlUrl;
size_t size = 0;

// Zapis do pliku tymczasowego; na końcu sprawdzenie, poprawka ramki Xing i podmiana pliku.
struct Writer {
  File f;
  size_t total = 0;
  bool failed = false;

  bool open() {
    total = 0;
    failed = false;
    f = FFat.open(TMP_PATH, "w");
    if (!f) error = "brak miejsca na FFat";
    return f;
  }

  void write(const uint8_t* data, size_t len) {
    if (failed || !f) return;
    if (total + len > MAX_SIZE) {
      error = "plik większy niż 3 MB";
      failed = true;
    } else if (f.write(data, len) != len) {
      error = "błąd zapisu na FFat";
      failed = true;
    }
    total += len;
  }

  bool finish() {
    if (f) f.close();
    if (!failed && total < 1024) {
      error = "plik jest pusty albo za krótki";
      failed = true;
    }
    if (!failed && !looksLikeMp3()) {
      error = "to nie jest plik MP3";
      failed = true;
    }
    if (failed) {
      FFat.remove(TMP_PATH);
      return false;
    }
    neutralizeXing();
    if (FFat.exists(FILE_PATH)) FFat.remove(FILE_PATH);
    if (!FFat.rename(TMP_PATH, FILE_PATH)) {
      error = "błąd zapisu na FFat";
      return false;
    }
    size = total;
    error = "";
    return true;
  }

  // Przesunięcie pierwszej ramki MP3 (za znacznikiem ID3v2, jeśli jest).
  static size_t audioStart(File& in) {
    uint8_t h[10];
    in.seek(0);
    if (in.read(h, 10) != 10 || memcmp(h, "ID3", 3) != 0) return 0;
    size_t tag = (h[6] & 0x7F) << 21 | (h[7] & 0x7F) << 14 | (h[8] & 0x7F) << 7 | (h[9] & 0x7F);
    return 10 + tag + ((h[5] & 0x10) ? 10 : 0);  // stopka ID3
  }

  static bool looksLikeMp3() {
    File in = FFat.open(TMP_PATH, "r");
    if (!in) return false;
    size_t off = audioStart(in);
    uint8_t b[2] = {0, 0};
    in.seek(off);
    bool ok = in.read(b, 2) == 2 && b[0] == 0xFF && (b[1] & 0xE0) == 0xE0;
    in.close();
    return ok;
  }

  // ESP32-audioI2S 4.0.0 dzieli przez zero przy ramce Xing/Info w pliku krótszym niż 1 s. Zamiana
  // znacznika robi z niej zwykłą ramkę z ciszą (trwa kilkadziesiąt ms), więc każdy plik jest bezpieczny.
  static void neutralizeXing() {
    File io = FFat.open(TMP_PATH, "r+");
    if (!io) return;
    size_t off = audioStart(io);
    uint8_t buf[256];
    io.seek(off);
    size_t n = io.read(buf, sizeof(buf));
    for (size_t i = 4; i + 4 <= n; i++) {
      if (memcmp(buf + i, "Xing", 4) == 0 || memcmp(buf + i, "Info", 4) == 0) {
        io.seek(off + i);
        io.write((const uint8_t*)"\0\0\0\0", 4);
        break;
      }
    }
    io.close();
  }
};

// Strumień dla HTTPClient::writeToStream, zapisujący przez Writer (limit rozmiaru, błędy zapisu).
struct WriterStream : Stream {
  Writer& w;
  explicit WriterStream(Writer& writer) : w(writer) {}
  size_t write(const uint8_t* data, size_t len) override {
    w.write(data, len);
    return w.failed ? 0 : len;
  }
  size_t write(uint8_t c) override { return write(&c, 1); }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

Writer uploadWriter;

void downloadTask(void*) {
  HTTPClient http;
  NetworkClientSecure secure;
  NetworkClient plain;
  secure.setInsecure();  // plik dźwięku, nie dane: bez sprawdzania certyfikatu
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  http.setReuse(false);  // „Connection: close”: serwer nie trzyma połączenia po pliku
  bool https = dlUrl.startsWith("https://");
  if (https) vTaskDelay(pdMS_TO_TICKS(1500));  // zadanie audio zamyka strumień i zwalnia pamięć TLS
  bool ok = false;
  if (!(https ? http.begin(secure, dlUrl) : http.begin(plain, dlUrl))) {
    error = "zły adres";
  } else {
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
      error = code > 0 ? "serwer odpowiedział kodem " + String(code) : "brak połączenia (" + http.errorToString(code) + ")";
      char tls[80];
      if (code < 0 && https && secure.lastError(tls, sizeof(tls))) error += String(", TLS: ") + tls;
    } else if (http.getSize() > (int)MAX_SIZE) {
      error = "plik większy niż 3 MB";
    } else {
      Writer w;
      if (w.open()) {
        // writeToStream rozkodowuje odpowiedź „chunked” i pilnuje długości z nagłówka.
        WriterStream out(w);
        int got = http.writeToStream(&out);
        if (got < 0 && !w.failed) {
          error = "pobieranie przerwane (" + http.errorToString(got) + ")";
          w.failed = true;
        }
        ok = w.finish();
      }
    }
    http.end();
  }
  Serial.printf("Plik Halloween: %s\n", ok ? "pobrany" : error.c_str());
  dlState = ok ? Download::Ok : Download::Error;
  vTaskDelete(nullptr);
}

}  // namespace

void begin() {
  File f = FFat.open(FILE_PATH, "r");
  size = f ? f.size() : 0;
  if (f) f.close();
}

bool startDownload(const String& url) {
  if (dlState == Download::Busy || uploading) return false;
  dlUrl = url;
  error = "";
  dlState = Download::Busy;
  // Duży stos: uzgadnianie SSL.
  if (xTaskCreatePinnedToCore(downloadTask, "scareDl", 12288, nullptr, 1, nullptr, 0) != pdPASS) {
    error = "brak pamięci";
    dlState = Download::Error;
  }
  return true;
}

Download downloadState() { return dlState; }
String lastError() { return error; }

void uploadStart() {
  uploading = true;
  error = "";
  uploadWriter.open();
}

void uploadWrite(const uint8_t* data, size_t len) { uploadWriter.write(data, len); }

bool uploadFinish() {
  bool ok = uploadWriter.finish();
  uploading = false;
  return ok;
}

void uploadAbort() {
  if (uploadWriter.f) uploadWriter.f.close();
  FFat.remove(TMP_PATH);
  error = "wgrywanie przerwane";
  uploading = false;
}

size_t fileSize() { return size; }
bool busy() { return dlState == Download::Busy || uploading; }

}  // namespace scare
