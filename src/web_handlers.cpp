#include "web_handlers.h"
#include <Update.h>

// Static flag to track if firmware update is in progress
bool WebHandlers::isUpdating = false;

WebHandlers::WebHandlers(AsyncWebServer *webServer, SensorManager *sensorMgr, LEDController *ledCtrl)
    : server(webServer), sensorManager(sensorMgr), ledController(ledCtrl) {}

// Determine MIME type based on file extension
String WebHandlers::getContentType(String filename)
{
    if (filename.endsWith(".html"))
        return F("text/html");
    if (filename.endsWith(".css"))
        return F("text/css");
    if (filename.endsWith(".js"))
        return F("application/javascript");
    if (filename.endsWith(".json"))
        return F("application/json");
    return F("text/plain");
}

// Serve index.html
void WebHandlers::handleRoot(AsyncWebServerRequest *request)
{
    if (SPIFFS.exists("/index.html"))
    {
        request->send(SPIFFS, "/index.html", F("text/html"));
    }
    else
    {
        Serial.println(F("Error: index.html missing"));
        request->send(500, F("text/plain"), F("index.html missing or empty"));
    }
}

// Return sensor data in JSON (data comes from ESP-NOW, not REST API)
void WebHandlers::handleGetSensorData(AsyncWebServerRequest *request)
{
    request->send(200, F("application/json"), sensorManager->getSensorDataJSON());
}

// Control LED color
void WebHandlers::handleColor(AsyncWebServerRequest *request)
{
    int r = 0, g = 0, b = 0;

    if (request->hasParam("r"))
        r = request->getParam("r")->value().toInt();
    if (request->hasParam("g"))
        g = request->getParam("g")->value().toInt();
    if (request->hasParam("b"))
        b = request->getParam("b")->value().toInt();

    ledController->setColor(r, g, b);
    request->send(200, F("text/plain"), F("OK"));
}

// Serve file manager UI
void WebHandlers::handleUpload(AsyncWebServerRequest *request)
{
    if (SPIFFS.exists("/file_manager.html"))
    {
        request->send(SPIFFS, "/file_manager.html", F("text/html"));
    }
    else
    {
        Serial.println(F("Error: file_manager.html missing"));
        request->send(500, F("text/plain"), F("file_manager.html missing or empty"));
    }
}

// Upload file handler
void WebHandlers::handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    static File uploadFile;

    if (!index)
    {
        Serial.printf("UploadStart: %s\n", filename.c_str());

        // Check file extension
        if (!filename.endsWith(".html") && !filename.endsWith(".css") &&
            !filename.endsWith(".js") && !filename.endsWith(".bin"))
        {
            Serial.println(F("Invalid file type"));
            return;
        }

        // Set update flag for firmware uploads to pause sensor polling
        if (filename.endsWith(".bin"))
        {
            isUpdating = true;
            ledController->setConnectingIndicator(); // Signal upload in progress
        }

        String filepath = "/" + filename;
        uploadFile = SPIFFS.open(filepath, "w");
        if (!uploadFile)
        {
            Serial.println(F("Failed to open file for writing"));
            if (filename.endsWith(".bin"))
                isUpdating = false; // Clear flag on error
            return;
        }
    }

    if (uploadFile)
    {
        uploadFile.write(data, len);
    }

    if (final)
    {
        if (uploadFile)
        {
            uploadFile.close();
            Serial.printf("UploadEnd: %s, %u bytes\n", filename.c_str(), index + len);

            // Clear update flag when firmware upload completes
            if (filename.endsWith(".bin"))
                isUpdating = false;
        }
    }
}

// Delete file from SPIFFS
void WebHandlers::handleDeleteFile(AsyncWebServerRequest *request)
{
    String filename = "";
    if (request->hasParam("file", true))
        filename = request->getParam("file", true)->value();

    if (!filename.startsWith("/"))
        filename = "/" + filename;

    if (!SPIFFS.exists(filename))
    {
        request->send(404, F("text/plain"), F("File not found"));
        return;
    }

    bool success = SPIFFS.remove(filename);
    request->send(success ? 200 : 500, F("text/plain"), success ? F("Deleted") : F("Delete failed"));
}

// List files in SPIFFS
void WebHandlers::handleListFiles(AsyncWebServerRequest *request)
{
    String json = "[";
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    bool first = true;

    while (file)
    {
        if (!first)
            json += ",";
        json += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "}";
        first = false;
        file = root.openNextFile();
    }
    json += "]";
    request->send(200, F("application/json"), json);
}

// Serve firmware update page
void WebHandlers::handleFirmware(AsyncWebServerRequest *request)
{
    if (SPIFFS.exists("/firmware_update.html"))
    {
        request->send(SPIFFS, "/firmware_update.html", F("text/html"));
    }
    else
    {
        request->send(500, F("text/plain"), F("firmware_update.html missing or empty"));
    }
}

// Perform firmware update from SPIFFS file
void WebHandlers::handleFirmwareUpdate(AsyncWebServerRequest *request)
{
    // Set update flag to pause other operations during firmware update
    isUpdating = true;
    ledController->setConnectingIndicator(); // Signal update in progress with LED

    String filename = "";
    // Support file passed as URL query (GET) or as POST body param
    if (request->hasParam("file", false))
        filename = request->getParam("file", false)->value();
    else if (request->hasParam("file", true))
        filename = request->getParam("file", true)->value();

    if (filename.length() == 0)
    {
        isUpdating = false; // Clear flag on error
        request->send(400, F("text/plain"), F("No firmware file specified"));
        return;
    }

    if (!filename.startsWith("/"))
        filename = "/" + filename;

    if (!filename.endsWith(".bin"))
    {
        isUpdating = false; // Clear flag on error
        request->send(400, F("text/plain"), F("Only .bin files allowed"));
        return;
    }

    File firmware = SPIFFS.open(filename, "r");
    if (!firmware)
    {
        isUpdating = false; // Clear flag on error
        request->send(404, F("text/plain"), F("Firmware file not found"));
        return;
    }

    if (!Update.begin(firmware.size()))
    {
        String err = "Update begin failed";
        isUpdating = false; // Clear flag on error
        request->send(500, F("text/plain"), err);
        firmware.close();
        return;
    }

    size_t written = Update.writeStream(firmware);
    if (written != (size_t)firmware.size())
    {
        String err = "Write failed: written=" + String(written) + ", expected=" + String(firmware.size());
        isUpdating = false; // Clear flag on error
        request->send(500, F("text/plain"), err);
        firmware.close();
        return;
    }

    if (!Update.end(true))
    {
        String err = String("Update end failed: ") + Update.errorString();
        isUpdating = false; // Clear flag on error
        request->send(500, F("text/plain"), err);
        firmware.close();
        return;
    }

    firmware.close();
    // Return a success marker the web UI expects
    request->send(200, F("text/plain"), F("SUCCESS: Update successful. Rebooting..."));
    delay(1000);
    ESP.restart();
}

// Serve sensor data page
void WebHandlers::handleSensorDataPage(AsyncWebServerRequest *request)
{
    if (SPIFFS.exists("/sensor_data.html"))
    {
        request->send(SPIFFS, "/sensor_data.html", F("text/html"));
    }
    else
    {
        request->send(500, F("text/plain"), F("sensor_data.html missing or empty"));
    }
}

// Serve static files (CSS/JS/HTML fallback)
void WebHandlers::handleStaticFile(AsyncWebServerRequest *request)
{
    String path = request->url();
    if (SPIFFS.exists(path))
    {
        request->send(SPIFFS, path, getContentType(path));
    }
    else
    {
        request->send(404, F("text/plain"), F("File not found"));
    }
}

// Define all routes
void WebHandlers::setupRoutes()
{
    // Root route
    server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleRoot(request); });

    // Sensor routes (REST endpoint removed - data comes via ESP-NOW)
    // The /sensorData GET endpoint remains for the web interface to fetch data
    server->on("/sensorData", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleGetSensorData(request); });

    server->on("/sensorpage", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleSensorDataPage(request); });

    // LED control
    server->on("/color", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleColor(request); });

    // File management routes
    server->on("/upload", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleUpload(request); });

    server->on("/upload", HTTP_POST, [](AsyncWebServerRequest *request)
               { request->send(200, F("application/json"), F("{\"success\":true,\"message\":\"Upload complete\"}")); }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
               { handleFileUpload(request, filename, index, data, len, final); });

    server->on("/delete", HTTP_POST, [this](AsyncWebServerRequest *request)
               { handleDeleteFile(request); });

    server->on("/list", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleListFiles(request); });

    // Firmware update routes
    server->on("/firmware", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleFirmware(request); });

    server->on("/firmwareUpdate", HTTP_POST, [this](AsyncWebServerRequest *request)
               { handleFirmwareUpdate(request); });

    // Static file handler for CSS, JS, HTML files
    server->onNotFound([this](AsyncWebServerRequest *request)
                       {
        String path = request->url();
        if (path.endsWith(".html") || path.endsWith(".css") || path.endsWith(".js")) {
            handleStaticFile(request);
        } else {
            request->send(404, F("text/plain"), F("Not found"));
        } });
}