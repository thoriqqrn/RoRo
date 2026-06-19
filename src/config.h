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

// 5. Pin Potensiometer Pengatur Kecepatan Motor
// Potensiometer 5K: GND --- [pot] --- 3.3V, wiper ke GPIO 34
// GPIO 34 = ADC1_CH6, input-only (tidak ada internal pull-up/down), JANGAN pakai 5V!
constexpr uint8_t PIN_POTENSIOMETER = 34;

// ==========================================
// KONFIGURASI MOTOR (KECEPATAN)
// ==========================================
// Kecepatan motor dikontrol potensiometer (0-255 PWM 8-bit)
// MIN_SPEED: kecepatan minimum agar motor tidak terlalu lambat / stall
// MAX_SPEED: kecepatan maksimum, bisa diset < 255 untuk keamanan
constexpr int MOTOR_MIN_SPEED = 0;    // 0 = motor berhenti penuh
constexpr int MOTOR_MAX_SPEED = 250;  // ~98% duty cycle
// Jumlah "level" kecepatan untuk trigger buzzer konfirmasi
constexpr int MOTOR_SPEED_LEVELS = 9;

// Tekan tombol gas 3x dengan cepat untuk MENGUNCI gas (hold tanpa perlu ditahan).
// Tekan sekali lagi untuk MELEPAS kunci. Fungsi hold normal (tahan tombol) tidak berubah.
constexpr uint8_t GAS_TRIPLE_TAP_COUNT = 3;             // jumlah tekan beruntun untuk mengunci
constexpr unsigned long GAS_TAP_MAX_INTERVAL_MS = 500;  // jeda maksimum antar-tekan agar dihitung beruntun
constexpr unsigned long GAS_TAP_DEBOUNCE_MS = 40;       // abaikan edge lebih cepat dari ini (pantulan tombol)

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
// KONFIGURASI MQTT (HiveMQ) — EVENT REALTIME
// ==========================================
// MQTT dipakai untuk PUSH event cepat (gas, SOS, IMU) ke aplikasi mobile.
// Firebase tetap dipakai untuk penyimpanan/riwayat. Lihat MQTT_REALTIME.md.
// HiveMQ Cloud (serverless) WAJIB TLS port 8883 + username/password.
// Topik dibangun otomatis dari kFirebaseRollatorId: rollators/<id>/{gas,sos,imu,status}
constexpr bool kMqttEnabled = true;
constexpr bool kMqttUseTls = true;                       // HiveMQ Cloud = true (port 8883)
constexpr char kMqttHost[] = "746434cf770c4a868dc6863ed27b6d0a.s1.eu.hivemq.cloud";  // mis. xxxxxxxx.s1.eu.hivemq.cloud
constexpr uint16_t kMqttPort = 8883;
constexpr char kMqttUsername[] = "robotrollator";
constexpr char kMqttPassword[] = "Roro12345";
constexpr bool kMqttAllowInsecureTls = true;             // prototipe: lewati verifikasi sertifikat TLS
constexpr unsigned long kMqttReconnectIntervalMs = 3000; // jeda non-blocking antar percobaan konek

// ==========================================
// KONFIGURASI DETEKSI IMU (JALAN / DIAM)
// ==========================================
// Ambang motionScore (skor SUDAH dihaluskan EMA di imu_manager). Naik ke "jalan"
// jika skor halus >= ON selama beberapa sampel, turun ke "diam" jika <= OFF.
// Sela ON-OFF = hysteresis anti-jitter.
// Nilai berikut dari hasil tuning nyata: diam~0.11, dorong~0.65 (lihat tools/imu_tune.py).
constexpr float kImuWalkingOnThreshold = 0.32f;
constexpr float kImuWalkingOffThreshold = 0.18f;
constexpr uint8_t kImuStableSamplesRequired = 2;
// Faktor penghalus skor (EMA). Makin kecil = makin halus tapi makin lambat reaksi.
// 0.25 ≈ konstanta waktu ~0.5 detik pada sampel 120 ms.
constexpr float kImuMotionSmoothing = 0.25f;

// Heartbeat: kirim ULANG status IMU ke MQTT tiap interval ini walau tak ada
// perubahan. Mencegah status "nyangkut" jika satu pesan QoS0 hilang.
// Firebase TIDAK ikut heartbeat (riwayat tetap hanya saat status berubah).
constexpr unsigned long kImuMqttHeartbeatMs = 3000;

// Debug: stream metrik IMU mentah ke topik rollators/<id>/imu_debug untuk tuning.
// Set false untuk produksi agar tidak ada traffic ekstra.
constexpr bool kImuDebugStream = false;
constexpr unsigned long kImuDebugIntervalMs = 150;

// ==========================================
// DATA GLOBAL STRUKTUR
// ==========================================

// Struct bersama untuk pertukaran data antar Task FreeRTOS
struct RobotData {
  float kecepatanKiri;
  float kecepatanKanan;
  float pitch;
  float roll;
  float imuMotionScore;
  float teganganBaterai;
  bool wifiTerhubung;
  bool emergencyStop;
  bool tombolGasDitekan;
  bool gasTerkunci;
  bool tombolSosDitekan;
  bool imuSensorReady;
  bool imuBerjalan;
  unsigned long imuStatusChangedMs;
  unsigned long imuLastUpdateMs;
};

#endif // CONFIG_H
