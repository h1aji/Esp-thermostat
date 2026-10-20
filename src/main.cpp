#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPDash.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

/* Fallback Access Point credentials, used only when the device cannot    */
/* connect to the configured home WiFi network (or none is configured).  */
const char* apSsid = "Esp-thermostat";
const char* apPassword = "";

// mDNS/OTA hostname: the device is reachable as "thermostat.local"
const char* mdnsName = "thermostat";

// Home WiFi network credentials, loaded from Preferences
String wifiSsid;
String wifiPassword;
bool staConnected = false;           // True if connected to wifiSsid as a station

// Temperature control variables
int setpoint = 0;
int hysteresis = 0;

// Persisted settings
Preferences prefs;

bool restartPending = false;
unsigned long restartAt = 0;

// Pin definitions
const int HEATING_PIN = 15;
#define ONE_WIRE_BUS 2

// Relays are active LOW (LOW = ON, HIGH = OFF)
const int RELAY_ON = LOW;
const int RELAY_OFF = HIGH;

// Temperature sensor
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// State
bool automaticMode = false;
bool heaterbtn = false;
bool heaterOn = false;

const unsigned long CONTROL_INTERVAL_MS = 3000;
unsigned long lastControl = 0;

AsyncWebServer server(80);
// ESPDash's websocket is hardcoded to "/dashws" internally, regardless of the
// uri given here, so only ONE ESPDash instance can ever work on a given
// server. The dashboard therefore lives at "/overview"; "/" picks the right
// page to send you to (see handleRoot below), and "/settings" is a plain
// hand-built page since ESPDash has no text-input card.
ESPDash dashboard(server, "/overview", true);

dash::TemperatureCard temperature(dashboard, "Current Temperature", "°C");
dash::TemperatureCard tempSet(dashboard, "Temperature Set Point", "°C");
dash::TemperatureCard tempHyss(dashboard, "Hysteresis Set Point", "°C");

dash::SliderCard setTemp(dashboard, "Set Temperature", 0, 50, 1, "°C");
dash::SliderCard setHyss(dashboard, "Set Hysteresis", 0, 5, 1, "°C");

dash::ToggleButtonCard autoMode(dashboard, "Auto Mode");
dash::FeedbackCard modeStatus(dashboard, "Auto Mode Status");

dash::ToggleButtonCard Heater(dashboard, "Heater");

// Only touches the pin / logs when the state actually changes
void setHeater(bool on) {
  if (on == heaterOn) return;
  heaterOn = on;
  digitalWrite(HEATING_PIN, on ? RELAY_ON : RELAY_OFF);
  Serial.println(on ? "Heater ON" : "Heater OFF");
}

void updateModeStatus() {
  if (automaticMode) {
    modeStatus.setFeedback("Enabled", dash::Status::SUCCESS);
  } else {
    modeStatus.setFeedback("Disabled", dash::Status::WARNING);
  }
}

// Applies manual button states (only when not in auto mode)
void applyManual() {
  setHeater(heaterbtn);
}

// Try to join the configured home WiFi network as a station.
// Returns false if no credentials are saved yet, or the connection times out.
bool connectToWiFi() {
  if (wifiSsid.length() == 0) {
    Serial.println("No WiFi credentials configured");
    return false;
  }
  Serial.print("Connecting to WiFi SSID: ");
  Serial.println(wifiSsid);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(mdnsName);  // DHCP hostname (what the router shows), set after mode(), before begin()
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

// Fall back to our own Access Point so the device stays reachable and the
// user can enter working WiFi credentials via the /settings page.
void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSsid, apPassword);
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());
}

// Minimal HTML-escaping for values reflected back into handleSettingsPageGet's markup
// (wifiSsid is user-controlled input, persisted and redisplayed on later page loads).
String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    switch (s[i]) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += s[i];
    }
  }
  return out;
}

// Serves a small standalone Settings page (ESPDash has no text-input card)
// for entering/changing the home WiFi SSID and password.
void handleSettingsPageGet(AsyncWebServerRequest* request) {
  String status = staConnected
    ? ("Connected to <b>" + htmlEscape(WiFi.SSID()) + "</b>, IP " + WiFi.localIP().toString())
    : ("Running as Access Point <b>" + String(apSsid) + "</b> (could not connect to a WiFi network)");

  String html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Settings</title>"
    "<style>body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 16px}"
    "nav a{margin-right:16px}"
    "label{display:block;margin-top:12px;font-weight:bold}"
    "input{width:100%;padding:8px;margin-top:4px;box-sizing:border-box}"
    "button{margin-top:20px;padding:10px 16px}"
    ".status{padding:10px;border-radius:4px;background:#eee}</style></head><body>"
    "<nav><a href='/overview'>Overview</a><a href='/settings'>Settings</a><a href='/about'>About</a></nav>"
    "<h1>Settings</h1>"
    "<h2>WiFi</h2>"
    "<div class='status'>" + status + "</div>"
    "<form id='f'>"
    "<label for='ssid'>SSID</label>"
    "<input type='text' id='ssid' name='ssid' value='" + htmlEscape(wifiSsid) + "' required>"
    "<label for='password'>Password</label>"
    "<input type='password' id='password' name='password'>"
    "<p><small>Leave password blank for an open network.</small></p>"
    "<button type='submit'>Save &amp; Restart</button>"
    "</form><p id='result'></p>"
    "<script>"
    "document.getElementById('f').addEventListener('submit', function(e){"
    "e.preventDefault();"
    "var data=new URLSearchParams();"
    "data.append('ssid', document.getElementById('ssid').value);"
    "data.append('password', document.getElementById('password').value);"
    "fetch('/settings',{method:'POST',body:data}).then(r=>r.text()).then(t=>{"
    "document.getElementById('result').textContent=t;});"
    "});"
    "</script></body></html>";

  request->send(200, "text/html", html);
}

void handleSettingsPagePost(AsyncWebServerRequest* request) {
  if (!request->hasParam("ssid", true) || request->getParam("ssid", true)->value().length() == 0) {
    request->send(400, "text/plain", "SSID is required");
    return;
  }
  wifiSsid = request->getParam("ssid", true)->value();
  wifiPassword = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";
  prefs.putString("wifi_ssid", wifiSsid);
  prefs.putString("wifi_pass", wifiPassword);
  request->send(200, "text/plain", "Saved. Restarting...");
  restartPending = true;
  restartAt = millis() + 1000;  // Give the async server time to flush the response
}

// "/" always exists, regardless of whether WiFi is set up yet, so it's the
// one URL worth memorizing: it sends you to Settings when the device needs
// attention, or the Overview dashboard once it's actually on the network.
void handleRoot(AsyncWebServerRequest* request) {
  request->redirect(staConnected ? "/overview" : "/settings");
}

String formatUptime() {
  unsigned long s = millis() / 1000;
  char buf[24];
  snprintf(buf, sizeof(buf), "%lud %luh %lum %lus", s / 86400, (s / 3600) % 24, (s / 60) % 60, s % 60);
  return String(buf);
}

// Static info page: no live data, so no ESPDash/websocket involvement needed.
void handleAboutPageGet(AsyncWebServerRequest* request) {
  char chipId[9];
  snprintf(chipId, sizeof(chipId), "%08X", ESP.getChipId());

  String html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>About</title>"
    "<style>body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 16px}"
    "nav a{margin-right:16px}"
    "table{border-collapse:collapse;margin-top:12px}"
    "td{padding:4px 12px 4px 0;vertical-align:top}"
    "td:first-child{font-weight:bold;white-space:nowrap}</style></head><body>"
    "<nav><a href='/overview'>Overview</a><a href='/settings'>Settings</a><a href='/about'>About</a></nav>"
    "<h1>About</h1>"
    "<p><b>Esp-thermostat</b> &mdash; WiFi thermostat based on ESP8266 and a DS18B20 temperature sensor.</p>"
    "<table>"
    "<tr><td>Firmware built</td><td>" __DATE__ " " __TIME__ "</td></tr>"
    "<tr><td>Uptime</td><td>" + formatUptime() + "</td></tr>"
    "<tr><td>Chip ID</td><td>" + String(chipId) + "</td></tr>"
    "</table>"
    "<p><a href='https://github.com/h1aji/Esp-thermostat' target='_blank'>Source on GitHub</a></p>"
    "<p><a href='https://iotprojectsideas.com/temperature-control-with-esp8266-asyncwebserver/' target='_blank'>Original project article</a></p>"
    "</body></html>";

  request->send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);

  // Set relays OFF before switching pins to output to avoid a glitch
  digitalWrite(HEATING_PIN, RELAY_OFF);
  pinMode(HEATING_PIN, OUTPUT);

  // Load persisted settings (falls back to the current defaults if not yet saved)
  prefs.begin("thermostat");
  setpoint = prefs.getInt("setpoint", setpoint);
  hysteresis = prefs.getInt("hysteresis", hysteresis);
  automaticMode = prefs.getBool("autoMode", automaticMode);
  heaterbtn = prefs.getBool("heaterBtn", heaterbtn);
  wifiSsid = prefs.getString("wifi_ssid", "");
  wifiPassword = prefs.getString("wifi_pass", "");

  /* Connect to the configured home WiFi network; if that's not configured  */
  /* or fails, fall back to our own Access Point so the device is always   */
  /* reachable and the settings can be fixed via the /settings page.       */
  staConnected = connectToWiFi();
  if (staConnected) {
    Serial.print("Connected! IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Could not connect to WiFi, starting fallback AP");
    startAP();
  }

  /* OTA (Over-The-Air) firmware updates. ArduinoOTA.begin() also starts the */
  /* mDNS responder (and ArduinoOTA.handle() keeps it updated), so the      */
  /* device becomes reachable as "thermostat.local" as a side effect.      */
  ArduinoOTA.setHostname(mdnsName);
  ArduinoOTA.onStart([]() {
    // Flashing pauses the main loop for several seconds; make sure the
    // heater relay isn't left on for the duration.
    setHeater(false);
    Serial.println("OTA update starting");
  });
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);  // Advertise the web dashboard/settings pages

  dashboard.setAuthentication("esp", "thermostat");

  // Pages: "/" routes to whichever of these actually matters right now
  server.on("/", HTTP_GET, handleRoot);
  server.on("/settings", HTTP_GET, handleSettingsPageGet);
  server.on("/settings", HTTP_POST, handleSettingsPagePost);
  server.on("/about", HTTP_GET, handleAboutPageGet);

  // Initial card values
  setTemp.setValue(setpoint);
  setHyss.setValue(hysteresis);
  tempSet.setValue(setpoint);
  tempHyss.setValue(hysteresis);
  autoMode.setValue(automaticMode);
  Heater.setValue(heaterbtn);
  updateModeStatus();

  // Callbacks are registered ONCE here, not in loop()
  setTemp.onChange([](int value) {
    Serial.println("[setTemp] " + String(value));
    setpoint = value;
    prefs.putInt("setpoint", value);
    setTemp.setValue(value);
    tempSet.setValue(value);
    dashboard.sendUpdates();
  });

  setHyss.onChange([](int value) {
    Serial.println("[setHyss] " + String(value));
    hysteresis = value;
    prefs.putInt("hysteresis", value);
    setHyss.setValue(value);
    tempHyss.setValue(value);
    dashboard.sendUpdates();
  });

  autoMode.onChange([](int value) {
    Serial.println("[autoMode] " + String(value ? "true" : "false"));
    automaticMode = value;
    prefs.putBool("autoMode", automaticMode);
    autoMode.setValue(value);
    updateModeStatus();
    if (!automaticMode) applyManual();  // return to manual button states
    dashboard.sendUpdates();
  });

  Heater.onChange([](int value) {
    Serial.println("[Heater] " + String(value ? "true" : "false"));
    heaterbtn = value;
    prefs.putBool("heaterBtn", heaterbtn);
    Heater.setValue(value);
    if (!automaticMode) applyManual();
    dashboard.sendUpdates();
  });

  server.begin();
  sensors.begin();
}

void loop() {
  ArduinoOTA.handle();  // Must run every iteration, not just every CONTROL_INTERVAL_MS

  if (restartPending && millis() >= restartAt) {
    ESP.restart();
  }

  // Non-blocking timer instead of delay(3000) so the web UI stays responsive
  if (millis() - lastControl < CONTROL_INTERVAL_MS) return;
  lastControl = millis();

  sensors.requestTemperatures();
  float currentTemp = sensors.getTempCByIndex(0);

  if (currentTemp == DEVICE_DISCONNECTED_C) {
    Serial.println("Sensor error! Turning outputs OFF");
    setHeater(false);
    return;
  }

  temperature.setValue((int)round(currentTemp));

  if (automaticMode) {
    if (currentTemp > setpoint + hysteresis) {
      setHeater(false);
    } else if (currentTemp < setpoint - hysteresis) {
      setHeater(true);
    } else {
      setHeater(false);
    }
  } else {
    applyManual();
  }

  dashboard.sendUpdates();
}
