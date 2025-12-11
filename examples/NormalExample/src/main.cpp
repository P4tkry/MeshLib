#include <Arduino.h>
#include <meshLib.h>

void onMeshReceive(const standard_mesh_message &msg) {
  Serial.printf("[RX] type=%s topic=%s sender=%s payload=%s ttl=%d mid=%lu\n",
                msg.type, msg.topic, msg.sender, msg.payload, msg.ttl, (unsigned long)msg.mid);
}

MeshLib mesh(onMeshReceive);

void setup() {
  Serial.begin(115200);
  // Start mesh on channel 1, no topic filter
  mesh.initMesh("normal-node", nullptr, 0, 1);
}

void loop() {
  if (mesh.loop()) return; // pause user logic when OTA/reboot active

  static unsigned long last = 0;
  if (millis() - last > 5000) {
    last = millis();
    // Send a simple data message with TTL=3
    mesh.sendMessage("demo/hello", "hi", 3);
  }
}
