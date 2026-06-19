#include <Arduino.h>

#include "config.h"
#include "imu_manager.h"
#include "firebase_manager.h"
#include "motor.h"
#include "mqtt_manager.h"
#include "safety.h"
#include "webserver.h"
#include "wifi_manager.h"

RobotData robotData;

void setup() {
  Serial.begin(115200);

  wifiManagerInit(robotData);
  firebaseManagerInit(robotData);
  mqttManagerInit(robotData);
  imuManagerInit(robotData);
  motorInit(robotData);
  safetyInit(robotData);
  webServerInit(robotData);
}

void loop() {
  wifiManagerLoop(robotData);
  firebaseManagerLoop(robotData);
  mqttManagerLoop(robotData);
  webServerLoop();
}
