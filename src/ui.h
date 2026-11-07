#pragma once

// =====================================================================
//  ui.h -- ส่วนกลางของ UI (LVGL 8.3)
//    ui_main.cpp       สร้าง tabview (ตาราง tabs[]) + ฟังก์ชันช่วยสร้าง widget
//    ui_calibrate.cpp  หน้า calibrate จอสัมผัส (5 จุด + จุดตรวจสอบ)
//    ui_wifi.cpp       แท็บ "WiFi"  สแกน / เชื่อมต่อ (ใส่รหัสผ่านด้วย keyboard) / ล้างรายการ / ลืม WiFi
//    ui_system.cpp     แท็บ "ระบบ"  ข้อมูลบอร์ด / สถานะ / ปุ่ม calibrate / คำสั่ง Serial
//
//  Template: เพิ่มแท็บใหม่ = สร้าง ui_xxx.cpp มีฟังก์ชัน ui_xxx_tab() ประกาศไว้ด้านล่าง
//  แล้วเพิ่มลงตาราง tabs[] ใน ui_main.cpp  (ตัวอย่างแท็บทดสอบเดิม: examples/removed_tabs/)
// =====================================================================

#include <lvgl.h>

LV_FONT_DECLARE(font_th_16);
LV_FONT_DECLARE(font_th_24);

constexpr int UI_W = 320;
constexpr int UI_H = 240;
constexpr int UI_TAB_H = 34;  // ความสูงแถบแท็บด้านบน

// ---- สร้างทั้ง UI ----
void ui_init();

// ---- แต่ละแท็บ (parent = หน้าของแท็บ, เลื่อนขึ้นลงได้) ----
void ui_wifi_tab(lv_obj_t *tab);
void ui_system_tab(lv_obj_t *tab);
// void ui_home_tab(lv_obj_t *tab);   <- ประกาศแท็บของโปรเจกต์ใหม่ตรงนี้

// ---- หน้าเต็มจอ (วางบน lv_layer_top ทับทุกอย่าง) ----
void ui_calibrate_start(bool firstTime);
bool ui_calibrate_active();

// เรียกเมื่อค่า calibrate เปลี่ยน (แท็บระบบอัปเดตตัวเลข)
void ui_touch_refresh();

// ---- ฟังก์ชันช่วย ----
lv_obj_t *ui_card(lv_obj_t *parent, const char *title);  // การ์ดเต็มความกว้าง เรียงลูกแนวตั้ง (flex column)
lv_obj_t *ui_row(lv_obj_t *parent);                       // แถวโปร่งใส เรียงลูกแนวนอน ขึ้นบรรทัดใหม่เมื่อเต็ม
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font = nullptr, lv_color_t color = lv_color_white());
lv_obj_t *ui_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user = nullptr, lv_event_code_t code = LV_EVENT_CLICKED);
lv_obj_t *ui_switch_row(lv_obj_t *parent, const char *text, bool on, lv_event_cb_t cb);  // ข้อความ + สวิตช์ (คืนสวิตช์)
lv_obj_t *ui_overlay();                                   // พื้นเต็มจอบน layer_top (แตะได้ ไม่เลื่อน)
lv_obj_t *ui_crosshair(lv_obj_t *parent, lv_color_t color);  // เป้ากากบาท 41x41 (วางด้วย ui_crosshair_move)
void ui_crosshair_move(lv_obj_t *cross, int x, int y);    // ให้ "กลางเป้า" อยู่ที่ (x, y)
void ui_no_scroll(lv_obj_t *obj);                         // ลากบน widget นี้ไม่ให้หน้าเลื่อนตาม
