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
extern bool studioOn;

namespace MQTTManager {
    static WiFiClient wifiClient;
    static PubSubClient client(wifiClient);
    
    static unsigned long lastReconnectAttempt = 0;

    // MQTT message receiver callback
    void callback(char* topic, byte* payload, unsigned int length) {
        Serial.print("[MQTT] Message arrived on topic: ");
        Serial.println(topic);
        
        // Parse incoming HA command (usually "ON" or "OFF")
        char payloadStr[16];
        unsigned int len = min(length, (unsigned int)sizeof(payloadStr) - 1);
        memcpy(payloadStr, payload, len);
        payloadStr[len] = '\0';
        
        Serial.printf("[MQTT] Payload received: %s\n", payloadStr);

        bool newStudioState = studioOn;
        if (strcmp(payloadStr, "ON") == 0) {
            newStudioState = true;
        } else if (strcmp(payloadStr, "OFF") == 0) {
            newStudioState = false;
        }

        // Handle initial boot sync from the state topic
        if (strcmp(topic, MQTT_REMOTE_STATE) == 0) {
            client.unsubscribe(MQTT_REMOTE_STATE);
            Serial.println("[MQTT] Initial state synchronized from state topic. Unsubscribed.");
            if (newStudioState != studioOn) {
                studioOn = newStudioState;
                Serial.printf("[MQTT] State initialized to: %s\n", studioOn ? "ON" : "OFF");
            }
            return;
        }

        // Handle incoming commands from Home Assistant on the set topic
        if (strcmp(topic, MQTT_REMOTE_SET) == 0) {
            if (newStudioState != studioOn) {
                studioOn = newStudioState;
                Serial.printf("[MQTT] Studio state updated from HA command: %s\n", studioOn ? "ON" : "OFF");
                // Sync physical devices and publish state back
                publishStudioState(studioOn);
            }
        }
    }

    // Connects to the MQTT broker and registers auto-discovery
    bool connect() {
        Serial.printf("[MQTT] Connecting to broker %s:%d...\n", MQTT_HOST, MQTT_PORT);
        String clientId = "studio_remote_" + String(random(0xffff), HEX);
        
        bool success = false;
        if (strlen(MQTT_USER) > 0) {
            success = client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);
        } else {
            success = client.connect(clientId.c_str());
        }
        
        if (success) {
            Serial.println("[MQTT] Connected successfully!");
            
            // Publish MQTT discovery payload to auto-register with Home Assistant as a switch
            StaticJsonDocument<256> doc;
            doc["name"] = "Studio Controller";
            doc["unique_id"] = "studio_controller_1";
            doc["state_topic"] = MQTT_REMOTE_STATE;
            doc["command_topic"] = MQTT_REMOTE_SET;
            doc["payload_on"] = "ON";
            doc["payload_off"] = "OFF";
            
            char buffer[256];
            serializeJson(doc, buffer);
            client.publish(MQTT_REMOTE_DISCOVERY, buffer, true);
            Serial.println("[MQTT] Switch discovery config published.");
            
            // Subscribe to incoming command topic
            client.subscribe(MQTT_REMOTE_SET);
            Serial.println("[MQTT] Subscribed to command topic.");
            
            // Subscribe to state topic temporarily to read last known state on startup
            client.subscribe(MQTT_REMOTE_STATE);
            Serial.println("[MQTT] Subscribed to state topic for initial sync.");
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

    void forceReconnect() {
        lastReconnectAttempt = 0;
    }

    bool publishStudioState(bool isOn) {
        if (!client.connected()) return false;
        
        const char* payload = isOn ? "ON" : "OFF";
        
        // 1. Publish command to Studio Smart Plug
        bool successPlug = client.publish(MQTT_STUDIO_PLUG_COMMAND, payload, true);
        
        // 2. Publish command to Studio Light Group
        bool successLight = client.publish(MQTT_STUDIO_LIGHT_COMMAND, payload, true);
        
        // 3. Publish state to Studio Remote self status
        bool successRemote = client.publish(MQTT_REMOTE_STATE, payload, true);
        
        bool allSuccess = successPlug && successLight && successRemote;
        
        if (allSuccess) {
            Serial.printf("[MQTT] Studio state broadcast: %s\n", payload);
        } else {
            Serial.printf("[MQTT] State broadcast warning: plug=%s, light=%s, remote=%s\n",
                          successPlug ? "OK" : "FAIL",
                          successLight ? "OK" : "FAIL",
                          successRemote ? "OK" : "FAIL");
        }
        return allSuccess;
    }
}
