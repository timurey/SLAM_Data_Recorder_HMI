// CYD HMI for Pulse — LVGL-based dashboard with WiFi/QR screen
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <lvgl.h>

// P5 (GPIO1/GPIO3) unusable: CH340C holds GPIO3 HIGH even without USB-C.
// Use CN1 connector: IO22=RX, IO27=TX → UART2.
HardwareSerial HmiSerial(2);
TFT_eSPI       tft = TFT_eSPI();

#define TOUCH_IRQ  36
#define BAUD_RATE  115200
#define DISP_W     240
#define DISP_H     320

// ── Status ────────────────────────────────────────────────────────────────────

struct Status {
    bool   sensors_running = false;
    float  lidar_hz        = 0.0f;
    float  imu_hz          = 0.0f;
    bool   lidar_ok        = false;
    bool   imu_ok          = false;
    bool   recording       = false;
    float  disk_gb         = 0.0f;
    int    rec_duration    = 0;
    String bag_name;
    String wifi_ip;
    String wifi_mode       = "disconnected";
    unsigned long last_rx  = 0;
} status;

// ── Forward declarations ──────────────────────────────────────────────────────
void send_cmd(const char *cmd);
void update_main_screen();
void update_wifi_screen();
void handle_touch_logic();

// ── LVGL display buffer ───────────────────────────────────────────────────────

static lv_disp_draw_buf_t draw_buf;
static lv_color_t         lvgl_buf[DISP_W * 30];

void lvgl_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&px->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

// Touch: only IRQ pin, no coordinates — tap anywhere = click at screen center.
void lvgl_touch_read(lv_indev_drv_t *, lv_indev_data_t *data) {
    data->state   = (digitalRead(TOUCH_IRQ) == LOW) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = DISP_W / 2;
    data->point.y = DISP_H / 2;
}

// ── Screens ───────────────────────────────────────────────────────────────────

static lv_obj_t *scr_main  = nullptr;
static lv_obj_t *scr_wifi  = nullptr;
static bool       on_wifi   = false;

// Main screen widgets
static lv_obj_t *lbl_title;
static lv_obj_t *lbl_conn;
static lv_obj_t *lbl_lidar;
static lv_obj_t *lbl_imu;
static lv_obj_t *lbl_disk;
static lv_obj_t *lbl_rec;
static lv_obj_t *btn_rec;
static lv_obj_t *lbl_btn;
static lv_obj_t *lbl_wifi_icon;

// WiFi screen widgets
static lv_obj_t *lbl_wifi_mode;
static lv_obj_t *lbl_wifi_ip;
static lv_obj_t *lbl_qr_hint;
static lv_obj_t *qr_code    = nullptr;

// ── Color helpers ─────────────────────────────────────────────────────────────

#define C_OK      lv_color_hex(0x00cc44)
#define C_WARN    lv_color_hex(0xe8a000)
#define C_DEAD    lv_color_hex(0xcc2200)
#define C_BG      lv_color_hex(0x0d0d0d)
#define C_CARD    lv_color_hex(0x111111)
#define C_TEXT    lv_color_hex(0xd0d0d0)
#define C_DIM     lv_color_hex(0x808080)
#define C_HDR     lv_color_hex(0x1a1a1a)

static lv_style_t style_screen;
static lv_style_t style_card;
static lv_style_t style_label;
static lv_style_t style_dim;
static lv_style_t style_btn_rec;
static lv_style_t style_btn_stop;

void init_styles() {
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, C_BG);

    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, C_CARD);
    lv_style_set_border_color(&style_card, lv_color_hex(0x1e1e1e));
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_radius(&style_card, 4);
    lv_style_set_pad_all(&style_card, 10);

    lv_style_init(&style_label);
    lv_style_set_text_color(&style_label, C_TEXT);

    lv_style_init(&style_dim);
    lv_style_set_text_color(&style_dim, C_DIM);

    lv_style_init(&style_btn_rec);
    lv_style_set_bg_color(&style_btn_rec, C_OK);
    lv_style_set_text_color(&style_btn_rec, lv_color_hex(0x000000));
    lv_style_set_radius(&style_btn_rec, 6);
    lv_style_set_border_width(&style_btn_rec, 0);

    lv_style_init(&style_btn_stop);
    lv_style_set_bg_color(&style_btn_stop, C_DEAD);
    lv_style_set_text_color(&style_btn_stop, C_TEXT);
    lv_style_set_radius(&style_btn_stop, 6);
    lv_style_set_border_width(&style_btn_stop, 0);
}

// ── Button handler ────────────────────────────────────────────────────────────

static unsigned long touch_start = 0;
static bool          is_touching  = false;
static unsigned long last_tap     = 0;

void handle_touch_logic() {
    bool touched = (digitalRead(TOUCH_IRQ) == LOW);
    unsigned long now = millis();

    if (touched && !is_touching) {
        is_touching = true;
        touch_start = now;
    }

    if (is_touching && !touched) {
        uint32_t held = now - touch_start;
        is_touching = false;

        if (now - last_tap < 300) return;  // debounce
        last_tap = now;

        if (held >= 2000) {
            // Long hold = shutdown
            send_cmd("shutdown");
            return;
        }

        if (on_wifi) {
            // Any tap on wifi screen → back to main
            lv_scr_load_anim(scr_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, false);
            on_wifi = false;
            return;
        }

        // Short tap on main screen: toggle recording (if connected & sensors running)
        bool connected = (millis() - status.last_rx < 2000);
        if (connected && status.sensors_running) {
            send_cmd(status.recording ? "stop_recording" : "start_recording");
        }
    }
}

// ── Protocol ──────────────────────────────────────────────────────────────────

void send_cmd(const char *cmd) {
  JsonDocument doc;
  doc["cmd"] = cmd;
  char buf[128];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  buf[n] = '\0';
  HmiSerial.print(buf);
  HmiSerial.print('\n');
  HmiSerial.flush();
}

void handle_json(const String &line) {
  JsonDocument doc;
  if (deserializeJson(doc, line))
    return;
  if (!doc["sensors_running"].is<bool>())
    return;
  status.sensors_running = doc["sensors_running"] | false;
  status.lidar_hz = doc["lidar_hz"] | 0.0f;
  status.imu_hz = doc["imu_hz"] | 0.0f;
  status.lidar_ok = doc["lidar_ok"] | false;
  status.imu_ok = doc["imu_ok"] | false;
  status.recording = doc["recording"] | false;
  status.disk_gb = doc["disk_gb"] | 0.0f;
  status.rec_duration = doc["rec_duration"] | 0;
  status.bag_name = doc["bag_name"] | "";
  status.wifi_ip = doc["wifi_ip"] | "";
  status.wifi_mode = doc["wifi_mode"] | "disconnected";
  status.last_rx = millis();
}

// ── Main screen ───────────────────────────────────────────────────────────────

void build_main_screen() {
    scr_main = lv_obj_create(nullptr);
    lv_obj_add_style(scr_main, &style_screen, 0);
    lv_obj_set_scrollbar_mode(scr_main, LV_SCROLLBAR_MODE_OFF);

    // Header bar
    lv_obj_t *hdr = lv_obj_create(scr_main);
    lv_obj_set_size(hdr, DISP_W, 36);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(hdr, &style_card, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);

    lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, "PULSE");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_title, C_DIM, 0);
    lv_obj_set_style_text_letter_space(lbl_title, 4, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 12, 0);

    // WiFi icon (top-right of header) — tap navigates to WiFi screen
    lbl_wifi_icon = lv_label_create(hdr);
    lv_label_set_text(lbl_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(lbl_wifi_icon, C_DIM, 0);
    lv_obj_align(lbl_wifi_icon, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_flag(lbl_wifi_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lbl_wifi_icon, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            update_wifi_screen();
            lv_scr_load_anim(scr_wifi, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
            on_wifi = true;
        }
    }, LV_EVENT_CLICKED, nullptr);

    // Connection dot
    lbl_conn = lv_label_create(hdr);
    lv_label_set_text(lbl_conn, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(lbl_conn, C_DEAD, 0);
    lv_obj_align(lbl_conn, LV_ALIGN_RIGHT_MID, -40, 0);

    // Sensors card
    lv_obj_t *card_s = lv_obj_create(scr_main);
    lv_obj_set_size(card_s, DISP_W - 12, 102);
    lv_obj_align(card_s, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_add_style(card_s, &style_card, 0);

    lv_obj_t *lbl_s_title = lv_label_create(card_s);
    lv_label_set_text(lbl_s_title, "SENSORS");
    lv_obj_set_style_text_color(lbl_s_title, C_DIM, 0);
    lv_obj_set_style_text_font(lbl_s_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_letter_space(lbl_s_title, 2, 0);
    lv_obj_align(lbl_s_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lbl_lidar = lv_label_create(card_s);
    lv_label_set_text(lbl_lidar, "LIDAR   --.- Hz");
    lv_obj_set_style_text_font(lbl_lidar, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_lidar, C_DIM, 0);
    lv_obj_align(lbl_lidar, LV_ALIGN_TOP_LEFT, 0, 18);

    lbl_imu = lv_label_create(card_s);
    lv_label_set_text(lbl_imu, "IMU    ---.-- Hz");
    lv_obj_set_style_text_font(lbl_imu, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_imu, C_DIM, 0);
    lv_obj_align(lbl_imu, LV_ALIGN_TOP_LEFT, 0, 40);

    lbl_disk = lv_label_create(card_s);
    lv_label_set_text(lbl_disk, "DISK  -----  GB");
    lv_obj_set_style_text_font(lbl_disk, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_disk, C_DIM, 0);
    lv_obj_align(lbl_disk, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // Rec info
    lbl_rec = lv_label_create(scr_main);
    lv_label_set_text(lbl_rec, "");
    lv_obj_set_style_text_color(lbl_rec, C_DEAD, 0);
    lv_obj_set_style_text_font(lbl_rec, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_rec, LV_ALIGN_TOP_MID, 0, 142);
    lv_obj_set_width(lbl_rec, DISP_W - 12);
    lv_label_set_long_mode(lbl_rec, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Record button
    btn_rec = lv_btn_create(scr_main);
    lv_obj_set_size(btn_rec, DISP_W - 24, 58);
    lv_obj_align(btn_rec, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_style(btn_rec, &style_btn_rec, 0);

    lbl_btn = lv_label_create(btn_rec);
    lv_label_set_text(lbl_btn, "START RECORD");
    lv_obj_set_style_text_font(lbl_btn, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_btn);

    lv_obj_add_event_cb(btn_rec, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            bool connected = (millis() - status.last_rx < 2000);
            if (connected && status.sensors_running)
                send_cmd(status.recording ? "stop_recording" : "start_recording");
        }
    }, LV_EVENT_CLICKED, nullptr);
}

// ── WiFi screen ───────────────────────────────────────────────────────────────

void build_wifi_screen() {
    scr_wifi = lv_obj_create(nullptr);
    lv_obj_add_style(scr_wifi, &style_screen, 0);
    lv_obj_set_scrollbar_mode(scr_wifi, LV_SCROLLBAR_MODE_OFF);

    // Header
    lv_obj_t *hdr = lv_obj_create(scr_wifi);
    lv_obj_set_size(hdr, DISP_W, 36);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(hdr, &style_card, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);

    lv_obj_t *lbl_hdr = lv_label_create(hdr);
    lv_label_set_text(lbl_hdr, LV_SYMBOL_LEFT "  WIFI");
    lv_obj_set_style_text_color(lbl_hdr, C_DIM, 0);
    lv_obj_set_style_text_letter_space(lbl_hdr, 2, 0);
    lv_obj_set_style_text_font(lbl_hdr, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_hdr, LV_ALIGN_LEFT_MID, 12, 0);

    // Mode label
    lbl_wifi_mode = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_wifi_mode, "MODE: DISCONNECTED");
    lv_obj_set_style_text_font(lbl_wifi_mode, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_wifi_mode, C_DIM, 0);
    lv_obj_align(lbl_wifi_mode, LV_ALIGN_TOP_MID, 0, 46);

    // IP label
    lbl_wifi_ip = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_wifi_ip, "");
    lv_obj_set_style_text_font(lbl_wifi_ip, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_wifi_ip, C_TEXT, 0);
    lv_obj_align(lbl_wifi_ip, LV_ALIGN_TOP_MID, 0, 68);

    // QR code placeholder (will be replaced in update_wifi_screen)
    lbl_qr_hint = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_qr_hint, "No IP assigned");
    lv_obj_set_style_text_color(lbl_qr_hint, C_DIM, 0);
    lv_obj_set_style_text_font(lbl_qr_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_qr_hint, LV_ALIGN_CENTER, 0, 20);

    // Back hint
    lv_obj_t *lbl_back = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_back, "Tap anywhere to go back");
    lv_obj_set_style_text_color(lbl_back, C_DIM, 0);
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_back, LV_ALIGN_BOTTOM_MID, 0, -12);
}

// ── Screen updaters ───────────────────────────────────────────────────────────

void update_main_screen() {
    bool connected = (millis() - status.last_rx < 2000);

    lv_obj_set_style_text_color(lbl_conn,
        connected ? C_OK : C_DEAD, 0);
    lv_label_set_text(lbl_conn, connected ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);

    // WiFi icon color by mode
    lv_color_t wc = C_DIM;
    if (status.wifi_mode == "client") wc = C_OK;
    else if (status.wifi_mode == "ap") wc = C_WARN;
    lv_obj_set_style_text_color(lbl_wifi_icon, wc, 0);

    if (!connected) {
        lv_obj_set_style_text_color(lbl_lidar, C_DIM, 0);
        lv_obj_set_style_text_color(lbl_imu,   C_DIM, 0);
        lv_label_set_text(lbl_lidar, "LIDAR   --.- Hz");
        lv_label_set_text(lbl_imu,   "IMU    ---.-- Hz");
        lv_label_set_text(lbl_disk,  "DISK  -----  GB");
        lv_label_set_text(lbl_rec,   "");
        lv_label_set_text(lbl_btn,   "WAITING...");
        lv_obj_add_style(btn_rec, &style_btn_stop, 0);
        lv_obj_set_style_bg_color(btn_rec, lv_color_hex(0x2a2a2a), 0);
        return;
    }

    // Lidar
    char buf[48];
    lv_color_t lc = status.lidar_ok ? C_OK : C_DEAD;
    snprintf(buf, sizeof(buf), "LIDAR  %5.1f Hz", status.lidar_hz);
    lv_label_set_text(lbl_lidar, buf);
    lv_obj_set_style_text_color(lbl_lidar, lc, 0);

    // IMU
    lv_color_t ic = status.imu_ok ? C_OK : C_DEAD;
    snprintf(buf, sizeof(buf), "IMU   %6.1f Hz", status.imu_hz);
    lv_label_set_text(lbl_imu, buf);
    lv_obj_set_style_text_color(lbl_imu, ic, 0);

    // Disk
    lv_color_t dc = status.disk_gb > 50 ? C_OK : (status.disk_gb > 10 ? C_WARN : C_DEAD);
    snprintf(buf, sizeof(buf), "DISK   %.1f GB free", status.disk_gb);
    lv_label_set_text(lbl_disk, buf);
    lv_obj_set_style_text_color(lbl_disk, dc, 0);

    // Rec timer
    if (status.recording) {
        int m = status.rec_duration / 60, s = status.rec_duration % 60;
        snprintf(buf, sizeof(buf), "● REC  %02d:%02d", m, s);
        lv_label_set_text(lbl_rec, buf);
    } else {
        lv_label_set_text(lbl_rec, "");
    }

    // Button
    if (!status.sensors_running) {
        lv_label_set_text(lbl_btn, "WAITING...");
        lv_obj_set_style_bg_color(btn_rec, lv_color_hex(0x2a2a2a), 0);
    } else if (status.recording) {
        lv_label_set_text(lbl_btn, "STOP RECORD");
        lv_obj_set_style_bg_color(btn_rec, C_DEAD, 0);
        lv_obj_set_style_text_color(lbl_btn, C_TEXT, 0);
    } else {
        lv_label_set_text(lbl_btn, "START RECORD");
        lv_obj_set_style_bg_color(btn_rec, C_OK, 0);
        lv_obj_set_style_text_color(lbl_btn, lv_color_hex(0x000000), 0);
    }
}

void update_wifi_screen() {
    // Mode label
    String modeStr = status.wifi_mode;
    modeStr.toUpperCase();
    String modeLabel = "MODE: " + modeStr;
    lv_label_set_text(lbl_wifi_mode, modeLabel.c_str());
    lv_color_t mc = C_DIM;
    if (status.wifi_mode == "client") mc = C_OK;
    else if (status.wifi_mode == "ap") mc = C_WARN;
    lv_obj_set_style_text_color(lbl_wifi_mode, mc, 0);

    // IP label
    lv_label_set_text(lbl_wifi_ip, status.wifi_ip.isEmpty() ? "" : status.wifi_ip.c_str());

    // QR code: encode "http://<ip>:3000"
    String qr_url;
    if (!status.wifi_ip.isEmpty()) {
        qr_url = "http://" + status.wifi_ip + ":3000";
    } else if (status.wifi_mode == "ap") {
        qr_url = "http://10.42.0.1:3000";
    }

    // Remove old QR + hint
    if (qr_code) { lv_obj_del(qr_code); qr_code = nullptr; }
    lv_label_set_text(lbl_qr_hint, "");

    if (!qr_url.isEmpty()) {
        // QR code: 160x160, centered
        qr_code = lv_qrcode_create(scr_wifi, 160, lv_color_hex(0x000000), lv_color_hex(0xffffff));
        lv_qrcode_update(qr_code, qr_url.c_str(), qr_url.length());
        lv_obj_align(qr_code, LV_ALIGN_CENTER, 0, 14);
    } else {
        lv_label_set_text(lbl_qr_hint, "No IP assigned");
    }
}

// ── setup / loop ──────────────────────────────────────────────────────────────

void setup() {
    HmiSerial.begin(BAUD_RATE, SERIAL_8N1, 22, 27);
    delay(100);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    pinMode(TOUCH_IRQ, INPUT);

    // Init LVGL
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, lvgl_buf, nullptr, DISP_W * 30);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res    = DISP_W;
    disp_drv.ver_res    = DISP_H;
    disp_drv.flush_cb   = lvgl_flush;
    disp_drv.draw_buf   = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type     = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb  = lvgl_touch_read;
    lv_indev_drv_register(&indev_drv);

    init_styles();
    build_main_screen();
    build_wifi_screen();
    lv_scr_load(scr_main);
}

static unsigned long last_ui_update = 0;
static String         serial_buf;

void loop() {
    // LVGL tick
    lv_timer_handler();
    delay(5);

    // Serial input
    while (HmiSerial.available()) {
        char c = (char)HmiSerial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            serial_buf.trim();
            if (serial_buf.length() > 0 && serial_buf.charAt(0) == '{')
                handle_json(serial_buf);
            serial_buf = "";
        } else {
            serial_buf += c;
        }
    }

    // Touch handling (non-LVGL: long-press shutdown, screen switch)
    handle_touch_logic();

    // Update screen ~2 Hz
    if (millis() - last_ui_update > 500) {
        if (!on_wifi) update_main_screen();
        last_ui_update = millis();
    }
}
