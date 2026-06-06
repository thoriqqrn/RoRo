#include "safety.h"

#include "firebase_manager.h"

namespace {
RobotData *robotData = nullptr;

constexpr unsigned long SOS_TIMEOUT_MS = 3000;
constexpr unsigned long SOS_BEEP_INTERVAL_MS = 250;
constexpr unsigned long GAS_BEEP_ON_MS = 70;
constexpr unsigned long GAS_BEEP_INTERVAL_MS = 900;

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
