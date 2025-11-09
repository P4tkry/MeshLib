#include <Arduino.h>
#include <meshLib.h>

static void onMsg(const standard_mesh_message &msg) {
  Serial.printf("[APP] RX type=%s topic=%s from=%s payload='%s' ttl=%d\n",
                msg.type, msg.topic, msg.sender, msg.payload, msg.ttl);
}

const char* SUBS[] = {
  MESH_TOPIC_DISCOVER_POST,
  "led/state"
};

MeshLib mesh(onMsg);

void setup() {
  Serial.begin(115200);
  delay(500);
  mesh.initMesh("node-01", SUBS, 2, 1);
  mesh.sendDiscover(2);
}

void loop() {
  static uint32_t t = 0;
  if (millis() - t > 3000) {
    t = millis();
    standard_mesh_message msg{};
    msg.ttl = 2;
    strncpy(msg.type,  MESH_TYPE_DATA, sizeof(msg.type)-1);
    strncpy(msg.topic, "led/state",    sizeof(msg.topic)-1);
    strncpy(msg.payload, "on",         sizeof(msg.payload)-1);
    mesh.sendMessage(msg);
  }
}
