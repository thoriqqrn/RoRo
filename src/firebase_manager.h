#pragma once

#include <Arduino.h>
#include "config.h"

void firebaseManagerInit(RobotData &data);
void firebaseManagerLoop(RobotData &data);

void firebaseManagerQueueSosTriggered();
void firebaseManagerQueueSosCleared();
void firebaseManagerQueueGasPressed();
void firebaseManagerQueueGasReleased();
void firebaseManagerQueueManualStarted();
void firebaseManagerQueueManualStopped();
void firebaseManagerQueueImuStatus(bool connected, bool berjalan, float pitch, float roll, float motionScore);

bool firebaseManagerIsConfigured();
bool firebaseManagerHasToken();
bool firebaseManagerHasPendingSos();
bool firebaseManagerHasPendingGas();
bool firebaseManagerHasPendingManual();
bool firebaseManagerHasPendingImu();
int firebaseManagerLastHttpCode();
const char *firebaseManagerUid();
const char *firebaseManagerLastMessage();
