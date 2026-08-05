#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>

namespace MQTTManager {
    // Initialize MQTT client, server details, and subscription callbacks
    void init();

    // Maintain non-blocking MQTT connection and handle loop polling
    void update();

    // Publish the current global lamp state to Home Assistant
    void publishState();
}

#endif // MQTT_MANAGER_H
