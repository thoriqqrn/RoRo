#include "mqtt_manager.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

namespace {
RobotData *robotData = nullptr;
portMUX_TYPE mqttMux = portMUX_INITIALIZER_UNLOCKED;

// Dua client: TLS untuk HiveMQ Cloud (8883), plain untuk broker lokal/test (1883).
WiFiClientSecure tlsClient;
WiFiClient plainClient;
PubSubClient mqtt;

unsigned long lastReconnectAttemptMs = 0;
char lastMessage[64] = "init";

// Topik dibangun sekali saat init dari rollator id.
String topicGas;
String topicSos;
String topicImu;
String topicImuDebug; // metrik mentah untuk tuning (non-retained)
String topicStatus;   // Last Will + heartbeat online/offline
String clientId;

// Event yang menunggu dipublish. Di-set dari task, dikonsumsi di loop().
struct PendingToggle {
  bool pending = false;
  bool value = false;
};

struct PendingImu {
  bool pending = false;
  bool connected = false;
  bool berjalan = false;
  float pitch = 0.0f;
  float roll = 0.0f;
  float motionScore = 0.0f;
};

struct PendingImuDebug {
  bool pending = false;
  float accelMag = 0.0f;
  float accelDelta = 0.0f;
  float gyroMag = 0.0f;
  float motionScore = 0.0f;
  bool walking = false;
};

PendingToggle pendingGas;
PendingToggle pendingSos;
PendingImu pendingImu;
PendingImuDebug pendingImuDebug;

void setLastMessage(const char *message) {
  snprintf(lastMessage, sizeof(lastMessage), "%s", message);
  Serial.print("[MQTT] ");
  Serial.println(lastMessage);
}

bool isWifiReady() {
  return WiFi.status() == WL_CONNECTED;
}

// Publish JSON ke topik dengan retained=true agar mobile yang baru connect
// langsung dapat state terakhir. QoS PubSubClient hanya 0 (cukup untuk realtime push).
bool publishJson(const String &topic, const JsonDocument &doc) {
  char payload[192];
  size_t len = serializeJson(doc, payload, sizeof(payload));
  return mqtt.publish(topic.c_str(), reinterpret_cast<const uint8_t *>(payload), len, true);
}

bool publishGas(bool pressed) {
  JsonDocument doc;
  doc["gas"] = pressed;
  doc["deviceMillis"] = static_cast<long long>(millis());
  return publishJson(topicGas, doc);
}

bool publishSos(bool active) {
  JsonDocument doc;
  doc["sos"] = active;
  doc["deviceMillis"] = static_cast<long long>(millis());
  return publishJson(topicSos, doc);
}

bool publishImu(bool connected, bool berjalan, float pitch, float roll, float motionScore) {
  JsonDocument doc;
  doc["connected"] = connected;
  doc["status"] = berjalan ? "jalan" : "diam";
  doc["walking"] = berjalan;
  doc["pitch"] = pitch;
  doc["roll"] = roll;
  doc["motionScore"] = motionScore;
  doc["deviceMillis"] = static_cast<long long>(millis());
  return publishJson(topicImu, doc);
}

// Stream debug non-retained: dipakai hanya untuk tuning ambang via tools/imu_tune.py.
bool publishImuDebug(float accelMag, float accelDelta, float gyroMag, float motionScore, bool walking) {
  JsonDocument doc;
  doc["accelMag"] = accelMag;
  doc["accelDelta"] = accelDelta;
  doc["gyroMag"] = gyroMag;
  doc["motionScore"] = motionScore;
  doc["walking"] = walking;
  doc["deviceMillis"] = static_cast<long long>(millis());
  char payload[160];
  size_t len = serializeJson(doc, payload, sizeof(payload));
  return mqtt.publish(topicImuDebug.c_str(), reinterpret_cast<const uint8_t *>(payload), len, false);
}

// Coba konek ke broker. Non-blocking: dipanggil paling sering tiap reconnect interval.
bool connect() {
  JsonDocument willDoc;
  willDoc["online"] = false;
  char willPayload[64];
  size_t willLen = serializeJson(willDoc, willPayload, sizeof(willPayload));
  (void)willLen;

  bool ok;
  if (kMqttUsername[0] != '\0') {
    // Last Will: broker kirim {"online":false} ke topic status jika ESP32 putus mendadak.
    ok = mqtt.connect(clientId.c_str(), kMqttUsername, kMqttPassword,
                      topicStatus.c_str(), 0, true, willPayload);
  } else {
    ok = mqtt.connect(clientId.c_str(), nullptr, nullptr,
                      topicStatus.c_str(), 0, true, willPayload);
  }

  if (!ok) {
    char msg[48];
    snprintf(msg, sizeof(msg), "connect_failed_rc=%d", mqtt.state());
    setLastMessage(msg);
    return false;
  }

  // Umumkan online (retained) lalu push state terkini sekali agar mobile sinkron.
  JsonDocument onlineDoc;
  onlineDoc["online"] = true;
  onlineDoc["deviceMillis"] = static_cast<long long>(millis());
  publishJson(topicStatus, onlineDoc);

  if (robotData) {
    publishGas(robotData->tombolGasDitekan);
    publishSos(robotData->tombolSosDitekan);
    publishImu(robotData->imuSensorReady, robotData->imuBerjalan,
               robotData->pitch, robotData->roll, robotData->imuMotionScore);
  }

  setLastMessage("connected");
  return true;
}

// Ambil snapshot pending di bawah mutex lalu publish. Jika publish gagal, re-arm pending.
void flushPending() {
  // Gas
  bool gasPending, gasValue;
  portENTER_CRITICAL(&mqttMux);
  gasPending = pendingGas.pending;
  gasValue = pendingGas.value;
  pendingGas.pending = false;
  portEXIT_CRITICAL(&mqttMux);
  if (gasPending && !publishGas(gasValue)) {
    portENTER_CRITICAL(&mqttMux);
    pendingGas.pending = true;
    portEXIT_CRITICAL(&mqttMux);
  }

  // SOS
  bool sosPending, sosValue;
  portENTER_CRITICAL(&mqttMux);
  sosPending = pendingSos.pending;
  sosValue = pendingSos.value;
  pendingSos.pending = false;
  portEXIT_CRITICAL(&mqttMux);
  if (sosPending && !publishSos(sosValue)) {
    portENTER_CRITICAL(&mqttMux);
    pendingSos.pending = true;
    portEXIT_CRITICAL(&mqttMux);
  }

  // IMU
  bool imuPending, imuConnected, imuBerjalan;
  float imuPitch, imuRoll, imuMotion;
  portENTER_CRITICAL(&mqttMux);
  imuPending = pendingImu.pending;
  imuConnected = pendingImu.connected;
  imuBerjalan = pendingImu.berjalan;
  imuPitch = pendingImu.pitch;
  imuRoll = pendingImu.roll;
  imuMotion = pendingImu.motionScore;
  pendingImu.pending = false;
  portEXIT_CRITICAL(&mqttMux);
  if (imuPending && !publishImu(imuConnected, imuBerjalan, imuPitch, imuRoll, imuMotion)) {
    portENTER_CRITICAL(&mqttMux);
    pendingImu.pending = true;
    portEXIT_CRITICAL(&mqttMux);
  }

  // IMU debug (non-retained, best-effort — kehilangan satu sampel tidak masalah)
  bool dbgPending;
  float dAccel, dDelta, dGyro, dScore;
  bool dWalk;
  portENTER_CRITICAL(&mqttMux);
  dbgPending = pendingImuDebug.pending;
  dAccel = pendingImuDebug.accelMag;
  dDelta = pendingImuDebug.accelDelta;
  dGyro = pendingImuDebug.gyroMag;
  dScore = pendingImuDebug.motionScore;
  dWalk = pendingImuDebug.walking;
  pendingImuDebug.pending = false;
  portEXIT_CRITICAL(&mqttMux);
  if (dbgPending) {
    publishImuDebug(dAccel, dDelta, dGyro, dScore, dWalk);
  }
}
} // namespace

void mqttManagerInit(RobotData &data) {
  robotData = &data;

  if (!kMqttEnabled) {
    setLastMessage("disabled");
    return;
  }

  String base = String("rollators/") + kFirebaseRollatorId;
  topicGas = base + "/gas";
  topicSos = base + "/sos";
  topicImu = base + "/imu";
  topicImuDebug = base + "/imu_debug";
  topicStatus = base + "/status";

  uint64_t mac = ESP.getEfuseMac();
  clientId = String("roro-") + String(static_cast<uint32_t>(mac & 0xFFFFFFFF), HEX);

  if (kMqttUseTls) {
    if (kMqttAllowInsecureTls) {
      tlsClient.setInsecure();
    }
    mqtt.setClient(tlsClient);
  } else {
    mqtt.setClient(plainClient);
  }

  mqtt.setServer(kMqttHost, kMqttPort);
  mqtt.setBufferSize(256);
  mqtt.setKeepAlive(15);

  setLastMessage("init_ok");
}

void mqttManagerLoop(RobotData &data) {
  (void)data;
  if (!kMqttEnabled || !isWifiReady()) {
    return;
  }

  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (lastReconnectAttemptMs == 0 || now - lastReconnectAttemptMs >= kMqttReconnectIntervalMs) {
      lastReconnectAttemptMs = now;
      connect();
    }
    return; // tunggu koneksi sebelum publish/flush
  }

  mqtt.loop();      // wajib dipanggil rutin: jaga keepalive + proses ack
  flushPending();
}

void mqttManagerQueueGas(bool pressed) {
  portENTER_CRITICAL(&mqttMux);
  pendingGas.pending = true;
  pendingGas.value = pressed;
  portEXIT_CRITICAL(&mqttMux);
}

void mqttManagerQueueSos(bool active) {
  portENTER_CRITICAL(&mqttMux);
  pendingSos.pending = true;
  pendingSos.value = active;
  portEXIT_CRITICAL(&mqttMux);
}

void mqttManagerQueueImu(bool connected, bool berjalan, float pitch, float roll, float motionScore) {
  portENTER_CRITICAL(&mqttMux);
  pendingImu.pending = true;
  pendingImu.connected = connected;
  pendingImu.berjalan = berjalan;
  pendingImu.pitch = pitch;
  pendingImu.roll = roll;
  pendingImu.motionScore = motionScore;
  portEXIT_CRITICAL(&mqttMux);
}

void mqttManagerQueueImuDebug(float accelMag, float accelDelta, float gyroMag, float motionScore, bool walking) {
  if (!kImuDebugStream) {
    return;
  }
  portENTER_CRITICAL(&mqttMux);
  pendingImuDebug.pending = true;
  pendingImuDebug.accelMag = accelMag;
  pendingImuDebug.accelDelta = accelDelta;
  pendingImuDebug.gyroMag = gyroMag;
  pendingImuDebug.motionScore = motionScore;
  pendingImuDebug.walking = walking;
  portEXIT_CRITICAL(&mqttMux);
}

bool mqttManagerIsEnabled() {
  return kMqttEnabled;
}

bool mqttManagerIsConnected() {
  return kMqttEnabled && mqtt.connected();
}

const char *mqttManagerLastMessage() {
  return lastMessage;
}
