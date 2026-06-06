#include "motor.h"

#include "firebase_manager.h"

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

constexpr int MOTOR_SPEED_ACTIVE = 250;
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

  for (;;) {
    int activeLeftSpeed = 0;
    int activeRightSpeed = 0;
    bool manualActive = false;

    portENTER_CRITICAL(&motorMux);
    manualActive = manualUntilMs != 0 && static_cast<long>(millis() - manualUntilMs) < 0;
    if (manualActive) {
      activeLeftSpeed = manualLeftSpeed;
      activeRightSpeed = manualRightSpeed;
    } else {
      manualUntilMs = 0;
    }
    portEXIT_CRITICAL(&motorMux);

    // Membaca input tombol gas (Aktif LOW berkat INPUT_PULLUP)
    data->tombolGasDitekan = (digitalRead(PIN_TOMBOL_GAS) == LOW);

    if (data->tombolGasDitekan != lastGasPressed) {
      lastGasPressed = data->tombolGasDitekan;
      Serial.print("[Motor] Tombol gas: ");
      Serial.println(lastGasPressed ? "ditekan" : "lepas");
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
      data->kecepatanKiri = MOTOR_SPEED_ACTIVE; // 150 dari 255 (kecepatan sedang)
      data->kecepatanKanan = MOTOR_SPEED_ACTIVE;
      setMotorSpeed(MOTOR_SPEED_ACTIVE, MOTOR_SPEED_ACTIVE);
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

  // Inisialisasi pin tombol gas dengan resistor pull-up internal
  pinMode(PIN_TOMBOL_GAS, INPUT_PULLUP);

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
