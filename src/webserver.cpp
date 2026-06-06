#include "webserver.h"

#include <ArduinoJson.h>
#include <WebServer.h>

#include "firebase_manager.h"
#include "motor.h"
#include "wifi_manager.h"

namespace {
WebServer server(80);
RobotData *robotData = nullptr;

void sendJsonResponse(int code, const JsonDocument &doc) {
  String payload;
  serializeJson(doc, payload);
  server.send(code, "application/json", payload);
}

void handleStatus() {
  JsonDocument doc;
  doc["mode"] = wifiManagerIsApMode() ? "AP" : "STA";
  doc["device_id"] = wifiManagerGetDeviceId();
  doc["uptime_ms"] = millis();
  doc["wifi_connected"] = robotData ? robotData->wifiTerhubung : false;
  doc["emergency_stop"] = robotData ? robotData->emergencyStop : false;
  doc["gas_pressed"] = robotData ? robotData->tombolGasDitekan : false;
  doc["sos_pressed"] = robotData ? robotData->tombolSosDitekan : false;
  doc["left_speed"] = robotData ? robotData->kecepatanKiri : 0;
  doc["right_speed"] = robotData ? robotData->kecepatanKanan : 0;
  doc["firebase_configured"] = firebaseManagerIsConfigured();
  doc["firebase_rollator_id"] = kFirebaseRollatorId;
  doc["firebase_has_token"] = firebaseManagerHasToken();
  doc["firebase_pending_sos"] = firebaseManagerHasPendingSos();
  doc["firebase_pending_gas"] = firebaseManagerHasPendingGas();
  doc["firebase_pending_manual"] = firebaseManagerHasPendingManual();
  doc["firebase_last_http_code"] = firebaseManagerLastHttpCode();
  doc["firebase_uid"] = firebaseManagerUid();
  doc["firebase_last_message"] = firebaseManagerLastMessage();

  sendJsonResponse(200, doc);
}

void handleWifiPost() {
  Serial.println("[Web] POST /api/wifi");
  if (!server.hasArg("plain")) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["message"] = "missing_body";
    sendJsonResponse(400, doc);
    return;
  }

  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["message"] = "invalid_json";
    sendJsonResponse(400, doc);
    return;
  }

  const char *ssid = body["ssid"] | "";
  const char *password = body["password"] | "";

  Serial.print("[Web] SSID length: ");
  Serial.println(strlen(ssid));
  if (kLogWifiCredentialsInsecure) {
    Serial.print("[Web] SSID: ");
    Serial.println(ssid);
    Serial.print("[Web] Password: ");
    Serial.println(password);
  }

  JsonDocument doc;
  if (!wifiManagerSaveCredentials(ssid, password)) {
    doc["ok"] = false;
    doc["message"] = "invalid_ssid";
    sendJsonResponse(400, doc);
    return;
  }

  doc["ok"] = true;
  doc["message"] = "saved";
  doc["restart_in_ms"] = kRestartDelayMs;
  sendJsonResponse(200, doc);
}

void handleProvisioningStart() {
  Serial.println("[Web] POST /api/provisioning/start");
  wifiManagerRequestProvisioning();

  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "switching_to_ap";
  doc["restart_in_ms"] = kRestartDelayMs;
  sendJsonResponse(200, doc);
}

void handleMotorTestPost() {
  Serial.println("[Web] POST /api/motor/test");
  if (!server.hasArg("plain")) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["message"] = "missing_body";
    sendJsonResponse(400, doc);
    return;
  }

  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["message"] = "invalid_json";
    sendJsonResponse(400, doc);
    return;
  }

  int leftSpeed = body["left"] | 150;
  int rightSpeed = body["right"] | 150;
  unsigned long durationMs = body["duration_ms"] | 1000;

  motorRunManual(leftSpeed, rightSpeed, durationMs);

  JsonDocument doc;
  doc["ok"] = true;
  doc["left"] = constrain(leftSpeed, -255, 255);
  doc["right"] = constrain(rightSpeed, -255, 255);
  doc["duration_ms"] = constrain(durationMs, 100UL, 5000UL);
  sendJsonResponse(200, doc);
}

void handleSosTestPost() {
  Serial.println("[Web] POST /api/sos/test");
  bool clearSos = false;

  if (server.hasArg("plain")) {
    JsonDocument body;
    DeserializationError err = deserializeJson(body, server.arg("plain"));
    if (err) {
      JsonDocument doc;
      doc["ok"] = false;
      doc["message"] = "invalid_json";
      sendJsonResponse(400, doc);
      return;
    }

    clearSos = body["clear"] | false;
  }

  if (clearSos) {
    firebaseManagerQueueSosCleared();
  } else {
    firebaseManagerQueueSosTriggered();
  }

  JsonDocument doc;
  doc["ok"] = true;
  doc["queued"] = clearSos ? "clear" : "trigger";
  doc["firebase_configured"] = firebaseManagerIsConfigured();
  doc["message"] = firebaseManagerLastMessage();
  sendJsonResponse(200, doc);
}

void handleNotFound() {
  Serial.print("[Web] 404: ");
  Serial.println(server.uri());

  JsonDocument doc;
  doc["ok"] = false;
  doc["message"] = "not_found";
  sendJsonResponse(404, doc);
}
} // namespace

void webServerInit(RobotData &data) {
  robotData = &data;

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/wifi", HTTP_POST, handleWifiPost);
  server.on("/api/motor/test", HTTP_POST, handleMotorTestPost);
  server.on("/api/sos/test", HTTP_POST, handleSosTestPost);
  server.on("/api/provisioning/start", HTTP_POST, handleProvisioningStart);
  server.onNotFound(handleNotFound);

  server.begin();
}

void webServerLoop() {
  server.handleClient();
}
