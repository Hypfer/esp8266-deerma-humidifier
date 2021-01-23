const int DOWNSTREAM_QUEUE_SIZE = 50;
const int DOWNSTREAM_QUEUE_ELEM_SIZE = 51;

char downstreamQueue[DOWNSTREAM_QUEUE_SIZE][DOWNSTREAM_QUEUE_ELEM_SIZE];
int downstreamQueueIndex = -1;
char nextDownstreamMessage[DOWNSTREAM_QUEUE_ELEM_SIZE];

boolean shouldUpdateState = false;



void clearRxBuf() {
  //Clear everything for the next message
  memset(serialRxBuf, 0, sizeof(serialRxBuf));
}
void clearTxBuf() {
  //Clear everything for the next message
  memset(serialTxBuf, 0, sizeof(serialTxBuf));
}

void clearDownstreamQueueAtIndex(int index) {
  memset(downstreamQueue[index], 0, sizeof(downstreamQueue[index]));
}


void queueDownstreamMessage(char *message) {
  if (downstreamQueueIndex >= DOWNSTREAM_QUEUE_SIZE - 1) {
    //Serial.print("Error: Queue is full. Dropping message:");
    //Serial.println(message);

    return;
  } else {
    downstreamQueueIndex++;

    snprintf(downstreamQueue[downstreamQueueIndex], sizeof(downstreamQueue[downstreamQueueIndex]), "%s", message);
  }
}

void fillNextDownstreamMessage() {
  memset(nextDownstreamMessage, 0, sizeof(nextDownstreamMessage));

  if (downstreamQueueIndex < 0) {
    snprintf(nextDownstreamMessage, DOWNSTREAM_QUEUE_ELEM_SIZE, "none");

  } else if (downstreamQueueIndex == 0) {
    snprintf(nextDownstreamMessage, DOWNSTREAM_QUEUE_ELEM_SIZE, downstreamQueue[0]);
    clearDownstreamQueueAtIndex(0);
    downstreamQueueIndex--;

  } else {
    /**
       This could be solved in a better way using less cycles, however, the queue should usually be mostly empty so this shouldn't matter much
    */

    snprintf(nextDownstreamMessage, DOWNSTREAM_QUEUE_ELEM_SIZE, downstreamQueue[0]);

    for (int i = 0; i < downstreamQueueIndex; i++) {
      snprintf(downstreamQueue[i], DOWNSTREAM_QUEUE_ELEM_SIZE, downstreamQueue[i + 1]);
    }

    clearDownstreamQueueAtIndex(downstreamQueueIndex);
    downstreamQueueIndex--;
  }
}

void handleUart() {
  if (Serial.available()) {
    Serial.readBytesUntil('\r', serialRxBuf, 250);

    char propName[30]; //30 chars for good measure
    int propValue;

    int sscanfResultCount;


    sscanfResultCount = sscanf(serialRxBuf, "props %s %d", propName, &propValue);

    if (sscanfResultCount == 2) {
      shouldUpdateState = true;

      if (strcmp(propName, "OnOff_State") == 0) {
        state.powerOn = (boolean)propValue;
      } else if (strcmp(propName, "Humidifier_Gear") == 0) {
        state.mode = (humMode_t)propValue;
      } else if (strcmp(propName, "HumiSet_Value") == 0) {
        state.humiditySetpoint = propValue;
      } else if (strcmp(propName, "Humidity_Value") == 0) {
        state.currentHumidity = propValue;
      } else if (strcmp(propName, "TemperatureValue") == 0) {
        state.currentTemperature = propValue;
      } else if (strcmp(propName, "TipSound_State") == 0) {
        state.soundEnabled = (boolean)propValue;
      } else if (strcmp(propName, "Led_State") == 0) {
        state.ledEnabled = (boolean)propValue;
      } else if (strcmp(propName, "watertankstatus") == 0) {
        state.waterTankInstalled = (boolean)propValue;
      } else if (strcmp(propName, "waterstatus") == 0) {
        state.waterTankEmpty = !(boolean)propValue;
      } else {
        //Serial.print("Received unhandled prop: ");
        //Serial.println(serialRxBuf);
      }

      clearRxBuf();
      Serial.print("ok\r");

    } else {
      if (shouldUpdateState == true) { //This prevents a spam wave of state updates since we only send them after all prop updates have been received
        publishState();
      }

      shouldUpdateState = false;


      if (strncmp (serialRxBuf, "get_down", 8) == 0) {
        fillNextDownstreamMessage();
        snprintf(serialTxBuf, sizeof(serialTxBuf), "down %s\r", nextDownstreamMessage);
        Serial.print(serialTxBuf);


        clearTxBuf();

      } else if (strncmp(serialRxBuf, "net", 3) == 0) {
        Serial.print("cloud\r");
        /**
           We need to always respond with cloud because otherwise the connection to the humidifier will break for some reason
           Not sure why but it doesn't really matter

          if (networkConnected == true) {

          } else {

          humidifierSerial.print("uap\r");
          } **/

      } else if (strncmp(serialRxBuf, "restore", 7) == 0) {
        resetWifiSettingsAndReboot();

      } else if (
        strncmp(serialRxBuf, "mcu_version", 11) == 0 ||
        strncmp(serialRxBuf, "model", 5) == 0
      ) {
        Serial.print("ok\r");
      } else if (strncmp(serialRxBuf, "event", 5) == 0) {
        //Intentionally left blank
        //We don't need to handle events, since we already get the prop updates
      } else if (strncmp(serialRxBuf, "result", 6) == 0) {
        //Serial.println(serialRxBuf);
      } else {
        //Serial.print("Received unhandled message: ");
        //Serial.println(serialRxBuf);
      }
    }


    clearRxBuf();
  }
}

void setPowerState(boolean powerOn) {
  if (powerOn == true) {
    queueDownstreamMessage("Set_OnOff 1");
  } else {
    queueDownstreamMessage("Set_OnOff 0");
  }

  state.powerOn = powerOn;
  shouldUpdateState = true;
}

void setLEDState(boolean ledEnabled) {
  if (ledEnabled == true) {
    queueDownstreamMessage("SetLedState 1");
  } else {
    queueDownstreamMessage("SetLedState 0");
  }

  state.ledEnabled = ledEnabled;
  shouldUpdateState = true;
}

void setSoundState(boolean soundEnabled) {
  if (soundEnabled == true) {
    queueDownstreamMessage("SetTipSound_Status 1");
  } else {
    queueDownstreamMessage("SetTipSound_Status 0");
  }

  state.soundEnabled = soundEnabled;
  shouldUpdateState = true;
}

void setHumMode(humMode_t mode) {
  switch (mode) {
    case low:
      queueDownstreamMessage("Set_HumidifierGears 1");
      break;
    case medium:
      queueDownstreamMessage("Set_HumidifierGears 2");
      break;
    case high:
      queueDownstreamMessage("Set_HumidifierGears 3");
      break;
    case setpoint:
      queueDownstreamMessage("Set_HumidifierGears 4");
      break;
  }

  state.mode = mode;
  shouldUpdateState = true;
}

void setHumiditySetpoint(uint8_t setpointValue) {
  char humiditySetpointMsg[40];
  memset(humiditySetpointMsg, 0, sizeof(humiditySetpointMsg));

  snprintf(humiditySetpointMsg, sizeof(humiditySetpointMsg), "Set_HumiValue %d", setpointValue);

  queueDownstreamMessage(humiditySetpointMsg);

  //This is required since we not always receive a prop update when setting this
  state.humiditySetpoint = (int)setpointValue;
  shouldUpdateState = true;
}


void sendNetworkStatus(boolean isConnected) {
  queueDownstreamMessage("MIIO_net_change cloud");

  /**

     Again: just always answer with cloud and the uC will be happy
    networkConnected = isConnected;

    if (isConnected == true) {
    queueDownstreamMessage("MIIO_net_change cloud");
    } else {
    queueDownstreamMessage("MIIO_net_change uap");
    } **/
}
