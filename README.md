# MeshLib — ESP8266/ESP32 Mesh over ESP‑NOW

MeshLib to lekka biblioteka do budowy szybkiej, bezserwerowej sieci mesh opartej o ESP‑NOW na układach ESP8266 oraz ESP32. Zawiera:

* automatyczne nadawanie unikalnego **MID (Message ID)**,
* deduplikację wiadomości opartą o **MID** (zamiast hash payloadu),
* obsługę TTL i automatyczny forwarding,
* automatyczne odpowiedzi na DISCOVER,
* prosty callback odbioru,
* kompatybilność ESP8266 ↔ ESP32.

---

## 🔧 Instalacja

Skopiuj folder `MeshLib` do:

```
Arduino/libraries/MeshLib/
```

Następnie wybierz dowolny przykład z `examples/` lub użyj API poniżej.

---

## 📦 Struktura wiadomości

Każda ramka mesh ma postać:

```cpp
struct standard_mesh_message {
  char sender[18];    // MAC "AA:BB:CC:DD:EE:FF"
  char type[16];
  char topic[64];
  char payload[140];
  int16_t ttl;
  uint32_t mid;       // unikalny Message ID
};
```

MID jest nadawany **automatycznie** podczas wywołania `sendMessage()` – aplikacja nie musi go ustawiać.

---

## 🚀 Szybki start

### Inicjalizacja mesh

```cpp
void onMsg(const standard_mesh_message &m) {
  Serial.println(m.payload);
}

MeshLib mesh(onMsg);

void setup() {
  Serial.begin(115200);
  mesh.initMesh("Node1", nullptr, 0, 1);
}
```

### Wysyłanie wiadomości

```cpp
standard_mesh_message m{};
strncpy(m.type, "data", sizeof(m.type));
strncpy(m.topic, "test/hello", sizeof(m.topic));
strncpy(m.payload, "Hello!", sizeof(m.payload));
m.ttl = 3;
mesh.sendMessage(m);
```

MID zostanie automatycznie nadany.

### Odbiór wiadomości

Każda odebrana wiadomość trafia do callbacka.
Dedup po MID zapobiega pętlom i duplikatom.

---

## 🔄 Forwarding (mesh routing)

Każdy węzeł:

1. odbiera ramkę,
2. sprawdza dedupe po MID,
3. jeżeli nie duplikat: wywołuje callback,
4. jeżeli TTL > 1 → zmniejsza TTL i retransmituje.

Dzięki temu tworzy się automatyczna, lekką siatkę bez serwera.

---

## 🔍 Discover (automatyczne wyszukiwanie węzłów)

Jeśli węzeł odeśle `DISCOVER_GET`, każdy node odpowiada automatycznie komunikatem `DISCOVER_POST`:

```
name=<node>;mac=<xx:xx:xx>;chip=<esp8266|esp32>;channel=<ch>
```

Wywołanie z aplikacji:

```cpp
mesh.sendDiscover(3);
```

---

## 🧪 Przykład: BasicMeshTest

```cpp
#include <Arduino.h>
#include <meshLib.h>

void onMeshReceive(const standard_mesh_message &msg) {
    Serial.print("[RX] mid="); Serial.print(msg.mid);
    Serial.print(" sender="); Serial.print(msg.sender);
    Serial.print(" topic="); Serial.print(msg.topic);
    Serial.print(" payload="); Serial.println(msg.payload);
}

MeshLib mesh(onMeshReceive);
unsigned long last = 0;

void setup() {
  Serial.begin(115200);
  mesh.initMesh("Node", nullptr, 0, 1);
}

void loop() {
  if (millis() - last > 5000) {
    last = millis();

    standard_mesh_message m{};
    m.ttl = 3;
    strncpy(m.type, "data", sizeof(m.type));
    strncpy(m.topic, "test/hello", sizeof(m.topic));
    strncpy(m.payload, "Hello from node!", sizeof(m.payload));

    mesh.sendMessage(m);
  }
}
```

---

## 🛡 Dedup po MID

Każda wiadomość ma unikalny ID:

```
MID = (nodeID << 16) | localCounter
```

Dzięki temu, forwarding nigdy nie tworzy pętli.

---

## 📡 Kompatybilność

* ESP8266 ↔ ESP8266
* ESP32 ↔ ESP32
* ESP8266 ↔ ESP32 (pełna zgodność binarna)

---

## 📜 Licencja

MIT.
