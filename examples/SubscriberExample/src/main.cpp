#include <Arduino.h>
#include <meshLib.h>

void onMeshReceive(const standard_mesh_message &msg) {
  // This callback will be invoked only for subscribed topics
  Serial.printf("[SUB RX] %s %s %s %s\n", msg.type, msg.topic, msg.sender, msg.payload);
}

// Subscribe to a single topic
const char* SUB_TOPICS[] = { "example/topic" };

MeshLib mesh(onMeshReceive);

void setup() {
  Serial.begin(115200);
  // Pass subscription list to initMesh; only matching topics reach the callback
  mesh.initMesh("subscriber-node", SUB_TOPICS, 1, 1);

  // Optionally send a message on same topic to see it locally and across the mesh
  delay(1000);
  mesh.sendMessage("/example/topic", "hello from subscriber", 3);
}

void loop() {
  (void)mesh.loop();
}
