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
    static bool lastColorPickerActive = true;
    static bool lastLampOn = false;
    static int lastBrightness = -1;
    static uint16_t lastColor = 0;
    static int lastSegmentIndex = -1;


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
        int mx = 120 + 75.0f * ca; // Centered between close button (25) and outer radius (110)
        int my = 120 + 75.0f * sa;
        
        tft.fillCircle(mx, my, 5, TFT_WHITE);
        tft.fillCircle(mx, my, 3, TFT_BLACK);
    }

    void eraseSegmentIndicator(int segmentIndex) {
        if (segmentIndex < 0 || segmentIndex >= 10) return;
        int start_angle = segmentIndex * 36;
        int end_angle = (segmentIndex + 1) * 36 - 2;
        uint16_t col = segmentColors[segmentIndex];
        
        for (int h = start_angle; h < end_angle; h++) {
            float angle1 = h * DEG_TO_RAD;
            float angle2 = (h + 1) * DEG_TO_RAD;
            
            int x1 = 120 + WHEEL_OUTER_RADIUS * cos(angle1);
            int y1 = 120 + WHEEL_OUTER_RADIUS * sin(angle1);
            int x2 = 120 + WHEEL_OUTER_RADIUS * cos(angle2);
            int y2 = 120 + WHEEL_OUTER_RADIUS * sin(angle2);
            
            tft.fillTriangle(120, 120, x1, y1, x2, y2, col);
        }

        // Redraw center checkmark button since the triangles cover it
        tft.fillCircle(120, 120, CLOSE_BTN_RADIUS, lastColor);
        tft.drawCircle(120, 120, CLOSE_BTN_RADIUS, TFT_WHITE);
        drawCheckmark(120, 120, lastColor);
    }

    void drawStaticQuadrants() {
        tft.fillScreen(TFT_BLACK);
        
        // Bottom-Left (LL): Bright -
        // Solid dark grey background
        uint16_t bg_ll = tft.color565(50, 50, 50);
        tft.fillRect(0, 121, 119, 119, bg_ll);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, bg_ll);
        tft.setTextSize(2); // Scale font twice as large
        tft.drawString("-", 70, 170, 4); // Offset +10 (70, 170)
        tft.setTextSize(1); // Reset scale

        // Bottom-Right (LR): Bright +
        // Solid light grey background
        uint16_t bg_lr = tft.color565(160, 160, 160);
        tft.fillRect(121, 121, 119, 119, bg_lr);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_BLACK, bg_lr); // High-contrast black "+" on light grey
        tft.setTextSize(2); // Scale font twice as large
        tft.drawString("+", 170, 170, 4); // Offset +10 (170, 170)
        tft.setTextSize(1); // Reset scale

        // Draw 2-pixel black divider lines
        tft.drawFastVLine(119, 0, 240, TFT_BLACK);
        tft.drawFastVLine(120, 0, 240, TFT_BLACK);
        tft.drawFastHLine(0, 119, 240, TFT_BLACK);
        tft.drawFastHLine(0, 120, 240, TFT_BLACK);

        // Draw outer boundary
        tft.drawCircle(120, 120, 119, TFT_WHITE);
        tft.drawCircle(120, 120, 118, TFT_DARKGREY);
    }

    void drawStaticColorPicker() {
        tft.fillScreen(TFT_BLACK);
        
        // Draw outer boundary
        tft.drawCircle(120, 120, 119, TFT_WHITE);
        tft.drawCircle(120, 120, 118, TFT_DARKGREY);

        // Draw 10 discrete color wedges with 2-degree gaps radiating from center
        for (int i = 0; i < 10; i++) {
            int start_angle = i * 36;
            int end_angle = (i + 1) * 36 - 2;
            uint16_t col = segmentColors[i];
            
            for (int h = start_angle; h < end_angle; h++) {
                float angle1 = h * DEG_TO_RAD;
                float angle2 = (h + 1) * DEG_TO_RAD;
                
                int x1 = 120 + WHEEL_OUTER_RADIUS * cos(angle1);
                int y1 = 120 + WHEEL_OUTER_RADIUS * sin(angle1);
                int x2 = 120 + WHEEL_OUTER_RADIUS * cos(angle2);
                int y2 = 120 + WHEEL_OUTER_RADIUS * sin(angle2);
                
                tft.fillTriangle(120, 120, x1, y1, x2, y2, col);
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

    void update(bool lampOn, int brightness, uint16_t color, int activeSegmentIndex, bool colorPickerActive) {
        
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

        // 2. Dynamic UI updating
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
                
                lastColor = color;
            }
        } else {
            // Top-Left (UL) Quadrant: Power Switch
            if (lampOn != lastLampOn) {
                uint16_t bg = lampOn ? TFT_WHITE : tft.color565(80, 80, 80);
                tft.fillRect(0, 0, 119, 119, bg);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(lampOn ? TFT_BLACK : TFT_WHITE, bg);
                tft.drawString(lampOn ? "ON" : "OFF", 70, 70, 4); // Offset at (70, 70)
                
                lastLampOn = lampOn;
                lastBrightness = -1; // Force HUD redraw on top
            }

            // Top-Right (UR) Quadrant: Color Preview Block
            if (color != lastColor) {
                tft.fillRect(121, 0, 119, 119, color);
                
                lastColor = color;
                lastBrightness = -1; // Force HUD redraw on top
            }

            // Central HUD: Brightness indicator
            if (brightness != lastBrightness) {
                // Redraw outer borders & dividers to keep them clean
                tft.drawCircle(120, 120, 119, TFT_WHITE);
                tft.drawCircle(120, 120, 118, TFT_DARKGREY);
                tft.drawFastVLine(119, 0, 240, TFT_BLACK);
                tft.drawFastVLine(120, 0, 240, TFT_BLACK);
                tft.drawFastHLine(0, 119, 240, TFT_BLACK);
                tft.drawFastHLine(0, 120, 240, TFT_BLACK);

                tft.fillCircle(120, 120, 33, TFT_BLACK);
                tft.drawCircle(120, 120, 33, TFT_BLACK);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(tft.color565(200, 200, 200), TFT_BLACK);
                
                char brBuf[8];
                snprintf(brBuf, sizeof(brBuf), "%d%%", brightness);
                tft.drawString(brBuf, 120, 120, 4); // Larger Font (Size 4)
                
                lastBrightness = brightness;
            }
        }
    }

    void drawOtaProgress(unsigned int progress, unsigned int total) {
        if (progress == 0) {
            tft.fillScreen(TFT_BLACK);
            tft.drawCircle(120, 120, 119, TFT_WHITE);
            tft.drawCircle(120, 120, 118, TFT_DARKGREY);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString("OTA Update", 120, 60, 4); // Large Title
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("Do not turn off power", 120, 95, 2); // Caution Subtitle
        }

        int percentage = (progress * 100) / total;
        char progressBuf[32];
        snprintf(progressBuf, sizeof(progressBuf), "Updating: %d%%", percentage);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawString(progressBuf, 120, 135, 2);

        int barWidth = 160;
        int barHeight = 16;
        int barX = 120 - barWidth / 2;
        int barY = 165;
        tft.drawRect(barX, barY, barWidth, barHeight, TFT_WHITE);
        int fillWidth = (percentage * (barWidth - 4)) / 100;
        tft.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, TFT_GREEN);
        tft.fillRect(barX + 2 + fillWidth, barY + 2, (barWidth - 4) - fillWidth, barHeight - 4, TFT_BLACK);
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
