#include "meshLib.h"

// Broadcast MAC
static const uint8_t BROADCAST_ADDR[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

MeshLib *MeshLib::_instance = nullptr;

// ========================================
// Konstruktor
// ========================================
MeshLib::MeshLib(ReceiveCallback callback) : _callback(callback) {
  _instance = this;
}

// ========================================
// Inicjalizacja ESP-NOW Mesh
// ========================================
void MeshLib::initMesh(const char *name,
                       const char *subscribed[],
                       int topics_count,
                       uint8_t wifi_channel)
{
  _name = name;
  _subscribed_topics = subscribed;
  _topics_count = topics_count;
  _channel = wifi_channel ? wifi_channel : 1;

#if defined(ARDUINO_ARCH_ESP32)
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    MESH_LOG("❌ ESP-NOW init failed (ESP32)\n");
    while (true) delay(1000);
  }

  esp_now_register_recv_cb(&_recvThunk);

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, BROADCAST_ADDR, 6);
  peer.channel = _channel;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    MESH_LOG("❌ esp_now_add_peer failed (ESP32)\n");
  }

#elif defined(ARDUINO_ARCH_ESP8266)
  WiFi.mode(WIFI_STA);
  wifi_set_channel(_channel);

  if (esp_now_init() != 0) {
    MESH_LOG("❌ ESP-NOW init failed (ESP8266)\n");
    while (true) delay(1000);
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(&_recvThunk);

  if (esp_now_add_peer((uint8_t*)BROADCAST_ADDR, ESP_NOW_ROLE_COMBO, _channel, NULL, 0) != 0) {
    MESH_LOG("❌ esp_now_add_peer failed (ESP8266)\n");
  }
#endif

  MESH_LOG("✅ MeshLib: %s ready (ch=%u, MAC=%s)\n",
           _name ? _name : "node", _channel, WiFi.macAddress().c_str());
}

// ========================================
// Wysyłanie wiadomości
// ========================================
bool MeshLib::sendMessage(const standard_mesh_message &message) {
#if defined(ARDUINO_ARCH_ESP32)
  esp_err_t r = esp_now_send(BROADCAST_ADDR,
                             reinterpret_cast<const uint8_t*>(&message),
                             sizeof(message));
  return (r == ESP_OK);
#else
  int r = esp_now_send((uint8_t*)BROADCAST_ADDR,
                       (uint8_t*)&message,
                       (uint8_t)sizeof(message));
  return (r == 0);
#endif
}

// ========================================
// Rozgłoszenie discover/get
// ========================================
bool MeshLib::sendDiscover(int ttl) {
  standard_mesh_message msg{};
  msg.ttl = (ttl > 0) ? ttl : 1;
  _fillSender(msg);
  strncpy(msg.type,  MESH_TYPE_CMD,           sizeof(msg.type)-1);
  strncpy(msg.topic, MESH_TOPIC_DISCOVER_GET, sizeof(msg.topic)-1);
  msg.payload[0] = '\0';
  return sendMessage(msg);
}

// ========================================
// Callback thunk
// ========================================
#if defined(ARDUINO_ARCH_ESP32)
void MeshLib::_recvThunk(const uint8_t *mac, const uint8_t *data, int len) {
  if (_instance) _instance->_handleReceive(mac, data, len);
}
#else
void MeshLib::_recvThunk(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (_instance) _instance->_handleReceive((const uint8_t*)mac, (const uint8_t*)data, (int)len);
}
#endif

// ========================================
// Obsługa odbioru
// ========================================
void MeshLib::_handleReceive(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != (int)sizeof(standard_mesh_message)) return;

  standard_mesh_message msg{};
  memcpy(&msg, data, sizeof(msg));

  // Pomiń własne ramki
  if (WiFi.macAddress().equals(String(msg.sender))) return;

  // Automatyka CMD
  if (_equals(msg.type, MESH_TYPE_CMD)) {
    _autoHandleCmd(msg);
  }

  // Filtr subskrypcji (0 = wszystkie)
  bool subscribed = (_topics_count == 0);
  for (int i = 0; !subscribed && i < _topics_count; ++i) {
    if (_equals(_subscribed_topics[i], msg.topic)) subscribed = true;
  }

  if (subscribed && _callback) {
    _callback(msg);
  }

  // Forward TTL
  if (msg.ttl > 0) {
    msg.ttl -= 1;
    if (msg.ttl > 0) {
#if defined(ARDUINO_ARCH_ESP32)
      esp_now_send(BROADCAST_ADDR, (uint8_t*)&msg, sizeof(msg));
#else
      esp_now_send((uint8_t*)BROADCAST_ADDR, (uint8_t*)&msg, (uint8_t)sizeof(msg));
#endif
    }
  }
}

// ========================================
// Auto-handling discover
// ========================================
void MeshLib::_autoHandleCmd(standard_mesh_message &msg) {
  if (_equals(msg.topic, MESH_TOPIC_DISCOVER_GET)) {
    _sendDiscoverPost();
  }
}

void MeshLib::_sendDiscoverPost() {
  standard_mesh_message resp{};
  resp.ttl = 1;
  _fillSender(resp);
  strncpy(resp.type,  MESH_TYPE_CMD,            sizeof(resp.type)-1);
  strncpy(resp.topic, MESH_TOPIC_DISCOVER_POST, sizeof(resp.topic)-1);

#if defined(ARDUINO_ARCH_ESP32)
  const char *chip = "esp32";
#else
  const char *chip = "esp8266";
#endif
  String mac = WiFi.macAddress();
  snprintf(resp.payload, sizeof(resp.payload),
           "name=%s;mac=%s;chip=%s;channel=%u",
           _name ? _name : "node", mac.c_str(), chip, _channel);

  (void)sendMessage(resp);
}

// ========================================
// Pomocnicze
// ========================================
void MeshLib::_fillSender(standard_mesh_message &msg) const {
  String mac = WiFi.macAddress();
  strncpy(msg.sender, mac.c_str(), sizeof(msg.sender)-1);
}

bool MeshLib::_equals(const char *a, const char *b) {
  if (!a || !b) return false;
  return strcmp(a, b) == 0;
}
