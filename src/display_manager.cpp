#include "display_manager.h"
#include "project_config.h"
#include <WiFi.h>
#include <TFT_eSPI.h>

namespace DisplayManager {
    static TFT_eSPI tft = TFT_eSPI();
    
    // UI State Caching
    static bool lastColorPickerActive = false;
    static bool lastLampOn = false;
    static int lastBrightness = -1;
    static uint16_t lastColor = 0;
    static int lastHue = -1;
    
    static int lastDotX = -1;
    static int lastDotY = -1;
    static bool lastTouched = false;

    // Helper: Fast HSL/HSV to RGB565 converter
    uint16_t hueToRGB565(int hue) {
        // hue should be in range 0-359
        hue = (hue % 360 + 360) % 360;
        
        float h = hue / 60.0;
        float x = 1.0 - fabs(fmod(h, 2.0) - 1.0);
        float r = 0, g = 0, b = 0;
        
        if (h < 1.0) { r = 1.0; g = x; }
        else if (h < 2.0) { r = x; g = 1.0; }
        else if (h < 3.0) { g = 1.0; b = x; }
        else if (h < 4.0) { g = x; b = 1.0; }
        else if (h < 5.0) { r = x; b = 1.0; }
        else { r = 1.0; b = x; }
        
        uint8_t red = r * 255;
        uint8_t green = g * 255;
        uint8_t blue = b * 255;
        
        return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3);
    }

    void drawStaticQuadrants() {
        tft.fillScreen(TFT_BLACK);
        
        // Draw screen boundary circle (white)
        tft.drawCircle(120, 120, 119, TFT_WHITE);
        tft.drawCircle(120, 120, 118, TFT_DARKGREY);

        // Draw quadrant divider crosshair (grey)
        tft.drawLine(120, 0, 120, 240, TFT_DARKGREY);
        tft.drawLine(0, 120, 240, 120, TFT_DARKGREY);

        tft.setTextDatum(MC_DATUM);

        // Bottom-Left Quadrant: Brightness Down (-)
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("BRIGHT -", 60, 150, 2);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("-", 60, 185, 4);

        // Bottom-Right Quadrant: Brightness Up (+)
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("BRIGHT +", 180, 150, 2);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("+", 180, 185, 4);
    }

    void drawStaticColorPicker() {
        tft.fillScreen(TFT_BLACK);
        
        // Draw outer boundary
        tft.drawCircle(120, 120, 119, TFT_WHITE);
        tft.drawCircle(120, 120, 118, TFT_DARKGREY);

        // Draw selection title
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("SELECT COLOR", 120, 25, 2);

        // Draw HSL continuous color wheel gradient
        // Sweep 360 degrees and draw radial lines from inner radius (55) to outer radius (110)
        for (int h = 0; h < 360; h++) {
            float angle = h * DEG_TO_RAD;
            float ca = cos(angle);
            float sa = sin(angle);
            uint16_t col = hueToRGB565(h);
            tft.drawLine(120 + WHEEL_INNER_RADIUS * ca, 120 + WHEEL_INNER_RADIUS * sa,
                         120 + WHEEL_OUTER_RADIUS * ca, 120 + WHEEL_OUTER_RADIUS * sa,
                         col);
        }

        // Draw central "X" close button (radius 25)
        tft.fillCircle(120, 120, CLOSE_BTN_RADIUS, TFT_RED);
        tft.drawCircle(120, 120, CLOSE_BTN_RADIUS, TFT_WHITE);
        tft.setTextColor(TFT_WHITE);
        tft.drawString("X", 120, 120, 4);
    }

    void init() {
        Serial.println("[DisplayManager] Initializing TFT display (GC9A01)...");
        
        // Ensure backlight pin is configured and ON
        pinMode(TFT_BLK_PIN, OUTPUT);
        digitalWrite(TFT_BLK_PIN, HIGH);

        // Perform manual hardware reset on the display module to ensure clean startup
        Serial.println("[DisplayManager] Performing hardware reset...");
        pinMode(TFT_RST_PIN, OUTPUT);
        digitalWrite(TFT_RST_PIN, HIGH);
        delay(10);
        digitalWrite(TFT_RST_PIN, LOW);
        delay(50);
        digitalWrite(TFT_RST_PIN, HIGH);
        delay(150);

        tft.init();
        tft.setRotation(0);
        
        // Render initial view
        drawStaticQuadrants();
        Serial.println("[DisplayManager] Display initialized.");
    }

    void update(bool isTouched, int x, int y, const char* gestureName, const char* eventName,
                bool lampOn, int brightness, uint16_t color, int hue, bool colorPickerActive) {
        
        // 1. Transition between screens
        if (colorPickerActive != lastColorPickerActive) {
            if (colorPickerActive) {
                drawStaticColorPicker();
                lastHue = -1; // Force selected hue marker update
            } else {
                drawStaticQuadrants();
                // Force redraw of all quadrant dynamic components
                lastBrightness = -1;
                lastLampOn = !lampOn; 
                lastColor = 0;
            }
            lastColorPickerActive = colorPickerActive;
        }

        // 2. Erase previous touch tracking dot
        if (lastTouched && (!isTouched || x != lastDotX || y != lastDotY)) {
            if (lastDotX != -1 && lastDotY != -1) {
                // Erase dot with black
                tft.fillCircle(lastDotX, lastDotY, 6, TFT_BLACK);
                
                // Repatch UI elements if the dot erased them
                int dist_ctr_sq = (lastDotX - 120)*(lastDotX - 120) + (lastDotY - 120)*(lastDotY - 120);
                
                if (colorPickerActive) {
                    // Repatch color wheel or X button
                    if (dist_ctr_sq <= (CLOSE_BTN_RADIUS+8)*(CLOSE_BTN_RADIUS+8)) {
                        tft.fillCircle(120, 120, CLOSE_BTN_RADIUS, TFT_RED);
                        tft.drawCircle(120, 120, CLOSE_BTN_RADIUS, TFT_WHITE);
                        tft.setTextColor(TFT_WHITE);
                        tft.drawString("X", 120, 120, 4);
                    } else if (dist_ctr_sq >= (WHEEL_INNER_RADIUS-8)*(WHEEL_INNER_RADIUS-8) && 
                               dist_ctr_sq <= (WHEEL_OUTER_RADIUS+8)*(WHEEL_OUTER_RADIUS+8)) {
                        // Repatch affected wheel slices
                        float touch_angle = atan2(lastDotY - 120, lastDotX - 120) * RAD_TO_DEG;
                        if (touch_angle < 0) touch_angle += 360;
                        int touch_hue = (int)touch_angle;
                        
                        for (int h = touch_hue - 4; h <= touch_hue + 4; h++) {
                            int norm_h = (h + 360) % 360;
                            float angle = norm_h * DEG_TO_RAD;
                            float ca = cos(angle);
                            float sa = sin(angle);
                            tft.drawLine(120 + WHEEL_INNER_RADIUS * ca, 120 + WHEEL_INNER_RADIUS * sa,
                                         120 + WHEEL_OUTER_RADIUS * ca, 120 + WHEEL_OUTER_RADIUS * sa,
                                         hueToRGB565(norm_h));
                        }
                    }
                } else {
                    // Repatch crosshairs or central HUD
                    if (dist_ctr_sq <= 30*30) {
                        lastBrightness = -1; // Forces center HUD redraw
                    }
                    if (abs(lastDotX - 120) <= 8) {
                        tft.drawLine(120, 0, 120, 240, TFT_DARKGREY);
                    }
                    if (abs(lastDotY - 120) <= 8) {
                        tft.drawLine(0, 120, 240, 120, TFT_DARKGREY);
                    }
                }

                // Repatch outer boundaries if damaged
                if (dist_ctr_sq >= 110*110) {
                    tft.drawCircle(120, 120, 119, TFT_WHITE);
                    tft.drawCircle(120, 120, 118, TFT_DARKGREY);
                }
            }
        }

        // 3. Dynamic HUD updates based on active screen
        if (colorPickerActive) {
            // Render Selected Hue Indicator
            if (hue != lastHue) {
                // Erase old marker by redrawing that part of the color wheel
                if (lastHue >= 0) {
                    for (int h = lastHue - 4; h <= lastHue + 4; h++) {
                        int norm_h = (h + 360) % 360;
                        float angle = norm_h * DEG_TO_RAD;
                        float ca = cos(angle);
                        float sa = sin(angle);
                        tft.drawLine(120 + WHEEL_INNER_RADIUS * ca, 120 + WHEEL_INNER_RADIUS * sa,
                                     120 + WHEEL_OUTER_RADIUS * ca, 120 + WHEEL_OUTER_RADIUS * sa,
                                     hueToRGB565(norm_h));
                    }
                }

                // Draw new marker (small white dot with black border)
                float angle = hue * DEG_TO_RAD;
                float ca = cos(angle);
                float sa = sin(angle);
                int mx = 120 + 82.5 * ca; // 82.5 is the midpoint of the color wheel ring
                int my = 120 + 82.5 * sa;
                tft.fillCircle(mx, my, 5, TFT_WHITE);
                tft.fillCircle(mx, my, 3, color);
                
                lastHue = hue;
            }
        } else {
            // Dynamic Quadrants View Updating
            
            // Top-Left (UL) Quadrant: Power Switch
            if (lampOn != lastLampOn) {
                // Clear UL background
                tft.fillRect(5, 5, 110, 110, TFT_BLACK);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(TFT_WHITE, TFT_BLACK);
                tft.drawString("POWER", 60, 35, 2);

                if (lampOn) {
                    // Glowing Green power label
                    tft.fillCircle(60, 75, 18, TFT_GREEN);
                    tft.setTextColor(TFT_BLACK);
                    tft.drawString("ON", 60, 75, 2);
                } else {
                    // Dark Grey/Red power label
                    tft.fillCircle(60, 75, 18, TFT_DARKGREY);
                    tft.setTextColor(TFT_WHITE);
                    tft.drawString("OFF", 60, 75, 2);
                }
                lastLampOn = lampOn;
            }

            // Top-Right (UR) Quadrant: Color Picker Trigger & Display active color
            if (color != lastColor) {
                tft.fillRect(125, 5, 110, 110, TFT_BLACK);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(TFT_WHITE, TFT_BLACK);
                tft.drawString("COLOR", 180, 35, 2);

                // Draw color circle showing active color with a white border
                tft.fillCircle(180, 75, 18, color);
                tft.drawCircle(180, 75, 18, TFT_WHITE);
                
                lastColor = color;
            }

            // Central HUD: Brightness indicator circle
            if (brightness != lastBrightness) {
                tft.fillCircle(120, 120, 22, TFT_BLACK);
                tft.drawCircle(120, 120, 22, TFT_CYAN);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(TFT_CYAN, TFT_BLACK);
                
                char brBuf[8];
                snprintf(brBuf, sizeof(brBuf), "%d%%", brightness);
                tft.drawString(brBuf, 120, 120, 1);
                
                lastBrightness = brightness;
            }
        }

        // 4. Render Touch Indicator
        if (isTouched) {
            tft.fillCircle(x, y, 6, TFT_GREEN);
            lastDotX = x;
            lastDotY = y;
        } else {
            lastDotX = -1;
            lastDotY = -1;
        }
        lastTouched = isTouched;
    }

    void drawOtaProgress(unsigned int progress, unsigned int total) {
        if (progress == 0) {
            tft.fillScreen(TFT_BLACK);
            tft.drawCircle(120, 120, 119, TFT_WHITE);
            tft.drawCircle(120, 120, 118, TFT_DARKGREY);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString("FIRMWARE UPDATE", 120, 80, 2);
        }

        int percentage = (progress * 100) / total;
        char progressBuf[32];
        snprintf(progressBuf, sizeof(progressBuf), "Updating: %d%% ", percentage);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawString(progressBuf, 120, 120, 4);

        int barWidth = 140;
        int barHeight = 12;
        int barX = 120 - barWidth / 2;
        int barY = 150;
        tft.drawRect(barX, barY, barWidth, barHeight, TFT_WHITE);
        int fillWidth = (percentage * (barWidth - 4)) / 100;
        tft.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, TFT_GREEN);
    }

    void drawOtaError(const char* errorMsg) {
        tft.fillScreen(TFT_BLACK);
        tft.drawCircle(120, 120, 119, TFT_RED);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("OTA UPDATE FAILED", 120, 80, 2);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(errorMsg, 120, 130, 2);
        tft.drawString("Rebooting...", 120, 170, 2);
    }
}
