#include <Arduino.h>
#include <ESPDash.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

/* WiFi AP Credentials */
const char* ssid = "Esp-thermostat";
const char* password = "";

// Temperature control variables
int setpoint = 0;
int hysteresis = 0;

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
ESPDash dashboard(server);

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

void setup() {
  Serial.begin(115200);

  // Set relays OFF before switching pins to output to avoid a glitch
  digitalWrite(HEATING_PIN, RELAY_OFF);
  pinMode(HEATING_PIN, OUTPUT);

  /* Access point */
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, password);
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());

  dashboard.setAuthentication("esp", "thermostat");

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
    setTemp.setValue(value);
    tempSet.setValue(value);
    dashboard.sendUpdates();
  });

  setHyss.onChange([](int value) {
    Serial.println("[setHyss] " + String(value));
    hysteresis = value;
    setHyss.setValue(value);
    tempHyss.setValue(value);
    dashboard.sendUpdates();
  });

  autoMode.onChange([](int value) {
    Serial.println("[autoMode] " + String(value ? "true" : "false"));
    automaticMode = value;
    autoMode.setValue(value);
    updateModeStatus();
    if (!automaticMode) applyManual();  // return to manual button states
    dashboard.sendUpdates();
  });

  Heater.onChange([](int value) {
    Serial.println("[Heater] " + String(value ? "true" : "false"));
    heaterbtn = value;
    Heater.setValue(value);
    if (!automaticMode) applyManual();
    dashboard.sendUpdates();
  });

  server.begin();
  sensors.begin();
}

void loop() {
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
