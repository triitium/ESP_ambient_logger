#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <array>
#include "main.h"

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
        Serial.printf("%s\n", pass);
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

class BME280Sensor {
public:
    BME280Sensor(uint8_t addr = 0x76) : addr(addr) {}

    bool begin() {
        if (!bme.begin(addr)) {
            addr = 0x77;
            if (!bme.begin(addr)) return false;
        }
        return true;
    }

    float getAverageTemperature() { return measureAverage(&BME280Sensor::readTemperature); }
    float getAverageHumidity()    { return measureAverage(&BME280Sensor::readHumidity); }
    float getAveragePressure()    { return measureAverage(&BME280Sensor::readPressure); }

private:
    Adafruit_BME280 bme;
    uint8_t addr;

    float measureAverage(float (BME280Sensor::*readFunc)()) {
        float sum = 0.0f;
        int validCount = 0;

        for (int i = 0; i < SAMPLE_SIZE; i++) {
            float val = (this->*readFunc)();
            if (!isnan(val)) {
                sum += val;
                validCount++;
            }
            delay(50);
        }

        if (validCount == 0) return NAN;
        return sum / validCount;
    }

    float readTemperature() { return bme.readTemperature(); }
    float readHumidity()    { return bme.readHumidity(); }
    float readPressure()    { return bme.readPressure(); }
};

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

WiFiManager wifiManager(WIFI_SSID, WIFI_PASS, WIFI_TIMEOUT_MS);
BME280Sensor sensor;
DataSender sender(SERVER_URL, ENDPOINT, API_KEY);

unsigned long lastSend = 0;

void setup() {
    Serial.begin(115200);
    Wire.begin();

    if (!sensor.begin()) {
        Serial.println("BME280 sensor not found. Check wiring!");
        while (1) delay(1000);
    }

    wifiManager.update();
}

void loop() {
    wifiManager.update();

    if (WiFi.status() != WL_CONNECTED) return;

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
