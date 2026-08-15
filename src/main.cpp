#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "project_config.h"
#include "display_manager.h"
#include "touch_manager.h"
#include <TFT_eSPI.h>
#include <Wire.h>

#include "mqtt_manager.h"
#include <esp_sleep.h>
#include "driver/rtc_io.h"

// Lamp State Variables
bool lampOn = false;
int brightness = DEFAULT_BRIGHTNESS;
uint16_t activeColor = 0xFF34; // Default to Warm White (RGB 255, 230, 160)
int activeSegmentIndex = 1;    // Default to Warm White segment (index 1)
// bool colorPickerActive = false;
bool allLightsActive = false;
bool brightnessPickerActive = false;
const char* activeScene = "Manual";

// Publication retry queue state
static bool needPublishState = false;
static const char* pendingAction = nullptr;
static int publishRetryCount = 0;

// Touch State Caching
static bool lastTouchedState = false;

// Sleep and Inactivity Tracking
static bool displaySleeping = false;
static bool ignoreUntilRelease = false;
static unsigned long lastInteractionTime = 0;

void setup() {
  // Initialize Serial Logging
  Serial.begin(115200);
#ifdef ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0); // Prevent blocking on serial writes if USB is disconnected/sleeping
#endif
  delay(1000);
  Serial.println("\n=== Home Remote Starting ===");

  // Initialize display first so we can draw boot logs
  DisplayManager::init();
  DisplayManager::addBootLogLine("Display Initialized.", TFT_GREEN);

  // Initialize LEDC PWM channel for display backlight control
  ledcSetup(0, 5000, 8); // Channel 0, 5kHz, 8-bit resolution
  ledcAttachPin(TFT_BLK_PIN, 0);
  ledcWrite(0, 255); // Default to full brightness (100% duty)

  // Initialize boot animation on display
  DisplayManager::drawBootAnimation(0);

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

  // Wait for Wi-Fi connection with 10-second timeout, retrying every 2 seconds, while animating dots
  unsigned long wifiStart = millis();
  bool wifiConnected = false;
  int animStep = 0;
  unsigned long lastWifiSerialPrint = 0;

  while (millis() - wifiStart < 10000) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      break;
    }
    
    if (millis() - lastWifiSerialPrint >= 2000) {
      Serial.println("[WiFi] Connecting...");
      lastWifiSerialPrint = millis();
    }

    DisplayManager::drawBootAnimation(animStep++);
    delay(200); // 5 frames per second
  }

  if (wifiConnected) {
    Serial.println("[WiFi] Connected successfully!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] Connection timed out!");
  }

  // Configure MQTT/diyHue
  MQTTManager::init();

  // Connect to diyHue (MQTT) with 10-second timeout, retrying every 2 seconds, while animating dots
  if (WiFi.status() == WL_CONNECTED) {
    unsigned long mqttStart = millis();
    bool mqttConnected = false;
    unsigned long lastMqttSerialPrint = 0;

    while (millis() - mqttStart < 10000) {
      if (MQTTManager::connect()) {
        mqttConnected = true;
        break;
      }
      
      if (millis() - lastMqttSerialPrint >= 2000) {
        Serial.println("[MQTT] Connecting...");
        lastMqttSerialPrint = millis();
      }

      DisplayManager::drawBootAnimation(animStep++);
      delay(200); // 5 frames per second
    }

    if (mqttConnected) {
      Serial.println("[MQTT] Connected successfully!");
    } else {
      Serial.println("[MQTT] Connection timed out!");
    }
  } else {
    Serial.println("[MQTT] No WiFi, skipped MQTT connection.");
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

  // Monitor connection and retry publishing only when display is awake
  if (!displaySleeping) {
    // Monitor and recover WiFi connection if dropped
    if (WiFi.status() != WL_CONNECTED) {
      static unsigned long lastWifiReconnect = 0;
      if (millis() - lastWifiReconnect > 3000) { // Retry every 3 seconds until connected
        lastWifiReconnect = millis();
        Serial.println("[WiFi] Re-initiating connection...");
        WiFi.mode(WIFI_STA);
        IPAddress local_IP(STATIC_IP_ADDR);
        IPAddress gateway(GATEWAY_IP_ADDR);
        IPAddress subnet(SUBNET_MASK);
        IPAddress primaryDNS(DNS_PRIMARY);
        IPAddress secondaryDNS(DNS_SECONDARY);
        WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
      }
    }

    // Process publication retry queue (non-blocking)
    if (needPublishState || pendingAction != nullptr) {
      static unsigned long lastPublishAttempt = 0;
      if (millis() - lastPublishAttempt > 1500) { // Try every 1.5 seconds if failing
        lastPublishAttempt = millis();
        
        if (WiFi.status() == WL_CONNECTED && MQTTManager::isConnected()) {
          bool success = false;
          if (pendingAction != nullptr) {
            success = MQTTManager::publishAction(pendingAction);
            if (success) {
              pendingAction = nullptr; // Clear pending action on success
              publishRetryCount = 0;
              Serial.println("[MQTT] Queued Action published successfully.");
            }
          } else if (needPublishState) {
            success = MQTTManager::publishState();
            if (success) {
              needPublishState = false; // Clear pending state on success
              publishRetryCount = 0;
              Serial.println("[MQTT] Queued State published successfully.");
            }
          }
          
          if (!success) {
            publishRetryCount++;
            Serial.printf("[MQTT] Publish failed. Retrying in 1.5s... (Attempt %d/5)\n", publishRetryCount);
          }
        } else {
          Serial.println("[MQTT] Queue deferred: Waiting for network/broker connection...");
        }

        // Drop after 5 attempts to prevent blocking or log spam
        if (publishRetryCount >= 5) {
          Serial.println("[MQTT] Queue dropped: Maximum publish retries (5) exceeded.");
          pendingAction = nullptr;
          needPublishState = false;
          publishRetryCount = 0;
        }
      }
    }
  }

  bool isTouched = TouchManager::isTouched();

  // Inactivity / Sleep state machine logic
  if (isTouched) {
    lastInteractionTime = millis();
  } else {
    // Clear ignore flag when user lifts their finger
    ignoreUntilRelease = false;
  }

  // Latched debug reading of TOUCH_INT_PIN
  static bool intPinWasLow = false;
  if (digitalRead(TOUCH_INT_PIN) == LOW) {
    intPinWasLow = true;
  }

  static unsigned long lastDebugPrint = 0;
  if (millis() - lastDebugPrint > 500) {
    lastDebugPrint = millis();
    if (isTouched) {
      Serial.printf("[Debug] TOUCH_INT_PIN (GPIO %d) latched LOW: %s\n", TOUCH_INT_PIN, intPinWasLow ? "YES" : "NO");
    }
    intPinWasLow = false;
  }

  // Check for inactivity timeout (10 seconds)
  if (!displaySleeping && (millis() - lastInteractionTime > 10000)) {
    // If any overlay is active, close it first
    if (allLightsActive || brightnessPickerActive) {
      allLightsActive = false;
      brightnessPickerActive = false;
      Serial.println("[Main] Inactivity timeout: Closing active overlay/picker.");
    }
    
    // Draw the final state before screen goes black
    DisplayManager::update(
      lampOn,
      brightness,
      activeColor,
      activeSegmentIndex,
      allLightsActive,
      brightnessPickerActive
    );
    
    // Put display backlight to sleep (turn off PWM)
    ledcWrite(0, 0);
    
    // Shut down Wi-Fi radio to save power
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[Main] Inactivity timeout: Entering Light Sleep & turning OFF WiFi...");
    
    displaySleeping = true;

    // Configure power domains: keep the RTC peripheral power domain powered on
    // so internal RTC pull-ups and the EXT1 wakeup controller remain functional.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Configure all touch screen pins as RTC IOs during sleep:
    // 1. INT pin (GPIO 1) as input with RTC pull-up enabled for ext1 wakeup
    rtc_gpio_init((gpio_num_t)TOUCH_INT_PIN);
    rtc_gpio_set_direction((gpio_num_t)TOUCH_INT_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en((gpio_num_t)TOUCH_INT_PIN);
    rtc_gpio_pulldown_dis((gpio_num_t)TOUCH_INT_PIN);

    // 2. RST pin (GPIO 2) as output driven HIGH by the RTC domain to prevent reset
    rtc_gpio_init((gpio_num_t)TOUCH_RST_PIN);
    rtc_gpio_set_direction((gpio_num_t)TOUCH_RST_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level((gpio_num_t)TOUCH_RST_PIN, 1);

    // 3. I2C SDA/SCL pins (GPIO 11/12) with RTC pull-ups enabled to prevent floating
    rtc_gpio_init((gpio_num_t)TOUCH_SDA_PIN);
    rtc_gpio_set_direction((gpio_num_t)TOUCH_SDA_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en((gpio_num_t)TOUCH_SDA_PIN);
    rtc_gpio_pulldown_dis((gpio_num_t)TOUCH_SDA_PIN);

    rtc_gpio_init((gpio_num_t)TOUCH_SCL_PIN);
    rtc_gpio_set_direction((gpio_num_t)TOUCH_SCL_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en((gpio_num_t)TOUCH_SCL_PIN);
    rtc_gpio_pulldown_dis((gpio_num_t)TOUCH_SCL_PIN);

    // Enable wakeup on touch interrupt (falling edge level-LOW on TOUCH_INT_PIN)
    esp_sleep_enable_ext1_wakeup(1ULL << TOUCH_INT_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
    
    // Enter hardware Light Sleep (CPU suspends execution here)
    esp_light_sleep_start();

    // De-initialize RTC IO pins to restore normal digital and I2C matrix configurations
    rtc_gpio_deinit((gpio_num_t)TOUCH_INT_PIN);
    rtc_gpio_deinit((gpio_num_t)TOUCH_RST_PIN);
    rtc_gpio_deinit((gpio_num_t)TOUCH_SDA_PIN);
    rtc_gpio_deinit((gpio_num_t)TOUCH_SCL_PIN);

    // Re-establish active digital pin modes for touch screen
    pinMode(TOUCH_INT_PIN, INPUT_PULLUP);
    pinMode(TOUCH_RST_PIN, OUTPUT);
    digitalWrite(TOUCH_RST_PIN, HIGH);

    // Re-initialize I2C bus to restore routing and settings
    Wire.begin(TOUCH_SDA_PIN, TOUCH_SCL_PIN);
    Wire.setTimeOut(50);

    displaySleeping = false;
    ignoreUntilRelease = true; // Wait for touch release to prevent command double-firing
    lastInteractionTime = millis();

    // Fade in the display backlight over 1 second (51 steps * 20ms = ~1.02s)
    // Touches are ignored during this period because we do not poll coordinates,
    // and ignoreUntilRelease forces user to lift finger first.
    for (int duty = 0; duty <= 255; duty += 5) {
      ledcWrite(0, duty);
      delay(20);
    }
    Serial.println("[Main] Woke up from Light Sleep! Display backlight faded ON (1s).");
    Serial.println("[Main] Hardware wake-up event detected from EXT1 (TP_INT LOW)!");

    // Re-enable WiFi and start connecting immediately on wake-up
    WiFi.mode(WIFI_STA);
    IPAddress local_IP(STATIC_IP_ADDR);
    IPAddress gateway(GATEWAY_IP_ADDR);
    IPAddress subnet(SUBNET_MASK);
    IPAddress primaryDNS(DNS_PRIMARY);
    IPAddress secondaryDNS(DNS_SECONDARY);
    WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.println("[WiFi] Display woke up. Initiated WiFi connection.");
    
    // Force immediate MQTT connection attempt once WiFi is ready
    MQTTManager::forceReconnect();
    
    delay(150); // Debounce
  }

  // Process UI touches only if display is awake and we are not ignoring the waking touch
  if (isTouched && !displaySleeping && !ignoreUntilRelease) {
    int tx = 240 - TouchManager::getX();
    int ty = 240 - TouchManager::getY();
    
    // Calculate polar coordinates relative to screen center
    int dx = tx - SCREEN_CENTER_X;
    int dy = ty - SCREEN_CENTER_Y;
    int dist_sq = dx * dx + dy * dy;

    if (allLightsActive) {
      if (!lastTouchedState) {
        if (tx < SCREEN_CENTER_X) {
          // Left side: GOODNIGHT
          Serial.println("[Main] GOODNIGHT button pressed.");
          
          // Queue the one-off "goodnight" action for the retry queue
          pendingAction = "goodnight";
          publishRetryCount = 0;
          
          allLightsActive = false;
          delay(150); // Debounce
        } else {
          // Right side: CANCEL
          Serial.println("[Main] CANCEL button pressed.");
          allLightsActive = false;
          delay(150); // Debounce
        }
      }
      
      /* Commented out old color picker logic for future use
      if (!lastTouchedState) {
        if (dist_sq <= CLOSE_BTN_RADIUS * CLOSE_BTN_RADIUS) {
          colorPickerActive = false;
          Serial.println("[Main] Color Picker overlay closed (checkmark).");
          delay(150);
        }
      }
      if (dist_sq >= CLOSE_BTN_RADIUS * CLOSE_BTN_RADIUS && 
          dist_sq <= WHEEL_OUTER_RADIUS * WHEEL_OUTER_RADIUS) {
        float angle_deg = atan2(dy, dx) * RAD_TO_DEG;
        if (angle_deg < 0) angle_deg += 360;
        int segIndex = (int)(angle_deg) / 36 % 10;
        activeSegmentIndex = segIndex;
        activeColor = DisplayManager::getSegmentColor(activeSegmentIndex);
        static unsigned long lastPrintTime = 0;
        if (millis() - lastPrintTime >= 100) {
          Serial.printf("[Main] Color drag: Segment=%d, RGB=0x%04X\n", activeSegmentIndex, activeColor);
          lastPrintTime = millis();
        }
      }
      */
    } else if (brightnessPickerActive) {
      if (dist_sq <= 33 * 33) {
        // Tapped checkmark button in the center
        if (!lastTouchedState) {
          brightnessPickerActive = false;
          Serial.println("[Main] Brightness Picker closed (checkmark).");
          delay(150); // Debounce
        }
      } else if (dist_sq >= 70 * 70 && dist_sq <= 110 * 110) {
        // Dragging/Tapping inside the slider zone
        float angle_deg = atan2(dy, dx) * RAD_TO_DEG;
        if (angle_deg < 0) angle_deg += 360;
        
        // Arc slider runs from 135 (bottom-left) to 405 (bottom-right)
        // Normalize the touch angle to fit within the active range, handling the dead-zone gap
        float angle = angle_deg;
        if (angle >= 45 && angle < 90) {
          // Closer to 100% end (45 degrees / 405 degrees)
          angle = 405.0f;
        } else if (angle >= 90 && angle < 135) {
          // Closer to 0% end (135 degrees)
          angle = 135.0f;
        } else if (angle < 45) {
          // Wrap angles in [0, 45] to [360, 405]
          angle += 360.0f;
        }
        
        int newBr = (int)((angle - 135.0f) * 100.0f / 270.0f);
        if (newBr < 1) newBr = 1;
        if (newBr > 100) newBr = 100;
        
        if (brightness != newBr) {
          brightness = newBr;
          static unsigned long lastPrintTime = 0;
          if (millis() - lastPrintTime >= 100) {
            Serial.printf("[Main] Brightness drag: %d%%\n", brightness);
            lastPrintTime = millis();
          }
        }
      }
    } else {
      // Normal Quadrant Interactions
      if (!lastTouchedState) {
        if (dist_sq <= 33 * 33) {
          // Center tapped: open brightness picker overlay!
          brightnessPickerActive = true;
          ignoreUntilRelease = true;
          Serial.println("[Main] Opening Brightness Picker overlay...");
        }
        else if (tx < SCREEN_CENTER_X && ty < SCREEN_CENTER_Y) {
          // Top-Left: Power Toggle
          lampOn = !lampOn;
          Serial.printf("[Main] Power Switch toggled: %s\n", lampOn ? "ON" : "OFF");
        } 
        else if (tx >= SCREEN_CENTER_X && ty < SCREEN_CENTER_Y) {
          // Top-Right: Open All Lights Overlay
          allLightsActive = true;
          ignoreUntilRelease = true;
          Serial.println("[Main] Opening All Lights overlay...");
        } 
        else if (tx < SCREEN_CENTER_X && ty >= SCREEN_CENTER_Y) {
          // Bottom-Left: Brightness Down (-)
          brightness = max(brightness - BRIGHTNESS_STEP, MIN_BRIGHTNESS);
          Serial.printf("[Main] Brightness decreased: %d%%\n", brightness);
        } 
        else if (tx >= SCREEN_CENTER_X && ty >= SCREEN_CENTER_Y) {
          // Bottom-Right: Brightness Up (+)
          brightness = min(brightness + BRIGHTNESS_STEP, MAX_BRIGHTNESS);
          Serial.printf("[Main] Brightness increased: %d%%\n", brightness);
        }
        delay(150); // Small debounce to prevent accidental double-clicks
      }
    }
  }
  
  // Queue state updates to Home Assistant when user releases touch
  if (lastTouchedState && !isTouched && !displaySleeping && !ignoreUntilRelease) {
    needPublishState = true;
    publishRetryCount = 0;
  }
  
  lastTouchedState = isTouched;

  // Redraw the screen only when the display is active/awake
  if (!displaySleeping) {
    DisplayManager::update(
      lampOn,
      brightness,
      activeColor,
      activeSegmentIndex,
      allLightsActive,
      brightnessPickerActive
    );
  }

  // Yield to keep the ESP32 Wi-Fi stack happy
  delay(10);
}
