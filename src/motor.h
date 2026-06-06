#pragma once

#include <Arduino.h>
#include "config.h"

// Fungsi untuk menginisialisasi pin dan membuat task FreeRTOS motor
void motorInit(RobotData &data);

// Menjalankan motor manual sementara untuk diagnosa wiring/PWM.
void motorRunManual(int leftSpeed, int rightSpeed, unsigned long durationMs);

// Catatan: Fungsi loop/task disembunyikan di dalam motor.cpp
