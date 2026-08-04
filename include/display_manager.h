#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>

namespace DisplayManager {
    // Initialize display and turn on backlight
    void init();

    // Redraw screen with latest touch information and state updates
    void update(bool isTouched, int x, int y, const char* gestureName, const char* eventName);

    // Draw full-screen OTA firmware update progress
    void drawOtaProgress(unsigned int progress, unsigned int total);

    // Draw full-screen OTA error message
    void drawOtaError(const char* errorMsg);
}

#endif // DISPLAY_MANAGER_H
