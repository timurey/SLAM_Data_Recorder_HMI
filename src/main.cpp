// CYD HMI for Pulse — LVGL-based dashboard with WiFi/QR screen
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <Cyd2UsbTouch.h>

// P5 (GPIO1/GPIO3) unusable: CH340C holds GPIO3 HIGH even without USB-C.
// Use CN1 connector: IO22=RX, IO27=TX → UART2.
HardwareSerial HmiSerial(2);
TFT_eSPI       tft = TFT_eSPI();

#define BAUD_RATE  115200
#define DISP_W     240
#define DISP_H     320

// Touch driver (XPT2046 on a dedicated SPI bus, NVS calibration, long-press)
static Cyd2UsbTouch touch;

// ── Status ────────────────────────────────────────────────────────────────────

struct Status {
    bool   sensors_running = false;
    float  lidar_hz        = 0.0f;
    float  imu_hz          = 0.0f;
    bool   lidar_ok        = false;
    bool   imu_ok          = false;
    bool   recording       = false;
    bool laser_warming = false;
    float  disk_gb         = 0.0f;
    int    rec_duration    = 0;
    String bag_name;
    String wifi_ip;
    String wifi_mode       = "disconnected";
    unsigned long last_rx  = 0;
} status;

// ── Pending button state ──────────────────────────────────────────────────────
// Gives immediate visual feedback on tap before Pi confirms the action.

enum class PendingAction
{
  NONE,
  STARTING,
  STOPPING
};
static PendingAction pending_action = PendingAction::NONE;
static unsigned long pending_since = 0;
// STARTING: 30s enough for rpm=600+laser; warming resets it via laser_warming field
// STOPPING: 15s enough for bag close+rpm=0
static constexpr unsigned long PENDING_STARTING_TIMEOUT = 30000;
static constexpr unsigned long PENDING_STOPPING_TIMEOUT = 15000;

// ── Forward declarations ──────────────────────────────────────────────────────
void send_cmd(const char *cmd);
void update_main_screen();
void update_wifi_screen();

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
static lv_obj_t *rec_dot;   // blinking circle indicator
static lv_obj_t *lbl_rec;   // "REC" text
static lv_obj_t *lbl_timer; // "00:42" right-aligned
static lv_obj_t *lbl_bag;
static lv_obj_t *btn_rec;
static lv_obj_t *lbl_btn;
static lv_obj_t *lbl_wifi_icon;
static bool rec_blink = false;

// WiFi screen widgets
static lv_obj_t *lbl_wifi_mode;
static lv_obj_t *lbl_wifi_ip;
static lv_obj_t *lbl_qr_hint;
static lv_obj_t *qr_code       = nullptr;
static lv_obj_t *btn_wifi_toggle;
static lv_obj_t *lbl_btn_wifi;

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

void init_styles() {
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, C_BG);
    lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);
    lv_style_set_border_width(&style_screen, 0);
    lv_style_set_outline_width(&style_screen, 0);
    lv_style_set_shadow_width(&style_screen, 0);
    lv_style_set_pad_all(&style_screen, 0);

    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, C_CARD);
    lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
    lv_style_set_border_color(&style_card, lv_color_hex(0x1e1e1e));
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_outline_width(&style_card, 0);
    lv_style_set_shadow_width(&style_card, 0);
    lv_style_set_radius(&style_card, 4);
    lv_style_set_pad_all(&style_card, 10);

    lv_style_init(&style_label);
    lv_style_set_text_color(&style_label, C_TEXT);
    lv_style_set_outline_width(&style_label, 0);

    lv_style_init(&style_dim);
    lv_style_set_text_color(&style_dim, C_DIM);
    lv_style_set_outline_width(&style_dim, 0);
}

// ── Protocol ──────────────────────────────────────────────────────────────────

void send_cmd(const char *cmd)
{
  JsonDocument doc;
  doc["cmd"] = cmd;
  char buf[128];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  buf[n] = '\0';
  HmiSerial.print(buf);
  HmiSerial.print('\n');
  HmiSerial.flush();
}

void handle_json(const String &line)
{
  // Serial.println("[RX] len=" + String(line.length()) + " " + line.substring(0, 80));
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err)
  {
    Serial.println("[ERR] parse: " + String(err.c_str()));
    return;
  }
  if (!doc["sensors_running"].is<bool>())
  {
    Serial.println("[ERR] no sensors_running in JSON");
    return;
  }
  status.sensors_running = doc["sensors_running"] | false;
  status.lidar_hz = doc["lidar_hz"] | 0.0f;
  status.imu_hz = doc["imu_hz"] | 0.0f;
  status.lidar_ok = doc["lidar_ok"] | false;
  status.imu_ok = doc["imu_ok"] | false;
  status.recording = doc["recording"] | false;
  status.laser_warming = doc["laser_warming"] | false;
  status.disk_gb = doc["disk_gb"] | 0.0f;
  status.rec_duration = doc["rec_duration"] | 0;
  status.bag_name = doc["bag_name"] | "";
  status.wifi_ip = doc["wifi_ip"] | "";
  status.wifi_mode = doc["wifi_mode"] | "disconnected";
  status.last_rx = millis();

  // Serial.println("[OK] sr=" + String(status.sensors_running) +
  //                " lidar=" + String(status.lidar_hz, 1) +
  //                " imu=" + String(status.imu_hz, 1) +
  //                " rec=" + String(status.recording));

  // Resolve pending state when Pi confirms the action
  if (pending_action == PendingAction::STARTING &&
      (status.recording || status.laser_warming))
  {
    pending_action = PendingAction::NONE;
  }
  if (pending_action == PendingAction::STOPPING && !status.recording)
  {
    pending_action = PendingAction::NONE;
  }
}

// ── Main screen ───────────────────────────────────────────────────────────────

void build_main_screen()
{
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
  lv_obj_set_style_outline_width(lbl_title, 0, 0);
  lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 12, 0);

  // WiFi icon (top-right of header) — tap navigates to WiFi screen
  lbl_wifi_icon = lv_label_create(hdr);
  lv_label_set_text(lbl_wifi_icon, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(lbl_wifi_icon, C_DIM, 0);
  lv_obj_align(lbl_wifi_icon, LV_ALIGN_RIGHT_MID, -12, 0);
  lv_obj_add_flag(lbl_wifi_icon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(lbl_wifi_icon, [](lv_event_t *e) {
      if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
      update_wifi_screen();
      on_wifi = true;
      lv_scr_load_anim(scr_wifi, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
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
  lv_obj_set_style_text_font(lbl_disk, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_disk, C_DIM, 0);
  lv_obj_align(lbl_disk, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  // Recording area — gap between sensors card (y=144) and button (y=250)
  // Row 1 y=160: [●] REC (left)    00:42 (right, fixed anchor)
  // Row 2 y=194: bag name scrolling

  // Dot: small LVGL circle (no font dependency)
  rec_dot = lv_obj_create(scr_main);
  lv_obj_remove_style_all(rec_dot); // clear default theme styles (shadow, border, outline)
  lv_obj_set_size(rec_dot, 12, 12);
  lv_obj_set_style_radius(rec_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(rec_dot, lv_color_hex(0xff0000), 0);
  lv_obj_set_style_bg_opa(rec_dot, LV_OPA_COVER, 0);
  lv_obj_align(rec_dot, LV_ALIGN_TOP_LEFT, 14, 164);
  lv_obj_add_flag(rec_dot, LV_OBJ_FLAG_HIDDEN);

  // "REC" label — fixed left position, never moves
  lbl_rec = lv_label_create(scr_main);
  lv_label_set_text(lbl_rec, "");
  lv_obj_set_style_text_color(lbl_rec, C_TEXT, 0);
  lv_obj_set_style_text_font(lbl_rec, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_rec, LV_ALIGN_TOP_LEFT, 32, 160);

  // Timer — fixed RIGHT position so digits don't shift the layout
  lbl_timer = lv_label_create(scr_main);
  lv_label_set_text(lbl_timer, "");
  lv_obj_set_style_text_color(lbl_timer, C_TEXT, 0);
  lv_obj_set_style_text_font(lbl_timer, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_timer, LV_ALIGN_TOP_RIGHT, -14, 160);

  // Bag name — scrolling, dim
  lbl_bag = lv_label_create(scr_main);
  lv_label_set_text(lbl_bag, "");
  lv_obj_set_style_text_color(lbl_bag, C_DIM, 0);
  lv_obj_set_style_text_font(lbl_bag, &lv_font_montserrat_12, 0);
  lv_obj_align(lbl_bag, LV_ALIGN_TOP_MID, 0, 194);
  lv_obj_set_width(lbl_bag, DISP_W - 24);
  lv_label_set_long_mode(lbl_bag, LV_LABEL_LONG_SCROLL_CIRCULAR);

  // Record button
  btn_rec = lv_btn_create(scr_main);
  lv_obj_remove_style_all(btn_rec); // strip default LVGL theme so local styles always win
  lv_obj_set_size(btn_rec, DISP_W - 24, 58);
  lv_obj_align(btn_rec, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_set_style_radius(btn_rec, 6, 0);
  lv_obj_set_style_border_width(btn_rec, 0, 0);
  lv_obj_set_style_bg_opa(btn_rec, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(btn_rec, C_OK, 0);

  lbl_btn = lv_label_create(btn_rec);
  lv_label_set_text(lbl_btn, "START RECORD");
  lv_obj_set_style_text_font(lbl_btn, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl_btn, lv_color_hex(0x000000), 0);
  lv_obj_center(lbl_btn);

  lv_obj_add_event_cb(btn_rec, [](lv_event_t *e) {
      if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
      bool connected = (millis() - status.last_rx < 2000);
      bool busy = status.laser_warming || pending_action != PendingAction::NONE;
      if (!connected || !status.sensors_running || busy) return;
      if (status.recording) {
          send_cmd("stop_recording");
          pending_action = PendingAction::STOPPING;
      } else {
          send_cmd("start_recording");
          pending_action = PendingAction::STARTING;
      }
      pending_since = millis();
      update_main_screen();
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

    // Tap header → back to main screen
    lv_obj_add_flag(hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hdr, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        on_wifi = false;
        update_main_screen();
        lv_scr_load_anim(scr_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
    }, LV_EVENT_CLICKED, nullptr);

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

    // QR code placeholder (replaced in update_wifi_screen)
    lbl_qr_hint = lv_label_create(scr_wifi);
    lv_label_set_text(lbl_qr_hint, "No IP assigned");
    lv_obj_set_style_text_color(lbl_qr_hint, C_DIM, 0);
    lv_obj_set_style_text_font(lbl_qr_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_qr_hint, LV_ALIGN_TOP_MID, 0, 110);

    // AP / Client toggle button (bottom, same geometry as main rec button)
    btn_wifi_toggle = lv_btn_create(scr_wifi);
    lv_obj_remove_style_all(btn_wifi_toggle);
    lv_obj_set_size(btn_wifi_toggle, DISP_W - 24, 50);
    lv_obj_align(btn_wifi_toggle, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_radius(btn_wifi_toggle, 6, 0);
    lv_obj_set_style_border_width(btn_wifi_toggle, 0, 0);
    lv_obj_set_style_bg_opa(btn_wifi_toggle, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn_wifi_toggle, C_WARN, 0);

    lbl_btn_wifi = lv_label_create(btn_wifi_toggle);
    lv_label_set_text(lbl_btn_wifi, "ENABLE AP");
    lv_obj_set_style_text_font(lbl_btn_wifi, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_btn_wifi, lv_color_hex(0x000000), 0);
    lv_obj_center(lbl_btn_wifi);

    lv_obj_add_event_cb(btn_wifi_toggle, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        if (status.wifi_mode == "ap")
            send_cmd("wifi_client");
        else
            send_cmd("wifi_ap");
    }, LV_EVENT_CLICKED, nullptr);
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
        lv_obj_add_flag(rec_dot, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lbl_rec,   "");
        lv_label_set_text(lbl_timer, "");
        lv_label_set_text(lbl_bag, "");
        lv_label_set_text(lbl_btn, "WAITING...");
        lv_obj_set_style_bg_color(btn_rec, lv_color_hex(0x2a2a2a), 0);
        lv_obj_set_style_text_color(lbl_btn, C_DIM, 0);
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

    // Rec timer + bag name
    if (status.recording) {
      rec_blink = !rec_blink;
      if (rec_blink)
        lv_obj_clear_flag(rec_dot, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(rec_dot, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(lbl_rec, "REC");
      int m = status.rec_duration / 60, s = status.rec_duration % 60;
      snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
      lv_label_set_text(lbl_timer, buf);
      lv_label_set_text(lbl_bag, status.bag_name.isEmpty() ? "" : status.bag_name.c_str());
    } else {
      lv_obj_add_flag(rec_dot, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(lbl_rec, "");
      lv_label_set_text(lbl_timer, "");
      lv_label_set_text(lbl_bag, "");
    }

    // Expire pending if Pi doesn't respond within timeout
    unsigned long pending_elapsed = millis() - pending_since;
    if (pending_action == PendingAction::STARTING &&
        pending_elapsed > PENDING_STARTING_TIMEOUT)
    {
      pending_action = PendingAction::NONE;
    }
    if (pending_action == PendingAction::STOPPING &&
        pending_elapsed > PENDING_STOPPING_TIMEOUT)
    {
      pending_action = PendingAction::NONE;
    }

    // Button — priority: no sensors > warming/starting > stopping/recording > idle
    if (!status.sensors_running) {
        lv_label_set_text(lbl_btn, "WAITING...");
        lv_obj_set_style_bg_color(btn_rec, lv_color_hex(0x2a2a2a), 0);
        lv_obj_set_style_text_color(lbl_btn, C_DIM, 0);
    }
    else if (status.laser_warming || pending_action == PendingAction::STARTING)
    {
      const char *lbl = status.laser_warming ? "WARMING UP..." : "STARTING...";
      lv_label_set_text(lbl_btn, lbl);
      lv_obj_set_style_bg_color(btn_rec, C_WARN, 0);
      lv_obj_set_style_text_color(lbl_btn, lv_color_hex(0x000000), 0);
    }
    else if (status.recording || pending_action == PendingAction::STOPPING)
    {
      const char *lbl = (pending_action == PendingAction::STOPPING) ? "STOPPING..." : "STOP RECORD";
      lv_label_set_text(lbl_btn, lbl);
      lv_obj_set_style_bg_color(btn_rec, C_DEAD, 0);
      lv_obj_set_style_text_color(lbl_btn, C_TEXT, 0);
    }
    else
    {
      lv_label_set_text(lbl_btn, "START RECORD");
      lv_obj_set_style_bg_color(btn_rec, C_OK, 0);
      lv_obj_set_style_text_color(lbl_btn, lv_color_hex(0x000000), 0);
    }
}

void update_wifi_screen() {
    // Mode label
    String modeStr = status.wifi_mode;
    modeStr.toUpperCase();
    lv_label_set_text(lbl_wifi_mode, ("MODE: " + modeStr).c_str());
    lv_color_t mc = C_DIM;
    if (status.wifi_mode == "client") mc = C_OK;
    else if (status.wifi_mode == "ap") mc = C_WARN;
    lv_obj_set_style_text_color(lbl_wifi_mode, mc, 0);

    // IP label
    lv_label_set_text(lbl_wifi_ip, status.wifi_ip.isEmpty() ? "" : status.wifi_ip.c_str());

    // QR: only recreate when url changes (avoid 200ms alloc/free loop)
    String qr_url;
    if (!status.wifi_ip.isEmpty())
        qr_url = "http://" + status.wifi_ip + ":3000";
    else if (status.wifi_mode == "ap")
        qr_url = "http://10.42.0.1:3000";

    static String last_qr_url;
    if (qr_url != last_qr_url) {
        last_qr_url = qr_url;
        if (qr_code) { lv_obj_del(qr_code); qr_code = nullptr; }
        lv_label_set_text(lbl_qr_hint, "");
        if (!qr_url.isEmpty()) {
            qr_code = lv_qrcode_create(scr_wifi, 150, lv_color_hex(0x000000), lv_color_hex(0xffffff));
            lv_qrcode_update(qr_code, qr_url.c_str(), qr_url.length());
            lv_obj_align(qr_code, LV_ALIGN_TOP_MID, 0, 90);
            lv_obj_clear_flag(qr_code, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_label_set_text(lbl_qr_hint, "No IP assigned");
        }
    }

    // Toggle button
    if (status.wifi_mode == "ap") {
        lv_label_set_text(lbl_btn_wifi, "CONNECT WIFI");
        lv_obj_set_style_bg_color(btn_wifi_toggle, C_OK, 0);
    } else {
        lv_label_set_text(lbl_btn_wifi, "ENABLE AP");
        lv_obj_set_style_bg_color(btn_wifi_toggle, C_WARN, 0);
    }
}

// ── setup / loop ──────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(BAUD_RATE); // diagnostics (UART0 via CH340C)
  HmiSerial.begin(BAUD_RATE, SERIAL_8N1, 22, 27);
  delay(100);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.invertDisplay(false);
  tft.setRotation(0);
  // ST7789 physical pixel order is BGR; setRotation() overwrites MADCTL without
  // the BGR bit when TFT_MAD_COLOR_ORDER resolves to 0x00 at compile time.
  // Write MADCTL manually: 0x08 = BGR only, portrait 240x320.
  tft.writecommand(0x36);
  tft.writedata(0x08);
  tft.fillScreen(TFT_BLACK);

  // Touch: XPT2046 on its own SPI bus; NVS calibration (corner-tap on first
  // boot or when held at power-up). Long press anywhere → send "shutdown".
  touch.setLongPressCb([]() { send_cmd("shutdown"); });
  touch.begin(DISP_W, DISP_H);

  // Init LVGL
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, lvgl_buf, nullptr, DISP_W * 30);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = DISP_W;
  disp_drv.ver_res = DISP_H;
  disp_drv.flush_cb = lvgl_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
  lv_disp_set_theme(disp, NULL); // disable default theme — all styles managed manually

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = [](lv_indev_drv_t *drv, lv_indev_data_t *data) {
      touch.lvglRead(drv, data);
  };
  lv_indev_drv_register(&indev_drv);

  init_styles();
  build_main_screen();
  build_wifi_screen();
  lv_scr_load(scr_main);
}

static unsigned long last_ui_update = 0;
static unsigned long last_heartbeat = 0;
static String serial_buf;

void loop() {
  lv_timer_handler();

  // Serial input — drain the whole FIFO before yielding
  while (HmiSerial.available())
  {
    char c = (char)HmiSerial.read();
    if (c == '\r')
      continue;
    if (c == '\n')
    {
      serial_buf.trim();
      Serial.printf("[RX] len=%u: %s\n", serial_buf.length(), serial_buf.substring(0, 60).c_str());
      if (serial_buf.length() > 0)
      {
        if (serial_buf.charAt(0) == '{')
        {
          handle_json(serial_buf);
        }
        else
        {
          Serial.println("[RAW] " + serial_buf);
        }
      }
      serial_buf = "";
    }
    else
    {
      serial_buf += c;
      if (serial_buf.length() > 512)
      {
        Serial.println("[HMI] overflow: " + serial_buf.substring(0, 40));
        serial_buf = "";
      }
    }
  }

  // Update screen at 200 ms
  if (millis() - last_ui_update > 200)
  {
    if (on_wifi)
      update_wifi_screen();
    else
      update_main_screen();
    last_ui_update = millis();
  }

  if (millis() - last_heartbeat > 5000) {
    Serial.printf("[HB] uptime=%lus rx=%lums on_wifi=%d sensors=%d\n",
        millis()/1000, millis() - status.last_rx, (int)on_wifi, (int)status.sensors_running);
    last_heartbeat = millis();
  }

  delay(5); // feed watchdog + yield to UART FIFO
}
