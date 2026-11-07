#include "hal.h"

#include <esp_system.h>

#include "app_config.h"
#include "board_config.h"

namespace hal {

namespace {
Preferences gPrefs;     // ค่าของบอร์ด
Preferences gAppPrefs;  // ค่าของโปรเจกต์
esp_reset_reason_t gReset = ESP_RST_UNKNOWN;

// ย้ายค่าบอร์ดจากชื่อเดิม (labstart) มาที่ที่เก็บกลาง ครั้งแรกครั้งเดียว: ไม่ต้อง calibrate ใหม่
// คัดลอกเฉพาะ key ที่ที่เก็บกลางยังไม่มี และไม่ลบของเดิม (ปลอดภัย ทำซ้ำได้)
void migrateLegacy()
{
    if (gPrefs.isKey("cal") && gPrefs.isKey("disp")) return;  // มีค่าแล้ว ไม่ต้องย้าย
    Preferences old;
    if (!old.begin(app::LEGACY_NVS_NAMESPACE, true)) return;  // ไม่มีค่าเดิม (บอร์ดใหม่)
    int moved = 0;
    if (!gPrefs.isKey("cal") && old.isKey("cal")) {
        uint8_t blob[128];
        const size_t n = old.getBytesLength("cal");
        if (n > 0 && n <= sizeof(blob) && old.getBytes("cal", blob, n) == n) {
            gPrefs.putBytes("cal", blob, n);
            moved++;
        }
    }
    if (!gPrefs.isKey("disp") && old.isKey("disp")) { gPrefs.putUShort("disp", old.getUShort("disp", 0)); moved++; }
    if (!gPrefs.isKey("bl") && old.isKey("bl")) { gPrefs.putUChar("bl", old.getUChar("bl", app::BL_DEFAULT_PERCENT)); moved++; }
    if (!gPrefs.isKey("vol") && old.isKey("vol")) { gPrefs.putUChar("vol", old.getUChar("vol", app::VOLUME_DEFAULT)); moved++; }
    old.end();
    if (moved > 0) Serial.printf("[NVS] migrated %d board setting(s) from '%s' to '%s'\n", moved, app::LEGACY_NVS_NAMESPACE, app::BOARD_NVS_NAMESPACE);
}
}  // namespace

Preferences &prefs()
{
    return gPrefs;
}

Preferences &appPrefs()
{
    return gAppPrefs;
}

void begin()
{
    gReset = esp_reset_reason();
    gPrefs.begin(app::BOARD_NVS_NAMESPACE, false);
    migrateLegacy();
    gAppPrefs.begin(app::NVS_NAMESPACE, false);
    pinMode(cyd::BOOT_BUTTON_PIN, INPUT_PULLUP);
}

const char *resetReasonText()
{
    switch (gReset) {
        case ESP_RST_POWERON: return "POWER ON";
        case ESP_RST_EXT: return "EXT PIN";
        case ESP_RST_SW: return "SOFTWARE";
        case ESP_RST_PANIC: return "PANIC (crash)";
        case ESP_RST_INT_WDT: return "INT WATCHDOG";
        case ESP_RST_TASK_WDT: return "TASK WATCHDOG";
        case ESP_RST_WDT: return "WATCHDOG";
        case ESP_RST_BROWNOUT: return "BROWNOUT (power dip)";
        default: return "UNKNOWN";
    }
}

int ldrRaw()
{
    long sum = 0;
    for (int i = 0; i < 8; i++) sum += analogRead(cyd::LDR_PIN);
    return static_cast<int>(sum / 8);
}

}  // namespace hal
