#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";

#define RELAY_PIN 26
#define SHOCK_DURATION_MS 200
#define COOLDOWN_MS 30000   // don't re-shock same app for 30s

WebServer server(80);

const char* unproductiveApps[] = {
  "discord",
  "steam",
  "among us",
  "geometry dash",
  "grand theft auto v enhanced",
  "learn to fly 3",
  "raft",
  "stardew valley",
  "totally accurate battle simulator",
  "worldbox",
  "sober"
};
const int numApps = sizeof(unproductiveApps) / sizeof(unproductiveApps[0]);

unsigned long lastShockTime = 0;
String lastWindow = "";

void handleWindow() {
  String window = server.arg("plain");
  window.toLowerCase();

  bool unproductive = false;
  for (int i = 0; i < numApps; i++) {
    if (window.indexOf(unproductiveApps[i]) != -1) {
      unproductive = true;
      break;
    }
  }

  unsigned long now = millis();
  if (unproductive) {
    bool changedApp = (window != lastWindow);
    bool cooldownPassed = (now - lastShockTime) > COOLDOWN_MS;
    if (changedApp || cooldownPassed) {
      digitalWrite(RELAY_PIN, HIGH);
      delay(SHOCK_DURATION_MS);
      digitalWrite(RELAY_PIN, LOW);
      lastShockTime = now;
    }
  }
  lastWindow = window;
  server.send(200, "text/plain", "ok");
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.localIP());

  server.on("/window", HTTP_POST, handleWindow);
  server.begin();
}

void loop() {
  server.handleClient();
}
