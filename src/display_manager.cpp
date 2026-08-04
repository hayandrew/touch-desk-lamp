#include "display_manager.h"
#include "project_config.h"
#include <WiFi.h>
#include <TFT_eSPI.h>

namespace DisplayManager {
    static TFT_eSPI tft = TFT_eSPI();
    
    static bool lampOn = false;
    static int lastDotX = -1;
    static int lastDotY = -1;
    static bool lastTouched = false;
    static String lastGesture = "";
    static String lastEvent = "";

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
        tft.setRotation(0); // 0-3
        tft.fillScreen(TFT_BLACK);

        // Draw screen boundary circle (white)
        tft.drawCircle(120, 120, 119, TFT_WHITE);
        tft.drawCircle(120, 120, 118, TFT_DARKGREY);

        // Draw initial UI static elements
        tft.setTextDatum(MC_DATUM); // Middle Centre
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.drawString("DESK LAMP DEMO", 120, 35, 2);

        // Draw central lamp button
        tft.fillCircle(120, 120, 45, TFT_DARKGREY);
        tft.setTextColor(TFT_WHITE);
        tft.drawString("OFF", 120, 120, 4);

        // Draw status labels
        tft.setTextColor(TFT_DARKCYAN, TFT_BLACK);
        tft.drawString("Touch Screen Test", 120, 185, 2);

        // Draw connection/info state
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.drawString("IP: Connecting...", 120, 205, 2);

        Serial.println("[DisplayManager] Display initialized.");
    }

    void update(bool isTouched, int x, int y, const char* gestureName, const char* eventName) {
        // 1. Erase/Draw touch feedback dot
        if (lastTouched && (!isTouched || x != lastDotX || y != lastDotY)) {
            // Erase old dot by drawing black circle over it
            if (lastDotX != -1 && lastDotY != -1) {
                tft.fillCircle(lastDotX, lastDotY, 6, TFT_BLACK);
                
                // Redraw boundary if erased
                int dist_border_sq = (lastDotX - 120)*(lastDotX - 120) + (lastDotY - 120)*(lastDotY - 120);
                if (dist_border_sq >= 110*110) {
                    tft.drawCircle(120, 120, 119, TFT_WHITE);
                    tft.drawCircle(120, 120, 118, TFT_DARKGREY);
                }

                // Redraw lamp button if overlap erased
                int dist_btn_sq = (lastDotX - 120)*(lastDotX - 120) + (lastDotY - 120)*(lastDotY - 120);
                if (dist_btn_sq <= 55*55) {
                    if (lampOn) {
                        tft.fillCircle(120, 120, 45, TFT_GOLD);
                        tft.setTextColor(TFT_BLACK);
                        tft.drawString("ON", 120, 120, 4);
                    } else {
                        tft.fillCircle(120, 120, 45, TFT_DARKGREY);
                        tft.setTextColor(TFT_WHITE);
                        tft.drawString("OFF", 120, 120, 4);
                    }
                }
            }
        }

        // Toggle state logic on touch Down/Click inside the circle
        if (isTouched && !lastTouched) {
            // Check if touch is within the button radius
            int dx = x - 120;
            int dy = y - 120;
            if ((dx * dx + dy * dy) <= 45 * 45) {
                lampOn = !lampOn;
                Serial.printf("[Display] Button clicked! Lamp state: %s\n", lampOn ? "ON" : "OFF");
                
                // Redraw button state
                if (lampOn) {
                    tft.fillCircle(120, 120, 45, TFT_GOLD);
                    tft.setTextColor(TFT_BLACK);
                    tft.drawString("ON", 120, 120, 4);
                } else {
                    tft.fillCircle(120, 120, 45, TFT_DARKGREY);
                    tft.setTextColor(TFT_WHITE);
                    tft.drawString("OFF", 120, 120, 4);
                }
            }
        }

        if (isTouched) {
            // Draw new touch dot
            tft.fillCircle(x, y, 6, TFT_GREEN);
            lastDotX = x;
            lastDotY = y;
        } else {
            lastDotX = -1;
            lastDotY = -1;
        }
        lastTouched = isTouched;

        // 2. Redraw coordinate and gesture strings if they change
        static int prevX = -1, prevY = -1;
        if (isTouched && (x != prevX || y != prevY)) {
            char coordBuf[32];
            snprintf(coordBuf, sizeof(coordBuf), "X: %3d  Y: %3d ", x, y);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(coordBuf, 120, 55, 2);
            prevX = x;
            prevY = y;
        } else if (!isTouched && (prevX != -1)) {
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString("              ", 120, 55, 2);
            prevX = -1;
            prevY = -1;
        }

        if (String(gestureName) != lastGesture || String(eventName) != lastEvent) {
            lastGesture = gestureName;
            lastEvent = eventName;
            
            char gestureBuf[64];
            snprintf(gestureBuf, sizeof(gestureBuf), "G: %s (%s)      ", gestureName, eventName);
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.drawString(gestureBuf, 120, 220, 2);
        }

        // Draw WiFi connection indicator if IP is assigned
        static String lastIpStr = "";
        String currentIpStr = WiFi.localIP().toString();
        if (currentIpStr != lastIpStr) {
            lastIpStr = currentIpStr;
            char ipBuf[48];
            if (WiFi.status() == WL_CONNECTED) {
                snprintf(ipBuf, sizeof(ipBuf), "IP: %s", currentIpStr.c_str());
                tft.setTextColor(TFT_GREEN, TFT_BLACK);
            } else {
                snprintf(ipBuf, sizeof(ipBuf), "IP: Offline");
                tft.setTextColor(TFT_RED, TFT_BLACK);
            }
            tft.drawString(ipBuf, 120, 205, 2);
        }
    }

    void drawOtaProgress(unsigned int progress, unsigned int total) {
        // Clear screen once when OTA starts (indicated by progress == 0)
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

        // Draw progress bar
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
