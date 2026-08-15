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

    // Redraw screen with latest lamp state and overlay state
    void update(bool lampOn, int brightness, uint16_t color, int activeSegmentIndex, bool allLightsActive, bool brightnessPickerActive);

    // Convert a hue value (0-359) to a 16-bit RGB565 color value
    uint16_t hueToRGB565(int hue);

    // Get the RGB565 color value of a specific segment index (0-9)
    uint16_t getSegmentColor(int segmentIndex);

    // Draw full-screen OTA firmware update progress
    void drawOtaProgress(unsigned int progress, unsigned int total);

    // Draw full-screen OTA error message
    void drawOtaError(const char* errorMsg);
}

#endif // DISPLAY_MANAGER_H
