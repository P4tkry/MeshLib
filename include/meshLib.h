#ifndef MESHLIB_H
#define MESHLIB_H

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
  #include <esp_wifi.h>
  #include <esp_now.h>
#elif defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
  extern "C" { #include <user_interface.h> }
  #include <espnow.h>
#else
  #error "MeshLib supports only ESP32 and ESP8266"
#endif

// ===== Konfiguracja logów =====
// Włącz w platformio.ini: -DMESH_LIB_LOG_ENABLED=1
#ifndef MESH_LIB_LOG_ENABLED
  #define MESH_LIB_LOG_ENABLED 0
#endif
#if MESH_LIB_LOG_ENABLED
  #define MESH_LOG(...) do { Serial.printf(__VA_ARGS__); } while(0)
#else
  #define MESH_LOG(...) do {} while(0)
#endif

// Domyślny TTL dla wiadomości tworzonych lokalnie
#ifndef MESH_DEFAULT_TTL
  #define MESH_DEFAULT_TTL 2
#endif

// Deduplikacja: identyczny (sender+type+topic+payload) w tym oknie → drop
#ifndef MESH_DEDUP_WINDOW_MS
  #define MESH_DEDUP_WINDOW_MS 1000
#endif

// Przepuszczaj zawsze „toggle” (0/1/ON/OFF) mimo dedupe? 0/1
#ifndef MESH_DEDUP_BYPASS_TOGGLES
  #define MESH_DEDUP_BYPASS_TOGGLES 0
#endif

// Protokół
#define MESH_TYPE_DATA  "data"
#define MESH_TYPE_CMD   "cmd"
#define MESH_TOPIC_DISCOVER_GET  "discover/get"
#define MESH_TOPIC_DISCOVER_POST "discover/post"

// Struktura wiadomości (<= 250 B)
typedef struct standard_mesh_message {
  int  ttl;          // ile hopów jeszcze
  char sender[32];   // MAC nadawcy (tekstowo)
  char type[16];     // "data"/"cmd"
  char topic[32];    // np. "workshop/fan"
  char payload[140]; // dane/polecenie
} standard_mesh_message;

static_assert(sizeof(standard_mesh_message) <= 250, "Mesh message too big for ESP-NOW");

class MeshLib {
public:
  using ReceiveCallback = void (*)(const standard_mesh_message &msg);

  explicit MeshLib(ReceiveCallback cb);

  // topics_count==0 → callback dostaje wszystko
  void initMesh(const char *name,
                const char *subscribed[],
                int topics_count,
                uint8_t wifi_channel);

  // Wysyłka broadcast (użyj MESH_DEFAULT_TTL, jeśli message.ttl==0)
  bool sendMessage(const standard_mesh_message &message);

  // Rozgłoś discover/get
  bool sendDiscover(int ttl);

private:
  const char **_subscribed_topics = nullptr;
  int          _topics_count = 0;
  const char  *_name = nullptr;
  uint8_t      _channel = 1;

  ReceiveCallback _callback = nullptr;

  // Deduplikacja
  static const int DEDUP_MAX = 24;
  struct DedupEntry { uint32_t h; uint32_t ts; };
  DedupEntry _dedup[DEDUP_MAX] = {};
  int        _dedup_idx = 0;

  static MeshLib *_instance;

#if defined(ARDUINO_ARCH_ESP32)
  static void _recvThunk(const uint8_t *mac, const uint8_t *data, int len);
#else
  static void _recvThunk(uint8_t *mac, uint8_t *data, uint8_t len);
#endif

  void _handleReceive(const uint8_t *mac, const uint8_t *data, int len);
  void _autoHandleCmd(standard_mesh_message &msg);
  void _sendDiscoverPost();

  void     _fillSender(standard_mesh_message &msg) const;
  static bool _equals(const char *a, const char *b);

  // Dedupe
  static uint32_t _hash32(const standard_mesh_message &m);
  void _dedupPurgeOld();
  bool _seenAndRemember(const standard_mesh_message &m);
};

#endif // MESHLIB_H
