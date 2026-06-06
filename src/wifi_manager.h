#pragma once

#include <Arduino.h>
#include "config.h"

void wifiManagerInit(RobotData &data);
void wifiManagerLoop(RobotData &data);

bool wifiManagerSaveCredentials(const char *ssid, const char *password);
void wifiManagerRequestProvisioning();

bool wifiManagerIsApMode();
const char *wifiManagerGetDeviceId();
