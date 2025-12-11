#include "meshLib.h"

#if defined(ARDUINO_ARCH_ESP32)
  #include <esp_wifi.h>
  #include <esp_now.h>
#elif defined(ARDUINO_ARCH_ESP8266)
  extern "C" {
    #include <user_interface.h>
    #include <espnow.h>
  }
#endif

// Broadcast FF:FF:FF:FF:FF:FF
static const uint8_t BROADCAST_ADDR[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

MeshLib *MeshLib::_instance = nullptr;

uint32_t MeshLib::rand32() {
#if defined(ARDUINO_ARCH_ESP32)
  return esp_random();
#else
  return (uint32_t(random()) << 16) ^ uint32_t(random());
#endif
}

// ================== KONSTRUKTOR ==================

MeshLib::MeshLib(ReceiveCallback cb)
: _callback(cb)
{
  _instance   = this;
  _topics_count = 0;
  _channel      = 1;
  _dedup_idx    = 0;
  // _dedup jest wyzerowany przez in-class init / statyczną inicjalizację
}

// ================== INIT MESH ==================

void MeshLib::initMesh(const char *name,
                       const char *subscribed[],
                       int topics_count,
                       uint8_t wifi_channel)
{
  _name              = name;
  _subscribed_topics = subscribed;
  _topics_count      = topics_count;
  _channel           = wifi_channel ? wifi_channel : 1;

#if defined(ARDUINO_ARCH_ESP32)

  WiFi.mode(WIFI_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_max_tx_power(78); // ~19.5 dBm
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_LR);
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
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setOutputPower(20.5f);
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

  uint8_t mac_bin[6];
#if defined(ARDUINO_ARCH_ESP32)
  esp_wifi_get_mac(WIFI_IF_STA, mac_bin);
#else
  wifi_get_macaddr(STATION_IF, mac_bin);
#endif

  // ziarno RNG: MAC + czas uruchomienia, żeby MID-y były losowe per urządzenie
  uint32_t seed = (uint32_t(mac_bin[2]) << 24) |
                  (uint32_t(mac_bin[3]) << 16) |
                  (uint32_t(mac_bin[4]) << 8)  |
                  uint32_t(mac_bin[5]);
  seed ^= millis();
  randomSeed(seed);

  MESH_LOG("✅ MeshLib: %s ready (ch=%u, MAC=%s)\n",
           _name ? _name : "node", _channel, WiFi.macAddress().c_str());
}

// ================== WYSYŁANIE ==================

bool MeshLib::_sendMessage(const standard_mesh_message &message) {
  standard_mesh_message m = message;
  if (m.ttl <= 0) m.ttl = MESH_DEFAULT_TTL;  // domyślny TTL

  _fillSender(m); // na wszelki wypadek, gdyby aplikacja nie ustawiła
  _fillMid(m);    // NOWE: nadaj MID, jeżeli brak
  (void)_seenAndRemember(m); // zapisz własny MID, by nie forwardować po zawróceniu

#if defined(ARDUINO_ARCH_ESP32)
  esp_err_t r = esp_now_send(BROADCAST_ADDR,
                             reinterpret_cast<const uint8_t*>(&m),
                             sizeof(m));
  return (r == ESP_OK);
#else
  int r = esp_now_send((uint8_t*)BROADCAST_ADDR,
                       (uint8_t*)&m,
                       (uint8_t)sizeof(m));
  return (r == 0);
#endif
}

bool MeshLib::sendMessage(const char *topic, const char *payload, int ttl) {
  standard_mesh_message m{};
  m.ttl = (ttl > 0) ? ttl : MESH_DEFAULT_TTL;
  _fillSender(m);
  strncpy(m.type,  MESH_TYPE_DATA, sizeof(m.type) - 1);
  if (topic)   strncpy(m.topic,   topic,   sizeof(m.topic) - 1);
  if (payload) strncpy(m.payload, payload, sizeof(m.payload) - 1);
  return _sendMessage(m);
}

bool MeshLib::sendCmd(const char *topic, const char *payload, int ttl) {
  standard_mesh_message m{};
  m.ttl = (ttl > 0) ? ttl : MESH_DEFAULT_TTL;
  _fillSender(m);
  strncpy(m.type,  MESH_TYPE_CMD, sizeof(m.type) - 1);
  if (topic)   strncpy(m.topic,   topic,   sizeof(m.topic) - 1);
  if (payload) strncpy(m.payload, payload, sizeof(m.payload) - 1);
  return _sendMessage(m);
}

bool MeshLib::sendDiscover(int ttl) {
  standard_mesh_message msg{};
  msg.ttl = (ttl > 0) ? ttl : MESH_DEFAULT_TTL;
  _fillSender(msg);
  strncpy(msg.type,  MESH_TYPE_CMD,           sizeof(msg.type)-1);
  strncpy(msg.topic, MESH_TOPIC_DISCOVER_GET, sizeof(msg.topic)-1);
  msg.payload[0] = '\0';
  // MID zostanie nadany w sendMessage()
  return _sendMessage(msg);
}

// ================== RECV THUNK ==================

#if defined(ARDUINO_ARCH_ESP32)
void MeshLib::_recvThunk(const uint8_t *mac, const uint8_t *data, int len) {
  if (_instance) _instance->_handleReceive(mac, data, len);
}
#else
void MeshLib::_recvThunk(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (_instance) _instance->_handleReceive((const uint8_t*)mac,
                                           (const uint8_t*)data,
                                           (int)len);
}
#endif

// ================== ODBIÓR I FORWARDING ==================

void MeshLib::_handleReceive(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != (int)sizeof(standard_mesh_message)) return;

  standard_mesh_message msg{};
  memcpy(&msg, data, sizeof(msg));

  // self MAC check (binarne)
  uint8_t my[6];
#if defined(ARDUINO_ARCH_ESP32)
  esp_wifi_get_mac(WIFI_IF_STA, my);
#else
  wifi_get_macaddr(STATION_IF, my);
#endif
  if (memcmp(my, mac, 6) == 0) return;  // ignoruj własne ramki

  // dedupe po MID
  if (_seenAndRemember(msg)) {
#if MESH_LIB_LOG_ENABLED
    MESH_LOG("↩️ dup drop mid=%lu type=%s topic=%s\n",
             (unsigned long)msg.mid, msg.type, msg.topic);
#endif
    return;
  }

  // auto-CMD
  if (_equals(msg.type, MESH_TYPE_CMD)) {
    _autoHandleCmd(msg);
  }

  // filtr subów → callback
  bool subscribed = (_topics_count == 0);
  for (int i = 0; !subscribed && i < _topics_count; ++i) {
    if (_equals(_subscribed_topics[i], msg.topic)) subscribed = true;
  }
  if (subscribed && _callback) {
    _callback(msg);
  }

  // forward z TTL + krótki backoff
  if (msg.ttl > 0) {
    msg.ttl -= 1;
    if (msg.ttl > 0) {
      // CMD packets with target MAC: forward tylko jeśli nie dla nas
      if (_equals(msg.type, MESH_TYPE_CMD) &&
          (_equals(msg.topic, MESH_TOPIC_OTA_START) || _equals(msg.topic, MESH_TOPIC_REBOOT))) {
        char target_mac[18];
        if (_parseTargetMac(msg.payload, target_mac, sizeof(target_mac)) && _isForUs(target_mac)) {
#if MESH_LIB_LOG_ENABLED
          MESH_LOG("⛔ %s packet for us (no forward)\n", msg.topic);
#endif
          return;
        }
      }
#if MESH_LIB_LOG_ENABLED
      MESH_LOG("↪️ forward: mid=%lu type=%s topic=%s ttl=%d\n",
               (unsigned long)msg.mid, msg.type, msg.topic, msg.ttl);
#endif
#if defined(ARDUINO_ARCH_ESP32)
      uint32_t us = 1000 + (esp_random() % 3000);
#else
      uint32_t us = 1000 + (random() % 3000);
#endif
      delayMicroseconds(us);
#if defined(ARDUINO_ARCH_ESP32)
      esp_err_t r = esp_now_send(BROADCAST_ADDR, (uint8_t*)&msg, sizeof(msg));
      if (r != ESP_OK) {
        MESH_LOG("⚠️ forward send failed: mid=%lu err=%d\n", (unsigned long)msg.mid, r);
      }
#else
      int r = esp_now_send((uint8_t*)BROADCAST_ADDR,
                           (uint8_t*)&msg,
                           (uint8_t)sizeof(msg));
      if (r != 0) {
        MESH_LOG("⚠️ forward send failed: mid=%lu err=%d\n", (unsigned long)msg.mid, r);
      }
#endif
    }
  }
}

// ================== AUTO CMD (DISCOVER) ==================

void MeshLib::_autoHandleCmd(standard_mesh_message &msg) {
  if (_equals(msg.topic, MESH_TOPIC_DISCOVER_GET)) {
    _sendDiscoverPost();
  } else if (_equals(msg.topic, MESH_TOPIC_OTA_START)) {
    _handleOTARequest(msg);
  } else if (_equals(msg.topic, MESH_TOPIC_REBOOT)) {
    _handleRebootRequest(msg);
  }
}

void MeshLib::_sendDiscoverPost() {
  standard_mesh_message resp{};
  resp.ttl = MESH_DEFAULT_TTL;
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

  // MID zostanie nadany w sendMessage()
  (void)_sendMessage(resp);
}

void MeshLib::_handleRebootRequest(const standard_mesh_message &msg) {
  char target_mac[18];
  if (_parseTargetMac(msg.payload, target_mac, sizeof(target_mac)) && _isForUs(target_mac)) {
#if MESH_LIB_LOG_ENABLED
    MESH_LOG("🔄 Reboot command received for us\n");
#endif
    _reboot_pending = true;
  }
}

void MeshLib::_doReboot() {
#if MESH_LIB_LOG_ENABLED
  MESH_LOG("🔄 Rebooting now...\n");
#endif
  delay(500);
  ESP.restart();
}

// ================== POMOCNICZE ==================

void MeshLib::_fillSender(standard_mesh_message &msg) const {
  if (msg.sender[0] != '\0') return; // już ustawione
  String mac = WiFi.macAddress();
  strncpy(msg.sender, mac.c_str(), sizeof(msg.sender)-1);
}


void MeshLib::_fillMid(standard_mesh_message &msg) {
  if (msg.mid != 0) return; // aplikacja mogła sama ustawić MID

  uint32_t mid = 0;
  // losowy MID z ziarna mac+time, unikamy zera
  do {
    mid = MeshLib::rand32();
  } while (mid == 0);

  msg.mid = mid;
}


bool MeshLib::_equals(const char *a, const char *b) {
  if (!a || !b) return false;
  return strcmp(a, b) == 0;
}

bool MeshLib::_parseTargetMac(const char *payload, char *out_mac, size_t mac_size) const {
  if (!payload || !out_mac || mac_size < 18) return false;
  
  const char *mac_start = strstr(payload, "mac=");
  if (!mac_start) return false;
  
  mac_start += 4;
  const char *mac_end = strchr(mac_start, ';');
  if (!mac_end) mac_end = mac_start + strlen(mac_start);
  
  size_t len = mac_end - mac_start;
  if (len >= mac_size) return false;
  
  strncpy(out_mac, mac_start, len);
  out_mac[len] = '\0';
  return true;
}

bool MeshLib::_isForUs(const char *target_mac) const {
  if (!target_mac || target_mac[0] == '\0') return false;
  String our_mac = WiFi.macAddress();
  return _equals(target_mac, our_mac.c_str());
}

// ================== DEDUP PO MID ==================

bool MeshLib::_seenAndRemember(const standard_mesh_message &m) {
  const uint32_t mid = m.mid;

  if (mid == 0) return false; // brak MID -> nie deduplikujemy

  for (int i = 0; i < DEDUP_MAX; ++i) {
    if (_dedup[i].mid == mid) {
      return true; // widziany wcześniej
    }
  }

  // nowy MID – zapisz w buforze cyklicznym
  _dedup[_dedup_idx].mid = mid;
  _dedup_idx = (_dedup_idx + 1) % DEDUP_MAX;
  return false;
}

bool MeshLib::loop() {
  // Execute pending reboot outside of ESP-NOW callback context
  if (_reboot_pending) {
    _reboot_pending = false;
    _doReboot();
    return true;
  }

  // Pick up pending OTA request outside of ESP-NOW callback context
  if (!_ota_mode && _ota_pending) {
    ota_request req = _ota_req; // make a local copy
    _ota_pending = false;
    _enterOTAMode(req.ssid, req.passwd, req.ip);
  }

  // When OTA mode is active, prioritize OTA handling and signal caller
  if (_ota_mode) {
    _handleOTA();
    return true;
  }
  return false;
}
