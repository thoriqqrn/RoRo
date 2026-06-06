#include "wifi_manager.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

namespace {
enum class WifiState { ApMode, StaConnecting, StaConnected };

Preferences prefs;
WifiState state = WifiState::ApMode;

char storedSsid[33] = {0};
char storedPass[65] = {0};
char deviceId[24] = {0};
char apSsid[33] = {0};

bool hasCredentials = false;
unsigned long connectStartMs = 0;
unsigned long restartAtMs = 0;
bool mdnsStarted = false;

void scheduleRestart(unsigned long delayMs) {
  restartAtMs = millis() + delayMs;
}

void startApMode() {
  Serial.println("[WiFi] Switching to AP mode");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);

  if (kApUsePassword && kApPassword[0] != '\0') {
    WiFi.softAP(apSsid, kApPassword);
  } else {
    WiFi.softAP(apSsid);
  }

  Serial.print("[WiFi] AP SSID: ");
  Serial.println(apSsid);

  if (mdnsStarted) {
    MDNS.end();
    mdnsStarted = false;
  }

  state = WifiState::ApMode;
}

void startStaMode() {
  Serial.println("[WiFi] Switching to STA mode");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(storedSsid, storedPass);
  connectStartMs = millis();
  state = WifiState::StaConnecting;

  Serial.print("[WiFi] STA SSID: ");
  Serial.println(storedSsid);
}

void loadCredentials() {
  prefs.begin("wifi", true);
  prefs.getString("ssid", storedSsid, sizeof(storedSsid));
  prefs.getString("pass", storedPass, sizeof(storedPass));
  bool forceAp = prefs.getBool("force_ap", false);
  prefs.end();

  hasCredentials = storedSsid[0] != '\0';

  if (forceAp) {
    hasCredentials = false;
  }

  if (forceAp) {
    prefs.begin("wifi", false);
    prefs.putBool("force_ap", false);
    prefs.end();
  }

  Serial.print("[WiFi] Credentials present: ");
  Serial.println(hasCredentials ? "yes" : "no");
  Serial.print("[WiFi] Force AP: ");
  Serial.println(forceAp ? "yes" : "no");
}
} // namespace

void wifiManagerInit(RobotData &data) {
  uint64_t mac = ESP.getEfuseMac();
  uint16_t tail = static_cast<uint16_t>(mac & 0xFFFF);
  snprintf(deviceId, sizeof(deviceId), "RR-ESP32-%04X", tail);
  snprintf(apSsid, sizeof(apSsid), "%s", kApSsid);

  Serial.print("[WiFi] Device ID: ");
  Serial.println(deviceId);

  loadCredentials();

  if (hasCredentials) {
    startStaMode();
  } else {
    startApMode();
  }

  data.wifiTerhubung = false;
}

void wifiManagerLoop(RobotData &data) {
  if (restartAtMs != 0 && static_cast<long>(millis() - restartAtMs) >= 0) {
    Serial.println("[WiFi] Restarting device");
    ESP.restart();
  }

  if (state == WifiState::StaConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      state = WifiState::StaConnected;
      data.wifiTerhubung = true;
      Serial.print("[WiFi] Connected. IP: ");
      Serial.println(WiFi.localIP());

      if (!mdnsStarted) {
        if (MDNS.begin(kMdnsHostname)) {
          mdnsStarted = true;
          Serial.print("[WiFi] mDNS hostname: ");
          Serial.print(kMdnsHostname);
          Serial.println(".local");
        } else {
          Serial.println("[WiFi] mDNS start failed");
        }
      }
    } else if (millis() - connectStartMs >= kStaConnectTimeoutMs) {
      data.wifiTerhubung = false;
      Serial.println("[WiFi] STA connect timeout, returning to AP");
      startApMode();
    }
  } else if (state == WifiState::StaConnected) {
    data.wifiTerhubung = (WiFi.status() == WL_CONNECTED);
    if (!data.wifiTerhubung) {
      Serial.println("[WiFi] Lost connection, returning to AP");
      startApMode();
    }
  }
}

bool wifiManagerSaveCredentials(const char *ssid, const char *password) {
  if (!ssid || ssid[0] == '\0') {
    return false;
  }

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password ? password : "");
  prefs.putBool("force_ap", false);
  prefs.end();

  Serial.println("[WiFi] Credentials saved, scheduling restart");

  scheduleRestart(kRestartDelayMs);
  return true;
}

void wifiManagerRequestProvisioning() {
  prefs.begin("wifi", false);
  prefs.putBool("force_ap", true);
  prefs.end();
  Serial.println("[WiFi] Provisioning requested, scheduling restart");
  scheduleRestart(kRestartDelayMs);
}

bool wifiManagerIsApMode() {
  return state == WifiState::ApMode;
}

const char *wifiManagerGetDeviceId() {
  return deviceId;
}
