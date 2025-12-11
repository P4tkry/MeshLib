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

#ifndef DEDUP_MAX
#define DEDUP_MAX               100
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

#ifndef MESH_TOPIC_OTA_START
#define MESH_TOPIC_OTA_START     "ota/start"
#endif

#ifndef MESH_TOPIC_REBOOT
#define MESH_TOPIC_REBOOT        "reboot"
#endif

// ================== STRUKTURA OTA ==================

struct ota_request {
  char ssid[32];       // WiFi SSID
  char passwd[64];     // WiFi hasło
  char target_mac[18]; // MAC urządzenia (format "AA:BB:CC:DD:EE:FF")
  char ip[16];         // Adres IP hosta OTA (np. "192.168.1.10")
};

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

  bool sendMessage(const char *topic, const char *payload, int ttl = -1);
  bool sendCmd(const char *topic, const char *payload, int ttl = -1);
  bool sendDiscover(int ttl);
  
  // OTA support (managed internally)
  bool loop();      // tick function; handles OTA when active

private:
  // instancja singletona dla callbacków ESP-NOW
  static MeshLib* _instance;

  // Losowanie 32-bitowe (ESP32: sprzętowe; ESP8266: miks dwóch random())
  static uint32_t rand32();

  const char *_name = nullptr;
  const char **_subscribed_topics = nullptr;
  int _topics_count = 0;
  uint8_t _channel = 1;

  ReceiveCallback _callback = nullptr;

  // ---- DEDUP po MID ----
  struct DedupEntry {
    uint32_t mid;  // zapamiętany MID; 0 oznacza pusty slot
  };

  DedupEntry _dedup[DEDUP_MAX]{};
  int _dedup_idx = 0;

  // OTA state
  bool _ota_mode = false;
  unsigned long _ota_start_time = 0;
  const unsigned long OTA_TIMEOUT_MS = 300000; // 5 minut

  // Defer switching from ESP-NOW callback to main loop
  volatile bool _ota_pending = false;
  ota_request _ota_req{};
  // Defer reboot from callback context
  volatile bool _reboot_pending = false;
  

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
  bool _parseTargetMac(const char *payload, char *out_mac, size_t mac_size) const;
  bool _isForUs(const char *target_mac) const;
  void _handleOTARequest(const standard_mesh_message &msg);
  void _handleRebootRequest(const standard_mesh_message &msg);
  void _enterOTAMode(const char *ssid, const char *passwd, const char *ip);
  void _handleOTA(); // call periodically when in OTA mode
  void _exitOTAMode(); // powrót do mesh'u
  void _doReboot();

  bool _sendMessage(const standard_mesh_message &message);

  // dedup
  bool _seenAndRemember(const standard_mesh_message &m);
};

