void saveConfig() {
  DynamicJsonDocument json(512);
  json["mqtt_server"] = mqtt_server;
  json["username"] = username;
  json["password"] = password;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    //Serial.println("Failed to open config file for writing");

    return;
  }

  serializeJson(json, configFile);
  configFile.close();
}

void loadConfig() {
  //Serial.println("Loading config");

  if (SPIFFS.begin()) {

    if (SPIFFS.exists("/config.json")) {
      File configFile = SPIFFS.open("/config.json", "r");

      if (configFile) {
        const size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonDocument json(512);

        if (DeserializationError::Ok == deserializeJson(json, buf.get())) {
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(username, json["username"]);
          strcpy(password, json["password"]);


          //Serial.println("Config loaded");
        } else {
          //Serial.println("Failed to parse config file");
        }
      } else {
        //Serial.println("Failed to open config file");
      }
    } else {
      //Serial.println("Config file not found");
    }
  } else {
    //Serial.println("Failed to open SPIFFS");
  }
}
