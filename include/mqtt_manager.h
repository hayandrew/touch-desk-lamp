#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>

namespace MQTTManager {
    // Initialize MQTT client, server details, and subscription callbacks
    void init();

    // Maintain non-blocking MQTT connection and handle loop polling
    void update();

    // Attempt to connect to the MQTT broker
    bool connect();

    // Check if the client is connected to the broker
    bool isConnected();

    // Publish the studio power state to all devices and the remote's own status topic
    bool publishStudioState(bool isOn);

    // Force an immediate MQTT reconnection attempt (bypassing the 5-second backoff)
    void forceReconnect();
}

#endif // MQTT_MANAGER_H
