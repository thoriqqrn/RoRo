#include "safety.h"

#include "firebase_manager.h"

// Flag dari motor.cpp — di-set saat level kecepatan potensiometer berubah
extern volatile bool gSpeedLevelChanged;

namespace {
RobotData *robotData = nullptr;

constexpr unsigned long SOS_TIMEOUT_MS = 3000;
constexpr unsigned long SOS_BEEP_INTERVAL_MS = 250;
constexpr unsigned long GAS_BEEP_ON_MS = 70;
constexpr unsigned long GAS_BEEP_INTERVAL_MS = 900;
constexpr unsigned long SPEED_CONFIRM_BEEP_MS = 50; // Durasi buzzer konfirmasi kecepatan (klik pendek)

void safetyTask(void *pvParameters) {
  RobotData *data = static_cast<RobotData *>(pvParameters);

  bool isSosActive = false;
  unsigned long sosStartTime = 0;
  bool buzzerState = false;
  unsigned long lastBuzzerToggle = 0;
  bool gasBeepActive = false;
  bool lastGasPressed = false;
  unsigned long gasBeepOffMs = 0;
  unsigned long nextGasBeepMs = 0;
  unsigned long speedBeepOffMs = 0; // Waktu selesai bunyi konfirmasi kecepatan

  for (;;) {
    unsigned long now = millis();

    // 1. Cek tombol SOS ditekan
    bool sosPressed = (digitalRead(PIN_TOMBOL_SOS) == LOW);
    if (sosPressed) {
      sosStartTime = now; // Reset timer selama ditekan
      if (!isSosActive) {
        isSosActive = true;
        gasBeepActive = false;
        data->tombolSosDitekan = true;
        firebaseManagerQueueSosTriggered();
        Serial.println("[Safety] Tombol SOS Ditekan!");
      }
    }

    // 2. Logic state machine untuk Buzzer "Tett Tett"
    // SOS selalu prioritas tertinggi dibanding bunyi indikator gas.
    if (isSosActive) {
      if (now - sosStartTime < SOS_TIMEOUT_MS) {
        if (now - lastBuzzerToggle >= SOS_BEEP_INTERVAL_MS) {
          buzzerState = !buzzerState;
          // Module low-level trigger: LOW = nyala, HIGH = mati
          digitalWrite(PIN_BUZZER, buzzerState ? LOW : HIGH);
          lastBuzzerToggle = now;
        }
      } else {
        isSosActive = false;
        digitalWrite(PIN_BUZZER, HIGH); // Pastikan buzzer mati (HIGH)
        data->tombolSosDitekan = false;
        firebaseManagerQueueSosCleared();
        Serial.println("[Safety] SOS Selesai (Timeout setelah dilepas)");
      }
    } else {
      bool gasPressed = data->tombolGasDitekan && !data->emergencyStop;

      if (gasPressed && !lastGasPressed) {
        nextGasBeepMs = now;
      }

      if (!gasPressed) {
        gasBeepActive = false;
        nextGasBeepMs = 0;
        digitalWrite(PIN_BUZZER, HIGH);
      } else if (!gasBeepActive &&
                 (nextGasBeepMs == 0 || static_cast<long>(now - nextGasBeepMs) >= 0)) {
        gasBeepActive = true;
        gasBeepOffMs = now + GAS_BEEP_ON_MS;
        nextGasBeepMs = now + GAS_BEEP_INTERVAL_MS;
        digitalWrite(PIN_BUZZER, LOW);
      } else if (gasBeepActive && static_cast<long>(now - gasBeepOffMs) >= 0) {
        gasBeepActive = false;
        digitalWrite(PIN_BUZZER, HIGH);
      }

      lastGasPressed = gasPressed;
    }

    // 3. Buzzer konfirmasi saat level kecepatan potensiometer berubah
    // Hanya aktif ketika tidak SOS dan tidak sedang gas-beep, agar tidak bentrok
    if (!isSosActive && !gasBeepActive && gSpeedLevelChanged) {
      gSpeedLevelChanged = false;          // Clear flag
      speedBeepOffMs = now + SPEED_CONFIRM_BEEP_MS;
      digitalWrite(PIN_BUZZER, LOW);       // Nyalakan buzzer (LOW = nyala)
    }
    // Matikan buzzer konfirmasi setelah durasi selesai
    if (!isSosActive && !gasBeepActive &&
        speedBeepOffMs != 0 && static_cast<long>(now - speedBeepOffMs) >= 0) {
      speedBeepOffMs = 0;
      digitalWrite(PIN_BUZZER, HIGH);      // Matikan buzzer
    }

    vTaskDelay(pdMS_TO_TICKS(20)); // Cek setiap 20ms
  }
}
} // namespace

void safetyInit(RobotData &data) {
  robotData = &data;

  pinMode(PIN_TOMBOL_SOS, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, HIGH); // Standby (HIGH = Mati untuk Low-Level Trigger)

  xTaskCreatePinnedToCore(safetyTask, "TaskSafety", 2048, &data, 3, NULL, 1);
}
