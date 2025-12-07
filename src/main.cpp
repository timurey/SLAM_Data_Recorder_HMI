// Final simplified CYD HMI - Single button toggle for recording
// Based on confirmed working cyd_with_touch_simple.cpp
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>

TFT_eSPI tft = TFT_eSPI();

// Serial settings - GPIO 1 & 3 confirmed working
#define BAUD_RATE 115200
#define RX_PIN 3
#define TX_PIN 1

// Touch pin - simple IRQ detection
#define TOUCH_IRQ 36
#define TOUCH_CS 33

// State
struct Status {
    bool sensors_running = false;
    float lidar_hz = 0.0;
    float imu_hz = 0.0;
    bool lidar_ok = false;
    bool imu_ok = false;
    bool recording = false;
    float disk_gb = 0.0;
    int rec_duration = 0;
    String bag_name = "";
    unsigned long last_rx = 0;
    int rx_count = 0;
    int json_ok = 0;
    int json_err = 0;
} status;

// Colors
#define COLOR_BG 0x0000
#define COLOR_TEXT 0xFFFF
#define COLOR_OK 0x07E0
#define COLOR_ERROR 0xF800
#define COLOR_WARNING 0xFFE0

unsigned long lastDisplay = 0;
unsigned long lastTouch = 0;

void drawBigButton(const char* label, uint16_t color, int y) {
    tft.fillRoundRect(20, y, 200, 60, 8, color);
    tft.drawRoundRect(20, y, 200, 60, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, color);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(label, 120, y + 30, 4);
}

void updateDisplay() {
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextDatum(TL_DATUM);

    int y = 10;
    tft.drawString("SLAM Data Recorder", 10, y, 2); y += 25;

    bool connected = (millis() - status.last_rx < 2000);
    tft.setTextColor(connected ? COLOR_OK : COLOR_ERROR, COLOR_BG);
    tft.drawString(connected ? "CONNECTED" : "WAITING...", 10, y, 2); y += 25;

    if (connected) {
        // Sensor status
        tft.setTextColor(COLOR_TEXT, COLOR_BG);
        tft.drawString("Sensors: " + String(status.sensors_running ? "ON" : "OFF"), 10, y, 2); y += 20;

        // LiDAR
        tft.setTextColor(status.lidar_ok ? COLOR_OK : COLOR_ERROR, COLOR_BG);
        tft.drawString("LiDAR: " + String(status.lidar_hz, 1) + " Hz", 10, y, 2); y += 20;

        // IMU
        tft.setTextColor(status.imu_ok ? COLOR_OK : COLOR_ERROR, COLOR_BG);
        tft.drawString("IMU: " + String(status.imu_hz, 1) + " Hz", 10, y, 2); y += 20;

        // Disk space
        tft.setTextColor(status.disk_gb > 10.0 ? COLOR_OK : COLOR_WARNING, COLOR_BG);
        tft.drawString("Disk: " + String(status.disk_gb, 1) + " GB", 10, y, 2); y += 30;

        // Recording status
        if (status.recording) {
            tft.fillCircle(15, y+8, 8, TFT_RED);
            tft.setTextColor(COLOR_TEXT, COLOR_BG);
            int mins = status.rec_duration / 60;
            int secs = status.rec_duration % 60;
            String timeStr = String(mins) + ":" + (secs < 10 ? "0" : "") + String(secs);
            tft.drawString(" REC " + timeStr, 30, y, 2);
            y += 25;

            if (status.bag_name.length() > 0) {
                tft.setTextColor(TFT_DARKGREY, COLOR_BG);
                tft.drawString(status.bag_name.substring(0, 20), 10, y, 1);
            }
            y += 25;
        } else {
            y += 20;
        }

        // Big button - toggle recording
        if (status.sensors_running) {
            if (status.recording) {
                drawBigButton("STOP RECORD", TFT_RED, 200);
            } else {
                drawBigButton("START RECORD", TFT_GREEN, 200);
            }
        } else {
            // Sensors not running - show disabled button
            tft.fillRoundRect(20, 200, 200, 60, 8, TFT_DARKGREY);
            tft.drawRoundRect(20, 200, 200, 60, 8, TFT_WHITE);
            tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("WAITING...", 120, 230, 4);
        }
    } else {
        tft.setTextColor(TFT_DARKGREY, COLOR_BG);
        tft.drawString("RX: " + String(status.rx_count), 10, y, 1); y += 15;
        tft.drawString("JSON OK: " + String(status.json_ok), 10, y, 1);
    }
}

void sendCommand(const char* cmd) {
    StaticJsonDocument<128> doc;
    doc["cmd"] = cmd;
    char buf[128];
    size_t n = serializeJson(doc, buf);
    buf[n] = '\0';
    Serial1.print(buf);
    Serial1.print('\n');
    Serial1.flush();
}

void handleTouch() {
    // Read touch IRQ pin (active LOW when touched)
    if (digitalRead(TOUCH_IRQ) == LOW) {
        unsigned long now = millis();

        // Debounce - ignore touches within 500ms of last touch
        if (now - lastTouch > 500) {
            lastTouch = now;

            bool connected = (millis() - status.last_rx < 2000);
            if (connected && status.sensors_running) {
                // Toggle recording
                if (status.recording) {
                    sendCommand("stop_recording");
                } else {
                    sendCommand("start_recording");
                }
            }

            // Wait for touch release
            while (digitalRead(TOUCH_IRQ) == LOW) {
                delay(10);
            }
            delay(50);  // Additional debounce
        }
    }
}

void handleIncomingJson(const String &line) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, line);

    if (err) {
        status.json_err++;
        return;
    }

    if (doc.containsKey("sensors_running")) {
        status.sensors_running = doc["sensors_running"] | false;
        status.lidar_hz = doc["lidar_hz"] | 0.0;
        status.imu_hz = doc["imu_hz"] | 0.0;
        status.lidar_ok = doc["lidar_ok"] | false;
        status.imu_ok = doc["imu_ok"] | false;
        status.recording = doc["recording"] | false;
        status.disk_gb = doc["disk_gb"] | 0.0;
        status.rec_duration = doc["rec_duration"] | 0;
        status.bag_name = doc["bag_name"] | "";
        status.last_rx = millis();
        status.json_ok++;
    }
}

String readLine(unsigned long timeoutMs = 100) {
    unsigned long start = millis();
    String s;
    while (millis() - start < timeoutMs) {
        while (Serial1.available()) {
            char c = (char)Serial1.read();
            status.rx_count++;
            if (c == '\r') continue;
            if (c == '\n') return s;
            s += c;
        }
        delay(2);
    }
    return s;
}

void setup() {
    // Initialize Serial1 FIRST - exactly like confirmed working test
    Serial1.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
    delay(100);

    // Initialize display
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.setRotation(0);

    // Setup touch IRQ pin as input with pullup
    pinMode(TOUCH_IRQ, INPUT_PULLUP);
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH);  // Deselect touch controller

    updateDisplay();
}

void loop() {
    // Check for incoming data
    if (Serial1.available()) {
        String line = readLine(100);
        line.trim();
        if (line.length() > 0 && line.charAt(0) == '{') {
            handleIncomingJson(line);
        }
    }

    // Handle touch input
    handleTouch();

    // Update display every 500ms
    if (millis() - lastDisplay > 500) {
        updateDisplay();
        lastDisplay = millis();
    }

    delay(10);
}
