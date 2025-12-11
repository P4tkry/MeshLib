#include "meshLib.h"

#if defined(ARDUINO_ARCH_ESP32)
  #include <esp_now.h>
  #include <esp_wifi.h>
#elif defined(ARDUINO_ARCH_ESP8266)
  extern "C" {
    #include <espnow.h>
  }
#endif

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
  #include <ArduinoOTA.h>
#endif

// ================== OTA (internal) ==================

void MeshLib::_enterOTAMode(const char *ssid, const char *passwd, const char *ip) {
  MESH_LOG("📡 Entering OTA mode...\n");
  if (ip && ip[0] != '\0') {
    MESH_LOG("👉 OTA host IP: %s\n", ip);
  }

  _ota_mode = false; // reset until WiFi succeeds

  // Deinitialize ESP-NOW before switching to OTA WiFi
#if defined(ARDUINO_ARCH_ESP32)
  esp_now_deinit();
  esp_wifi_set_promiscuous(false);
  // Disable Wi-Fi power-save for stable OTA throughput (IDF: WIFI_PS_NONE)
  esp_wifi_set_ps(WIFI_PS_NONE);

#elif defined(ARDUINO_ARCH_ESP8266)
  esp_now_deinit();
  // Disable modem sleep during OTA on ESP8266
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif
  
  delay(200);

  // Nie zapisuj nic do NVS przy każdym begin() – mniej problemów
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
  WiFi.persistent(false);
#endif

  // Ustaw czysty tryb stacji
  WiFi.mode(WIFI_STA);

  // Podnieś moc nadawania dla lepszego połączenia
#if defined(ARDUINO_ARCH_ESP32)
  esp_wifi_set_max_tx_power(78); // ~19.5 dBm
#elif defined(ARDUINO_ARCH_ESP8266)
  WiFi.setOutputPower(20.5f);    // maks. moc dla 8266
#endif

  // Krótkie rozłączenie, ale BEZ true
  WiFi.disconnect();      // to jest równoważne disconnect(false)
  delay(100);

  // Jeśli podano IP w żądaniu OTA – ustaw statyczną konfigurację
  if (ip && ip[0] != '\0') {
    IPAddress local;
    if (local.fromString(ip)) {
      IPAddress subnet(255, 255, 255, 0);
      IPAddress gateway(local[0], local[1], local[2], 1);
#if defined(ARDUINO_ARCH_ESP32)
      // ESP32 Arduino: można podać również secondary DNS (tu = gateway)
      WiFi.config(local, gateway, subnet, gateway, gateway);
#else
      // ESP8266 Arduino
      WiFi.config(local, gateway, subnet, gateway);
#endif
      MESH_LOG("🌐 Static IP set: %s, gw=%d.%d.%d.%d\n",
               ip, gateway[0], gateway[1], gateway[2], gateway[3]);
    } else {
      MESH_LOG("⚠️ Invalid static IP string: %s (ignoring)\n", ip);
    }
  }

  // Po prostu łączymy się z nową siecią
  WiFi.begin(ssid, passwd);
  
  MESH_LOG("Connecting to \"%s\" ...", ssid);
  int attempts = 0;
  const int maxAttempts = 50; // ~3.6s
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(300);
    MESH_LOG(".");
    attempts++;
  }
  MESH_LOG("\n");
  
  if (WiFi.status() != WL_CONNECTED) {
    MESH_LOG("✗ WiFi connection failed – staying in mesh mode\n");
    _exitOTAMode();   // przywróć mesh / ESP-NOW
    return;           // nie idziemy dalej
  }

  _ota_mode = true;
  _ota_start_time = millis();

  IPAddress localIP = WiFi.localIP();
  MESH_LOG("✓ WiFi connected! IP: %d.%d.%d.%d\n", localIP[0], localIP[1], localIP[2], localIP[3]);
  
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
  ArduinoOTA.setHostname("MeshNode-OTA");
  ArduinoOTA.onStart([]() {
    MESH_LOG("🔧 OTA Update started!\n");
  });
  ArduinoOTA.onEnd([]() {
    MESH_LOG("✓ OTA Update finished! Restarting...\n");
    delay(1000);
    ESP.restart();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    MESH_LOG("✗ OTA Error[%d]\n", error);
  });
  ArduinoOTA.begin();
  MESH_LOG("⏳ Waiting for OTA upload (5 minutes)...\n");
#endif
}



void MeshLib::_handleOTA() {
  if (!_ota_mode) return;
  
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
  ArduinoOTA.handle();
#endif
  
  // Check timeout
  if (millis() - _ota_start_time > OTA_TIMEOUT_MS) {
    MESH_LOG("⏰ OTA Timeout – returning to mesh mode\n");
    _exitOTAMode();
  }
}

void MeshLib::_exitOTAMode() {
  _ota_mode = false;
  MESH_LOG("🔄 Restarting to mesh mode...\n");
  delay(1000);
  ESP.restart();
}

// ================== OTA REQUEST HANDLER ==================

void MeshLib::_handleOTARequest(const standard_mesh_message &msg) {
  // Parse payload: "ssid=X;passwd=Y;mac=TARGET_MAC"
  
  ota_request req{};
  const char *payload = msg.payload;
  
  // ssid=
  const char *ssid_start = strstr(payload, "ssid=");
  if (ssid_start) {
    ssid_start += 5;
    const char *ssid_end = strchr(ssid_start, ';');
    if (ssid_end) {
      size_t len = ssid_end - ssid_start;
      if (len < sizeof(req.ssid)) {
        strncpy(req.ssid, ssid_start, len);
        req.ssid[len] = '\0';
      }
    }
  }
  
  // passwd=
  const char *passwd_start = strstr(payload, "passwd=");
  if (passwd_start) {
    passwd_start += 7;
    const char *passwd_end = strchr(passwd_start, ';');
    if (passwd_end) {
      size_t len = passwd_end - passwd_start;
      if (len < sizeof(req.passwd)) {
        strncpy(req.passwd, passwd_start, len);
        req.passwd[len] = '\0';
      }
    }
  }
  
  // mac=
  const char *mac_start = strstr(payload, "mac=");
  if (mac_start) {
    mac_start += 4;
    const char *mac_end = strchr(mac_start, ';');
    if (!mac_end) mac_end = mac_start + strlen(mac_start);
    size_t len = mac_end - mac_start;
    if (len < sizeof(req.target_mac)) {
      strncpy(req.target_mac, mac_start, len);
      req.target_mac[len] = '\0';
    }
  }

  // ip=
  const char *ip_start = strstr(payload, "ip=");
  if (ip_start) {
    ip_start += 3;
    const char *ip_end = strchr(ip_start, ';');
    if (!ip_end) ip_end = ip_start + strlen(ip_start);
    size_t len = ip_end - ip_start;
    if (len < sizeof(req.ip)) {
      strncpy(req.ip, ip_start, len);
      req.ip[len] = '\0';
    }
  }
  
  // Sprawdzenie target_mac
  if (_isForUs(req.target_mac)) {
    // To dla nas – sygnalizuj pending OTA do wykonania w loop()
    _ota_req = req;
    _ota_pending = true;
  }
}
