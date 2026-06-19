#pragma once

#include <Arduino.h>
#include "config.h"

// Manager MQTT (HiveMQ) untuk PUSH event realtime ke aplikasi mobile.
// Pola sama seperti firebase_manager: task hanya memanggil Queue*() (non-blocking),
// koneksi + publish sebenarnya dilakukan di mqttManagerLoop() dari Arduino loop().

void mqttManagerInit(RobotData &data);
void mqttManagerLoop(RobotData &data);

// Dipanggil dari task (Core 1). Hanya menandai pending + cache nilai di bawah mutex.
void mqttManagerQueueGas(bool pressed);
void mqttManagerQueueSos(bool active);
void mqttManagerQueueImu(bool connected, bool berjalan, float pitch, float roll, float motionScore);

// Stream debug metrik IMU mentah (non-retained) untuk tuning ambang. No-op jika kImuDebugStream=false.
void mqttManagerQueueImuDebug(float accelMag, float accelDelta, float gyroMag, float motionScore, bool walking);

// Status untuk ditampilkan di /api/status
bool mqttManagerIsEnabled();
bool mqttManagerIsConnected();
const char *mqttManagerLastMessage();
