#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "project_config.h"
#include "display_manager.h"
#include "touch_manager.h"
#include <TFT_eSPI.h>

// Lamp State Variables
static bool lampOn = false;
static int brightness = DEFAULT_BRIGHTNESS;
static uint16_t activeColor = 0xFD20; // Default to Orange (approx same as TFT_GOLD)
static int activeSegmentIndex = 4;    // Default to segment 4 (Orange)
static bool colorPickerActive = false;

// Touch State Caching
static bool lastTouchedState = false;

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

  bool isTouched = TouchManager::isTouched();
  if (isTouched) {
    int tx = TouchManager::getX();
    int ty = TouchManager::getY();
    
    // Calculate polar coordinates relative to screen center
    int dx = tx - SCREEN_CENTER_X;
    int dy = ty - SCREEN_CENTER_Y;
    int dist_sq = dx * dx + dy * dy;

    if (colorPickerActive) {
      if (!lastTouchedState) {
        // Tapped close button (checkmark) in the center
        if (dist_sq <= CLOSE_BTN_RADIUS * CLOSE_BTN_RADIUS) {
          colorPickerActive = false;
          Serial.println("[Main] Color Picker overlay closed (checkmark).");
          delay(150); // Small debounce
        }
      }
      // Dragging/Tapping inside the Color Ring boundaries
      if (dist_sq >= CLOSE_BTN_RADIUS * CLOSE_BTN_RADIUS && 
          dist_sq <= WHEEL_OUTER_RADIUS * WHEEL_OUTER_RADIUS) {
        float angle_deg = atan2(dy, dx) * RAD_TO_DEG;
        if (angle_deg < 0) angle_deg += 360;
        
        int segIndex = (int)(angle_deg) / 36 % 10;
        activeSegmentIndex = segIndex;
        activeColor = DisplayManager::getSegmentColor(activeSegmentIndex);
        
        // Print color update to serial console
        static unsigned long lastPrintTime = 0;
        if (millis() - lastPrintTime >= 100) {
          Serial.printf("[Main] Color drag: Segment=%d, RGB=0x%04X\n", activeSegmentIndex, activeColor);
          lastPrintTime = millis();
        }
      }
    } else {
      // Normal Quadrant Interactions
      if (!lastTouchedState) {
        if (tx < SCREEN_CENTER_X && ty < SCREEN_CENTER_Y) {
          // Top-Left: Power Toggle
          lampOn = !lampOn;
          Serial.printf("[Main] Power Switch toggled: %s\n", lampOn ? "ON" : "OFF");
        } 
        else if (tx >= SCREEN_CENTER_X && ty < SCREEN_CENTER_Y) {
          // Top-Right: Open Color Picker Overlay
          colorPickerActive = true;
          Serial.println("[Main] Opening Color Picker overlay...");
        } 
        else if (tx < SCREEN_CENTER_X && ty >= SCREEN_CENTER_Y) {
          // Bottom-Left: Brightness Down (-)
          if (brightness > MIN_BRIGHTNESS) {
            brightness -= BRIGHTNESS_STEP;
            Serial.printf("[Main] Brightness decreased: %d%%\n", brightness);
          }
        } 
        else if (tx >= SCREEN_CENTER_X && ty >= SCREEN_CENTER_Y) {
          // Bottom-Right: Brightness Up (+)
          if (brightness < MAX_BRIGHTNESS) {
            brightness += BRIGHTNESS_STEP;
            Serial.printf("[Main] Brightness increased: %d%%\n", brightness);
          }
        }
        delay(150); // Small debounce to prevent accidental double-clicks
      }
    }
  }
  
  lastTouchedState = isTouched;

  // Draw updated states
  DisplayManager::update(
    isTouched,
    TouchManager::getX(),
    TouchManager::getY(),
    TouchManager::getGestureName(),
    TouchManager::getEventName(),
    lampOn,
    brightness,
    activeColor,
    activeSegmentIndex,
    colorPickerActive
  );

  // Yield to keep the ESP32 Wi-Fi stack happy
  delay(10);
}
