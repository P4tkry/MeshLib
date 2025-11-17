#pragma once

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
#elif defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
#endif

// ================== KONFIGURACJA / DOMYŚLNE ==================

#ifndef MESH_DEFAULT_TTL
#define MESH_DEFAULT_TTL        4
#endif

#ifndef MESH_DEDUP_WINDOW_MS
#define MESH_DEDUP_WINDOW_MS    3000
#endif

#ifndef DEDUP_MAX
#define DEDUP_MAX               32
#endif

#ifndef MESH_LIB_LOG_ENABLED
#define MESH_LIB_LOG_ENABLED    1
#endif

#if MESH_LIB_LOG_ENABLED
  #define MESH_LOG(...)  Serial.printf(__VA_ARGS__)
#else
  #define MESH_LOG(...)
#endif

#ifndef MESH_TYPE_CMD
#define MESH_TYPE_CMD           "cmd"
#endif

#ifndef MESH_TYPE_DATA
#define MESH_TYPE_DATA          "data"
#endif

#ifndef MESH_TOPIC_DISCOVER_GET
#define MESH_TOPIC_DISCOVER_GET  "discover/get"
#endif

#ifndef MESH_TOPIC_DISCOVER_POST
#define MESH_TOPIC_DISCOVER_POST "discover/post"
#endif

#ifndef MESH_DEDUP_BYPASS_TOGGLES
#define MESH_DEDUP_BYPASS_TOGGLES 1
#endif

// ================== STRUKTURA WIADOMOŚCI ==================

struct standard_mesh_message {
  char sender[18];    // MAC jako string "AA:BB:CC:DD:EE:FF"
  char type[16];
  char topic[64];
  char payload[140];
  int16_t ttl;
  uint32_t mid;       // NOWE: Message ID do deduplikacji
};

// ================== KLASA MeshLib ==================

class MeshLib {
public:
  using ReceiveCallback = void(*)(const standard_mesh_message&);

  explicit MeshLib(ReceiveCallback cb);

  void initMesh(const char *name,
                const char *subscribed[],
                int topics_count,
                uint8_t wifi_channel);

  bool sendMessage(const standard_mesh_message &message);
  bool sendDiscover(int ttl);

private:
  // instancja singletona dla callbacków ESP-NOW
  static MeshLib* _instance;

  const char *_name = nullptr;
  const char **_subscribed_topics = nullptr;
  int _topics_count = 0;
  uint8_t _channel = 1;

  ReceiveCallback _callback = nullptr;

  // ---- DEDUP po MID ----
  struct DedupEntry {
    uint32_t mid;
    uint32_t ts;
  };

  DedupEntry _dedup[DEDUP_MAX]{};
  int _dedup_idx = 0;

  uint16_t _nodeId = 0;        // wyciągnięty z binarnego MACa
  uint32_t _midCounter = 1;    // lokalny licznik MID

  // wewnętrzne: thunk + obsługa odbioru
#if defined(ARDUINO_ARCH_ESP32)
  static void _recvThunk(const uint8_t *mac, const uint8_t *data, int len);
#else
  static void _recvThunk(uint8_t *mac, uint8_t *data, uint8_t len);
#endif

  void _handleReceive(const uint8_t *mac, const uint8_t *data, int len);
  void _autoHandleCmd(standard_mesh_message &msg);
  void _sendDiscoverPost();
  void _fillSender(standard_mesh_message &msg) const;
  void _fillMid(standard_mesh_message &msg);
  static bool _equals(const char *a, const char *b);

  // dedup
  void _dedupPurgeOld();
  bool _seenAndRemember(const standard_mesh_message &m);
};

