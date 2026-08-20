#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

#include <Arduino.h>

// Screen Dimensions
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240
#define SCREEN_CENTER_X 120
#define SCREEN_CENTER_Y 120

// Display (SPI) Pin Mappings
#define TFT_SCLK_PIN 10
#define TFT_MOSI_PIN 9
#define TFT_RST_PIN  8
#define TFT_DC_PIN   7
#define TFT_CS_PIN   6
#define TFT_BLK_PIN  5

// Touch (I2C) Pin Mappings (CST816S)
#define TOUCH_SDA_PIN 11
#define TOUCH_SCL_PIN 12
#define TOUCH_RST_PIN 2
#define TOUCH_INT_PIN 1

// Wi-Fi Configuration
#define WIFI_HOSTNAME "home-remote"

// Network Configuration (Static IP Setup for OTA)
#define STATIC_IP_ADDR  192, 168, 68, 51
#define GATEWAY_IP_ADDR 192, 168, 68, 1
#define SUBNET_MASK     255, 255, 255, 0
#define DNS_PRIMARY     8, 8, 8, 8
#define DNS_SECONDARY   8, 8, 4, 4

// Arduino OTA Settings
#define OTA_PORT 3232

// Studio Device Topics
#define MQTT_STUDIO_PLUG_COMMAND    "homeassistant/switch/studio_plug/set"
#define MQTT_STUDIO_LIGHT_COMMAND   "homeassistant/light/studio_studio/set"

// Studio Remote (Self) Topics
#define MQTT_REMOTE_DISCOVERY "homeassistant/switch/studio_remote/config"
#define MQTT_REMOTE_STATE     "homeassistant/switch/studio_remote/state"
#define MQTT_REMOTE_SET       "homeassistant/switch/studio_remote/set"

#endif // PROJECT_CONFIG_H
