#include "meshLib.h"

// FF:FF:FF:FF:FF:FF
static const uint8_t BROADCAST_ADDR[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// singleton
MeshLib *MeshLib::_instance = nullptr;

// ====== konstruktor ======
MeshLib::MeshLib(ReceiveCallback cb) : _callback(cb) { _instance = this; }

// ====== init ======
void MeshLib::initMesh(const char *name,
                       const char *subscribed[],
                       int topics_count,
                       uint8_t wifi_channel)
{
  _name            = name;
  _subscribed_topics = subscribed;
  _topics_count    = topics_count;
  _channel         = wifi_channel ? wifi_channel : 1;

#if defined(ARDUINO_ARCH_ESP32)
  WiFi.mode(WIFI_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);                         // pełna moc, bez sleep
  esp_wifi_set_max_tx_power(78);                         // ~19.5 dBm
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_LR); // long range
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
  WiFi.setSleepMode(WIFI_NONE_SLEEP);                    // pełna moc, bez sleep
  WiFi.setOutputPower(20.5f);                            // ~20.5 dBm
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

// ====== send ======
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

// ====== discover ======
bool MeshLib::sendDiscover(int ttl) {
  standard_mesh_message msg{};
  msg.ttl = (ttl > 0) ? ttl : 1;
  _fillSender(msg);
  strncpy(msg.type,  MESH_TYPE_CMD,           sizeof(msg.type)-1);
  strncpy(msg.topic, MESH_TOPIC_DISCOVER_GET, sizeof(msg.topic)-1);
  msg.payload[0] = '\0';
  return sendMessage(msg);
}

// ====== thunk RX ======
#if defined(ARDUINO_ARCH_ESP32)
void MeshLib::_recvThunk(const uint8_t *mac, const uint8_t *data, int len) {
  if (_instance) _instance->_handleReceive(mac, data, len);
}
#else
void MeshLib::_recvThunk(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (_instance) _instance->_handleReceive((const uint8_t*)mac, (const uint8_t*)data, (int)len);
}
#endif

// ====== RX handler ======
void MeshLib::_handleReceive(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != (int)sizeof(standard_mesh_message)) return;

  standard_mesh_message msg{};
  memcpy(&msg, data, sizeof(msg));

  // --- binarny self-check po MAC z callbacka ---
  uint8_t my[6];
#if defined(ARDUINO_ARCH_ESP32)
  esp_wifi_get_mac(WIFI_IF_STA, my);
#else
  wifi_get_macaddr(STATION_IF, my);
#endif
  if (memcmp(my, mac, 6) == 0) return; // moja własna ramka → ignoruj

  // (Opcjonalnie) bypass dedupe dla toggle (0/1/ON/OFF) data
#if MESH_DEDUP_BYPASS_TOGGLES
  auto isToggle = [&](){
    if (!_equals(msg.type, MESH_TYPE_DATA)) return false;
    if (msg.payload[0]=='0' && msg.payload[1]==0) return true;
    if (msg.payload[0]=='1' && msg.payload[1]==0) return true;
    if (strcasecmp(msg.payload,"ON")==0)  return true;
    if (strcasecmp(msg.payload,"OFF")==0) return true;
    return false;
  };
  const bool bypass = isToggle();
#else
  const bool bypass = false;
#endif

  // --- deduplikacja z oknem czasowym ---
  if (!bypass && _seenAndRemember(msg)) {
    MESH_LOG("↩️ dup drop %s/%s\n", msg.type, msg.topic);
    return;
  }

  // --- automatyka CMD (np. discover) — PRZED filtrem subskrypcji ---
  if (_equals(msg.type, MESH_TYPE_CMD)) {
    _autoHandleCmd(msg);
  }

  // --- filtr subów dla callbacka (0=wszystko) ---
  bool subscribed = (_topics_count == 0);
  for (int i = 0; !subscribed && i < _topics_count; ++i) {
    if (_equals(_subscribed_topics[i], msg.topic)) subscribed = true;
  }
  if (subscribed && _callback) {
    _callback(msg);
  }

  // --- flood z TTL i krótkim backoffem (mniej kolizji) ---
  if (msg.ttl > 0) {
    msg.ttl -= 1;
    if (msg.ttl > 0) {
#if defined(ARDUINO_ARCH_ESP32)
      uint32_t us = 1000 + (esp_random() % 3000); // 1..4 ms
#else
      uint32_t us = 1000 + (random() % 3000);     // 1..4 ms
#endif
      delayMicroseconds(us);
#if defined(ARDUINO_ARCH_ESP32)
      esp_now_send(BROADCAST_ADDR, (uint8_t*)&msg, sizeof(msg));
#else
      esp_now_send((uint8_t*)BROADCAST_ADDR, (uint8_t*)&msg, (uint8_t)sizeof(msg));
#endif
    }
  }
}

// ====== automatyka CMD ======
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

// ====== helpers ======
void MeshLib::_fillSender(standard_mesh_message &msg) const {
  String mac = WiFi.macAddress();
  strncpy(msg.sender, mac.c_str(), sizeof(msg.sender)-1);
}

bool MeshLib::_equals(const char *a, const char *b) {
  if (!a || !b) return false;
  return strcmp(a, b) == 0;
}

// ====== deduplikacja (hash z oknem czasowym) ======
void MeshLib::_dedupPurgeOld() {
  const uint32_t now = millis();
  for (int i = 0; i < DEDUP_MAX; ++i) {
    if (_dedup[i].h != 0 && (now - _dedup[i].ts) > (uint32_t)MESH_DEDUP_WINDOW_MS) {
      _dedup[i].h = 0;
      _dedup[i].ts = 0;
    }
  }
}

uint32_t MeshLib::_hash32(const standard_mesh_message &m) {
  // FNV-1a po kluczowych polach; BEZ TTL (TTL zmienia się przy hopach)
  uint32_t h = 2166136261u;
  auto mix = [&](const char* s){
    if (!s) return;
    for (; *s; ++s) { h ^= (uint8_t)(*s); h *= 16777619u; }
  };
  mix(m.sender);
  mix(m.type);
  mix(m.topic);
  mix(m.payload);
  if (h == 0) h = 1;
  return h;
}

bool MeshLib::_seenAndRemember(const standard_mesh_message &m) {
  _dedupPurgeOld();

  const uint32_t now = millis();
  const uint32_t h = _hash32(m);

  // znajdź istniejący wpis
  for (int i = 0; i < DEDUP_MAX; ++i) {
    if (_dedup[i].h == h) {
      if ((now - _dedup[i].ts) <= (uint32_t)MESH_DEDUP_WINDOW_MS) {
        return true;              // świeży duplikat → drop
      } else {
        _dedup[i].ts = now;       // przeterminowany → odśwież
        return false;
      }
    }
  }

  // brak wpisu → dodaj nowy
  _dedup[_dedup_idx].h  = h;
  _dedup[_dedup_idx].ts = now;
  _dedup_idx = (_dedup_idx + 1) % DEDUP_MAX;
  return false;
}
