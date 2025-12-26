#ifndef ESP_NOW_MANAGER_H
#define ESP_NOW_MANAGER_H

#include <esp_now.h>
#include <WiFi.h>
#include "sensor_manager.h"

// Structure to match the sender's data format
typedef struct struct_message
{
    char clientId[32];
    int touchValue;
    float batteryPercent;
} struct_message;

class ESPNowManager
{
private:
    SensorManager *sensorManager;
    static ESPNowManager *instance;

    static void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len);
    bool setCustomMacAddress(const uint8_t *mac);

public:
    ESPNowManager(SensorManager *sensorMgr);
    bool init();
    void handleIncomingData(const uint8_t *mac, const uint8_t *incomingData, int len);
    void printMacAddress();
};

#endif // ESP_NOW_MANAGER_H