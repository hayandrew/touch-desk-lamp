#ifndef TOUCH_MANAGER_H
#define TOUCH_MANAGER_H

#include <Arduino.h>

namespace TouchManager {
    // Initialize the CST816S touch controller
    void init();

    // Poll the touch controller for new events. Should be called in the main loop.
    void update();

    // Check if the screen is currently being touched
    bool isTouched();

    // Get the current touch coordinates
    int getX();
    int getY();

    // Get the text representation of the last detected gesture
    const char* getGestureName();

    // Get the text representation of the touch event state (Down, Up, Contact)
    const char* getEventName();

    // Get the raw gesture ID
    uint8_t getGestureID();
}

#endif // TOUCH_MANAGER_H
