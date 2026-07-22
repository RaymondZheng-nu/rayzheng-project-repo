#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";

#define RELAY_PIN 26           // active-low relay module: LOW closes the relay, HIGH is idle
#define BATTERY_RELAY_PIN 27
#define SHOCK_DURATION_MS 4500 // shorter and repeat shocks don't reliably register (cold-boots the TENS unit each time)
#define DWELL_BEFORE_SHOCK_MS 5000
#define SHOCK_REPEAT_MS 5000

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

unsigned long lastShockTime = 0;      // 0 = no shock yet this streak
unsigned long unproductiveSince = 0;  // 0 = not currently in an unproductive streak
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

  if (window != lastWindow) {
    // app changed, restart the timers
    unproductiveSince = unproductive ? now : 0;
    lastShockTime = 0;
    lastWindow = window;
  }

  if (unproductive && unproductiveSince != 0) {
    bool readyForShock = (lastShockTime == 0)
      ? (now - unproductiveSince) >= DWELL_BEFORE_SHOCK_MS
      : (now - lastShockTime) >= SHOCK_REPEAT_MS;
    if (readyForShock) {
      digitalWrite(RELAY_PIN, LOW);
      digitalWrite(BATTERY_RELAY_PIN, LOW);
      delay(SHOCK_DURATION_MS);
      digitalWrite(RELAY_PIN, HIGH);
      digitalWrite(BATTERY_RELAY_PIN, HIGH);
      lastShockTime = now;
    }
  }

  server.send(200, "text/plain", "ok");
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  pinMode(BATTERY_RELAY_PIN, OUTPUT);
  digitalWrite(BATTERY_RELAY_PIN, HIGH);

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
