#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <array>
#include "main.h"

// ---------------------------- Wi-Fi Manager ----------------------------
class WiFiManager {
public:
    WiFiManager(const char* ssid, const char* pass, unsigned long timeout)
        : ssid(ssid), pass(pass), timeout(timeout), lastAttempt(0) {}

    void update() {
        if (WiFi.status() == WL_CONNECTED) return;

        unsigned long now = millis();
        if (now - lastAttempt < timeout) return;

        lastAttempt = now;
        Serial.printf("Connecting to Wi-Fi: %s\n", ssid);
        WiFi.begin(ssid, pass);

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < timeout) {
            delay(200);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWi-Fi Connected!");
            Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("\nWi-Fi connection failed, will retry...");
        }
    }

private:
    const char* ssid;
    const char* pass;
    unsigned long timeout;
    unsigned long lastAttempt;
};

// ---------------------------- BME280 Sensor ----------------------------
class BME280Sensor {
public:
    BME280Sensor(uint8_t addr = 0x76) : addr(addr), index(0), filled(false) {}

    bool begin() {
        if (!bme.begin(addr)) {
            addr = 0x77;
            if (!bme.begin(addr)) return false;
        }
        return true;
    }

    void update() {
        float t = bme.readTemperature();
        float h = bme.readHumidity();
        float p = bme.readPressure() / 100.0F;

        // Handle invalid readings
        if (isnan(t) || isnan(h) || isnan(p)) {
            Serial.println("Sensor read failed, skipping sample...");
            return;
        }

        tempBuffer[index] = t;
        humBuffer[index]  = h;
        presBuffer[index] = p;

        index = (index + 1) % SAMPLE_SIZE;
        if (index == 0) filled = true;
    }

    float getAverageTemperature() { return average(tempBuffer); }
    float getAverageHumidity()    { return average(humBuffer); }
    float getAveragePressure()    { return average(presBuffer); }

private:
    Adafruit_BME280 bme;
    uint8_t addr;

    std::array<float, SAMPLE_SIZE> tempBuffer{};
    std::array<float, SAMPLE_SIZE> humBuffer{};
    std::array<float, SAMPLE_SIZE> presBuffer{};

    size_t index;
    bool filled;

    float average(const std::array<float, SAMPLE_SIZE>& buffer) {
        size_t count = filled ? SAMPLE_SIZE : index;
        if (count == 0) return NAN;

        float sum = 0.0f;
        for (size_t i = 0; i < count; i++) sum += buffer[i];
        return sum / count;
    }
};

// ---------------------------- Data Sender ----------------------------
class DataSender {
public:
    DataSender(const char* serverUrl, const char* endpoint, const char* apiKey)
        : serverUrl(serverUrl), endpoint(endpoint), apiKey(apiKey) {}

    void send(float temperature, float humidity, float pressure) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Wi-Fi disconnected, cannot send data.");
            return;
        }

        HTTPClient http;
        WiFiClient client;
        String url = String(serverUrl) + endpoint;
        http.begin(client, url);
        http.addHeader("Content-Type", "application/json");

        String payload = "{";
        payload += "\"api_key\":\"" + String(apiKey) + "\",";
        payload += "\"content\":{";
        payload += "\"temperature\":" + String(temperature, 2) + ",";
        payload += "\"humidity\":" + String(humidity, 2) + ",";
        payload += "\"pressure\":" + String(pressure, 2);
        payload += "}}";

        int code = http.POST(payload);
        if (code > 0) {
            Serial.printf("Data sent! HTTP code: %d\n", code);
        } else {
            Serial.printf("Failed to send data: %s\n", http.errorToString(code).c_str());
        }
        http.end();
    }

private:
    const char* serverUrl;
    const char* endpoint;
    const char* apiKey;
};

// ---------------------------- Globals ----------------------------
WiFiManager wifiManager(WIFI_SSID, WIFI_PASS, WIFI_TIMEOUT_MS);
BME280Sensor sensor;
DataSender sender(SERVER_URL, ENDPOINT, API_KEY);

unsigned long lastSend = 0;

// ---------------------------- Setup ----------------------------
void setup() {
    Serial.begin(115200);
    Wire.begin();

    if (!sensor.begin()) {
        Serial.println("BME280 sensor not found. Check wiring!");
        while (1) delay(1000);
    }

    wifiManager.update();
}

// ---------------------------- Loop ----------------------------
void loop() {
    wifiManager.update();

    if (WiFi.status() != WL_CONNECTED) return;

    sensor.update(); // store a new sample

    unsigned long now = millis();
    if (now - lastSend >= INTERVAL_MS) {
        lastSend = now;

        float temp = sensor.getAverageTemperature();
        float hum  = sensor.getAverageHumidity();
        float pres = sensor.getAveragePressure();

        if (isnan(temp) || isnan(hum) || isnan(pres)) {
            Serial.println("Skipping send due to invalid sensor averages.");
            return;
        }

        sender.send(temp, hum, pres);
    }
}
