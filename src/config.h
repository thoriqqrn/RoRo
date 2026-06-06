#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==========================================
// KONFIGURASI PIN ESP32 UNTUK ROBOT ROLLATOR
// ==========================================

// 1. Pin Motor Driver (BTS7960)
constexpr uint8_t PIN_MOTOR_L_RPWM = 25; // PWM Maju Kiri
constexpr uint8_t PIN_MOTOR_L_LPWM = 26; // PWM Mundur Kiri
constexpr int8_t PIN_MOTOR_L_REN = -1;   // Set ke pin R_EN kiri jika tidak di-jumper HIGH
constexpr int8_t PIN_MOTOR_L_LEN = -1;   // Set ke pin L_EN kiri jika tidak di-jumper HIGH

constexpr uint8_t PIN_MOTOR_R_RPWM = 27; // PWM Maju Kanan
constexpr uint8_t PIN_MOTOR_R_LPWM = 14; // PWM Mundur Kanan
constexpr int8_t PIN_MOTOR_R_REN = -1;   // Set ke pin R_EN kanan jika tidak di-jumper HIGH
constexpr int8_t PIN_MOTOR_R_LEN = -1;   // Set ke pin L_EN kanan jika tidak di-jumper HIGH
constexpr bool MOTOR_RIGHT_INVERTED = false; // true jika kedua jalur PWM kanan aktif dan ingin membalik arah via software

// 2. Pin Encoder (Gunakan pin yang mendukung interrupt)
// constexpr uint8_t PIN_ENCODER_L_A = 34; // Input only, tidak ada internal pull-up
// constexpr uint8_t PIN_ENCODER_L_B = 35; // Input only
// constexpr uint8_t PIN_ENCODER_R_A = 32;
// constexpr uint8_t PIN_ENCODER_R_B = 33;

// 3. Pin I2C Sensor (IMU MPU6050)
constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;

// 4. Pin Sensor / Sistem Keamanan
// constexpr uint8_t PIN_BATTERY_ADC = 36;
// constexpr uint8_t PIN_EMERGENCY_STOP = 15;
constexpr uint8_t PIN_TOMBOL_SOS = 4;  // Tombol SOS aktif LOW
constexpr uint8_t PIN_BUZZER = 5;      // Buzzer Low-Level Trigger
constexpr uint8_t PIN_TOMBOL_GAS = 16; // Tombol untuk mengaktifkan motor (Aman, merupakan strapping pin untuk boot log)

// ==========================================
// KONFIGURASI WIFI & WEBSERVER
// ==========================================
constexpr char kApSsid[] = "Rollator_Setup";
constexpr bool kApUsePassword = false;
constexpr char kApPassword[] = ""; // Kosongkan jika tidak pakai password
constexpr char kMdnsHostname[] = "rorro";
constexpr unsigned long kStaConnectTimeoutMs = 15000; // 15 detik sebelum kembali ke AP
constexpr unsigned long kRestartDelayMs = 2000;       // Jeda 2 detik sebelum ESP restart
constexpr bool kLogWifiCredentialsInsecure = false;   // Ubah ke true untuk debug password di Serial

// ==========================================
// KONFIGURASI FIREBASE
// ==========================================
constexpr char kFirebaseProjectId[] = "roro-90c6b";
constexpr char kFirebaseRollatorId[] = "471grmOw38iBx5v5m9uC";
constexpr char kFirebaseWebApiKey[] = "AIzaSyDA8ypBCXX-v7wzKTHmsucD9YUSpBG54w4";
constexpr bool kFirebaseAllowInsecureTls = true; // Untuk prototipe ESP32. Ganti ke sertifikat root untuk produksi.

// ==========================================
// DATA GLOBAL STRUKTUR
// ==========================================

// Struct bersama untuk pertukaran data antar Task FreeRTOS
struct RobotData {
  float kecepatanKiri;
  float kecepatanKanan;
  float pitch;
  float roll;
  float teganganBaterai;
  bool wifiTerhubung;
  bool emergencyStop;
  bool tombolGasDitekan;
  bool tombolSosDitekan;
};

#endif // CONFIG_H
