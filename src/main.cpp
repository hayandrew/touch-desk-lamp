#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "project_config.h"
#include "display_manager.h"
#include "touch_manager.h"
#include <TFT_eSPI.h>

#include "mqtt_manager.h"

// Lamp State Variables
bool lampOn = false;
int brightness = DEFAULT_BRIGHTNESS;
uint16_t activeColor = 0xFF34; // Default to Warm White (RGB 255, 230, 160)
int activeSegmentIndex = 1;    // Default to Warm White segment (index 1)
bool colorPickerActive = false;

// Touch State Caching
static bool lastTouchedState = false;

// Sleep and Inactivity Tracking
static bool displaySleeping = false;
static bool ignoreUntilRelease = false;
static unsigned long lastInteractionTime = 0;

void setup() {
  // Initialize Serial Logging
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Home Remote Starting ===");

  // Initialize display first so we can draw boot logs
  DisplayManager::init();
  DisplayManager::addBootLogLine("Display Initialized.", TFT_GREEN);

  // Initialize touch manager
  TouchManager::init();
  DisplayManager::addBootLogLine("Touch Initialized.", TFT_GREEN);

  // Connect to Wi-Fi using static IP settings
  DisplayManager::addBootLogLine("Connecting to WiFi...", TFT_WHITE);
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

  // Wait for Wi-Fi connection with 10-second timeout, retrying every 2 seconds
  unsigned long wifiStart = millis();
  bool wifiConnected = false;
  while (millis() - wifiStart < 10000) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      break;
    }
    Serial.print("[WiFi] Connecting...\n");
    DisplayManager::addBootLogLine("WiFi: Connecting...", TFT_YELLOW);
    delay(2000);
  }

  if (wifiConnected) {
    Serial.println("[WiFi] Connected successfully!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
    char wifiBuf[64];
    snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: OK (%s)", WiFi.localIP().toString().c_str());
    DisplayManager::addBootLogLine(wifiBuf, TFT_GREEN);
  } else {
    Serial.println("[WiFi] Connection timed out!");
    DisplayManager::addBootLogLine("WiFi: No Connection", TFT_RED);
  }

  // Configure MQTT/diyHue
  MQTTManager::init();

  // Connect to diyHue (MQTT) with 10-second timeout, retrying every 2 seconds
  if (WiFi.status() == WL_CONNECTED) {
    DisplayManager::addBootLogLine("Connecting to diyHue...", TFT_WHITE);
    unsigned long mqttStart = millis();
    bool mqttConnected = false;
    while (millis() - mqttStart < 10000) {
      if (MQTTManager::connect()) {
        mqttConnected = true;
        break;
      }
      Serial.print("[MQTT] Connecting...\n");
      DisplayManager::addBootLogLine("diyHue: Retrying...", TFT_YELLOW);
      delay(2000);
    }

    if (mqttConnected) {
      DisplayManager::addBootLogLine("diyHue: Connected!", TFT_GREEN);
    } else {
      DisplayManager::addBootLogLine("diyHue: No Connection", TFT_RED);
    }
  } else {
    DisplayManager::addBootLogLine("diyHue: No WiFi, skipped", TFT_RED);
  }

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
  DisplayManager::addBootLogLine("OTA Services Ready.", TFT_GREEN);

  DisplayManager::addBootLogLine("Boot Complete!", TFT_GREEN);
  delay(1500); // Give the user time to read the final screen status before main loop

  lastInteractionTime = millis();
  Serial.println("=== Setup Complete. Entering loop ===\n");
}

void loop() {
  // Process OTA requests
  ArduinoOTA.handle();

  // Process MQTT messages and maintain connection
  MQTTManager::update();

  // Poll touch panel state
  TouchManager::update();

  bool isTouched = TouchManager::isTouched();

  // Inactivity / Sleep state machine logic
  if (isTouched) {
    lastInteractionTime = millis();
    
    if (displaySleeping) {
      displaySleeping = false;
      ignoreUntilRelease = true;
      
      // Wake up the backlight
      pinMode(TFT_BLK_PIN, OUTPUT);
      digitalWrite(TFT_BLK_PIN, HIGH);
      Serial.println("[Main] Display woken up by touch. Ignoring initial press coordinates.");
      
      delay(150); // Small debounce
    }
  } else {
    // Clear ignore flag when user lifts their finger
    ignoreUntilRelease = false;
  }

  // Check for inactivity timeout (30 seconds)
  if (!displaySleeping && (millis() - lastInteractionTime > 30000)) {
    // If the color picker is active, close it first
    if (colorPickerActive) {
      colorPickerActive = false;
      Serial.println("[Main] Inactivity timeout: Closing Color Picker.");
    }
    
    // Draw the final state (clean quadrants screen) before screen goes black
    DisplayManager::update(
      lampOn,
      brightness,
      activeColor,
      activeSegmentIndex,
      colorPickerActive
    );
    
    // Put display backlight to sleep
    pinMode(TFT_BLK_PIN, OUTPUT);
    digitalWrite(TFT_BLK_PIN, LOW);
    Serial.println("[Main] Inactivity timeout: Putting display to sleep.");
    
    displaySleeping = true;
  }

  // Process UI touches only if display is awake and we are not ignoring the waking touch
  if (isTouched && !displaySleeping && !ignoreUntilRelease) {
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
  
  // Broadcast local state updates to Home Assistant when user releases touch
  if (lastTouchedState && !isTouched && !displaySleeping && !ignoreUntilRelease) {
    MQTTManager::publishState();
  }
  
  lastTouchedState = isTouched;

  // Redraw the screen only when the display is active/awake
  if (!displaySleeping) {
    DisplayManager::update(
      lampOn,
      brightness,
      activeColor,
      activeSegmentIndex,
      colorPickerActive
    );
  }

  // Yield to keep the ESP32 Wi-Fi stack happy
  delay(10);
}
