#include "imu_manager.h"

#include <Wire.h>

#include "firebase_manager.h"
#include "mqtt_manager.h"

namespace {
RobotData *robotData = nullptr;
portMUX_TYPE imuMux = portMUX_INITIALIZER_UNLOCKED;

constexpr uint8_t MPU6050_ADDR = 0x68;
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t REG_WHO_AM_I = 0x75;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
constexpr uint8_t REG_GYRO_XOUT_H = 0x43;
constexpr unsigned long SENSOR_RETRY_MS = 5000;
constexpr unsigned long SAMPLE_INTERVAL_MS = 120;
// Ambang & sampel-stabil sekarang dari config.h supaya gampang di-tuning.
constexpr float WALKING_ON_THRESHOLD = kImuWalkingOnThreshold;
constexpr float WALKING_OFF_THRESHOLD = kImuWalkingOffThreshold;
constexpr uint8_t STABLE_SAMPLES_REQUIRED = kImuStableSamplesRequired;

bool sensorReady = false;
unsigned long lastSensorAttemptMs = 0;

bool readBytes(uint8_t registerAddress, uint8_t *buffer, size_t length) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(registerAddress);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  size_t requested = Wire.requestFrom(static_cast<int>(MPU6050_ADDR), static_cast<int>(length), static_cast<int>(true));
  if (requested != length) {
    return false;
  }

  for (size_t index = 0; index < length && Wire.available(); ++index) {
    buffer[index] = Wire.read();
  }

  return true;
}

bool readWord(uint8_t registerAddress, int16_t &value) {
  uint8_t buffer[2] = {0, 0};
  if (!readBytes(registerAddress, buffer, sizeof(buffer))) {
    return false;
  }

  value = static_cast<int16_t>((static_cast<uint16_t>(buffer[0]) << 8) | buffer[1]);
  return true;
}

bool initSensor() {
  uint8_t whoAmI = 0;
  if (!readBytes(REG_WHO_AM_I, &whoAmI, 1)) {
    return false;
  }

  if (whoAmI != 0x68 && whoAmI != 0x69) {
    return false;
  }

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(REG_PWR_MGMT_1);
  Wire.write(static_cast<uint8_t>(0x00));
  if (Wire.endTransmission(true) != 0) {
    return false;
  }

  return true;
}

void publishImuState(bool connected, bool walking, float pitch, float roll, float motionScore) {
  if (!robotData) {
    return;
  }

  unsigned long now = millis();
  bool stateChanged = false;

  portENTER_CRITICAL(&imuMux);
  stateChanged = robotData->imuBerjalan != walking || robotData->imuSensorReady != connected;
  robotData->imuSensorReady = connected;
  robotData->imuBerjalan = walking;
  robotData->pitch = pitch;
  robotData->roll = roll;
  robotData->imuMotionScore = motionScore;
  robotData->imuLastUpdateMs = now;
  if (stateChanged) {
    robotData->imuStatusChangedMs = now;
  }
  portEXIT_CRITICAL(&imuMux);

  static bool lastQueuedWalking = false;
  static bool lastQueuedConnected = false;
  static bool firstPublish = true;
  if (firstPublish || stateChanged || lastQueuedWalking != walking || lastQueuedConnected != connected) {
    mqttManagerQueueImu(connected, walking, pitch, roll, motionScore); // push realtime via MQTT
    firebaseManagerQueueImuStatus(connected, walking, pitch, roll, motionScore);
    lastQueuedWalking = walking;
    lastQueuedConnected = connected;
    firstPublish = false;
  }
}

void imuTask(void *pvParameters) {
  RobotData *data = static_cast<RobotData *>(pvParameters);
  float filteredPitch = 0.0f;
  float filteredRoll = 0.0f;
  float filteredMotion = 0.0f;  // motionScore yang sudah dihaluskan (EMA)
  bool walking = false;
  uint8_t aboveCount = 0;
  uint8_t belowCount = 0;
  bool lastPublishedSensorReady = true;  // true supaya pertama kali disconnect langsung publish
  bool bootPublished = false;            // pastikan publish status nyata sekali saat boot
  unsigned long lastHeartbeatMs = 0;     // timer kirim ulang status ke MQTT
  unsigned long lastDebugMs = 0;         // timer stream debug metrik

  for (;;) {
    unsigned long now = millis();

    if (!sensorReady) {
      if (lastSensorAttemptMs == 0 || now - lastSensorAttemptMs >= SENSOR_RETRY_MS) {
        lastSensorAttemptMs = now;
        sensorReady = initSensor();
        if (data) {
          data->imuSensorReady = sensorReady;
        }
      }

      if (!sensorReady) {
        // Hanya publish SEKALI saat sensor disconnect, bukan tiap 500ms
        if (lastPublishedSensorReady) {
          publishImuState(false, false, 0.0f, 0.0f, 0.0f);
          lastPublishedSensorReady = false;
        }
        if (data) {
          data->imuLastUpdateMs = now;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
    }

    // Sensor baru connect kembali — publish sekali dengan state diam
    if (!lastPublishedSensorReady) {
      lastPublishedSensorReady = true;
      publishImuState(true, false, filteredPitch, filteredRoll, 0.0f);
    }

    // Saat boot: paksa publish status nyata sekali (diam) agar pesan retained "jalan"
    // yang basi di broker langsung tertimpa, tanpa menunggu transisi gerakan.
    if (!bootPublished) {
      bootPublished = true;
      publishImuState(true, walking, filteredPitch, filteredRoll, 0.0f);
    }

    int16_t axRaw = 0;
    int16_t ayRaw = 0;
    int16_t azRaw = 0;
    int16_t gxRaw = 0;
    int16_t gyRaw = 0;
    int16_t gzRaw = 0;

    if (!readWord(REG_ACCEL_XOUT_H, axRaw) ||
        !readWord(REG_ACCEL_XOUT_H + 2, ayRaw) ||
        !readWord(REG_ACCEL_XOUT_H + 4, azRaw) ||
        !readWord(REG_GYRO_XOUT_H, gxRaw) ||
        !readWord(REG_GYRO_XOUT_H + 2, gyRaw) ||
        !readWord(REG_GYRO_XOUT_H + 4, gzRaw)) {
      sensorReady = false;
      lastPublishedSensorReady = true;  // reset agar publish disconnect sekali di iterasi berikutnya
      if (data) {
        data->imuSensorReady = false;
      }
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    float ax = static_cast<float>(axRaw) / 16384.0f;
    float ay = static_cast<float>(ayRaw) / 16384.0f;
    float az = static_cast<float>(azRaw) / 16384.0f;
    float gx = static_cast<float>(gxRaw) / 131.0f;
    float gy = static_cast<float>(gyRaw) / 131.0f;
    float gz = static_cast<float>(gzRaw) / 131.0f;

    float pitch = atan2f(-ax, sqrtf((ay * ay) + (az * az))) * 180.0f / PI;
    float roll = atan2f(ay, az) * 180.0f / PI;

    filteredPitch = filteredPitch * 0.85f + pitch * 0.15f;
    filteredRoll = filteredRoll * 0.85f + roll * 0.15f;

    float accelMagnitude = sqrtf((ax * ax) + (ay * ay) + (az * az));
    float accelDelta = fabsf(accelMagnitude - 1.0f);
    float gyroMagnitude = fabsf(gx) + fabsf(gy) + fabsf(gz);
    float motionScore = (accelDelta * 3.5f) + (gyroMagnitude / 45.0f);

    // Haluskan skor (EMA). Dorongan rollator berirama (skor naik-turun), tanpa
    // dihaluskan status akan kedip jalan/diam. Setelah halus: diam~0.11, dorong~0.65.
    filteredMotion = filteredMotion * (1.0f - kImuMotionSmoothing) + motionScore * kImuMotionSmoothing;

    if (filteredMotion >= WALKING_ON_THRESHOLD) {
      if (aboveCount < 255) {
        ++aboveCount;
      }
      belowCount = 0;
    } else if (filteredMotion <= WALKING_OFF_THRESHOLD) {
      if (belowCount < 255) {
        ++belowCount;
      }
      aboveCount = 0;
    }

    if (!walking && aboveCount >= STABLE_SAMPLES_REQUIRED) {
      walking = true;
      aboveCount = 0;  // reset penuh
      belowCount = 0;
      publishImuState(true, walking, filteredPitch, filteredRoll, filteredMotion);
    } else if (walking && belowCount >= STABLE_SAMPLES_REQUIRED) {
      walking = false;
      aboveCount = 0;
      belowCount = 0;  // reset penuh
      publishImuState(true, walking, filteredPitch, filteredRoll, filteredMotion);
    }

    // Update robotData tiap sample (non-state-change data: pitch, roll, motionScore)
    // State (walking, sensorReady) hanya update via publishImuState saat ada perubahan
    if (data) {
      portENTER_CRITICAL(&imuMux);
      data->pitch = filteredPitch;
      data->roll = filteredRoll;
      data->imuMotionScore = filteredMotion;
      data->imuLastUpdateMs = now;
      portEXIT_CRITICAL(&imuMux);
    }

    // Stream debug untuk tuning: accelDelta & gyro mentah, tapi skor = nilai HALUS
    // (yang dipakai ambang) supaya kelihatan persis apa yang menggerakkan keputusan.
    if (now - lastDebugMs >= kImuDebugIntervalMs) {
      lastDebugMs = now;
      mqttManagerQueueImuDebug(accelMagnitude, accelDelta, gyroMagnitude, filteredMotion, walking);
    }

    // Heartbeat MQTT: kirim ULANG status terkini berkala supaya tidak "nyangkut"
    // walau satu pesan transisi (QoS0) hilang. Firebase TIDAK ikut (riwayat tetap saat berubah).
    if (now - lastHeartbeatMs >= kImuMqttHeartbeatMs) {
      lastHeartbeatMs = now;
      mqttManagerQueueImu(true, walking, filteredPitch, filteredRoll, filteredMotion);
    }

    vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
  }
}
} // namespace

void imuManagerInit(RobotData &data) {
  robotData = &data;

  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  data.imuSensorReady = false;
  data.imuBerjalan = false;
  data.imuMotionScore = 0;
  data.imuStatusChangedMs = 0;
  data.imuLastUpdateMs = 0;

  xTaskCreatePinnedToCore(imuTask, "TaskIMU", 4096, &data, 2, NULL, 1);
  Serial.println("[IMU] Task diinisialisasi di Core 1");
}