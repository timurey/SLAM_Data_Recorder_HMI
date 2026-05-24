// CYD HMI for SLAM Data Recorder
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>

TFT_eSPI tft = TFT_eSPI();

#define BAUD_RATE 115200
#define RX_PIN 3
#define TX_PIN 1

#define TOUCH_IRQ 36
#define TOUCH_CS 33

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

#define COLOR_BG 0x0000
#define COLOR_TEXT 0xFFFF
#define COLOR_OK 0x07E0
#define COLOR_ERROR 0xF800
#define COLOR_WARNING 0xFFE0

// Portrait 240x320 layout
#define DISP_W 240
#define DISP_H 320
#define COL_X 5
#define ROW_H 18

#define ROW_TITLE 5
#define ROW_CONN 28
#define ROW_LIDAR 52
#define ROW_IMU 72
#define ROW_SENSORS 92
#define ROW_DISK 112
#define ROW_REC 140
#define ROW_BAGNAME 162
#define ROW_BTN 190

struct DisplayCache
{
  bool initialized = false;
  bool connected = false;
  bool sensors_running = false;
  float lidar_hz = -1.0f;
  float imu_hz = -1.0f;
  bool lidar_ok = false;
  bool imu_ok = false;
  float disk_gb = -1.0f;
  bool recording = false;
  int rec_duration = -1;
  String bag_name = "";
} cache;

unsigned long lastDisplay = 0;
unsigned long lastTouch = 0;

void clearRow(int y, int h = ROW_H)
{
  tft.fillRect(0, y, DISP_W, h, COLOR_BG);
}

void drawButton()
{
  if (status.sensors_running)
  {
    uint16_t color = status.recording ? TFT_RED : TFT_GREEN;
    const char *label = status.recording ? "STOP RECORD" : "START RECORD";
    tft.fillRoundRect(10, ROW_BTN, 220, 50, 8, color);
    tft.drawRoundRect(10, ROW_BTN, 220, 50, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, color);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(label, 120, ROW_BTN + 25, 4);
  }
  else
  {
    tft.fillRoundRect(10, ROW_BTN, 220, 50, 8, TFT_DARKGREY);
    tft.drawRoundRect(10, ROW_BTN, 220, 50, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("WAITING...", 120, ROW_BTN + 25, 4);
  }
  tft.setTextDatum(TL_DATUM);
}

void fullRedraw(bool connected)
{
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawString("SLAM Data Recorder", COL_X, ROW_TITLE, 2);

  tft.setTextColor(connected ? COLOR_OK : COLOR_ERROR, COLOR_BG);
  tft.drawString(connected ? "CONNECTED" : "WAITING...", COL_X, ROW_CONN, 2);

  if (connected)
  {
    tft.setTextColor(status.lidar_ok ? COLOR_OK : COLOR_ERROR, COLOR_BG);
    tft.drawString("LiDAR: " + String(status.lidar_hz, 1) + " Hz", COL_X, ROW_LIDAR, 2);

    tft.setTextColor(status.imu_ok ? COLOR_OK : COLOR_ERROR, COLOR_BG);
    tft.drawString("IMU:   " + String(status.imu_hz, 1) + " Hz", COL_X, ROW_IMU, 2);

    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString("Sensors: " + String(status.sensors_running ? "ON" : "OFF"), COL_X, ROW_SENSORS, 2);

    tft.setTextColor(status.disk_gb > 10.0 ? COLOR_OK : COLOR_WARNING, COLOR_BG);
    tft.drawString("Disk: " + String(status.disk_gb, 1) + " GB free", COL_X, ROW_DISK, 2);

    if (status.recording)
    {
      tft.fillCircle(COL_X + 8, ROW_REC + 8, 8, TFT_RED);
      tft.setTextColor(COLOR_TEXT, COLOR_BG);
      int mins = status.rec_duration / 60;
      int secs = status.rec_duration % 60;
      String t = String(mins) + ":" + (secs < 10 ? "0" : "") + String(secs);
      tft.drawString(" REC " + t, COL_X + 20, ROW_REC, 2);

      tft.setTextColor(TFT_DARKGREY, COLOR_BG);
      tft.drawString(status.bag_name.substring(0, 28), COL_X, ROW_BAGNAME, 1);
    }

    drawButton();
  }
  else
  {
    tft.setTextColor(TFT_DARKGREY, COLOR_BG);
    tft.drawString("RX: " + String(status.rx_count), COL_X, ROW_LIDAR, 1);
    tft.drawString("JSON OK: " + String(status.json_ok), COL_X, ROW_LIDAR + 15, 1);
  }

  cache.initialized = true;
  cache.connected = connected;
  cache.sensors_running = status.sensors_running;
  cache.lidar_hz = status.lidar_hz;
  cache.imu_hz = status.imu_hz;
  cache.lidar_ok = status.lidar_ok;
  cache.imu_ok = status.imu_ok;
  cache.disk_gb = status.disk_gb;
  cache.recording = status.recording;
  cache.rec_duration = status.rec_duration;
  cache.bag_name = status.bag_name;
}

void updateDisplay()
{
  bool connected = (millis() - status.last_rx < 2000);

  if (!cache.initialized || connected != cache.connected || status.sensors_running != cache.sensors_running || status.recording != cache.recording)
  {
    fullRedraw(connected);
    return;
  }

  if (!connected)
    return;

  tft.setTextDatum(TL_DATUM);

  if (status.lidar_hz != cache.lidar_hz || status.lidar_ok != cache.lidar_ok)
  {
    clearRow(ROW_LIDAR);
    tft.setTextColor(status.lidar_ok ? COLOR_OK : COLOR_ERROR, COLOR_BG);
    tft.drawString("LiDAR: " + String(status.lidar_hz, 1) + " Hz", COL_X, ROW_LIDAR, 2);
    cache.lidar_hz = status.lidar_hz;
    cache.lidar_ok = status.lidar_ok;
  }

  if (status.imu_hz != cache.imu_hz || status.imu_ok != cache.imu_ok)
  {
    clearRow(ROW_IMU);
    tft.setTextColor(status.imu_ok ? COLOR_OK : COLOR_ERROR, COLOR_BG);
    tft.drawString("IMU:   " + String(status.imu_hz, 1) + " Hz", COL_X, ROW_IMU, 2);
    cache.imu_hz = status.imu_hz;
    cache.imu_ok = status.imu_ok;
  }

  if (status.disk_gb != cache.disk_gb)
  {
    clearRow(ROW_DISK);
    tft.setTextColor(status.disk_gb > 10.0 ? COLOR_OK : COLOR_WARNING, COLOR_BG);
    tft.drawString("Disk: " + String(status.disk_gb, 1) + " GB free", COL_X, ROW_DISK, 2);
    cache.disk_gb = status.disk_gb;
  }

  if (status.recording && status.rec_duration != cache.rec_duration)
  {
    clearRow(ROW_REC, 20);
    tft.fillCircle(COL_X + 8, ROW_REC + 8, 8, TFT_RED);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    int mins = status.rec_duration / 60;
    int secs = status.rec_duration % 60;
    String t = String(mins) + ":" + (secs < 10 ? "0" : "") + String(secs);
    tft.drawString(" REC " + t, COL_X + 20, ROW_REC, 2);
    cache.rec_duration = status.rec_duration;
  }

  if (status.recording && status.bag_name != cache.bag_name)
  {
    clearRow(ROW_BAGNAME, 12);
    tft.setTextColor(TFT_DARKGREY, COLOR_BG);
    tft.drawString(status.bag_name.substring(0, 28), COL_X, ROW_BAGNAME, 1);
    cache.bag_name = status.bag_name;
  }
}

void sendCommand(const char *cmd)
{
  StaticJsonDocument<128> doc;
  doc["cmd"] = cmd;
  char buf[128];
  size_t n = serializeJson(doc, buf);
  buf[n] = '\0';
  Serial1.print(buf);
  Serial1.print('\n');
  Serial1.flush();
}

void handleTouch()
{
  if (digitalRead(TOUCH_IRQ) == LOW)
  {
    unsigned long now = millis();
    if (now - lastTouch > 500)
    {
      lastTouch = now;
      bool connected = (millis() - status.last_rx < 2000);
      if (connected && status.sensors_running)
      {
        if (status.recording)
        {
          sendCommand("stop_recording");
        }
        else
        {
          sendCommand("start_recording");
        }
      }
      while (digitalRead(TOUCH_IRQ) == LOW)
        delay(10);
      delay(50);
    }
  }
}

void handleIncomingJson(const String &line) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err)
    {
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

void setup()
{
  Serial1.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(100);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.invertDisplay(false); // ST7789 displays colors inverted by default
  tft.setRotation(0);
  tft.fillScreen(COLOR_BG);
  tft.setRotation(4);
  tft.fillScreen(COLOR_BG);

  pinMode(TOUCH_IRQ, INPUT_PULLUP);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);

  updateDisplay();
}

void loop()
{
  if (Serial1.available())
  {
    String line = readLine(100);
    line.trim();
    if (line.length() > 0 && line.charAt(0) == '{')
    {
      handleIncomingJson(line);
    }
  }

  handleTouch();

  if (millis() - lastDisplay > 500)
  {
    updateDisplay();
    lastDisplay = millis();
  }

  delay(10);
}
