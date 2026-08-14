#include "touch_manager.h"
#include "project_config.h"
#include <Wire.h>
#include <CST816S.h>

namespace TouchManager {
    static CST816S* touchPtr = nullptr;
    static bool touched = false;
    static int lastX = 0;
    static int lastY = 0;
    static uint8_t lastGesture = 0;
    static uint8_t lastEvent = 1; // Start with UP event (1)
    static bool touchInitialized = false;

    void init() {
        Serial.println("\n[TouchManager] Initializing touch screen (CST816S)...");
        Serial.printf("  - SDA: %d\n", TOUCH_SDA_PIN);
        Serial.printf("  - SCL: %d\n", TOUCH_SCL_PIN);
        Serial.printf("  - RST: %d\n", TOUCH_RST_PIN);
        Serial.printf("  - INT: %d\n", TOUCH_INT_PIN);

        // Initialize the touch screen instance with configured pins
        touchPtr = new CST816S(TOUCH_SDA_PIN, TOUCH_SCL_PIN, TOUCH_RST_PIN, TOUCH_INT_PIN);
        touchPtr->begin(FALLING);
        pinMode(TOUCH_INT_PIN, INPUT_PULLUP);
        
        // Re-apply I2C settings since touchPtr->begin() resets Wire
        Wire.setTimeOut(50);
        Wire.setClock(100000); // Use 100kHz standard speed
        
        // Keep touch sensor awake for testing
        touchPtr->disable_auto_sleep();
        touchInitialized = true;
        Serial.println("[TouchManager] Touch screen initialization completed.\n");
    }

    void update() {
        if (!touchInitialized || touchPtr == nullptr) return;

        static unsigned long lastPollTime = 0;
        unsigned long now = millis();
        if (now - lastPollTime < 30) return;
        lastPollTime = now;

        Wire.beginTransmission(0x15);
        Wire.write(0x01);
        byte error = Wire.endTransmission(true);

        if (error == 0) {
            if (Wire.requestFrom(0x15, 6) == 6) {
                byte data_raw[6];
                for (int i = 0; i < 6; i++) {
                    data_raw[i] = Wire.read();
                }

                touchPtr->data.gestureID = data_raw[0];
                touchPtr->data.points = data_raw[1];
                touchPtr->data.event = data_raw[2] >> 6;
                touchPtr->data.x = ((data_raw[2] & 0xF) << 8) + data_raw[3];
                touchPtr->data.y = ((data_raw[4] & 0xF) << 8) + data_raw[5];

                touched = (touchPtr->data.points > 0);
                lastX = touchPtr->data.x;
                lastY = touchPtr->data.y;
                lastGesture = touchPtr->data.gestureID;
                lastEvent = touchPtr->data.event;

                if (touched) {
                    Serial.printf("[Touch Polled] Event: %s (%d), X: %d, Y: %d, Gesture: %s (0x%02X)\n",
                                  getEventName(), lastEvent, lastX, lastY, getGestureName(), lastGesture);
                }
            }
        } else {
            if (lastEvent == 1) { // 1 = Up
                touched = false;
            }
        }
    }

    bool isTouched() {
        return touched;
    }

    int getX() {
        return lastX;
    }

    int getY() {
        return lastY;
    }

    uint8_t getGestureID() {
        return lastGesture;
    }

    const char* getGestureName() {
        switch (lastGesture) {
            case 0x00: return "None";
            case 0x01: return "Swipe Up";
            case 0x02: return "Swipe Down";
            case 0x03: return "Swipe Left";
            case 0x04: return "Swipe Right";
            case 0x05: return "Single Click";
            case 0x0B: return "Double Click";
            case 0x0C: return "Long Press";
            default: return "Unknown";
        }
    }

    const char* getEventName() {
        switch (lastEvent) {
            case 0: return "Down";
            case 1: return "Up";
            case 2: return "Contact";
            default: return "Unknown";
        }
    }
}
