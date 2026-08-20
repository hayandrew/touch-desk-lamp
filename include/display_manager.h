#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>

namespace DisplayManager {
    // Initialize display and turn on backlight
    void init();

    // Display a line in the scrolling verbose boot log screen
    void addBootLogLine(const char* line, uint16_t color);

    // Draw three animated horizontal blue loading dots
    void drawBootAnimation(int step);

    // Redraw screen with latest studio power state
    void update(bool isOn);

    // Draw full-screen OTA firmware update progress
    void drawOtaProgress(unsigned int progress, unsigned int total);

    // Draw full-screen OTA error message
    void drawOtaError(const char* errorMsg);
}

#endif // DISPLAY_MANAGER_H
