#include "mqtt_manager.h"
#include "project_config.h"
#include "display_manager.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Fallback declarations if not defined by SCons builder
#ifndef MQTT_HOST
#define MQTT_HOST "192.168.68.50"
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_USER
#define MQTT_USER "mqtt-user"
#endif
#ifndef MQTT_PASS
#define MQTT_PASS "mqtt-user"
#endif

// Link global state variables in main.cpp
extern bool lampOn;
extern int brightness;
extern uint16_t activeColor;
extern int activeSegmentIndex;
extern bool colorPickerActive;

namespace MQTTManager {
    struct RGBColor {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    // Standard RGB values mapped to our 10 discrete wedges
    static const RGBColor SEGMENT_RGBS[10] = {
        {225, 240, 255}, // 0: Cold White
        {255, 230, 160}, // 1: Warm White
        {255, 0, 0},     // 2: Red
        {255, 105, 180}, // 3: Pink
        {255, 165, 0},   // 4: Orange
        {255, 255, 0},   // 5: Yellow
        {0, 200, 0},     // 6: Green
        {0, 0, 255},     // 7: Blue
        {75, 0, 130},    // 8: Indigo
        {180, 50, 240}   // 9: Violet
    };

    static WiFiClient wifiClient;
    static PubSubClient client(wifiClient);
    
    static unsigned long lastReconnectAttempt = 0;
    
    // Home Assistant MQTT Light Topics
    static const char* discoveryTopic = "homeassistant/light/desk_lamp/config";
    static const char* stateTopic     = "homeassistant/light/desk_lamp/state";
    static const char* setTopic       = "homeassistant/light/desk_lamp/set";

    // Helper: Finds which of our 10 segments is closest to the target RGB color
    int findClosestSegment(uint8_t r, uint8_t g, uint8_t b) {
        int min_dist = 999999;
        int best_index = 1; // Default to Warm White
        
        for (int i = 0; i < 10; i++) {
            int dr = (int)r - SEGMENT_RGBS[i].r;
            int dg = (int)g - SEGMENT_RGBS[i].g;
            int db = (int)b - SEGMENT_RGBS[i].b;
            int dist = dr * dr + dg * dg + db * db;
            if (dist < min_dist) {
                min_dist = dist;
                best_index = i;
            }
        }
        return best_index;
    }

    // MQTT message receiver callback
    void callback(char* topic, byte* payload, unsigned int length) {
        Serial.print("[MQTT] Message arrived on topic: ");
        Serial.println(topic);
        
        // Parse incoming HA command JSON
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, payload, length);
        if (error) {
            Serial.print("[MQTT] JSON parse failed: ");
            Serial.println(error.c_str());
            return;
        }
        
        // 1. Parse ON/OFF switch state
        if (doc.containsKey("state")) {
            const char* stateVal = doc["state"];
            bool newLampOn = (strcmp(stateVal, "ON") == 0);
            if (newLampOn != lampOn) {
                lampOn = newLampOn;
                Serial.printf("[MQTT] State updated from HA: %s\n", lampOn ? "ON" : "OFF");
            }
        }
        
        // 2. Parse Brightness level (0-255)
        if (doc.containsKey("brightness")) {
            int briVal = doc["brightness"];
            // Map 0-255 range to 10%-100%
            int newBrightness = (briVal * 100) / 255;
            // Round to the nearest BRIGHTNESS_STEP (10)
            newBrightness = ((newBrightness + BRIGHTNESS_STEP / 2) / BRIGHTNESS_STEP) * BRIGHTNESS_STEP;
            if (newBrightness < MIN_BRIGHTNESS) newBrightness = MIN_BRIGHTNESS;
            if (newBrightness > MAX_BRIGHTNESS) newBrightness = MAX_BRIGHTNESS;
            
            if (newBrightness != brightness) {
                brightness = newBrightness;
                Serial.printf("[MQTT] Brightness updated from HA: %d%%\n", brightness);
            }
        }
        
        // 3. Parse RGB Color values
        if (doc.containsKey("color")) {
            uint8_t r = doc["color"]["r"];
            uint8_t g = doc["color"]["g"];
            uint8_t b = doc["color"]["b"];
            
            int newSeg = findClosestSegment(r, g, b);
            if (newSeg != activeSegmentIndex) {
                activeSegmentIndex = newSeg;
                activeColor = DisplayManager::getSegmentColor(activeSegmentIndex);
                Serial.printf("[MQTT] Color updated from HA: Segment %d (0x%04X)\n", activeSegmentIndex, activeColor);
            }
        }
    }

    // Connects to the MQTT broker and registers auto-discovery
    bool connect() {
        Serial.printf("[MQTT] Connecting to broker %s:%d...\n", MQTT_HOST, MQTT_PORT);
        String clientId = "esp32c3_lamp_" + String(random(0xffff), HEX);
        
        bool success = false;
        if (strlen(MQTT_USER) > 0) {
            success = client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);
        } else {
            success = client.connect(clientId.c_str());
        }
        
        if (success) {
            Serial.println("[MQTT] Connected successfully!");
            
            // Publish MQTT discovery payload to auto-register with Home Assistant
            StaticJsonDocument<512> doc;
            doc["name"] = "Desk Lamp Touch";
            doc["unique_id"] = "esp32c3_touch_lamp_1";
            doc["state_topic"] = stateTopic;
            doc["command_topic"] = setTopic;
            doc["schema"] = "json";
            doc["brightness"] = true;
            JsonArray colorModes = doc.createNestedArray("supported_color_modes");
            colorModes.add("rgb");
            
            char buffer[512];
            serializeJson(doc, buffer);
            client.publish(discoveryTopic, buffer, true);
            Serial.println("[MQTT] Auto-discovery config published.");
            
            // Subscribe to incoming Home Assistant set commands
            client.subscribe(setTopic);
            Serial.println("[MQTT] Subscribed to command topic.");
            
            // Publish initial switch state
            publishState();
            return true;
        } else {
            Serial.print("[MQTT] Connection failed, state code: ");
            Serial.println(client.state());
            return false;
        }
    }

    void init() {
        client.setServer(MQTT_HOST, MQTT_PORT);
        client.setCallback(callback);
        // Increase MQTT packet buffer size for discovery JSON packets
        client.setBufferSize(512);
        Serial.println("[MQTT] MQTT Manager initialized.");
    }

    void update() {
        if (WiFi.status() != WL_CONNECTED) return;
        
        if (!client.connected()) {
            unsigned long now = millis();
            if (now - lastReconnectAttempt > 5000) {
                lastReconnectAttempt = now;
                if (connect()) {
                    lastReconnectAttempt = 0;
                }
            }
        } else {
            client.loop();
        }
    }

    bool isConnected() {
        return client.connected();
    }

    void publishState() {
        if (!client.connected()) return;
        
        StaticJsonDocument<256> doc;
        doc["state"] = lampOn ? "ON" : "OFF";
        doc["brightness"] = (brightness * 255) / 100;
        
        // Decode RGB values from active segment color to update the color wheel slider in HA
        uint16_t color = activeColor;
        uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
        uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
        uint8_t b = (color & 0x1F) * 255 / 31;
        
        JsonObject colorObj = doc.createNestedObject("color");
        colorObj["r"] = r;
        colorObj["g"] = g;
        colorObj["b"] = b;
        doc["color_mode"] = "rgb";
        
        char buffer[256];
        serializeJson(doc, buffer);
        client.publish(stateTopic, buffer, true);
        
        Serial.print("[MQTT] State broadcast: ");
        Serial.println(buffer);
    }
}
