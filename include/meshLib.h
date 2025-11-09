#ifndef MESHLIB_H
#define MESHLIB_H

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
  #include <esp_now.h>
  #include <esp_wifi.h>
#elif defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
  #include <espnow.h>
  #include <user_interface.h>   // wifi_set_channel()
#else
  #error "MeshLib supports only ESP32 or ESP8266."
#endif

// ========================================
// 🔧 Konfiguracja logowania
// ========================================
#ifndef MESH_LIB_LOG_ENABLED
#define MESH_LIB_LOG_ENABLED 0   // 0 = brak logów, 1 = Serial.printf
#endif

#if MESH_LIB_LOG_ENABLED
  #define MESH_LOG(fmt, ...) do { Serial.printf("[MeshLib] " fmt, ##__VA_ARGS__); } while(0)
#else
  #define MESH_LOG(...) do {} while(0)
#endif

// ========================================
// Struktura wiadomości
// ========================================
typedef struct standard_mesh_message {
    int  ttl;            // hop limit
    char sender[32];     // MAC nadawcy
    char type[32];       // "data" | "cmd"
    char topic[32];      // np. "led/state" | "discover/get"
    char payload[140];   // dane użytkownika
} standard_mesh_message;

static_assert(sizeof(standard_mesh_message) <= 250, "Mesh message too big!");

// Typy i tematy systemowe
#define MESH_TYPE_DATA            "data"
#define MESH_TYPE_CMD             "cmd"
#define MESH_TOPIC_DISCOVER_GET   "discover/get"
#define MESH_TOPIC_DISCOVER_POST  "discover/post"

// ========================================
// Klasa MeshLib
// ========================================
class MeshLib {
public:
    using ReceiveCallback = void (*)(const standard_mesh_message &msg);

    explicit MeshLib(ReceiveCallback callback);

    void initMesh(const char *name,
                  const char *subscribed[],
                  int topics_count,
                  uint8_t wifi_channel = 1);

    bool sendMessage(const standard_mesh_message &message);
    bool sendDiscover(int ttl = 1);

private:
    const char **_subscribed_topics = nullptr;
    int          _topics_count = 0;
    const char  *_name = nullptr;
    ReceiveCallback _callback = nullptr;
    uint8_t      _channel = 1;

#if defined(ARDUINO_ARCH_ESP32)
    static void _recvThunk(const uint8_t *mac, const uint8_t *data, int len);
#elif defined(ARDUINO_ARCH_ESP8266)
    static void _recvThunk(uint8_t *mac, uint8_t *data, uint8_t len);
#endif
    void _handleReceive(const uint8_t *mac, const uint8_t *data, int len);

    // automatyka CMD (discover)
    void _autoHandleCmd(standard_mesh_message &msg);
    void _sendDiscoverPost();

    // pomocnicze
    void _fillSender(standard_mesh_message &msg) const;
    static bool _equals(const char *a, const char *b);

    static MeshLib *_instance;
};

#endif // MESHLIB_H
