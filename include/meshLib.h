#ifndef MESHLIB_H
#define MESHLIB_H

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
  #include <esp_wifi.h>
  #include <esp_now.h>
#elif defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
  extern "C" {
    #include <user_interface.h>
  }
  #include <espnow.h>
#else
  #error "MeshLib supports only ESP32 and ESP8266"
#endif

// ===== Konfiguracja logów (włącz w platformio.ini: -DMESH_LIB_LOG_ENABLED=1) ====
#ifndef MESH_LIB_LOG_ENABLED
  #define MESH_LIB_LOG_ENABLED 0
#endif
#if MESH_LIB_LOG_ENABLED
  #define MESH_LOG(...) do { Serial.printf(__VA_ARGS__); } while(0)
#else
  #define MESH_LOG(...) do {} while(0)
#endif

// ===== Stałe protokołu =====
#define MESH_TYPE_DATA  "data"
#define MESH_TYPE_CMD   "cmd"
#define MESH_TOPIC_DISCOVER_GET  "discover/get"
#define MESH_TOPIC_DISCOVER_POST "discover/post"

// ===== Struktura wiadomości (<= 250 bajtów dla ESP-NOW) =====
typedef struct standard_mesh_message {
  int  ttl;          // ile hopów zostało (1..N)
  char sender[32];   // MAC nadawcy jako tekst
  char type[16];     // "data" / "cmd"
  char topic[32];    // np. "workshop/fan"
  char payload[140]; // dane
} standard_mesh_message;

static_assert(sizeof(standard_mesh_message) <= 250, "Mesh message too big for ESP-NOW");

// ===== MeshLib =====
class MeshLib {
public:
  using ReceiveCallback = void (*)(const standard_mesh_message &msg);

  explicit MeshLib(ReceiveCallback cb);

  // name: nazwa węzła (do discover/post)
  // subscribed: lista subów; gdy topics_count==0 => przyjmuj wszystko (callback)
  // wifi_channel: 1..14 (zwykle 1/6/11); musi być taki sam u wszystkich
  void initMesh(const char *name,
                const char *subscribed[],
                int topics_count,
                uint8_t wifi_channel);

  bool sendMessage(const standard_mesh_message &message);
  bool sendDiscover(int ttl);

private:
  // konfiguracja
  const char **_subscribed_topics = nullptr;
  int          _topics_count = 0;
  const char  *_name = nullptr;
  uint8_t      _channel = 1;

  // callback użytkownika
  ReceiveCallback _callback = nullptr;

  // deduplikacja
  static const int DEDUP_MAX = 24;
  uint32_t _dedup[DEDUP_MAX] = {0};
  int      _dedup_idx = 0;

  // singleton do thunków C
  static MeshLib *_instance;

  // RX callbacky od ESP-NOW
#if defined(ARDUINO_ARCH_ESP32)
  static void _recvThunk(const uint8_t *mac, const uint8_t *data, int len);
#elif defined(ARDUINO_ARCH_ESP8266)
  static void _recvThunk(uint8_t *mac, uint8_t *data, uint8_t len);
#endif

  // obsługa wewnętrzna
  void _handleReceive(const uint8_t *mac, const uint8_t *data, int len);
  void _autoHandleCmd(standard_mesh_message &msg);
  void _sendDiscoverPost();

  // pomocnicze
  void     _fillSender(standard_mesh_message &msg) const;
  static bool _equals(const char *a, const char *b);

  // deduplikacja
  static uint32_t _hash32(const standard_mesh_message &m);
  bool _seenAndRemember(const standard_mesh_message &m);
};

#endif // MESHLIB_H
