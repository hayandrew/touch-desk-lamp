#include "touch_manager.h"
#include "project_config.h"
#include <Wire.h>
#include <CST816S.h>

namespace TouchManager {
    static CST816S touch(TOUCH_SDA_PIN, TOUCH_SCL_PIN, TOUCH_RST_PIN, TOUCH_INT_PIN);
    static bool touched = false;
    static int lastX = 0;
    static int lastY = 0;
    static uint8_t lastGesture = 0;
    static uint8_t lastEvent = 1; // Start with UP event (1)

    void init() {
        Serial.println("[TouchManager] Initializing Wire (I2C) master...");
        Wire.begin(TOUCH_SDA_PIN, TOUCH_SCL_PIN);
        
        Serial.println("[TouchManager] Initializing touch screen (CST816S)...");
        touch.begin();
        
        // Keep touch sensor awake for the testing/demo phase
        touch.disable_auto_sleep();
        Serial.println("[TouchManager] Touch screen initialized.");
    }

    void update() {
        if (touch.available()) {
            touched = (touch.data.points > 0);
            lastX = touch.data.x;
            lastY = touch.data.y;
            lastGesture = touch.data.gestureID;
            lastEvent = touch.data.event;

            if (touched) {
                Serial.printf("[Touch] Event: %s (%d), X: %d, Y: %d, Gesture: %s (0x%02X)\n",
                              getEventName(), lastEvent, lastX, lastY, getGestureName(), lastGesture);
            }
        } else {
            // If the library does not report new touch events, check if our last event
            // was an UP event to transition the state back to not touched.
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
