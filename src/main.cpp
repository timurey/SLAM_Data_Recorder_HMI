// CYD HMI for SLAM Data Recorder — dashboard UI
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>

TFT_eSPI tft = TFT_eSPI();

#define BAUD_RATE 115200
#define RX_PIN    3
#define TX_PIN    1
#define TOUCH_IRQ 36
#define TOUCH_CS  33

struct Status {
    bool  sensors_running = false;
    float lidar_hz        = 0.0;
    float imu_hz          = 0.0;
    bool  lidar_ok        = false;
    bool  imu_ok          = false;
    bool  recording       = false;
    float disk_gb         = 0.0;
    int   rec_duration    = 0;
    String bag_name       = "";
    unsigned long last_rx = 0;
    int rx_count = 0;
    int json_ok  = 0;
    int json_err = 0;
} status;

// Colors
#define C_BG      0x0000   // pure black
#define C_HDR     0x1082   // dark navy header
#define C_DIVIDER 0x4228   // dark grey lines
#define C_LABEL   0x8410   // medium grey labels
#define C_TEXT    0xFFFF   // white
#define C_OK      0x07E0   // green
#define C_ERROR   0xF800   // red
#define C_WARN    0xFFE0   // yellow
#define C_BAR_BG  0x2104   // progress bar track

// Layout — portrait 240×320
#define DISP_W   240
#define DISP_H   320
#define X_SPLIT  120   // vertical divider between LIDAR / IMU

#define Y_HDR_H  36
#define Y_SENS_TOP 37
#define Y_SENS_BOT 116
#define Y_DISK_TOP 117
#define Y_DISK_BOT 168
#define Y_REC_TOP  169
#define Y_REC_BOT  210
#define Y_BTN      240
#define Y_BTN_H    65

struct DisplayCache {
    bool initialized     = false;
    bool connected       = false;
    bool sensors_running = false;
    float lidar_hz       = -1.f;
    float imu_hz         = -1.f;
    bool  lidar_ok       = false;
    bool  imu_ok         = false;
    float disk_gb        = -1.f;
    bool  recording      = false;
    int   rec_duration   = -1;
    String bag_name      = "";
} cache;

unsigned long lastDisplay = 0;
unsigned long lastTouch   = 0;

// ── helpers ───────────────────────────────────────────────────────────────────

void hline(int y) {
    tft.drawFastHLine(0, y, DISP_W, C_DIVIDER);
}

void clearArea(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, C_BG);
}

// ── section drawers ───────────────────────────────────────────────────────────

void drawHeader(bool connected) {
    tft.fillRect(0, 0, DISP_W, Y_HDR_H, C_HDR);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_TEXT, C_HDR);
    tft.drawString("SLAM RECORDER", 8, 11, 2);
    tft.fillCircle(224, 18, 8, connected ? C_OK : C_ERROR);
    tft.drawCircle(224, 18, 8, C_TEXT);
}

void drawSensors(bool connected) {
    clearArea(0, Y_SENS_TOP, DISP_W, Y_SENS_BOT - Y_SENS_TOP);
    tft.drawFastVLine(X_SPLIT, Y_SENS_TOP, Y_SENS_BOT - Y_SENS_TOP, C_DIVIDER);

    tft.setTextDatum(TL_DATUM);

    // Labels
    tft.setTextColor(C_LABEL, C_BG);
    tft.drawString("LIDAR",        8,           44, 1);
    tft.drawString("IMU",          X_SPLIT + 8, 44, 1);
    tft.drawString("Hz",           8,           93, 1);
    tft.drawString("Hz",           X_SPLIT + 8, 93, 1);

    // Values
    auto drawVal = [&](int x, float hz, bool ok) {
        tft.setTextColor(connected ? (ok ? C_OK : C_ERROR) : C_LABEL, C_BG);
        tft.drawString(connected ? String(hz, 1) : "--", x + 6, 57, 4);
    };
    drawVal(0,       status.lidar_hz, status.lidar_ok);
    drawVal(X_SPLIT, status.imu_hz,   status.imu_ok);
}

void drawDisk(bool connected) {
    clearArea(0, Y_DISK_TOP, DISP_W, Y_DISK_BOT - Y_DISK_TOP);
    tft.setTextColor(C_LABEL, C_BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("DISK FREE", 8, 123, 1);

    if (!connected) return;

    uint16_t c = status.disk_gb > 50 ? C_OK : (status.disk_gb > 10 ? C_WARN : C_ERROR);
    tft.setTextColor(c, C_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String(status.disk_gb, 1) + " GB", 232, 122, 2);
    tft.setTextDatum(TL_DATUM);

    // Progress bar (scale: 0–200 GB)
    const int bx = 8, by = 141, bw = 224, bh = 12;
    tft.fillRoundRect(bx, by, bw, bh, 3, C_BAR_BG);
    int fw = max(4, (int)(bw * min(status.disk_gb / 200.f, 1.f)));
    tft.fillRoundRect(bx, by, fw, bh, 3, c);
}

void drawRec() {
    clearArea(0, Y_REC_TOP, DISP_W, Y_REC_BOT - Y_REC_TOP);
    if (!status.recording) return;

    tft.fillCircle(16, 184, 7, C_ERROR);

    int mins = status.rec_duration / 60;
    int secs = status.rec_duration % 60;
    String t = "REC  " + String(mins) + ":" + (secs < 10 ? "0" : "") + String(secs);
    tft.setTextColor(C_TEXT, C_BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(t, 30, 176, 2);

    tft.setTextColor(C_LABEL, C_BG);
    tft.drawString(status.bag_name.substring(0, 33), 8, 197, 1);
}

void drawButton() {
    tft.setTextDatum(MC_DATUM);
    if (!status.sensors_running) {
        tft.fillRoundRect(10, Y_BTN, 220, Y_BTN_H, 10, C_DIVIDER);
        tft.setTextColor(C_LABEL, C_DIVIDER);
        tft.drawString("WAITING...", 120, Y_BTN + Y_BTN_H / 2, 4);
    } else if (status.recording) {
        tft.fillRoundRect(10, Y_BTN, 220, Y_BTN_H, 10, C_ERROR);
        tft.setTextColor(C_TEXT, C_ERROR);
        tft.drawString("STOP RECORD", 120, Y_BTN + Y_BTN_H / 2, 4);
    } else {
        tft.fillRoundRect(10, Y_BTN, 220, Y_BTN_H, 10, C_OK);
        tft.setTextColor(C_BG, C_OK);
        tft.drawString("START RECORD", 120, Y_BTN + Y_BTN_H / 2, 4);
    }
    tft.setTextDatum(TL_DATUM);
}

// ── full / partial redraw ─────────────────────────────────────────────────────

void fullRedraw(bool connected) {
    tft.fillScreen(C_BG);

    drawHeader(connected);  hline(Y_HDR_H);
    drawSensors(connected); hline(Y_SENS_BOT);
    drawDisk(connected);    hline(Y_DISK_BOT);

    if (status.recording) {
        drawRec();
        hline(Y_REC_BOT);
    }

    drawButton();

    cache.initialized     = true;
    cache.connected       = connected;
    cache.sensors_running = status.sensors_running;
    cache.lidar_hz        = status.lidar_hz;
    cache.imu_hz          = status.imu_hz;
    cache.lidar_ok        = status.lidar_ok;
    cache.imu_ok          = status.imu_ok;
    cache.disk_gb         = status.disk_gb;
    cache.recording       = status.recording;
    cache.rec_duration    = status.rec_duration;
    cache.bag_name        = status.bag_name;
}

void updateDisplay() {
    bool connected = (millis() - status.last_rx < 2000);

    if (!cache.initialized
        || connected               != cache.connected
        || status.recording        != cache.recording
        || status.sensors_running  != cache.sensors_running) {
        fullRedraw(connected);
        return;
    }

    if (!connected) return;

    bool sensor_changed = status.lidar_hz != cache.lidar_hz
                       || status.imu_hz   != cache.imu_hz
                       || status.lidar_ok != cache.lidar_ok
                       || status.imu_ok   != cache.imu_ok;
    if (sensor_changed) {
        drawSensors(connected);
        tft.drawFastVLine(X_SPLIT, Y_SENS_TOP, Y_SENS_BOT - Y_SENS_TOP, C_DIVIDER);
        cache.lidar_hz = status.lidar_hz;
        cache.imu_hz   = status.imu_hz;
        cache.lidar_ok = status.lidar_ok;
        cache.imu_ok   = status.imu_ok;
    }

    if (status.disk_gb != cache.disk_gb) {
        drawDisk(connected);
        cache.disk_gb = status.disk_gb;
    }

    if (status.recording && status.rec_duration != cache.rec_duration) {
        drawRec();
        cache.rec_duration = status.rec_duration;
        cache.bag_name     = status.bag_name;
    }
}

// ── protocol ──────────────────────────────────────────────────────────────────

void sendCommand(const char *cmd) {
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
    if (digitalRead(TOUCH_IRQ) == LOW) {
        unsigned long now = millis();
        if (now - lastTouch > 500) {
            lastTouch = now;
            bool connected = (millis() - status.last_rx < 2000);
            if (connected && status.sensors_running)
                sendCommand(status.recording ? "stop_recording" : "start_recording");
            while (digitalRead(TOUCH_IRQ) == LOW) delay(10);
            delay(50);
        }
    }
}

void handleIncomingJson(const String &line) {
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, line)) { status.json_err++; return; }
    if (!doc["sensors_running"].is<bool>()) return;

    status.sensors_running = doc["sensors_running"] | false;
    status.lidar_hz        = doc["lidar_hz"]        | 0.0;
    status.imu_hz          = doc["imu_hz"]          | 0.0;
    status.lidar_ok        = doc["lidar_ok"]        | false;
    status.imu_ok          = doc["imu_ok"]          | false;
    status.recording       = doc["recording"]       | false;
    status.disk_gb         = doc["disk_gb"]         | 0.0;
    status.rec_duration    = doc["rec_duration"]    | 0;
    status.bag_name        = doc["bag_name"]        | "";
    status.last_rx         = millis();
    status.json_ok++;
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

// ── setup / loop ──────────────────────────────────────────────────────────────

void setup() {
    Serial1.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
    delay(100);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.invertDisplay(false);
    tft.setRotation(0);
    tft.fillScreen(C_BG);
    tft.setRotation(4);
    tft.fillScreen(C_BG);

    pinMode(TOUCH_IRQ, INPUT_PULLUP);
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH);

    updateDisplay();
}

void loop() {
    if (Serial1.available()) {
        String line = readLine(100);
        line.trim();
        if (line.length() > 0 && line.charAt(0) == '{')
            handleIncomingJson(line);
    }

    handleTouch();

    if (millis() - lastDisplay > 500) {
        updateDisplay();
        lastDisplay = millis();
    }

    delay(10);
}
