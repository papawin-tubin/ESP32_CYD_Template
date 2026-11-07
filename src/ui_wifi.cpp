#include <string.h>

#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "WiFi": สแกนหาเครือข่าย -> แตะเลือก -> ใส่รหัสผ่าน -> เชื่อมต่อ
//    [สแกน]        ค้นหาเครือข่ายรอบตัว (เบื้องหลัง ประมาณ 2-4 วินาที)
//    [ล้างรายการ]  ล้างผลสแกนบนจอ
//    [ลืม WiFi]    (กดค้าง) ตัดการเชื่อมต่อ + ลบชื่อ/รหัสที่บันทึกไว้
//  แตะเครือข่ายที่บันทึกไว้แล้ว = เชื่อมต่อใหม่ทันที ไม่ต้องใส่รหัสซ้ำ
//  การเชื่อมต่อทำใน hal_wifi.cpp (ไฟล์นี้เป็นแค่หน้าจอ)
// =====================================================================

namespace {

lv_obj_t *gStatus = nullptr;
lv_obj_t *gScanInfo = nullptr;
lv_obj_t *gList = nullptr;
lv_obj_t *gPwd = nullptr;  // หน้าใส่รหัสผ่าน (overlay)
lv_obj_t *gPwdTitle = nullptr;
lv_obj_t *gPwdTa = nullptr;
char gSel[33];  // SSID ที่เลือก (ยาวสุด 32 ตัวอักษร)
uint32_t gShownGen = UINT32_MAX;
bool gShownScanning = false;

// ---------------------------------------------------------------------
//  สถานะการเชื่อมต่อ
// ---------------------------------------------------------------------
void updateStatus()
{
    static char last[160];
    char buf[160];
    lv_color_t color = lv_color_white();
    const char *target = hal::wifiTargetSsid().c_str();
    switch (hal::wifiState()) {
        case hal::WifiState::NoConfig:
            snprintf(buf, sizeof(buf), "ยังไม่ได้ตั้งค่า WiFi\nกด สแกน แล้วแตะเลือกเครือข่าย");
            color = lv_palette_lighten(LV_PALETTE_GREY, 2);
            break;
        case hal::WifiState::Disconnected:
            snprintf(buf, sizeof(buf), "ตัดการเชื่อมต่อแล้ว\nบันทึกไว้: %s", hal::wifiSavedSsid().c_str());
            color = lv_palette_lighten(LV_PALETTE_GREY, 2);
            break;
        case hal::WifiState::Connecting:
            snprintf(buf, sizeof(buf), "กำลังเชื่อมต่อ %s ...", target);
            color = lv_palette_main(LV_PALETTE_AMBER);
            break;
        case hal::WifiState::Connected:
            snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %s\nIP %s   สัญญาณ %d dBm", target, hal::wifiIp().c_str(), hal::wifiRssi());
            color = lv_palette_main(LV_PALETTE_LIGHT_GREEN);
            break;
        case hal::WifiState::Failed:
            snprintf(buf, sizeof(buf), "เชื่อมต่อ %s ไม่สำเร็จ\nจะลองใหม่อัตโนมัติ", hal::wifiSavedSsid().c_str());
            color = lv_palette_main(LV_PALETTE_RED);
            break;
    }
    if (strcmp(buf, last) == 0) return;  // ข้อความเดิม ไม่ต้องวาดใหม่
    strcpy(last, buf);
    lv_label_set_text(gStatus, buf);
    lv_obj_set_style_text_color(gStatus, color, 0);
}

// ---------------------------------------------------------------------
//  หน้าใส่รหัสผ่าน (เต็มจอ: ชื่อเครือข่าย + ช่องรหัส + keyboard)
// ---------------------------------------------------------------------
void closePassword()
{
    if (gPwd == nullptr) return;
    lv_obj_del_async(gPwd);  // ถูกเรียกจาก event ของ keyboard ที่อยู่ในหน้านี้ จึงลบแบบ async
    gPwd = nullptr;
}

void onKeyboard(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char *pass = lv_textarea_get_text(gPwdTa);
        if (strlen(pass) < 8) {  // WPA/WPA2 ต้องยาว 8-63 ตัวอักษร
            lv_label_set_text(gPwdTitle, "รหัสผ่านต้องยาว 8 ตัวอักษรขึ้นไป");
            lv_obj_set_style_text_color(gPwdTitle, lv_palette_main(LV_PALETTE_RED), 0);
            hal::soundPlay(hal::Melody::Error);
            return;
        }
        hal::wifiConnect(gSel, pass, true);
        closePassword();
        updateStatus();
    } else if (code == LV_EVENT_CANCEL) {
        closePassword();
    }
}

void onShowPassword(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const bool hide = !lv_textarea_get_password_mode(gPwdTa);
    lv_textarea_set_password_mode(gPwdTa, hide);
    lv_label_set_text(lv_obj_get_child(btn, 0), hide ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
}

void showPassword(const char *ssid)
{
    gPwd = ui_overlay();
    lv_obj_set_style_bg_color(gPwd, lv_color_hex(0x101820), 0);

    gPwdTitle = ui_label(gPwd, "", nullptr, lv_palette_main(LV_PALETTE_CYAN));
    lv_label_set_text_fmt(gPwdTitle, "รหัสผ่าน: %s", ssid);
    lv_label_set_long_mode(gPwdTitle, LV_LABEL_LONG_DOT);
    lv_obj_set_width(gPwdTitle, UI_W - 12);
    lv_obj_set_pos(gPwdTitle, 6, 3);

    gPwdTa = lv_textarea_create(gPwd);
    lv_textarea_set_one_line(gPwdTa, true);
    lv_textarea_set_password_mode(gPwdTa, true);
    lv_textarea_set_max_length(gPwdTa, 63);
    lv_textarea_set_placeholder_text(gPwdTa, "ใส่รหัสผ่าน แล้วกด " LV_SYMBOL_OK);
    lv_obj_set_size(gPwdTa, UI_W - 12 - 56, 40);
    lv_obj_set_pos(gPwdTa, 6, 26);
    lv_obj_add_state(gPwdTa, LV_STATE_FOCUSED);  // ให้เห็นเคอร์เซอร์

    lv_obj_t *eye = ui_btn(gPwd, LV_SYMBOL_EYE_OPEN, onShowPassword);
    lv_obj_set_size(eye, 50, 40);
    lv_obj_set_pos(eye, UI_W - 56, 26);

    lv_obj_t *kb = lv_keyboard_create(gPwd);
    lv_obj_set_size(kb, UI_W, UI_H - 70);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, gPwdTa);
    lv_obj_add_event_cb(kb, onKeyboard, LV_EVENT_ALL, nullptr);
}

// ---------------------------------------------------------------------
//  รายการเครือข่าย
// ---------------------------------------------------------------------
void onNetwork(lv_event_t *e)
{
    const int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (i < 0 || i >= hal::wifiScanCount()) return;
    const hal::WifiNet &net = hal::wifiScanGet(i);
    strncpy(gSel, net.ssid.c_str(), sizeof(gSel) - 1);
    gSel[sizeof(gSel) - 1] = '\0';

    if (net.ssid == hal::wifiSavedSsid()) hal::wifiConnectSaved();  // เคยบันทึกไว้แล้ว
    else if (net.open) hal::wifiConnect(gSel, "", true);           // ไม่มีรหัสผ่าน
    else showPassword(gSel);
    updateStatus();
}

void rebuildList()
{
    lv_obj_clean(gList);
    const int n = hal::wifiScanCount();
    if (hal::wifiScanning()) lv_label_set_text(gScanInfo, LV_SYMBOL_REFRESH " กำลังสแกน ...");
    else if (hal::wifiScanFailed()) lv_label_set_text(gScanInfo, "สแกนไม่สำเร็จ ลองกดสแกนอีกครั้ง");
    else if (n == 0) lv_label_set_text(gScanInfo, "กด สแกน เพื่อค้นหาเครือข่าย");
    else lv_label_set_text_fmt(gScanInfo, "พบ %d เครือข่าย แตะเพื่อเชื่อมต่อ", n);

    for (int i = 0; i < n; i++) {
        const hal::WifiNet &net = hal::wifiScanGet(i);
        lv_obj_t *b = lv_btn_create(gList);
        lv_obj_set_size(b, LV_PCT(100), 34);
        lv_obj_set_style_pad_hor(b, 8, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x243447), 0);
        lv_obj_add_event_cb(b, onNetwork, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));

        const bool saved = net.ssid == hal::wifiSavedSsid();
        lv_obj_t *name = ui_label(b, "", nullptr, saved ? lv_palette_main(LV_PALETTE_LIGHT_GREEN) : lv_color_white());
        lv_label_set_text_fmt(name, "%s%s", saved ? LV_SYMBOL_OK " " : "", net.ssid.c_str());
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, 180);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *info = ui_label(b, "", nullptr, lv_palette_lighten(LV_PALETTE_GREY, 2));
        lv_label_set_text_fmt(info, "%d dBm %s", static_cast<int>(net.rssi), net.open ? "เปิด" : "รหัส");
        lv_obj_align(info, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

void onScan(lv_event_t *)
{
    hal::wifiScanStart();
    rebuildList();
}

void onClear(lv_event_t *)
{
    hal::wifiScanClear();
}

void onForget(lv_event_t *)
{
    hal::wifiForget();
    hal::soundPlay(hal::Melody::Success);
    gShownGen = UINT32_MAX;  // วาดรายการใหม่ (ลบเครื่องหมายเครือข่ายที่บันทึกไว้)
}

void tick(lv_timer_t *)
{
    updateStatus();
    const bool scanning = hal::wifiScanning();
    if (hal::wifiScanGeneration() != gShownGen || scanning != gShownScanning) {
        gShownGen = hal::wifiScanGeneration();
        gShownScanning = scanning;
        rebuildList();
    }
}

}  // namespace

void ui_wifi_tab(lv_obj_t *tab)
{
    lv_obj_t *card = ui_card(tab, "สถานะ WiFi");
    gStatus = ui_label(card, "");
    lv_obj_t *forget = ui_btn(card, LV_SYMBOL_TRASH " ลืม WiFi (กดค้าง)", onForget, nullptr, LV_EVENT_LONG_PRESSED);
    lv_obj_set_style_bg_color(forget, lv_palette_main(LV_PALETTE_DEEP_ORANGE), 0);

    card = ui_card(tab, "เครือข่ายที่พบ");
    lv_obj_t *row = ui_row(card);
    ui_btn(row, LV_SYMBOL_REFRESH " สแกน", onScan);
    lv_obj_t *clear = ui_btn(row, LV_SYMBOL_CLOSE " ล้างรายการ", onClear);
    lv_obj_set_style_bg_color(clear, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
    gScanInfo = ui_label(card, "", nullptr, lv_palette_lighten(LV_PALETTE_GREY, 2));

    gList = lv_obj_create(card);
    lv_obj_remove_style_all(gList);
    lv_obj_set_size(gList, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(gList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(gList, 4, 0);
    lv_obj_clear_flag(gList, LV_OBJ_FLAG_SCROLLABLE);

    lv_timer_create(tick, 300, nullptr);
    tick(nullptr);
}
