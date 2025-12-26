#include "esp_now_manager.h"
#include "config.h"
#include <esp_wifi.h>

// Static instance pointer for callback
ESPNowManager *ESPNowManager::instance = nullptr;

ESPNowManager::ESPNowManager(SensorManager *sensorMgr)
    : sensorManager(sensorMgr)
{
    instance = this;
}

bool ESPNowManager::setCustomMacAddress(const uint8_t *mac)
{
    esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, mac);
    if (err == ESP_OK)
    {
        Serial.println("Custom MAC address set successfully");
        return true;
    }
    else
    {
        Serial.printf("Failed to set custom MAC address: %d\n", err);
        return false;
    }
}

bool ESPNowManager::init()
{
    // Set custom MAC address BEFORE initializing ESP-NOW
    uint8_t customMac[] = RECEIVER_MAC_ADDRESS;
    setCustomMacAddress(customMac);

    // Print the MAC address for verification
    printMacAddress();

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return false;
    }

    Serial.println("ESP-NOW initialized successfully");

    // Register receive callback
    esp_now_register_recv_cb(onDataRecv);

    return true;
}

void ESPNowManager::printMacAddress()
{
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);

    Serial.println("\n========================================");
    Serial.println("    RECEIVER ESP-NOW MAC ADDRESS");
    Serial.println("========================================");
    Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.println("========================================");
    Serial.println("Copy this address to your sender code!");
    Serial.println("Format for sender:");
    Serial.printf("uint8_t receiverMacAddress[] = {0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X};\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.println("========================================\n");
}

void ESPNowManager::onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    if (instance != nullptr)
    {
        instance->handleIncomingData(mac, incomingData, len);
    }
}

void ESPNowManager::handleIncomingData(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    // Convert MAC address to string for identification
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Parse incoming data
    struct_message receivedData;
    memcpy(&receivedData, incomingData, sizeof(receivedData));

    // Update sensor manager with received data
    String senderMAC = String(macStr);
    String clientId = String(receivedData.clientId);

    sensorManager->updateSensorData(
        senderMAC,
        clientId,
        receivedData.touchValue,
        receivedData.batteryPercent);

    // Serial.printf("ESP-NOW data received from %s: clientId=%s, touch=%d, battery=%.1f%%\n",
    //               macStr, receivedData.clientId, receivedData.touchValue, receivedData.batteryPercent);
}