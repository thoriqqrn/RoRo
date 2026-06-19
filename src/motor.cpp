#include "motor.h"

#include "firebase_manager.h"
#include "mqtt_manager.h"

// Variabel global visible linker untuk komunikasi antar-modul (diakses safety.cpp via extern)
volatile int gActiveMotorSpeed = MOTOR_MIN_SPEED;
volatile bool gSpeedLevelChanged = false;

namespace {
RobotData *robotData = nullptr;
portMUX_TYPE motorMux = portMUX_INITIALIZER_UNLOCKED;

// Konfigurasi PWM ESP32
const int PWM_FREQ = 5000; // 5 kHz cukup optimal untuk driver BTS7960
const int PWM_RES = 8;     // Resolusi 8-bit (0 - 255)

// Alokasi Channel PWM (ESP32 memiliki total 16 channel independen)
const int CH_L_RPWM = 0;
const int CH_L_LPWM = 1;
const int CH_R_RPWM = 2;
const int CH_R_LPWM = 3;

int manualLeftSpeed = 0;
int manualRightSpeed = 0;
unsigned long manualUntilMs = 0;

void enableMotorPin(int8_t pin) {
  if (pin < 0) {
    return;
  }

  pinMode(static_cast<uint8_t>(pin), OUTPUT);
  digitalWrite(static_cast<uint8_t>(pin), HIGH);
}

int clampMotorSpeed(int speed) {
  return constrain(speed, -255, 255);
}

/**
 * Membaca potensiometer dan memperbarui gActiveMotorSpeed.
 * Menggunakan moving-average 4 sampel untuk mengurangi noise ADC.
 * Dipanggil dari motorTask setiap iterasi loop.
 * Jika level kecepatan berubah (dari 5 level diskrit), set gSpeedLevelChanged = true
 * agar safetyTask bisa membunyikan buzzer konfirmasi.
 */
void updateSpeedFromPot() {
  // Moving-average 4 sampel (cukup ringan)
  static int samples[4] = {0, 0, 0, 0};
  static uint8_t idx = 0;
  samples[idx] = analogRead(PIN_POTENSIOMETER); // 12-bit: 0–4095
  idx = (idx + 1) & 0x03;
  int avg = (samples[0] + samples[1] + samples[2] + samples[3]) >> 2;

  // Map ADC (0–4095) ke kecepatan (MOTOR_MIN_SPEED – MOTOR_MAX_SPEED)
  int newSpeed = map(avg, 0, 4095, MOTOR_MIN_SPEED, MOTOR_MAX_SPEED);
  newSpeed = constrain(newSpeed, MOTOR_MIN_SPEED, MOTOR_MAX_SPEED);

  // Deteksi perubahan level (diskritisasi ke MOTOR_SPEED_LEVELS level)
  // Ini mencegah buzzer berbunyi terus-terusan karena noise kecil
  static int lastLevel = -1;
  int speedRange = MOTOR_MAX_SPEED - MOTOR_MIN_SPEED;
  int currentLevel = ((newSpeed - MOTOR_MIN_SPEED) * MOTOR_SPEED_LEVELS) / (speedRange + 1);
  currentLevel = constrain(currentLevel, 0, MOTOR_SPEED_LEVELS - 1);

  if (currentLevel != lastLevel) {
    lastLevel = currentLevel;
    gSpeedLevelChanged = true; // Beri sinyal ke safetyTask
    Serial.print("[Motor] Kecepatan level ");
    Serial.print(currentLevel + 1);
    Serial.print("/");
    Serial.print(MOTOR_SPEED_LEVELS);
    Serial.print(" speed=");
    Serial.println(newSpeed);
  }

  gActiveMotorSpeed = newSpeed;
}

void setMotorSpeed(int leftSpeed, int rightSpeed) {
  leftSpeed = clampMotorSpeed(leftSpeed);
  rightSpeed = clampMotorSpeed(rightSpeed);

  if (MOTOR_RIGHT_INVERTED) {
    rightSpeed = -rightSpeed;
  }

  // Motor Kiri (Maju jika positif, Mundur jika negatif)
  if (leftSpeed > 0) {
    ledcWrite(CH_L_RPWM, leftSpeed);
    ledcWrite(CH_L_LPWM, 0);
  } else if (leftSpeed < 0) {
    ledcWrite(CH_L_RPWM, 0);
    ledcWrite(CH_L_LPWM, -leftSpeed);
  } else {
    ledcWrite(CH_L_RPWM, 0);
    ledcWrite(CH_L_LPWM, 0);
  }

  // Motor Kanan (Maju jika positif, Mundur jika negatif)
  if (rightSpeed > 0) {
    ledcWrite(CH_R_RPWM, rightSpeed);
    ledcWrite(CH_R_LPWM, 0);
  } else if (rightSpeed < 0) {
    ledcWrite(CH_R_RPWM, 0);
    ledcWrite(CH_R_LPWM, -rightSpeed);
  } else {
    ledcWrite(CH_R_RPWM, 0);
    ledcWrite(CH_R_LPWM, 0);
  }
}

void motorTask(void *pvParameters) {
  RobotData *data = static_cast<RobotData *>(pvParameters);
  bool lastGasPressed = false;
  bool lastEmergencyStop = false;
  bool lastManualActive = false;

  // State deteksi tekan tombol gas (debounce + hitung triple-tap untuk kunci gas)
  int lastRawGas = HIGH;             // bacaan mentah terakhir (HIGH = lepas, pull-up)
  int stableGas = HIGH;             // level stabil setelah debounce
  unsigned long lastGasEdgeMs = 0;   // waktu bacaan mentah terakhir berubah
  uint8_t gasTapCount = 0;           // jumlah tekan beruntun saat ini
  unsigned long lastGasTapMs = 0;    // waktu tekan (press-edge) terakhir
  bool gasLatched = false;           // true = gas terkunci (hold tanpa ditahan)

  for (;;) {
    int activeLeftSpeed = 0;
    int activeRightSpeed = 0;
    bool manualActive = false;

    // Perbarui kecepatan dari potensiometer setiap iterasi
    updateSpeedFromPot();

    portENTER_CRITICAL(&motorMux);
    manualActive = manualUntilMs != 0 && static_cast<long>(millis() - manualUntilMs) < 0;
    if (manualActive) {
      activeLeftSpeed = manualLeftSpeed;
      activeRightSpeed = manualRightSpeed;
    } else {
      manualUntilMs = 0;
    }
    portEXIT_CRITICAL(&motorMux);

    // ── Input tombol gas: debounce + deteksi triple-tap untuk kunci gas ──
    // Aktif LOW berkat INPUT_PULLUP (LOW = ditekan, HIGH = lepas).
    unsigned long nowGas = millis();
    int rawGas = digitalRead(PIN_TOMBOL_GAS);
    if (rawGas != lastRawGas) {
      lastRawGas = rawGas;
      lastGasEdgeMs = nowGas;
    }
    // Terima level baru hanya jika sudah stabil melewati waktu debounce
    if (stableGas != rawGas && (nowGas - lastGasEdgeMs) >= GAS_TAP_DEBOUNCE_MS) {
      bool pressEdge = (stableGas == HIGH && rawGas == LOW); // transisi lepas→tekan
      stableGas = rawGas;

      if (pressEdge) {
        if (gasLatched) {
          // Sudah terkunci: cukup tekan sekali lagi untuk melepas kunci
          gasLatched = false;
          gasTapCount = 0;
          Serial.println("[Motor] Gas hold dilepas (tekan sekali)");
        } else {
          // Hitung tekan beruntun untuk mendeteksi triple-tap cepat
          if (nowGas - lastGasTapMs <= GAS_TAP_MAX_INTERVAL_MS) {
            ++gasTapCount;
          } else {
            gasTapCount = 1; // tekan pertama / mulai rangkaian baru
          }
          lastGasTapMs = nowGas;

          if (gasTapCount >= GAS_TRIPLE_TAP_COUNT) {
            gasLatched = true;
            gasTapCount = 0;
            Serial.println("[Motor] Gas dikunci (triple-tap) — motor jalan tanpa ditahan");
          }
        }
      }
    }

    // Reset hitungan tap bila jeda antar tekan sudah kelewat lama
    if (gasTapCount != 0 && (nowGas - lastGasTapMs) > GAS_TAP_MAX_INTERVAL_MS) {
      gasTapCount = 0;
    }

    bool gasPhysical = (stableGas == LOW);
    // Gas aktif efektif: ditekan fisik ATAU sedang terkunci (latch).
    // Hold normal (tahan tombol) tetap lewat jalur gasPhysical, tidak berubah.
    bool gasActive = gasPhysical || gasLatched;
    data->gasTerkunci = gasLatched;
    data->tombolGasDitekan = gasActive;

    if (gasActive != lastGasPressed) {
      lastGasPressed = gasActive;
      Serial.print("[Motor] Tombol gas: ");
      Serial.println(lastGasPressed ? "ditekan" : "lepas");
      mqttManagerQueueGas(lastGasPressed); // push realtime via MQTT
      if (lastGasPressed) {
        firebaseManagerQueueGasPressed();
      } else {
        firebaseManagerQueueGasReleased();
      }
    }

    if (data->emergencyStop != lastEmergencyStop) {
      lastEmergencyStop = data->emergencyStop;
      Serial.print("[Motor] Emergency stop: ");
      Serial.println(lastEmergencyStop ? "aktif" : "nonaktif");
      if (lastEmergencyStop && gasLatched) {
        // Keselamatan: batalkan kunci gas saat emergency stop agar tidak
        // melanjut sendiri ketika emergency dilepas.
        gasLatched = false;
        data->gasTerkunci = false;
        Serial.println("[Motor] Gas hold dibatalkan oleh emergency stop");
      }
    }

    bool manualJustStarted = manualActive && !lastManualActive;
    bool manualJustStopped = !manualActive && lastManualActive;

    if (manualJustStarted) {
      firebaseManagerQueueManualStarted();
    }
    if (manualJustStopped) {
      firebaseManagerQueueManualStopped();
    }

    lastManualActive = manualActive;

    // Syarat motor jalan: mode diagnosa manual, atau tombol ditekan DAN tidak emergency stop.
    if (manualActive && !data->emergencyStop) {
      data->kecepatanKiri = activeLeftSpeed;
      data->kecepatanKanan = activeRightSpeed;
      setMotorSpeed(activeLeftSpeed, activeRightSpeed);
    } else if (data->tombolGasDitekan && !data->emergencyStop) {
      int spd = gActiveMotorSpeed; // Baca kecepatan dari potensiometer
      data->kecepatanKiri = spd;
      data->kecepatanKanan = spd;
      setMotorSpeed(spd, spd);
    } else {
      data->kecepatanKiri = 0;
      data->kecepatanKanan = 0;
      setMotorSpeed(0, 0);
    }

    // Target performa sistem: loop berjalan setiap 10ms (100Hz) secara non-blocking
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
} // namespace

void motorInit(RobotData &data) {
  robotData = &data;

  data.gasTerkunci = false;

  // Inisialisasi pin tombol gas dengan resistor pull-up internal
  pinMode(PIN_TOMBOL_GAS, INPUT_PULLUP);

  // Inisialisasi pin ADC potensiometer (input-only, tidak perlu pinMode)
  // GPIO 34 secara default adalah input; analogRead() langsung bisa dipakai.
  // Resolusi ADC default ESP32 = 12-bit (0–4095)
  analogSetAttenuation(ADC_11db); // Rentang input 0–3.3V penuh

  // BTS7960 perlu R_EN dan L_EN HIGH agar output aktif.
  // Jika pin enable sudah di-jumper ke 5V/3V3, biarkan konfigurasi di config.h bernilai -1.
  enableMotorPin(PIN_MOTOR_L_REN);
  enableMotorPin(PIN_MOTOR_L_LEN);
  enableMotorPin(PIN_MOTOR_R_REN);
  enableMotorPin(PIN_MOTOR_R_LEN);

  // Setup channel PWM
  ledcSetup(CH_L_RPWM, PWM_FREQ, PWM_RES);
  ledcSetup(CH_L_LPWM, PWM_FREQ, PWM_RES);
  ledcSetup(CH_R_RPWM, PWM_FREQ, PWM_RES);
  ledcSetup(CH_R_LPWM, PWM_FREQ, PWM_RES);

  // Menyambungkan pin GPIO ke channel PWM
  ledcAttachPin(PIN_MOTOR_L_RPWM, CH_L_RPWM);
  ledcAttachPin(PIN_MOTOR_L_LPWM, CH_L_LPWM);
  ledcAttachPin(PIN_MOTOR_R_RPWM, CH_R_RPWM);
  ledcAttachPin(PIN_MOTOR_R_LPWM, CH_R_LPWM);

  // Pastikan motor dalam keadaan mati (stop) saat perangkat pertama kali menyala
  setMotorSpeed(0, 0);

  // Membuat Task FreeRTOS untuk motor yang ditugaskan ke Core 1
  xTaskCreatePinnedToCore(motorTask, "TaskMotor", 2048, &data, 2, NULL, 1);
  Serial.println("[Motor] Task diinisialisasi di Core 1");
  Serial.println("[Motor] Tombol gas aktif LOW: tekan harus terbaca LOW/GND");
  Serial.print("[Motor] Potensiometer di GPIO ");
  Serial.print(PIN_POTENSIOMETER);
  Serial.println(" (ADC1_CH6). Rentang speed: MOTOR_MIN–MOTOR_MAX");
}

void motorRunManual(int leftSpeed, int rightSpeed, unsigned long durationMs) {
  durationMs = constrain(durationMs, 100UL, 5000UL);

  portENTER_CRITICAL(&motorMux);
  manualLeftSpeed = clampMotorSpeed(leftSpeed);
  manualRightSpeed = clampMotorSpeed(rightSpeed);
  manualUntilMs = millis() + durationMs;
  portEXIT_CRITICAL(&motorMux);

  Serial.print("[Motor] Manual test kiri=");
  Serial.print(manualLeftSpeed);
  Serial.print(" kanan=");
  Serial.print(manualRightSpeed);
  Serial.print(" durasi_ms=");
  Serial.println(durationMs);
}
