#include <Arduino.h>
#include <meshLib.h>

// Minimal OTA example modeled after tests/two_node_channel2/esp8266_node.cpp

const char *OTA_SSID   = "MyAP";            // set your SSID
const char *OTA_PASS   = "secret";          // set your password
const char *TARGET_MAC = "AA:BB:CC:DD:EE:FF"; // set target MAC
const char *OTA_IP     = "192.168.100.50";    // optional static IP during OTA

void onMeshReceive(const standard_mesh_message &msg) {
  Serial.print("[RX] mid="); Serial.print(msg.mid);
  Serial.print(" sender="); Serial.print(msg.sender);
  Serial.print(" topic="); Serial.print(msg.topic);
  Serial.print(" payload="); Serial.print(msg.payload);
  Serial.print(" ttl="); Serial.println(msg.ttl);
}

MeshLib mesh(onMeshReceive);

void setup() {
  Serial.begin(115200);
  delay(200);

  mesh.initMesh("ota-node", nullptr, 0, 1);

  delay(500);
  Serial.printf("Sending ota..\n");
  char payload[160];
  snprintf(payload, sizeof(payload), "ssid=%s;passwd=%s;mac=%s;ip=%s", OTA_SSID, OTA_PASS, TARGET_MAC, OTA_IP);
  const bool ok = mesh.sendCmd("ota/start", payload, 3);
  Serial.printf("[TX] ota/start to %s -> %s\n", TARGET_MAC, ok ? "OK" : "FAIL");
}

void loop() {
  if (mesh.loop()) return; // OTA in progress
  delay(10);
}