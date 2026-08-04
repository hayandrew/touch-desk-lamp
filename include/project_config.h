#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

#include <Arduino.h>

// Screen Dimensions
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240

// Display (SPI) Pin Mappings
// Note: SCLK/MOSI/CS/DC/RST/BL are configured in platformio.ini for TFT_eSPI
// We list them here for logical reference.
#define TFT_SCLK_PIN 10
#define TFT_MOSI_PIN 9
#define TFT_RST_PIN  8
#define TFT_DC_PIN   7
#define TFT_CS_PIN   6
#define TFT_BLK_PIN  5

// Touch (I2C) Pin Mappings (CST816S)
#define TOUCH_SDA_PIN 3
#define TOUCH_SCL_PIN 4
#define TOUCH_RST_PIN 2
#define TOUCH_INT_PIN 1

// Wi-Fi Configuration
#define WIFI_HOSTNAME "esp32c3-touch-lamp"

// Network Configuration (Static IP Setup for OTA)
#define STATIC_IP_ADDR  192, 168, 68, 51
#define GATEWAY_IP_ADDR 192, 168, 68, 1
#define SUBNET_MASK     255, 255, 255, 0
#define DNS_PRIMARY     8, 8, 8, 8
#define DNS_SECONDARY   8, 8, 4, 4

// Arduino OTA Settings
#define OTA_PORT 3232

#endif // PROJECT_CONFIG_H
