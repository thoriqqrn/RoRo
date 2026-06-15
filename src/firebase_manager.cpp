#include "firebase_manager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

namespace {
constexpr unsigned long TOKEN_REFRESH_MARGIN_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long RETRY_INTERVAL_MS = 5000;

struct ToggleUpdateState {
  bool pending = false;
  bool value = false;
  unsigned long lastAttemptMs = 0;
};

struct ImuUpdateState {
  bool pending = false;
  bool connected = false;
  bool berjalan = false;
  float pitch = 0.0f;
  float roll = 0.0f;
  float motionScore = 0.0f;
  unsigned long lastAttemptMs = 0;
};

RobotData *robotData = nullptr;
portMUX_TYPE firebaseMux = portMUX_INITIALIZER_UNLOCKED;

String idToken;
String refreshToken;
String firebaseUid;
unsigned long tokenExpiresAtMs = 0;
ToggleUpdateState pendingSos;
ToggleUpdateState pendingGas;
ToggleUpdateState pendingManual;
ImuUpdateState pendingImu;
int lastHttpCode = 0;
char lastMessage[96] = "idle";

void setLastMessage(const char *message) {
  snprintf(lastMessage, sizeof(lastMessage), "%s", message);
  Serial.print("[Firebase] ");
  Serial.println(lastMessage);
}

bool isPlaceholderApiKey() {
  return strcmp(kFirebaseWebApiKey, "ISI_FIREBASE_WEB_API_KEY_DI_SINI") == 0 ||
         kFirebaseWebApiKey[0] == '\0';
}

bool isWifiReady() {
  return WiFi.status() == WL_CONNECTED;
}

void beginSecureRequest(WiFiClientSecure &client) {
  if (kFirebaseAllowInsecureTls) {
    client.setInsecure();
  }
}

bool parseAuthResponse(const String &payload) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    setLastMessage("auth_response_invalid_json");
    return false;
  }

  const char *newIdToken = doc["idToken"] | "";
  const char *newRefreshToken = doc["refreshToken"] | "";
  const char *localId = doc["localId"] | "";
  const char *expiresInText = doc["expiresIn"] | "3600";
  unsigned long expiresInSec = strtoul(expiresInText, nullptr, 10);

  if (newIdToken[0] == '\0' || newRefreshToken[0] == '\0') {
    setLastMessage("auth_response_missing_token");
    return false;
  }

  idToken = newIdToken;
  refreshToken = newRefreshToken;
  firebaseUid = localId;
  tokenExpiresAtMs = millis() + (expiresInSec * 1000UL) - TOKEN_REFRESH_MARGIN_MS;
  setLastMessage("auth_ok");
  return true;
}

bool signInAnonymously() {
  if (isPlaceholderApiKey()) {
    setLastMessage("firebase_api_key_not_set");
    return false;
  }

  WiFiClientSecure client;
  beginSecureRequest(client);

  HTTPClient http;
  String url = "https://identitytoolkit.googleapis.com/v1/accounts:signUp?key=";
  url += kFirebaseWebApiKey;

  if (!http.begin(client, url)) {
    setLastMessage("auth_begin_failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  int code = http.POST("{\"returnSecureToken\":true}");
  lastHttpCode = code;
  String payload = http.getString();
  http.end();

  if (code < 200 || code >= 300) {
    Serial.print("[Firebase] Auth HTTP ");
    Serial.print(code);
    Serial.print(": ");
    Serial.println(payload);
    setLastMessage("auth_http_failed");
    return false;
  }

  return parseAuthResponse(payload);
}

bool refreshIdToken() {
  if (refreshToken.length() == 0) {
    return signInAnonymously();
  }

  WiFiClientSecure client;
  beginSecureRequest(client);

  HTTPClient http;
  String url = "https://securetoken.googleapis.com/v1/token?key=";
  url += kFirebaseWebApiKey;

  if (!http.begin(client, url)) {
    setLastMessage("refresh_begin_failed");
    return false;
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "grant_type=refresh_token&refresh_token=" + refreshToken;
  int code = http.POST(body);
  lastHttpCode = code;
  String payload = http.getString();
  http.end();

  if (code < 200 || code >= 300) {
    Serial.print("[Firebase] Refresh HTTP ");
    Serial.print(code);
    Serial.print(": ");
    Serial.println(payload);
    setLastMessage("refresh_http_failed");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    setLastMessage("refresh_response_invalid_json");
    return false;
  }

  const char *newIdToken = doc["id_token"] | "";
  const char *newRefreshToken = doc["refresh_token"] | "";
  const char *expiresInText = doc["expires_in"] | "3600";
  unsigned long expiresInSec = strtoul(expiresInText, nullptr, 10);

  if (newIdToken[0] == '\0' || newRefreshToken[0] == '\0') {
    setLastMessage("refresh_response_missing_token");
    return false;
  }

  idToken = newIdToken;
  refreshToken = newRefreshToken;
  tokenExpiresAtMs = millis() + (expiresInSec * 1000UL) - TOKEN_REFRESH_MARGIN_MS;
  setLastMessage("refresh_ok");
  return true;
}

bool ensureToken() {
  if (idToken.length() == 0) {
    return signInAnonymously();
  }

  if (static_cast<long>(millis() - tokenExpiresAtMs) >= 0) {
    return refreshIdToken();
  }

  return true;
}

bool getUtcIsoTimestamp(char *buffer, size_t bufferSize) {
  time_t now = time(nullptr);
  if (now < 1700000000) {
    return false;
  }

  tm timeinfo;
  gmtime_r(&now, &timeinfo);
  strftime(buffer, bufferSize, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return true;
}

bool sendToggleUpdate(const char *fieldName, bool fieldValue) {
  if (!ensureToken()) {
    return false;
  }

  char timestamp[25] = {0};
  bool hasTimestamp = getUtcIsoTimestamp(timestamp, sizeof(timestamp));

  String url = "https://firestore.googleapis.com/v1/projects/";
  url += kFirebaseProjectId;
  url += "/databases/(default)/documents:commit";

  String docPath = "projects/";
  docPath += kFirebaseProjectId;
  docPath += "/databases/(default)/documents/rollators/";
  docPath += kFirebaseRollatorId;

  JsonDocument body;
  JsonArray writes = body["writes"].to<JsonArray>();

  // 1. Update status terkini di dokumen rollator utama
  JsonObject updateWrite = writes.add<JsonObject>();
  updateWrite["update"]["name"] = docPath;
  updateWrite["update"]["fields"][fieldName]["booleanValue"] = fieldValue;
  updateWrite["updateMask"]["fieldPaths"].add(fieldName);

  // 2. Tambahkan dokumen log/history di subcollection untuk merekam waktu
  if (hasTimestamp) {
    String logDocPath = docPath + "/" + fieldName + "/" + String(millis());
    JsonObject logWrite = writes.add<JsonObject>();
    logWrite["update"]["name"] = logDocPath;
    logWrite["update"]["fields"]["status"]["booleanValue"] = fieldValue;
    logWrite["update"]["fields"]["timestamp"]["timestampValue"] = timestamp;
  }

  String payload;
  serializeJson(body, payload);

  WiFiClientSecure client;
  beginSecureRequest(client);

  HTTPClient http;
  if (!http.begin(client, url)) {
    setLastMessage("firestore_begin_failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + idToken);
  int code = http.POST(payload);
  lastHttpCode = code;
  String response = http.getString();
  http.end();

  if (code < 200 || code >= 300) {
    Serial.print("[Firebase] Firestore HTTP ");
    Serial.print(code);
    Serial.print(": ");
    Serial.println(response);
    setLastMessage("firestore_post_failed");
    return false;
  }

  char message[48] = {0};
  snprintf(message, sizeof(message), "%s_%s_sent", fieldName, fieldValue ? "on" : "off");
  setLastMessage(message);
  return true;
}

bool sendImuUpdate(bool connected, bool berjalan, float pitch, float roll, float motionScore) {
  if (!ensureToken()) {
    return false;
  }

  char timestamp[25] = {0};
  bool hasTimestamp = getUtcIsoTimestamp(timestamp, sizeof(timestamp));

  String url = "https://firestore.googleapis.com/v1/projects/";
  url += kFirebaseProjectId;
  url += "/databases/(default)/documents:commit";

  String docPath = "projects/";
  docPath += kFirebaseProjectId;
  docPath += "/databases/(default)/documents/rollators/";
  docPath += kFirebaseRollatorId;

  JsonDocument body;
  JsonArray writes = body["writes"].to<JsonArray>();

  JsonObject updateWrite = writes.add<JsonObject>();
  updateWrite["update"]["name"] = docPath;
  updateWrite["update"]["fields"]["imuConnected"]["booleanValue"] = connected;
  updateWrite["update"]["fields"]["imuStatus"]["stringValue"] = berjalan ? "jalan" : "diam";
  updateWrite["update"]["fields"]["imuBerjalan"]["booleanValue"] = berjalan;
  updateWrite["update"]["fields"]["imuPitch"]["doubleValue"] = pitch;
  updateWrite["update"]["fields"]["imuRoll"]["doubleValue"] = roll;
  updateWrite["update"]["fields"]["imuMotionScore"]["doubleValue"] = motionScore;
  updateWrite["update"]["fields"]["imuUpdatedAtMs"]["integerValue"] = static_cast<long long>(millis());
  updateWrite["updateMask"]["fieldPaths"].add("imuStatus");
  updateWrite["updateMask"]["fieldPaths"].add("imuConnected");
  updateWrite["updateMask"]["fieldPaths"].add("imuBerjalan");
  updateWrite["updateMask"]["fieldPaths"].add("imuPitch");
  updateWrite["updateMask"]["fieldPaths"].add("imuRoll");
  updateWrite["updateMask"]["fieldPaths"].add("imuMotionScore");
  updateWrite["updateMask"]["fieldPaths"].add("imuUpdatedAtMs");

  JsonObject logWrite = writes.add<JsonObject>();
  String logDocPath = docPath + "/imuHistory/" + String(millis());
  logWrite["update"]["name"] = logDocPath;
  logWrite["update"]["fields"]["connected"]["booleanValue"] = connected;
  logWrite["update"]["fields"]["status"]["stringValue"] = berjalan ? "jalan" : "diam";
  logWrite["update"]["fields"]["walking"]["booleanValue"] = berjalan;
  logWrite["update"]["fields"]["pitch"]["doubleValue"] = pitch;
  logWrite["update"]["fields"]["roll"]["doubleValue"] = roll;
  logWrite["update"]["fields"]["motionScore"]["doubleValue"] = motionScore;
  logWrite["update"]["fields"]["deviceMillis"]["integerValue"] = static_cast<long long>(millis());
  if (hasTimestamp) {
    logWrite["update"]["fields"]["timestamp"]["timestampValue"] = timestamp;
  }

  String payload;
  serializeJson(body, payload);

  WiFiClientSecure client;
  beginSecureRequest(client);

  HTTPClient http;
  if (!http.begin(client, url)) {
    setLastMessage("imu_begin_failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + idToken);
  int code = http.POST(payload);
  lastHttpCode = code;
  String response = http.getString();
  http.end();

  if (code < 200 || code >= 300) {
    Serial.print("[Firebase] IMU HTTP ");
    Serial.print(code);
    Serial.print(": ");
    Serial.println(response);
    setLastMessage("imu_post_failed");
    return false;
  }

  char message[48] = {0};
  snprintf(message, sizeof(message), "imu_%s_sent", berjalan ? "jalan" : "diam");
  setLastMessage(message);
  return true;
}

void queueToggle(ToggleUpdateState &state, bool value) {
  portENTER_CRITICAL(&firebaseMux);
  state.pending = true;
  state.value = value;
  portEXIT_CRITICAL(&firebaseMux);
}

bool processToggleUpdate(ToggleUpdateState &state, const char *fieldName, bool *syncValue) {
  if (!state.pending || !isWifiReady()) {
    return false;
  }

  unsigned long now = millis();
  if (state.lastAttemptMs != 0 && now - state.lastAttemptMs < RETRY_INTERVAL_MS) {
    return false;
  }
  state.lastAttemptMs = now;

  bool fieldValue = false;
  portENTER_CRITICAL(&firebaseMux);
  fieldValue = state.value;
  state.pending = false;
  portEXIT_CRITICAL(&firebaseMux);

  if (!sendToggleUpdate(fieldName, fieldValue)) {
    portENTER_CRITICAL(&firebaseMux);
    state.pending = true;
    portEXIT_CRITICAL(&firebaseMux);
    return false;
  }

  if (syncValue != nullptr) {
    *syncValue = fieldValue;
  }

  return true;
}

bool processImuUpdate() {
  if (!pendingImu.pending || !isWifiReady()) {
    return false;
  }

  unsigned long now = millis();
  if (pendingImu.lastAttemptMs != 0 && now - pendingImu.lastAttemptMs < RETRY_INTERVAL_MS) {
    return false;
  }
  pendingImu.lastAttemptMs = now;

  bool berjalan = false;
  bool connected = false;
  float pitch = 0.0f;
  float roll = 0.0f;
  float motionScore = 0.0f;

  portENTER_CRITICAL(&firebaseMux);
  connected = pendingImu.connected;
  berjalan = pendingImu.berjalan;
  pitch = pendingImu.pitch;
  roll = pendingImu.roll;
  motionScore = pendingImu.motionScore;
  pendingImu.pending = false;
  portEXIT_CRITICAL(&firebaseMux);

  if (!sendImuUpdate(connected, berjalan, pitch, roll, motionScore)) {
    portENTER_CRITICAL(&firebaseMux);
    pendingImu.pending = true;
    portEXIT_CRITICAL(&firebaseMux);
    return false;
  }

  return true;
}
} // namespace

void firebaseManagerInit(RobotData &data) {
  robotData = &data;
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  setLastMessage("init");
}

void firebaseManagerLoop(RobotData &data) {
  (void)data;

  processToggleUpdate(pendingSos, "sos", robotData ? &robotData->tombolSosDitekan : nullptr);
  processToggleUpdate(pendingGas, "gas", robotData ? &robotData->tombolGasDitekan : nullptr);
  processToggleUpdate(pendingManual, "manual", nullptr);
  processImuUpdate();
}

void firebaseManagerQueueSosTriggered() {
  queueToggle(pendingSos, true);
  setLastMessage("sos_on_queued");
}

void firebaseManagerQueueSosCleared() {
  queueToggle(pendingSos, false);
  setLastMessage("sos_off_queued");
}

void firebaseManagerQueueGasPressed() {
  queueToggle(pendingGas, true);
  setLastMessage("gas_on_queued");
}

void firebaseManagerQueueGasReleased() {
  queueToggle(pendingGas, false);
  setLastMessage("gas_off_queued");
}

void firebaseManagerQueueManualStarted() {
  queueToggle(pendingManual, true);
  setLastMessage("manual_on_queued");
}

void firebaseManagerQueueManualStopped() {
  queueToggle(pendingManual, false);
  setLastMessage("manual_off_queued");
}

void firebaseManagerQueueImuStatus(bool connected, bool berjalan, float pitch, float roll, float motionScore) {
  portENTER_CRITICAL(&firebaseMux);
  pendingImu.pending = true;
  pendingImu.connected = connected;
  pendingImu.berjalan = berjalan;
  pendingImu.pitch = pitch;
  pendingImu.roll = roll;
  pendingImu.motionScore = motionScore;
  portEXIT_CRITICAL(&firebaseMux);

  if (!connected) {
    setLastMessage("imu_offline_queued");
  } else {
    setLastMessage(berjalan ? "imu_jalan_queued" : "imu_diam_queued");
  }
}

bool firebaseManagerIsConfigured() {
  return !isPlaceholderApiKey();
}

bool firebaseManagerHasToken() {
  return idToken.length() > 0;
}

bool firebaseManagerHasPendingSos() {
  return pendingSos.pending;
}

bool firebaseManagerHasPendingGas() {
  return pendingGas.pending;
}

bool firebaseManagerHasPendingManual() {
  return pendingManual.pending;
}

bool firebaseManagerHasPendingImu() {
  return pendingImu.pending;
}

int firebaseManagerLastHttpCode() {
  return lastHttpCode;
}

const char *firebaseManagerUid() {
  return firebaseUid.c_str();
}

const char *firebaseManagerLastMessage() {
  return lastMessage;
}
