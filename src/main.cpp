#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "project_config.h"
#include "display_manager.h"
#include "touch_manager.h"

void setup() {
  // Initialize Serial Logging
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32-C3 Touch Lamp Starting ===");

  // Connect to Wi-Fi using static IP settings
  WiFi.mode(WIFI_STA);
  IPAddress local_IP(STATIC_IP_ADDR);
  IPAddress gateway(GATEWAY_IP_ADDR);
  IPAddress subnet(SUBNET_MASK);
  IPAddress primaryDNS(DNS_PRIMARY);
  IPAddress secondaryDNS(DNS_SECONDARY);

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("[WiFi] Static IP configuration failed!");
  }

  // Retrieve SSID/PASS from preprocessor defines (loaded from .env by load_env.py)
  #if defined(WIFI_SSID) && defined(WIFI_PASS)
    Serial.printf("[WiFi] Connecting to SSID: %s...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  #else
    #error "WIFI_SSID and WIFI_PASS must be defined in .env!"
  #endif

  // Wait for Wi-Fi connection with timeout (10 seconds)
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected successfully!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] Wi-Fi connection timed out! Running offline.");
  }

  // Initialize display and touch managers
  DisplayManager::init();
  TouchManager::init();

  // Configure ArduinoOTA
  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setHostname(WIFI_HOSTNAME);

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("[OTA] Start updating " + type);
    DisplayManager::drawOtaProgress(0, 100);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] End of update. Rebooting...");
    DisplayManager::drawOtaProgress(100, 100);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
    DisplayManager::drawOtaProgress(progress, total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]: ", error);
    const char* msg = "Unknown Error";
    if (error == OTA_AUTH_ERROR) { Serial.println("Auth Failed"); msg = "Auth Failed"; }
    else if (error == OTA_BEGIN_ERROR) { Serial.println("Begin Failed"); msg = "Begin Failed"; }
    else if (error == OTA_CONNECT_ERROR) { Serial.println("Connect Failed"); msg = "Connect Failed"; }
    else if (error == OTA_RECEIVE_ERROR) { Serial.println("Receive Failed"); msg = "Receive Failed"; }
    else if (error == OTA_END_ERROR) { Serial.println("End Failed"); msg = "End Failed"; }
    DisplayManager::drawOtaError(msg);
  });

  ArduinoOTA.begin();
  Serial.println("[OTA] OTA Services Ready.");
  Serial.println("=== Setup Complete. Entering loop ===\n");
}

void loop() {
  // Process OTA requests
  ArduinoOTA.handle();

  // Poll touch panel state
  TouchManager::update();

  // Draw UI and check for actions
  DisplayManager::update(
    TouchManager::isTouched(),
    TouchManager::getX(),
    TouchManager::getY(),
    TouchManager::getGestureName(),
    TouchManager::getEventName()
  );

  // Yield to keep the ESP32 Wi-Fi stack happy
  delay(10);
}
