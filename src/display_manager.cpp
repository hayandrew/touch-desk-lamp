#include "display_manager.h"
#include "project_config.h"
#include <WiFi.h>
#include <TFT_eSPI.h>

namespace DisplayManager {
    static TFT_eSPI tft = TFT_eSPI();
    static TFT_eSprite spr = TFT_eSprite(&tft);

    void addBootLogLine(const char* line, uint16_t color) {
        // Redirect boot logs to the serial port
        Serial.println(line);
    }

    void drawBootAnimation(int step) {
        spr.fillScreen(TFT_BLACK);

        // Neon bright blue and dim blue colors
        uint16_t brightBlue = spr.color565(0, 191, 255);
        uint16_t dimBlue = spr.color565(0, 40, 80);

        int activeDot = step % 3;

        // Draw three horizontal dots pulsing sequentially (active radius 3, inactive radius 1)
        spr.fillCircle(80, 120, (activeDot == 0) ? 3 : 1, (activeDot == 0) ? brightBlue : dimBlue);
        spr.fillCircle(120, 120, (activeDot == 1) ? 3 : 1, (activeDot == 1) ? brightBlue : dimBlue);
        spr.fillCircle(160, 120, (activeDot == 2) ? 3 : 1, (activeDot == 2) ? brightBlue : dimBlue);

        spr.pushSprite(0, 0);
    }

    void init() {
        Serial.println("[DisplayManager] Initializing TFT display (GC9A01)...");
        
        pinMode(TFT_BLK_PIN, OUTPUT);
        digitalWrite(TFT_BLK_PIN, HIGH);

        Serial.println("[DisplayManager] Performing hardware reset...");
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
        
        // Initial rendering
        update(false);
        Serial.println("[DisplayManager] Display and Sprite initialized.");
    }

    void update(bool isOn) {
        spr.fillScreen(TFT_BLACK);

        if (isOn) {
            // Background: Slate-navy dark theme
            uint16_t bg = spr.color565(10, 15, 25);
            spr.fillScreen(bg);

            // Glowing Outer Ring: Neon/Emerald Green
            uint16_t ringCol = spr.color565(0, 240, 150);
            for (int r = 74; r <= 78; r++) {
                spr.drawCircle(120, 120, r, ringCol);
            }

            // Inner Fill: Subtle dark forest green
            uint16_t innerBg = spr.color565(15, 45, 35);
            spr.fillCircle(120, 120, 73, innerBg);

            // Text Rendering
            spr.setTextDatum(MC_DATUM);
            spr.setTextColor(TFT_WHITE, innerBg);
            spr.drawString("ON", 120, 108, 6); // Font 6 (Large 48px)

            // Subtext
            spr.setTextColor(spr.color565(180, 255, 220), innerBg);
            spr.drawString("STUDIO", 120, 148, 4); // Font 4 (Medium)
        } else {
            // Background: Ultra dark black
            uint16_t bg = spr.color565(5, 5, 8);
            spr.fillScreen(bg);

            // Ring: Soft dark grey
            uint16_t ringCol = spr.color565(60, 60, 65);
            for (int r = 74; r <= 78; r++) {
                spr.drawCircle(120, 120, r, ringCol);
            }

            // Inner Fill: Slightly lighter charcoal
            uint16_t innerBg = spr.color565(20, 20, 22);
            spr.fillCircle(120, 120, 73, innerBg);

            // Text Rendering
            spr.setTextDatum(MC_DATUM);
            spr.setTextColor(spr.color565(120, 120, 125), innerBg);
            spr.drawString("OFF", 120, 108, 6); // Font 6 (Large 48px)

            // Subtext
            spr.setTextColor(spr.color565(100, 100, 105), innerBg);
            spr.drawString("STUDIO", 120, 148, 4); // Font 4 (Medium)
        }

        // Push outer decorative bounding circles to fit the round screen
        spr.drawCircle(120, 120, 119, TFT_WHITE);
        spr.drawCircle(120, 120, 118, TFT_DARKGREY);

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
