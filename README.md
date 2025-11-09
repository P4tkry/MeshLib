# MeshLib

Prosta biblioteka **ESP-NOW Mesh** dla **ESP32** i **ESP8266** stworzona przez [p4tkry](https://github.com/p4tkry).

Umożliwia automatyczne odkrywanie węzłów (`discover`), rozgłaszanie wiadomości (`broadcast`) oraz routowanie wiadomości z ograniczeniem TTL.

## Instalacja

### Przez PlatformIO (lokalnie)

Umieść folder `MeshLib/` obok swojego projektu i dodaj w `platformio.ini`:

```ini
lib_deps =
  file://../MeshLib
```

### Przez GitHub (jeśli opublikowana)

```ini
lib_deps =
  p4tkry/MeshLib
```

## Przykład użycia

Zajrzyj do folderu [`examples/BasicDiscover`](examples/BasicDiscover/src/main.cpp):

```cpp
#include <Arduino.h>
#include <meshLib.h>

static void onMsg(const standard_mesh_message &msg) {
  Serial.printf("[APP] RX type=%s topic=%s from=%s payload='%s' ttl=%d\n",
                msg.type, msg.topic, msg.sender, msg.payload, msg.ttl);
}

const char* SUBS[] = {
  MESH_TOPIC_DISCOVER_POST,
  "led/state"
};

MeshLib mesh(onMsg);

void setup() {
  Serial.begin(115200);
  delay(500);
  mesh.initMesh("node-01", SUBS, 2, 1);
  mesh.sendDiscover(2);
}

void loop() {
  static uint32_t t = 0;
  if (millis() - t > 3000) {
    t = millis();
    standard_mesh_message msg{};
    msg.ttl = 2;
    strncpy(msg.type,  MESH_TYPE_DATA, sizeof(msg.type)-1);
    strncpy(msg.topic, "led/state",    sizeof(msg.topic)-1);
    strncpy(msg.payload, "on",         sizeof(msg.payload)-1);
    mesh.sendMessage(msg);
  }
}
```

## Licencja

MIT © 2025 [p4tkry](https://github.com/p4tkry)
