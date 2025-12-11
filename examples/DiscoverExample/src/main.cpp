#include <Arduino.h>
#include <meshLib.h>

void onMeshReceive(const standard_mesh_message &msg) {
  if (strcmp(msg.type, MESH_TYPE_CMD) == 0 && strcmp(msg.topic, MESH_TOPIC_DISCOVER_POST) == 0) {
    // Expected payload: name=<n>;mac=<m>;chip=<esp32|esp8266>;channel=<ch>
    Serial.printf("[DISCOVER_POST] %s\n", msg.payload);
  } else {
    Serial.printf("[RX] %s %s %s %s\n", msg.type, msg.topic, msg.sender, msg.payload);
  }
}

MeshLib mesh(onMeshReceive);

void setup() {
  Serial.begin(115200);
  mesh.initMesh("discover-node", nullptr, 0, 1);

  delay(1000);
  Serial.println("Sending discover/get ...");
  // Broadcast discover with TTL=4 (default if pass <=0)
  mesh.sendDiscover(4);
}

void loop() {
  // Handle any internal work (not required for discover, but good practice)
  (void)mesh.loop();
}
