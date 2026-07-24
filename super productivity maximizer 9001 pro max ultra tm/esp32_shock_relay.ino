#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";

#define BATTERY_RELAY_PIN 26    // active-low relay module: LOW closes the relay, HIGH is idle
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
  Serial.print("[recv] window=\"");
  Serial.print(window);
  Serial.println("\"");

  bool unproductive = false;
  for (int i = 0; i < numApps; i++) {
    if (window.indexOf(unproductiveApps[i]) != -1) {
      unproductive = true;
      Serial.print("[match] matched app: ");
      Serial.println(unproductiveApps[i]);
      break;
    }
  }
  if (!unproductive) {
    Serial.println("[match] no match, productive");
  }

  unsigned long now = millis();

  if (window != lastWindow) {
    // app changed, restart the timers
    unproductiveSince = unproductive ? now : 0;
    lastShockTime = 0;
    lastWindow = window;
    Serial.println("[state] window changed, timers reset");
  }

  if (unproductive && unproductiveSince != 0) {
    unsigned long waited = (lastShockTime == 0) ? (now - unproductiveSince) : (now - lastShockTime);
    unsigned long threshold = (lastShockTime == 0) ? DWELL_BEFORE_SHOCK_MS : SHOCK_REPEAT_MS;
    bool readyForShock = waited >= threshold;
    Serial.print("[dwell] waited=");
    Serial.print(waited);
    Serial.print("ms / threshold=");
    Serial.print(threshold);
    Serial.print("ms ready=");
    Serial.println(readyForShock ? "yes" : "no");
    if (readyForShock) {
      // pot knobs are manually pre-set to "on" + desired intensity; the battery
      // relay alone gates whether the unit is powered at all
      Serial.println("[relay] closing battery relay");
      digitalWrite(BATTERY_RELAY_PIN, LOW);   // power on
      delay(SHOCK_DURATION_MS);
      digitalWrite(BATTERY_RELAY_PIN, HIGH);  // power off
      Serial.println("[relay] battery relay reopened");
      lastShockTime = now;
    }
  }

  server.send(200, "text/plain", "ok");
}

void setup() {
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
