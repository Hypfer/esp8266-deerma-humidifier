#include "types.h"
#include "wifi.h"

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
//#include <SoftwareSerial.h>
#include <ArduinoOTA.h>

humidifierState_t state;

char serialRxBuf[255];
char serialTxBuf[255];
uint8_t mqttRetryCounter = 0;



WiFiManager wifiManager;
WiFiClient wifiClient;
PubSubClient mqttClient;

WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, sizeof(mqtt_server));
WiFiManagerParameter custom_mqtt_user("user", "MQTT username", username, sizeof(username));
WiFiManagerParameter custom_mqtt_pass("pass", "MQTT password", password, sizeof(password));

unsigned long lastMqttConnectionAttempt = millis();
const long mqttConnectionInterval = 60000;

unsigned long stateUpdatePreviousMillis = millis();
const long stateUpdateInterval = 30000;

char identifier[24];
#define FIRMWARE_PREFIX "esp8266-deerma-humidifier"
#define AVAILABILITY_ONLINE "online"
#define AVAILABILITY_OFFLINE "offline"
char MQTT_TOPIC_AVAILABILITY[128];
char MQTT_TOPIC_STATE[128];
char MQTT_TOPIC_COMMAND[128];


char MQTT_TOPIC_AUTOCONF_HUMIDITY_SENSOR[128];
char MQTT_TOPIC_AUTOCONF_TEMPERATURE_SENSOR[128];
char MQTT_TOPIC_AUTOCONF_WIFI_SENSOR[128];
char MQTT_TOPIC_AUTOCONF_WATER_TANK_SENSOR[128];

//TODO: replace when HA gains a better mqtt component for these
char MQTT_TOPIC_AUTOCONF_MODE_SENSOR[128];
char MQTT_TOPIC_AUTOCONF_HUMIDITY_SETPOINT_SENSOR[128];

char MQTT_TOPIC_AUTOCONF_POWER_SWITCH[128];
char MQTT_TOPIC_AUTOCONF_SOUND_SWITCH[128];
char MQTT_TOPIC_AUTOCONF_LED_SWITCH[128];


bool shouldSaveConfig = false;

void saveConfigCallback () {
  shouldSaveConfig = true;
}

void setup() {
  Serial.begin(115200);
  Serial.swap();

  delay(1500);
  sendNetworkStatus(false);


  snprintf(identifier, sizeof(identifier), "HUMIDIFIER-%X", ESP.getChipId());
  snprintf(MQTT_TOPIC_AVAILABILITY, 127, "%s/%s/status", FIRMWARE_PREFIX, identifier);
  snprintf(MQTT_TOPIC_STATE, 127, "%s/%s/state", FIRMWARE_PREFIX, identifier);
  snprintf(MQTT_TOPIC_COMMAND, 127, "%s/%s/command", FIRMWARE_PREFIX, identifier);

  snprintf(MQTT_TOPIC_AUTOCONF_HUMIDITY_SENSOR, 127, "homeassistant/sensor/%s/%s_humidity/config", FIRMWARE_PREFIX, identifier);
  snprintf(MQTT_TOPIC_AUTOCONF_TEMPERATURE_SENSOR, 127, "homeassistant/sensor/%s/%s_temperature/config", FIRMWARE_PREFIX, identifier);
  snprintf(MQTT_TOPIC_AUTOCONF_WIFI_SENSOR, 127, "homeassistant/sensor/%s/%s_wifi/config", FIRMWARE_PREFIX, identifier);
  snprintf(MQTT_TOPIC_AUTOCONF_WATER_TANK_SENSOR, 127, "homeassistant/sensor/%s/%s_water_tank/config", FIRMWARE_PREFIX, identifier);

  snprintf(MQTT_TOPIC_AUTOCONF_MODE_SENSOR, 127, "homeassistant/sensor/%s/%s_mode/config", FIRMWARE_PREFIX, identifier);
  snprintf(MQTT_TOPIC_AUTOCONF_HUMIDITY_SETPOINT_SENSOR, 127, "homeassistant/sensor/%s/%s_humidity_setpoint/config", FIRMWARE_PREFIX, identifier);

  snprintf(MQTT_TOPIC_AUTOCONF_POWER_SWITCH, 127, "homeassistant/switch/%s/%s_power/config", FIRMWARE_PREFIX, identifier);
  snprintf(MQTT_TOPIC_AUTOCONF_SOUND_SWITCH, 127, "homeassistant/switch/%s/%s_sound/config", FIRMWARE_PREFIX, identifier);
  snprintf(MQTT_TOPIC_AUTOCONF_LED_SWITCH, 127, "homeassistant/switch/%s/%s_led/config", FIRMWARE_PREFIX, identifier);



  WiFi.hostname(identifier);

  loadConfig();

  setupWifi();
  setupOTA();
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setKeepAlive(10);
  mqttClient.setBufferSize(2048);
  mqttClient.setCallback(mqttCallback);

  mqttReconnect();
}

void setupOTA() {

  ArduinoOTA.onStart([]() {
    //Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    //Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    /*
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
    */
  });

  ArduinoOTA.setHostname(identifier);

  //This is less of a security measure and more a accidential flash prevention
  ArduinoOTA.setPassword(identifier);
  ArduinoOTA.begin();
}


void loop() {
  ArduinoOTA.handle();
  handleUart();
  mqttClient.loop();

  if (!mqttClient.connected() && (mqttConnectionInterval <= (millis() - lastMqttConnectionAttempt)) )  {
    lastMqttConnectionAttempt = millis();
    mqttReconnect();
  }

  if (stateUpdateInterval <= (millis() - stateUpdatePreviousMillis)) {
    publishState();
  }
}

void setupWifi() {
  wifiManager.setDebugOutput(false);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);

  WiFi.hostname(identifier);
  wifiManager.autoConnect(identifier);
  mqttClient.setClient(wifiClient);

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(username, custom_mqtt_user.getValue());
  strcpy(password, custom_mqtt_pass.getValue());

  if (shouldSaveConfig) {
    saveConfig();
  } else {
    //For some reason, the read values get overwritten in this function
    //To combat this, we just reload the config
    //This is most likely a logic error which could be fixed otherwise
    loadConfig();
  }
}

void resetWifiSettingsAndReboot() {
  wifiManager.resetSettings();
  sendNetworkStatus(false);
  delay(3000);
  ESP.restart();
}

void mqttReconnect()
{
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (mqttClient.connect(identifier, username, password, MQTT_TOPIC_AVAILABILITY, 1, true, AVAILABILITY_OFFLINE)) {
      mqttClient.publish(MQTT_TOPIC_AVAILABILITY, AVAILABILITY_ONLINE, true);
      sendNetworkStatus(true);
      publishAutoConfig();

      mqttClient.subscribe(MQTT_TOPIC_COMMAND);
      break;
    } else {
      delay(5000);
    }
  }
}

boolean isMqttConnected() {
  return mqttClient.connected();
}

void publishState() {
  DynamicJsonDocument wifiJson(192);
  DynamicJsonDocument stateJson(604);
  char payload[256];


  wifiJson["ssid"] = WiFi.SSID();
  wifiJson["ip"] = WiFi.localIP().toString();
  wifiJson["rssi"] = WiFi.RSSI();

  stateJson["state"] = state.powerOn ? "on" : "off";

  switch (state.mode) {
    case (humMode_t)setpoint:
      stateJson["mode"] = "setpoint";
      break;
    case (humMode_t)low:
      stateJson["mode"] = "low";
      break;
    case (humMode_t)medium:
      stateJson["mode"] = "medium";
      break;
    case (humMode_t)high:
      stateJson["mode"] = "high";
      break;
  }

  stateJson["humiditySetpoint"] = state.humiditySetpoint;

  stateJson["humidity"] = state.currentHumidity;
  stateJson["temperature"] = state.currentTemperature;

  stateJson["sound"] = state.soundEnabled ? "on" : "off";
  stateJson["led"] = state.ledEnabled ? "on" : "off";

  stateJson["waterTank"] = state.waterTankInstalled ? (state.waterTankEmpty ? "empty" : "full") : "missing";

  stateJson["wifi"] = wifiJson.as<JsonObject>();

  serializeJson(stateJson, payload);
  mqttClient.publish(MQTT_TOPIC_STATE, payload, true);

  stateUpdatePreviousMillis = millis();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, MQTT_TOPIC_COMMAND) == 0) {
    DynamicJsonDocument commandJson(256);
    char payloadText[length + 1];

    snprintf(payloadText, length + 1, "%s", payload);

    DeserializationError err = deserializeJson(commandJson, payloadText);

    if (!err) {
      String stateCommand = commandJson["state"].as<String>();
      String modeCommand = commandJson["mode"].as<String>();
      String soundCommand = commandJson["sound"].as<String>();
      String ledCommand = commandJson["led"].as<String>();

      long humiditySetpointCommand = commandJson["humiditySetpoint"] | -1;


      String rebootCommand = commandJson["__reboot"].as<String>();

      if (stateCommand == "off") {
        setPowerState(false);
      } else if (stateCommand == "on") {
        setPowerState(true);
      }

      if (modeCommand == "low") {
        setHumMode((humMode_t)low);
      } else if (modeCommand == "medium") {
        setHumMode((humMode_t)medium);
      } else if (modeCommand == "high") {
        setHumMode((humMode_t)high);
      } else if (modeCommand == "setpoint") {
        setHumMode((humMode_t)setpoint);
      }

      if (soundCommand == "off") {
        setSoundState(false);
      } else if (soundCommand == "on") {
        setSoundState(true);
      }

      if (ledCommand == "off") {
        setLEDState(false);
      } else if (ledCommand == "on") {
        setLEDState(true);
      }

      if (humiditySetpointCommand > -1) {
        setHumiditySetpoint((uint8_t)humiditySetpointCommand);
      }

      if(rebootCommand == "reboot__") {
        ESP.restart();
      }
    }
  }
}

void publishAutoConfig() {
  char mqttPayload[2048];
  DynamicJsonDocument device(256);
  DynamicJsonDocument autoconfPayload(1024);
  StaticJsonDocument<64> identifiersDoc;
  JsonArray identifiers = identifiersDoc.to<JsonArray>();

  identifiers.add(identifier);

  device["identifiers"] = identifiers;
  device["manufacturer"] = "Foshan Shunde Deerma Electric Appliance Co., Ltd.";
  device["model"] = "Mi Smart Antibacterial Humidifier";
  device["name"] = identifier;
  device["sw_version"] = "2021.01.1";




  autoconfPayload["device"] = device.as<JsonObject>();
  autoconfPayload["availability_topic"] = MQTT_TOPIC_AVAILABILITY;
  autoconfPayload["state_topic"] = MQTT_TOPIC_STATE;
  autoconfPayload["name"] = identifier + String(" Humidity");
  autoconfPayload["device_class"] = "humidity";
  autoconfPayload["unit_of_measurement"] = "%";
  autoconfPayload["value_template"] = "{{value_json.humidity}}";
  autoconfPayload["unique_id"] = identifier + String("_humidity");

  serializeJson(autoconfPayload, mqttPayload);
  mqttClient.publish(MQTT_TOPIC_AUTOCONF_HUMIDITY_SENSOR, mqttPayload, true);

  autoconfPayload.clear();

  autoconfPayload["device"] = device.as<JsonObject>();
  autoconfPayload["availability_topic"] = MQTT_TOPIC_AVAILABILITY;
  autoconfPayload["state_topic"] = MQTT_TOPIC_STATE;
  autoconfPayload["name"] = identifier + String(" Temperature");
  autoconfPayload["device_class"] = "temperature";
  autoconfPayload["unit_of_measurement"] = "°C";
  autoconfPayload["value_template"] = "{{value_json.temperature}}";
  autoconfPayload["unique_id"] = identifier + String("_temperature");

  serializeJson(autoconfPayload, mqttPayload);
  mqttClient.publish(MQTT_TOPIC_AUTOCONF_TEMPERATURE_SENSOR, mqttPayload, true);

  autoconfPayload.clear();

  autoconfPayload["device"] = device.as<JsonObject>();
  autoconfPayload["availability_topic"] = MQTT_TOPIC_AVAILABILITY;
  autoconfPayload["state_topic"] = MQTT_TOPIC_STATE;
  autoconfPayload["name"] = identifier + String(" WiFi");
  autoconfPayload["value_template"] = "{{value_json.wifi.rssi}}";
  autoconfPayload["unique_id"] = identifier + String("_wifi");
  autoconfPayload["unit_of_measurement"] = "dBm";
  autoconfPayload["json_attributes_topic"] = MQTT_TOPIC_STATE;
  autoconfPayload["json_attributes_template"] = "{\"ssid\": \"{{value_json.wifi.ssid}}\", \"ip\": \"{{value_json.wifi.ip}}\"}";
  autoconfPayload["icon"] = "mdi:wifi";

  serializeJson(autoconfPayload, mqttPayload);
  mqttClient.publish(MQTT_TOPIC_AUTOCONF_WIFI_SENSOR, mqttPayload, true);

  autoconfPayload.clear();

  autoconfPayload["device"] = device.as<JsonObject>();
  autoconfPayload["availability_topic"] = MQTT_TOPIC_AVAILABILITY;
  autoconfPayload["state_topic"] = MQTT_TOPIC_STATE;
  autoconfPayload["name"] = identifier + String(" Water Tank");
  autoconfPayload["value_template"] = "{{value_json.waterTank}}";
  autoconfPayload["unique_id"] = identifier + String("_water_tank");
  autoconfPayload["icon"] = "mdi:cup-water";

  serializeJson(autoconfPayload, mqttPayload);
  mqttClient.publish(MQTT_TOPIC_AUTOCONF_WATER_TANK_SENSOR, mqttPayload, true);

  autoconfPayload.clear();


  autoconfPayload["device"] = device.as<JsonObject>();
  autoconfPayload["availability_topic"] = MQTT_TOPIC_AVAILABILITY;
  autoconfPayload["state_topic"] = MQTT_TOPIC_STATE;
  autoconfPayload["name"] = identifier + String(" Mode");
  autoconfPayload["value_template"] = "{{value_json.mode}}";
  autoconfPayload["unique_id"] = identifier + String("_mode");
  autoconfPayload["icon"] = "mdi:fan";

  serializeJson(autoconfPayload, mqttPayload);
  mqttClient.publish(MQTT_TOPIC_AUTOCONF_MODE_SENSOR, mqttPayload, true);

  autoconfPayload.clear();

  autoconfPayload["device"] = device.as<JsonObject>();
  autoconfPayload["availability_topic"] = MQTT_TOPIC_AVAILABILITY;
  autoconfPayload["state_topic"] = MQTT_TOPIC_STATE;
  autoconfPayload["name"] = identifier + String(" Humidity Setpoint");
  autoconfPayload["value_template"] = "{{value_json.humiditySetpoint}}";
  autoconfPayload["unit_of_measurement"] = "%";
  autoconfPayload["unique_id"] = identifier + String("_humidity_setpoint");
  autoconfPayload["icon"] = "mdi:water-check";

  serializeJson(autoconfPayload, mqttPayload);
  mqttClient.publish(MQTT_TOPIC_AUTOCONF_HUMIDITY_SETPOINT_SENSOR, mqttPayload, true);

  autoconfPayload.clear();




  autoconfPayload["device"] = device.as<JsonObject>();
  autoconfPayload["availability_topic"] = MQTT_TOPIC_AVAILABILITY;
  autoconfPayload["state_topic"] = MQTT_TOPIC_STATE;
  autoconfPayload["command_topic"] = MQTT_TOPIC_COMMAND;
  autoconfPayload["name"] = identifier + String(" Power");
  autoconfPayload["value_template"] = "{{value_json.state}}";
  autoconfPayload["unique_id"] = identifier + String("_power");
  autoconfPayload["payload_on"] = "{\"state\": \"on\"}";
  autoconfPayload["payload_off"] = "{\"state\": \"off\"}";
  autoconfPayload["state_on"] = "on";
  autoconfPayload["state_off"] = "off";
  autoconfPayload["icon"] = "mdi:power";

  serializeJson(autoconfPayload, mqttPayload);
  mqttClient.publish(MQTT_TOPIC_AUTOCONF_POWER_SWITCH, mqttPayload, true);

  autoconfPayload.clear();

  autoconfPayload["device"] = device.as<JsonObject>();
  autoconfPayload["availability_topic"] = MQTT_TOPIC_AVAILABILITY;
  autoconfPayload["state_topic"] = MQTT_TOPIC_STATE;
  autoconfPayload["command_topic"] = MQTT_TOPIC_COMMAND;
  autoconfPayload["name"] = identifier + String(" Sound");
  autoconfPayload["value_template"] = "{{value_json.sound}}";
  autoconfPayload["unique_id"] = identifier + String("_sound");
  autoconfPayload["payload_on"] = "{\"sound\": \"on\"}";
  autoconfPayload["payload_off"] = "{\"sound\": \"off\"}";
  autoconfPayload["state_on"] = "on";
  autoconfPayload["state_off"] = "off";
  autoconfPayload["icon"] = "mdi:volume-high";

  serializeJson(autoconfPayload, mqttPayload);
  mqttClient.publish(MQTT_TOPIC_AUTOCONF_SOUND_SWITCH, mqttPayload, true);

  autoconfPayload.clear();


  autoconfPayload["device"] = device.as<JsonObject>();
  autoconfPayload["availability_topic"] = MQTT_TOPIC_AVAILABILITY;
  autoconfPayload["state_topic"] = MQTT_TOPIC_STATE;
  autoconfPayload["command_topic"] = MQTT_TOPIC_COMMAND;
  autoconfPayload["name"] = identifier + String(" LED");
  autoconfPayload["value_template"] = "{{value_json.led}}";
  autoconfPayload["unique_id"] = identifier + String("_led");
  autoconfPayload["payload_on"] = "{\"led\": \"on\"}";
  autoconfPayload["payload_off"] = "{\"led\": \"off\"}";
  autoconfPayload["state_on"] = "on";
  autoconfPayload["state_off"] = "off";
  autoconfPayload["icon"] = "mdi:led-outline";

  serializeJson(autoconfPayload, mqttPayload);
  mqttClient.publish(MQTT_TOPIC_AUTOCONF_LED_SWITCH, mqttPayload, true);

  autoconfPayload.clear();
}
