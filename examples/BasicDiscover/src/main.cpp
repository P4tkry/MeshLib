#include <Arduino.h>
#include <meshLib.h>

// ==================== CALLBACK ====================
// Wywoływany przy każdej odebranej wiadomości mesh
void onMeshReceive(const standard_mesh_message &msg) {
    Serial.print("[RX]  mid=");
    Serial.print(msg.mid);
    Serial.print("  sender=");
    Serial.print(msg.sender);
    Serial.print("  type=");
    Serial.print(msg.type);
    Serial.print("  topic=");
    Serial.print(msg.topic);
    Serial.print("  payload=");
    Serial.print(msg.payload);
    Serial.print("  ttl=");
    Serial.println(msg.ttl);
}

// ==================== MESH ====================
MeshLib mesh(onMeshReceive);

unsigned long lastSend = 0;

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n=== Basic Mesh Test ===");

    // Nie subskrybujemy nic → odbieramy wszystko
    mesh.initMesh("Node", nullptr, 0, 1);

    Serial.println("[INIT] Mesh ready");
}

void loop() {
    unsigned long now = millis();

    // Co 5 sekund nadajemy testową wiadomość
    if (now - lastSend > 5000) {
        lastSend = now;

        standard_mesh_message m{};
        m.ttl = 3;  // coś do przetestowania forwarding
        strncpy(m.type,  "data", sizeof(m.type));
        strncpy(m.topic, "test/hello", sizeof(m.topic));
        strncpy(m.payload, "Hello from node!", sizeof(m.payload));

        bool ok = mesh.sendMessage(m);

        Serial.print("[TX] ");
        Serial.print(ok ? "Sent OK" : "Send FAIL");
        Serial.print("  (MID assigned automatically)\n");
    }
}
