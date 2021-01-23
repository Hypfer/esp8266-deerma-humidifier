enum humMode_t {
  low = 1,
  medium = 2,
  high = 3,
  setpoint = 4
};

struct humidifierState_t { 
  boolean powerOn;
  
  humMode_t mode;
  
  int humiditySetpoint; //This is 0 when not in setpoint mode
  
  int currentHumidity;
  int currentTemperature;

  boolean soundEnabled;
  boolean ledEnabled;

  boolean waterTankInstalled;
  boolean waterTankEmpty;
};
