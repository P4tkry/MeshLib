# MeshLib — ESP8266/ESP32 Mesh over ESP-NOW

Lekka biblioteka do szybkiej, bezserwerowej sieci mesh na ESP8266/ESP32 w oparciu o ESP-NOW. Kluczowe cechy:
- broadcast mesh z TTL, krótkim backoffem i deduplikacją po losowym MID (pierścień 100 wpisów),
- automatyczne DISCOVER (GET/POST) oraz gotowe komendy `ota/start` i `reboot` z selektywnym forwardowaniem,
- wbudowana obsługa OTA (ArduinoOTA) i rebootu wykonywana poza callbackiem ESP-NOW,
- kompatybilność ESP8266 ↔ ESP32 bez dodatkowej konfiguracji.

---
## Wymagania i instalacja
- ESP8266 lub ESP32 z Arduino core; wszystkie węzły muszą używać **tego samego kanału Wi-Fi**.
- Logi używają `Serial.printf` (włączone, gdy `MESH_LIB_LOG_ENABLED=1`).
- Instalacja: skopiuj folder `MeshLib` do `Arduino/libraries/` albo do `lib/` w PlatformIO (możesz też dodać repo do `lib_deps`).

---
## Jak to działa (skrót)
1) Węzeł nadaje przez ESP-NOW broadcast na zadanym kanale.
2) MID jest losowany 32-bitowo (seed: MAC+millis, zero pomijane) i zapisywany w buforze dedup (100 ostatnich MID).
3) Odbiór: dedup → auto-komendy (discover/ota/reboot) → filtr topiców → callback użytkownika.
4) Forwarding: gdy TTL>0 → TTL-- → 1–4 ms backoff → retransmisja broadcast. Docelowe `ota/start` i `reboot` **nie są** forwardowane przez urządzenie, którego MAC jest w payloadzie.
5) Pętla `mesh.loop()` obsługuje oczekujące OTA i reboot; w trakcie OTA/reboot zwraca `true`, aby użytkownik mógł wstrzymać swoje zadania.

---
## Struktura wiadomości
```cpp
struct standard_mesh_message {
  char sender[18];    // "AA:BB:CC:DD:EE:FF"
  char type[16];      // "data" albo "cmd"
  char topic[64];
  char payload[140];
  int16_t ttl;        // <=0 oznacza: ustaw domyślne TTL
  uint32_t mid;       // losowy MID; 0 oznacza: wypełnij automatycznie
};
```
`sendMessage`/`sendCmd` zawsze uzupełniają `sender`, `mid` i TTL (domyślnie 4) jeśli pozostawisz je puste/zerowe.

---
## Publiczne API (szczegóły)
- `MeshLib(ReceiveCallback cb)` — `cb` ma sygnaturę `void cb(const standard_mesh_message&)`.
- `initMesh(name, subscribed, topics_count, wifi_channel)` — `subscribed=nullptr` i `topics_count=0` oznacza brak filtra (odbieraj wszystko). `wifi_channel=0` ustawia kanał 1.
- `sendMessage(topic, payload, ttl)` — typ `data`; jeśli `ttl<=0`, używa `MESH_DEFAULT_TTL` (4).
- `sendCmd(topic, payload, ttl)` — typ `cmd`; analogiczny TTL.
- `sendDiscover(ttl)` — wysyła `discover/get`; payload pusty.
- `loop()` — wywołuj często; przetwarza pending OTA/reboot. Zwraca `true`, gdy biblioteka jest zajęta (OTA lub właśnie wykonuje reboot).

---
## Forwarding i deduplikacja
- Dedup: tylko po MID, tablica cykliczna 100 wpisów; MID=0 nie jest deduplikowany.
- Po nadaniu własnej wiadomości MID trafia od razu do dedup, więc echo nie zostanie ponownie rozgłoszone.
- Warunek forwardingu: po odebraniu `ttl>0` → zmniejsz do `ttl-1`; jeśli wynik >0, wiadomość jest retransmitowana po losowym backoffie 1–4 ms.
- Komendy z MAC-em celu (`ota/start`, `reboot`) nie są retransmitowane przez urządzenie, które jest celem (reszta sieci forwarduje normalnie z TTL>0).

---
## Komendy i format payload
- `discover/get` — autoobsługa; odpowiedź `discover/post` z `name=<n>;mac=<m>;chip=<esp32|esp8266>;channel=<ch>`.
- `ota/start` — `ssid=<ssid>;passwd=<pwd>;mac=<target_mac>;ip=<optional_static_ip>`
  - `mac` wskazuje urządzenie docelowe OTA; tylko ono zatrzyma forward.
  - `ip` opcjonalne: ustawia statyczny IP; gateway = *.1, maska 255.255.255.0, DNS=gateway.
- `reboot` — `mac=<target_mac>`; cel ustawia flagę reboot i wykona restart w `loop()`.

---
## OTA — przebieg krok po kroku
1) Pakiet `ota/start` trafia do celu, parsuje payload i zapisuje żądanie jako `pending` (poza callbackiem ESP-NOW).
2) W `mesh.loop()` wykonywany jest `_enterOTAMode`: `esp_now_deinit`, wyłączenie power-save, tryb STA, opcjonalny static IP, `WiFi.begin()`.
3) Próba Wi-Fi trwa ~3.6 s (50 prób po 300 ms). Niepowodzenie → restart i powrót do mesh.
4) Po połączeniu startuje ArduinoOTA: timeout 5 min na upload, logi przez `Serial`.
5) Po sukcesie OTA następuje restart. Po timeout lub błędzie `_exitOTAMode()` również restartuje, aby wrócić do mesh.

---
## Reboot — przebieg
1) `reboot` z `mac=<target>` jest obsługiwany automatycznie.
2) Urządzenie docelowe ustawia flagę `_reboot_pending` i nie forwarduje pakietu dalej.
3) `mesh.loop()` wywołuje restart po krótkim opóźnieniu; w tym czasie zwraca `true`.

---
## Minimalny przykład
```cpp
#include <Arduino.h>
#include <meshLib.h>

void onMeshReceive(const standard_mesh_message &msg) {
  Serial.printf("[RX] %s %s %s %s\n", msg.type, msg.topic, msg.sender, msg.payload);
}

MeshLib mesh(onMeshReceive);

void setup() {
  Serial.begin(115200);
  mesh.initMesh("node1", nullptr, 0, 1); // brak filtra topiców
}

void loop() {
  if (mesh.loop()) return; // OTA/reboot w toku
  static unsigned long last = 0;
  if (millis() - last > 5000) {
    last = millis();
    mesh.sendMessage("demo/hello", "hi", 3);
  }
}
```

---
## Typowe pułapki
- Zawsze wołaj `mesh.loop()` w głównej pętli — bez tego OTA/reboot nie ruszą.
- Każdy węzeł musi pracować na **tym samym kanale Wi-Fi** (argument `wifi_channel`).
- Dedup trzyma 100 ostatnich MID — w bardzo gęstym ruchu starsze wpisy mogą się nadpisywać.
- ESP-NOW w tej wersji nie jest szyfrowany; payload leci jako tekst jawny.
- W trakcie OTA ESP-NOW jest zdezaktywowane do momentu restartu po zakończeniu OTA.

---
## Licencja
MIT
