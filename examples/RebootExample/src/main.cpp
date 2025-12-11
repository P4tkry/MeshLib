#include <Arduino.h>
#include <meshLib.h>

void onMeshReceive(const standard_mesh_message &msg) {
  Serial.printf("[RX] %s %s %s %s\n", msg.type, msg.topic, msg.sender, msg.payload);
}

MeshLib mesh(onMeshReceive);

// Replace with the MAC of the device to reboot
const char* TARGET_MAC = "AA:BB:CC:DD:EE:FF"; // TODO: set real MAC

void setup() {
  Serial.begin(115200);
  mesh.initMesh("reboot-node", nullptr, 0, 1);

  delay(2000);
  Serial.println("Triggering remote reboot...");
  char payload[64];
  snprintf(payload, sizeof(payload), "mac=%s", TARGET_MAC);
  // Send reboot command with TTL=3
  mesh.sendCmd(MESH_TOPIC_REBOOT, payload, 3);
}

void loop() {
  // loop handles pending reboot if this node is the target
  (void)mesh.loop();
}
