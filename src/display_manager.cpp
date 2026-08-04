#include "display_manager.h"
#include "project_config.h"
#include <WiFi.h>
#include <TFT_eSPI.h>

namespace DisplayManager {
    static TFT_eSPI tft = TFT_eSPI();
    
    // Segmented Colors Initialization
    static uint16_t segmentColors[10];
    static const char* segmentNames[10] = {
        "Cold White", "Warm White", "Red", "Pink", "Orange",
        "Yellow", "Green", "Blue", "Indigo", "Violet"
    };
    
    // UI State Caching
    static bool lastColorPickerActive = false;
    static bool lastLampOn = false;
    static int lastBrightness = -1;
    static uint16_t lastColor = 0;
    static int lastSegmentIndex = -1;
    
    static int lastDotX = -1;
    static int lastDotY = -1;
    static bool lastTouched = false;

    // Helper: Hue to RGB565 (kept for fallback compatibility if needed, but not primary)
    uint16_t hueToRGB565(int hue) {
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

    uint16_t getSegmentColor(int segmentIndex) {
        if (segmentIndex < 0 || segmentIndex >= 10) return TFT_WHITE;
        return segmentColors[segmentIndex];
    }

    // Draw checkmark symbol inside the center button with high-contrast luminance logic
    void drawCheckmark(int cx, int cy, uint16_t color) {
        // Extract RGB component weights to calculate relative luminance
        uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
        uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
        uint8_t b = (color & 0x1F) * 255 / 31;
        float luminance = 0.299f * r + 0.587f * g + 0.114f * b;
        
        // Use black checkmark for light background, white for dark background
        uint16_t chkColor = (luminance > 140.0f) ? TFT_BLACK : TFT_WHITE;
        
        // Draw ✓ symbol vector path (bold)
        tft.drawLine(cx - 8, cy, cx - 2, cy + 6, chkColor);
        tft.drawLine(cx - 8, cy + 1, cx - 2, cy + 7, chkColor);
        tft.drawLine(cx - 8, cy - 1, cx - 2, cy + 5, chkColor);
        
        tft.drawLine(cx - 2, cy + 6, cx + 10, cy - 6, chkColor);
        tft.drawLine(cx - 2, cy + 7, cx + 10, cy - 5, chkColor);
        tft.drawLine(cx - 2, cy + 5, cx + 10, cy - 7, chkColor);
    }

    void drawSegmentIndicator(int segmentIndex) {
        if (segmentIndex < 0 || segmentIndex >= 10) return;
        // Midpoint angle of the segment (wedge width is 34 degrees plus 2 degrees gap)
        float angle = (segmentIndex * 36 + 17) * DEG_TO_RAD;
        float ca = cos(angle);
        float sa = sin(angle);
        int mx = 120 + 82.5f * ca; // Centered radially in the ring
        int my = 120 + 82.5f * sa;
        
        tft.fillCircle(mx, my, 5, TFT_WHITE);
        tft.fillCircle(mx, my, 3, TFT_BLACK);
    }

    void eraseSegmentIndicator(int segmentIndex) {
        if (segmentIndex < 0 || segmentIndex >= 10) return;
        int start_angle = segmentIndex * 36;
        int end_angle = (segmentIndex + 1) * 36 - 2;
        uint16_t col = segmentColors[segmentIndex];
        
        for (int h = start_angle; h <= end_angle; h++) {
            float angle = h * DEG_TO_RAD;
            float ca = cos(angle);
            float sa = sin(angle);
            tft.drawLine(120 + WHEEL_INNER_RADIUS * ca, 120 + WHEEL_INNER_RADIUS * sa,
                         120 + WHEEL_OUTER_RADIUS * ca, 120 + WHEEL_OUTER_RADIUS * sa,
                         col);
        }
    }

    void drawStaticQuadrants() {
        tft.fillScreen(TFT_BLACK);
        
        // Draw outer boundary
        tft.drawCircle(120, 120, 119, TFT_WHITE);
        tft.drawCircle(120, 120, 118, TFT_DARKGREY);

        // Draw quadrant divider crosshair (grey)
        tft.drawLine(120, 0, 120, 240, TFT_DARKGREY);
        tft.drawLine(0, 120, 240, 120, TFT_DARKGREY);

        tft.setTextDatum(MC_DATUM);

        // Bottom-Left: Brightness Down
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("BRIGHT -", 60, 150, 2);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("-", 60, 185, 4);

        // Bottom-Right: Brightness Up
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

        // Draw 10 discrete color wedges with 2-degree gaps
        for (int i = 0; i < 10; i++) {
            int start_angle = i * 36;
            int end_angle = (i + 1) * 36 - 2;
            uint16_t col = segmentColors[i];
            
            for (int h = start_angle; h <= end_angle; h++) {
                float angle = h * DEG_TO_RAD;
                float ca = cos(angle);
                float sa = sin(angle);
                tft.drawLine(120 + WHEEL_INNER_RADIUS * ca, 120 + WHEEL_INNER_RADIUS * sa,
                             120 + WHEEL_OUTER_RADIUS * ca, 120 + WHEEL_OUTER_RADIUS * sa,
                             col);
            }
        }
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
        
        // Initialize dynamic colors
        segmentColors[0] = tft.color565(225, 240, 255); // Cold White
        segmentColors[1] = tft.color565(255, 230, 160); // Warm White
        segmentColors[2] = 0xF800;                       // Red
        segmentColors[3] = tft.color565(255, 105, 180); // Pink
        segmentColors[4] = 0xFD20;                       // Orange
        segmentColors[5] = 0xFFE0;                       // Yellow
        segmentColors[6] = tft.color565(0, 200, 0);     // Green
        segmentColors[7] = 0x001F;                       // Blue
        segmentColors[8] = tft.color565(75, 0, 130);     // Indigo
        segmentColors[9] = tft.color565(180, 50, 240);   // Violet
        
        // Render initial quadrants
        drawStaticQuadrants();
        Serial.println("[DisplayManager] Display initialized.");
    }

    void update(bool isTouched, int x, int y, const char* gestureName, const char* eventName,
                bool lampOn, int brightness, uint16_t color, int activeSegmentIndex, bool colorPickerActive) {
        
        // 1. Transition between screens
        if (colorPickerActive != lastColorPickerActive) {
            if (colorPickerActive) {
                drawStaticColorPicker();
                lastSegmentIndex = -1; // Force redraw of segment indicator
                lastColor = 0;         // Force redraw of checkmark preview
            } else {
                drawStaticQuadrants();
                // Force redraw of all quadrant components
                lastBrightness = -1;
                lastLampOn = !lampOn; 
                lastColor = 0;
            }
            lastColorPickerActive = colorPickerActive;
        }

        // 2. Erase previous touch tracking dot
        if (lastTouched && (!isTouched || x != lastDotX || y != lastDotY)) {
            if (lastDotX != -1 && lastDotY != -1) {
                tft.fillCircle(lastDotX, lastDotY, 6, TFT_BLACK);
                
                int dist_ctr_sq = (lastDotX - 120)*(lastDotX - 120) + (lastDotY - 120)*(lastDotY - 120);
                
                if (colorPickerActive) {
                    if (dist_ctr_sq <= (CLOSE_BTN_RADIUS+8)*(CLOSE_BTN_RADIUS+8)) {
                        // Repatch center checkmark
                        tft.fillCircle(120, 120, CLOSE_BTN_RADIUS, color);
                        tft.drawCircle(120, 120, CLOSE_BTN_RADIUS, TFT_WHITE);
                        drawCheckmark(120, 120, color);
                    } else if (dist_ctr_sq >= (WHEEL_INNER_RADIUS-8)*(WHEEL_INNER_RADIUS-8) && 
                               dist_ctr_sq <= (WHEEL_OUTER_RADIUS+8)*(WHEEL_OUTER_RADIUS+8)) {
                        // Repatch touched segment(s)
                        float touch_angle = atan2(lastDotY - 120, lastDotX - 120) * RAD_TO_DEG;
                        if (touch_angle < 0) touch_angle += 360;
                        int segIndex = (int)(touch_angle) / 36;
                        
                        eraseSegmentIndicator(segIndex);
                        if (segIndex == activeSegmentIndex) {
                            drawSegmentIndicator(segIndex);
                        }
                    }
                } else {
                    if (dist_ctr_sq <= 30*30) {
                        lastBrightness = -1; // Force center HUD redraw
                    }
                    if (abs(lastDotX - 120) <= 8) {
                        tft.drawLine(120, 0, 120, 240, TFT_DARKGREY);
                    }
                    if (abs(lastDotY - 120) <= 8) {
                        tft.drawLine(0, 120, 240, 120, TFT_DARKGREY);
                    }
                }

                if (dist_ctr_sq >= 110*110) {
                    tft.drawCircle(120, 120, 119, TFT_WHITE);
                    tft.drawCircle(120, 120, 118, TFT_DARKGREY);
                }
            }
        }

        // 3. Dynamic UI updating
        if (colorPickerActive) {
            // Draw segment selected dot
            if (activeSegmentIndex != lastSegmentIndex) {
                if (lastSegmentIndex >= 0) {
                    eraseSegmentIndicator(lastSegmentIndex);
                }
                drawSegmentIndicator(activeSegmentIndex);
                lastSegmentIndex = activeSegmentIndex;
            }

            // Draw center checkmark button filled with active color selection
            if (color != lastColor) {
                tft.fillCircle(120, 120, CLOSE_BTN_RADIUS, color);
                tft.drawCircle(120, 120, CLOSE_BTN_RADIUS, TFT_WHITE);
                drawCheckmark(120, 120, color);
                
                // Clear selection title and write active color name
                tft.fillRect(35, 10, 170, 25, TFT_BLACK);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(color, TFT_BLACK);
                tft.drawString(segmentNames[activeSegmentIndex], 120, 25, 2);
                
                lastColor = color;
            }
        } else {
            // Top-Left (UL) Quadrant: Power Switch
            if (lampOn != lastLampOn) {
                tft.fillRect(5, 5, 110, 110, TFT_BLACK);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(TFT_WHITE, TFT_BLACK);
                tft.drawString("POWER", 60, 35, 2);

                if (lampOn) {
                    tft.fillCircle(60, 75, 18, TFT_GREEN);
                    tft.setTextColor(TFT_BLACK);
                    tft.drawString("ON", 60, 75, 2);
                } else {
                    tft.fillCircle(60, 75, 18, TFT_DARKGREY);
                    tft.setTextColor(TFT_WHITE);
                    tft.drawString("OFF", 60, 75, 2);
                }
                lastLampOn = lampOn;
            }

            // Top-Right (UR) Quadrant: Color Preview
            if (color != lastColor) {
                tft.fillRect(125, 5, 110, 110, TFT_BLACK);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(TFT_WHITE, TFT_BLACK);
                tft.drawString("COLOR", 180, 35, 2);

                tft.fillCircle(180, 75, 18, color);
                tft.drawCircle(180, 75, 18, TFT_WHITE);
                
                lastColor = color;
            }

            // Central HUD: Brightness indicator
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

        // 4. Render active Touch Dot
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
