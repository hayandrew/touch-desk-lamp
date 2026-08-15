#include "display_manager.h"
#include "project_config.h"
#include <WiFi.h>
#include <TFT_eSPI.h>

namespace DisplayManager {
    static TFT_eSPI tft = TFT_eSPI();
    static TFT_eSprite spr = TFT_eSprite(&tft);
    
    // Segmented Colors Initialization
    static uint16_t segmentColors[10];
    static const char* segmentNames[10] = {
        "Cold White", "Warm White", "Red", "Pink", "Orange",
        "Yellow", "Green", "Blue", "Indigo", "Violet"
    };

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

    void drawCheckmark(int cx, int cy, uint16_t color) {
        uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
        uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
        uint8_t b = (color & 0x1F) * 255 / 31;
        float luminance = 0.299f * r + 0.587f * g + 0.114f * b;
        
        uint16_t chkColor = (luminance > 140.0f) ? TFT_BLACK : TFT_WHITE;
        
        spr.drawLine(cx - 8, cy, cx - 2, cy + 6, chkColor);
        spr.drawLine(cx - 8, cy + 1, cx - 2, cy + 7, chkColor);
        spr.drawLine(cx - 8, cy - 1, cx - 2, cy + 5, chkColor);
        
        spr.drawLine(cx - 2, cy + 6, cx + 10, cy - 6, chkColor);
        spr.drawLine(cx - 2, cy + 7, cx + 10, cy - 5, chkColor);
        spr.drawLine(cx - 2, cy + 5, cx + 10, cy - 7, chkColor);
    }

    /* Commented out color wheel segment indicator
    void drawSegmentIndicator(int segmentIndex) {
        if (segmentIndex < 0 || segmentIndex >= 10) return;
        float angle = (segmentIndex * 36 + 17) * DEG_TO_RAD;
        float ca = cos(angle);
        float sa = sin(angle);
        int mx = 120 + 75.0f * ca;
        int my = 120 + 75.0f * sa;
        
        spr.fillCircle(mx, my, 5, TFT_WHITE);
        spr.fillCircle(mx, my, 3, TFT_BLACK);
    }
    */

    void drawStaticQuadrants() {
        // Bottom-Left (LL): Bright -
        uint16_t bg_ll = spr.color565(50, 50, 50);
        spr.fillRect(0, 121, 119, 119, bg_ll);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(TFT_WHITE, bg_ll);
        spr.setTextSize(2);
        spr.drawString("-", 70, 170, 4);
        spr.setTextSize(1);

        // Bottom-Right (LR): Bright +
        uint16_t bg_lr = spr.color565(160, 160, 160);
        spr.fillRect(121, 121, 119, 119, bg_lr);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(TFT_BLACK, bg_lr);
        spr.setTextSize(2);
        spr.drawString("+", 170, 170, 4);
        spr.setTextSize(1);

        // Draw 2-pixel black divider lines
        spr.drawFastVLine(119, 0, 240, TFT_BLACK);
        spr.drawFastVLine(120, 0, 240, TFT_BLACK);
        spr.drawFastHLine(0, 119, 240, TFT_BLACK);
        spr.drawFastHLine(0, 120, 240, TFT_BLACK);

        // Draw outer boundary
        spr.drawCircle(120, 120, 119, TFT_WHITE);
        spr.drawCircle(120, 120, 118, TFT_DARKGREY);
    }

    /* Commented out color wheel drawing code
    void drawStaticColorPicker() {
        // Draw outer boundary
        spr.drawCircle(120, 120, 119, TFT_WHITE);
        spr.drawCircle(120, 120, 118, TFT_DARKGREY);

        // Draw 10 discrete color wedges
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
                
                spr.fillTriangle(120, 120, x1, y1, x2, y2, col);
            }
        }
    }
    */

    void drawAllLightsOverlay() {
        // Clear screen
        spr.fillScreen(TFT_BLACK);

        // Left Half (X: 0 to 119): Goodnight Button
        uint16_t bg_gn = spr.color565(25, 35, 75); // Midnight navy blue
        spr.fillRect(0, 0, 119, 240, bg_gn);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(TFT_WHITE, bg_gn);
        spr.drawString("GOOD", 60, 105, 4);
        spr.drawString("NIGHT", 60, 135, 4);

        // Right Half (X: 121 to 239): Cancel Button
        uint16_t bg_cn = spr.color565(75, 75, 75); // Sleek charcoal grey
        spr.fillRect(121, 0, 119, 240, bg_cn);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(TFT_WHITE, bg_cn);
        spr.drawString("CANCEL", 180, 120, 4);

        // Draw 2-pixel black divider line in the center
        spr.drawFastVLine(119, 0, 240, TFT_BLACK);
        spr.drawFastVLine(120, 0, 240, TFT_BLACK);

        // Draw outer boundary circles to frame the round screen
        spr.drawCircle(120, 120, 119, TFT_WHITE);
        spr.drawCircle(120, 120, 118, TFT_DARKGREY);
    }

    void drawBrightnessPicker(int brightness) {
        // Clear screen
        spr.fillScreen(TFT_BLACK);

        // Arc starts at 135 degrees (bottom-left) and ends at 405 degrees (bottom-right)
        // Total range = 270 degrees
        int activeAngle = 135 + (brightness * 270 / 100);
        
        uint16_t activeCol = spr.color565(255, 215, 0); // Gold
        uint16_t inactiveCol = spr.color565(40, 40, 40); // Dark Grey
        
        // Draw the slider ring using filled triangles (wedges) from the center to outer radius (100)
        for (int a = 135; a < 405; a++) {
            float rad1 = a * DEG_TO_RAD;
            float rad2 = (a + 1) * DEG_TO_RAD;
            
            int x1 = 120 + 100 * cos(rad1);
            int y1 = 120 + 100 * sin(rad1);
            int x2 = 120 + 100 * cos(rad2);
            int y2 = 120 + 100 * sin(rad2);
            
            uint16_t col = (a < activeAngle) ? activeCol : inactiveCol;
            spr.fillTriangle(120, 120, x1, y1, x2, y2, col);
        }

        // Mask out the center of the ring by filling it with a black circle (radius 80)
        spr.fillCircle(120, 120, 80, TFT_BLACK);

        // Draw outer boundary circles
        spr.drawCircle(120, 120, 119, TFT_WHITE);
        spr.drawCircle(120, 120, 118, TFT_DARKGREY);

        // Draw central checkmark button
        uint16_t chkBg = spr.color565(34, 139, 34); // Forest green
        spr.fillCircle(120, 120, 33, chkBg);
        spr.drawCircle(120, 120, 33, TFT_WHITE);
        drawCheckmark(120, 120, chkBg);

        // Draw text info
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(TFT_WHITE, TFT_BLACK);
        char pctBuf[16];
        snprintf(pctBuf, sizeof(pctBuf), "%d%%", brightness);
        spr.drawString(pctBuf, 120, 70, 4);
        spr.setTextColor(spr.color565(150, 150, 150), TFT_BLACK);
        spr.drawString("BRIGHTNESS", 120, 190, 2);
    }

    void addBootLogLine(const char* line, uint16_t color) {
        // Redirect boot logs to the serial port
        Serial.println(line);
    }

    void drawBootAnimation(int step) {
        spr.fillScreen(TFT_BLACK);

        // Draw a clean premium circular frame border
        spr.drawCircle(120, 120, 119, TFT_WHITE);
        spr.drawCircle(120, 120, 118, spr.color565(30, 30, 30));

        uint16_t brightBlue = spr.color565(0, 150, 255);
        uint16_t dimBlue = spr.color565(0, 40, 80);

        int activeDot = step % 3;

        // Draw three horizontal dots pulsing sequentially
        spr.fillCircle(80, 120, (activeDot == 0) ? 10 : 6, (activeDot == 0) ? brightBlue : dimBlue);
        spr.fillCircle(120, 120, (activeDot == 1) ? 10 : 6, (activeDot == 1) ? brightBlue : dimBlue);
        spr.fillCircle(160, 120, (activeDot == 2) ? 10 : 6, (activeDot == 2) ? brightBlue : dimBlue);

        spr.pushSprite(0, 0);
    }

    void init() {
        Serial.println("[DisplayManager] Initializing TFT display (GC9A01)...");
        
        pinMode(TFT_BLK_PIN, OUTPUT);
        digitalWrite(TFT_BLK_PIN, HIGH);

        Serial.println("[DisplayManager] Performing hardware reset... (DMA Frame Buffer enabled)");
        pinMode(TFT_RST_PIN, OUTPUT);
        digitalWrite(TFT_RST_PIN, HIGH);
        delay(10);
        digitalWrite(TFT_RST_PIN, LOW);
        delay(50);
        digitalWrite(TFT_RST_PIN, HIGH);
        delay(150);

        tft.init();
        tft.setRotation(2);
        
        // Create full frame buffer sprite in RAM
        spr.createSprite(240, 240);
        spr.setRotation(2);
        
        segmentColors[0] = spr.color565(225, 240, 255); // Cold White
        segmentColors[1] = spr.color565(255, 230, 160); // Warm White
        segmentColors[2] = 0xF800;                       // Red
        segmentColors[3] = spr.color565(255, 105, 180); // Pink
        segmentColors[4] = 0xFD20;                       // Orange
        segmentColors[5] = 0xFFE0;                       // Yellow
        segmentColors[6] = spr.color565(0, 200, 0);     // Green
        segmentColors[7] = 0x001F;                       // Blue
        segmentColors[8] = spr.color565(75, 0, 130);     // Indigo
        segmentColors[9] = spr.color565(180, 50, 240);   // Violet
        
        // Initial quadrants rendering
        update(false, DEFAULT_BRIGHTNESS, segmentColors[1], 1, false, false);
        Serial.println("[DisplayManager] Display and Sprite initialized.");
    }

    void update(bool lampOn, int brightness, uint16_t color, int activeSegmentIndex, bool allLightsActive, bool brightnessPickerActive) {
        spr.fillScreen(TFT_BLACK);

        if (allLightsActive) {
            drawAllLightsOverlay();
        } else if (brightnessPickerActive) {
            drawBrightnessPicker(brightness);
        } else {
            drawStaticQuadrants();

            // Top-Left (UL) Quadrant: Power Switch
            uint16_t bg = lampOn ? TFT_WHITE : spr.color565(80, 80, 80);
            spr.fillRect(0, 0, 119, 119, bg);
            spr.setTextDatum(MC_DATUM);
            spr.setTextColor(lampOn ? TFT_BLACK : TFT_WHITE, bg);
            spr.drawString(lampOn ? "ON" : "OFF", 70, 70, 4);

            // Top-Right (UR) Quadrant: All Lights Button
            uint16_t bg_ur = spr.color565(70, 80, 150); // Premium dark purple/indigo
            spr.fillRect(121, 0, 119, 119, bg_ur);
            spr.setTextDatum(MC_DATUM);
            spr.setTextColor(TFT_WHITE, bg_ur);
            spr.drawString("ALL", 180, 70, 4);

            // Redraw central divider lines to clean up overlapping drawings
            spr.drawFastVLine(119, 0, 240, TFT_BLACK);
            spr.drawFastVLine(120, 0, 240, TFT_BLACK);
            spr.drawFastHLine(0, 119, 240, TFT_BLACK);
            spr.drawFastHLine(0, 120, 240, TFT_BLACK);
            spr.drawCircle(120, 120, 119, TFT_WHITE);
            spr.drawCircle(120, 120, 118, TFT_DARKGREY);

            // Central HUD: Brightness indicator
            spr.fillCircle(120, 120, 33, TFT_BLACK);
            spr.drawCircle(120, 120, 33, TFT_BLACK);
            spr.setTextDatum(MC_DATUM);
            spr.setTextColor(spr.color565(200, 200, 200), TFT_BLACK);
            
            char brBuf[8];
            snprintf(brBuf, sizeof(brBuf), "%d%%", brightness);
            spr.drawString(brBuf, 120, 120, 4);
        }

        // Push frame buffer to screen instantly
        spr.pushSprite(0, 0);
    }

    void drawOtaProgress(unsigned int progress, unsigned int total) {
        spr.fillScreen(TFT_BLACK);
        spr.drawCircle(120, 120, 119, TFT_WHITE);
        spr.drawCircle(120, 120, 118, TFT_DARKGREY);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(TFT_WHITE, TFT_BLACK);
        spr.drawString("OTA Update", 120, 60, 4);
        spr.setTextColor(TFT_RED, TFT_BLACK);
        spr.drawString("Do not turn off power", 120, 95, 2);

        int percentage = (progress * 100) / total;
        char progressBuf[32];
        snprintf(progressBuf, sizeof(progressBuf), "Updating: %d%%", percentage);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(TFT_YELLOW, TFT_BLACK);
        spr.drawString(progressBuf, 120, 135, 2);

        int barWidth = 160;
        int barHeight = 16;
        int barX = 120 - barWidth / 2;
        int barY = 165;
        spr.drawRect(barX, barY, barWidth, barHeight, TFT_WHITE);
        int fillWidth = (percentage * (barWidth - 4)) / 100;
        spr.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, TFT_GREEN);
        spr.fillRect(barX + 2 + fillWidth, barY + 2, (barWidth - 4) - fillWidth, barHeight - 4, TFT_BLACK);
        
        spr.pushSprite(0, 0);
    }

    void drawOtaError(const char* errorMsg) {
        spr.fillScreen(TFT_BLACK);
        spr.drawCircle(120, 120, 119, TFT_RED);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(TFT_RED, TFT_BLACK);
        spr.drawString("OTA UPDATE FAILED", 120, 80, 2);
        spr.setTextColor(TFT_WHITE, TFT_BLACK);
        spr.drawString(errorMsg, 120, 130, 2);
        spr.drawString("Rebooting...", 120, 170, 2);
        
        spr.pushSprite(0, 0);
    }
}
