#include <WiFi.h>
#include <esp_wifi.h>

#include <algorithm>
#include <vector>

#include "app_config.h"
#include "hal.h"

// =====================================================================
//  WiFi (โหมด STA): สแกน / เชื่อมต่อ / จำค่า / ลืมค่า  -- ไม่บล็อก loop()
//
//  - ชื่อและรหัส WiFi เก็บในที่เก็บค่าของบอร์ด (prefs(), BOARD_NVS_NAMESPACE)
//    ทุกโปรเจกต์ที่สร้างจาก template นี้จึงใช้ WiFi ชุดเดียวกัน ตั้งครั้งเดียวพอ
//  - บันทึกค่า "หลังเชื่อมต่อสำเร็จ" เท่านั้น พิมพ์รหัสผิดค่าเดิมจะไม่หาย
//  - ปิด WiFi.persistent() ไม่ให้ไลบรารีเก็บค่าซ้ำอีกชุด (ควบคุมการล้างค่าได้เองทั้งหมด)
//  - สแกนแบบ async: wifiScanStart() แล้ว wifiPoll() คอยดูผล (เรียกจาก loop())
//  - หลุด/เชื่อมไม่ติด: หมดเวลา WIFI_CONNECT_TIMEOUT_MS แล้วลองใหม่ทุก WIFI_RETRY_MS
// =====================================================================

namespace hal {

namespace {

WifiState gState = WifiState::NoConfig;
String gSavedSsid, gSavedPass;    // ค่าที่บันทึกไว้
String gTargetSsid, gTargetPass;  // ค่าที่กำลังลองเชื่อมต่อ
bool gPendingSave = false;        // เชื่อมต่อสำเร็จแล้วให้บันทึก gTarget*
uint32_t gConnectStart = 0;
uint32_t gLastFail = 0;

// ---- สแกน ----
// ไม่ใช้ WiFi.scanComplete(): ใน core 2.0.17 มันตรวจ timeout (max_ms_per_chan x 20 = 6 วินาที) "ก่อน" ตรวจว่าสแกนเสร็จ
// สแกนที่นานกว่า 6 วินาที (มักเกิดตอนเชื่อมต่ออยู่) จึงได้ FAILED ทั้งที่มีผลแล้ว -> รับผลจาก event SCAN_DONE เองแทน
constexpr uint32_t SCAN_TIMEOUT_MS = 15000;
constexpr uint8_t SCAN_START_TRIES = 4;
constexpr uint32_t SCAN_RETRY_GAP_MS = 400;

bool gScanning = false;       // กำลังสแกน (รอ event)
bool gScanPending = false;    // รอเริ่มสแกน (เริ่มไม่ได้ทันที เช่น ชิปยังยุ่งกับการเชื่อมต่อ)
uint8_t gScanTries = 0;
uint32_t gScanAt = 0;         // เวลาเริ่มสแกน / เวลาที่จะลองเริ่มอีกครั้ง
bool gScanFailed = false;
uint32_t gScanGen = 0;  // เพิ่มทุกครั้งที่ผลสแกนเปลี่ยน (UI ใช้ตรวจว่าต้องวาดรายการใหม่)
std::vector<WifiNet> gNets;
volatile bool gScanEvent = false;    // ตั้งจาก event task ของ WiFi
volatile uint8_t gScanEvtStatus = 0; // 0 = สำเร็จ
volatile uint8_t gScanEvtCount = 0;

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info)
{
    if (event == ARDUINO_EVENT_WIFI_SCAN_DONE) {  // core เก็บผลไว้ให้แล้วก่อนเรียก callback นี้
        gScanEvtStatus = static_cast<uint8_t>(info.wifi_scan_done.status);
        gScanEvtCount = info.wifi_scan_done.number;
        gScanEvent = true;
    }
}

void stopScan()
{
    if (gScanning) esp_wifi_scan_stop();
    gScanning = false;
    gScanPending = false;
    gScanEvent = false;
}

void startConnect(const String &ssid, const String &pass, uint32_t now)
{
    stopScan();  // ชิปสแกนกับเชื่อมต่อพร้อมกันไม่ได้
    gTargetSsid = ssid;
    gTargetPass = pass;
    WiFi.disconnect();
    WiFi.begin(gTargetSsid.c_str(), gTargetPass.isEmpty() ? nullptr : gTargetPass.c_str());
    gState = WifiState::Connecting;
    gConnectStart = now;
    Serial.printf("[WIFI] connecting to '%s'\n", gTargetSsid.c_str());
}

void collectScan(int n)
{
    gNets.clear();
    for (int i = 0; i < n; i++) {
        if (WiFi.BSSID(i) == nullptr) break;  // เกินจำนวนผลที่ core เก็บไว้
        WifiNet net;
        net.ssid = WiFi.SSID(i);
        if (net.ssid.isEmpty()) continue;  // เครือข่ายซ่อนชื่อ
        net.rssi = WiFi.RSSI(i);
        net.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        // ชื่อซ้ำ (หลายจุดกระจายสัญญาณ) เก็บเฉพาะตัวที่แรงที่สุด
        auto same = std::find_if(gNets.begin(), gNets.end(), [&](const WifiNet &x) { return x.ssid == net.ssid; });
        if (same == gNets.end()) gNets.push_back(net);
        else if (net.rssi > same->rssi) *same = net;
    }
    std::sort(gNets.begin(), gNets.end(), [](const WifiNet &a, const WifiNet &b) { return a.rssi > b.rssi; });
    if (gNets.size() > static_cast<size_t>(app::WIFI_SCAN_MAX)) gNets.resize(app::WIFI_SCAN_MAX);
    WiFi.scanDelete();
}

void scanFail(const char *why)
{
    stopScan();
    gScanFailed = true;
    gScanGen++;
    Serial.printf("[WIFI] scan failed: %s\n", why);
}

// พยายามเริ่มสแกน 1 ครั้ง: true = เริ่มได้แล้ว
bool tryStartScan(uint32_t now)
{
    // ยังไม่ได้เชื่อมต่อ แต่ core อาจกำลังเชื่อมต่อ/ต่อใหม่เองอยู่เบื้องหลัง -> ชิปจะไม่ยอมสแกน ให้หยุดก่อน
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect();
        if (gState == WifiState::Connecting) {
            gPendingSave = false;
            gState = gSavedSsid.isEmpty() ? WifiState::NoConfig : WifiState::Failed;
            gLastFail = now;  // จะลองเชื่อมต่อใหม่หลังสแกนเสร็จตาม WIFI_RETRY_MS
        }
    }
    gScanEvent = false;
    const int r = WiFi.scanNetworks(true);  // async: ผลมาทาง event SCAN_DONE
    if (r == WIFI_SCAN_RUNNING || r >= 0) {
        gScanning = true;
        gScanPending = false;
        gScanAt = now;
        return true;
    }
    return false;
}

}  // namespace

void wifiBegin()
{
    WiFi.persistent(false);
    WiFi.onEvent(onWifiEvent);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    gSavedSsid = prefs().getString("wifi_ssid", "");
    gSavedPass = prefs().getString("wifi_pass", "");
    if (gSavedSsid.isEmpty()) {
        gState = WifiState::NoConfig;
        Serial.println("[WIFI] no saved network");
        return;
    }
    if (app::WIFI_AUTO_CONNECT) startConnect(gSavedSsid, gSavedPass, millis());
    else gState = WifiState::Disconnected;
}

void wifiPoll(uint32_t now)
{
    // ---- ผลสแกน ----
    if (gScanPending && now - gScanAt >= SCAN_RETRY_GAP_MS) {
        if (!tryStartScan(now)) {
            if (++gScanTries >= SCAN_START_TRIES) scanFail("radio busy (could not start)");
            else gScanAt = now;
        }
    }
    if (gScanning) {
        if (gScanEvent) {
            gScanEvent = false;
            gScanning = false;
            if (gScanEvtStatus == 0) {
                collectScan(gScanEvtCount);
                gScanGen++;
                Serial.printf("[WIFI] scan done: %u network(s)\n", static_cast<unsigned>(gNets.size()));
            } else {
                WiFi.scanDelete();
                scanFail("driver reported error");
            }
        } else if (now - gScanAt >= SCAN_TIMEOUT_MS) {
            scanFail("timeout");
        }
    }

    // ---- สถานะการเชื่อมต่อ ----
    const bool up = WiFi.status() == WL_CONNECTED;
    if (up) {
        if (gState != WifiState::Connected) {
            gState = WifiState::Connected;
            Serial.printf("[WIFI] connected '%s' ip=%s rssi=%d dBm\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
            if (gPendingSave) {
                gPendingSave = false;
                gSavedSsid = gTargetSsid;
                gSavedPass = gTargetPass;
                prefs().putString("wifi_ssid", gSavedSsid);
                prefs().putString("wifi_pass", gSavedPass);
                Serial.println("[WIFI] saved");
            }
        }
        return;
    }

    switch (gState) {
        case WifiState::Connected:  // หลุด: ไลบรารีพยายามต่อใหม่เอง ให้นับเวลาเหมือนเริ่มเชื่อมต่อ
            Serial.println("[WIFI] connection lost");
            gState = WifiState::Connecting;
            gConnectStart = now;
            break;
        case WifiState::Connecting:
            if (now - gConnectStart >= app::WIFI_CONNECT_TIMEOUT_MS) {
                Serial.printf("[WIFI] connect '%s' timeout\n", gTargetSsid.c_str());
                WiFi.disconnect();
                gPendingSave = false;  // ค่าใหม่ใช้ไม่ได้ -> ไม่บันทึก ค่าเดิม (ถ้ามี) ยังอยู่
                gState = gSavedSsid.isEmpty() ? WifiState::NoConfig : WifiState::Failed;
                gLastFail = now;
            }
            break;
        case WifiState::Failed:
            if (app::WIFI_RETRY_MS != 0 && !gScanning && !gScanPending &&!gSavedSsid.isEmpty() && now - gLastFail >= app::WIFI_RETRY_MS)
                startConnect(gSavedSsid, gSavedPass, now);
            break;
        default: break;
    }
}

bool wifiScanStart()
{
    if (gScanning || gScanPending) return true;
    const uint32_t now = millis();
    gScanFailed = false;
    gScanTries = 1;
    if (tryStartScan(now)) {
        Serial.println("[WIFI] scan started");
        return true;
    }
    // เริ่มไม่ได้ทันที (ชิปยังหยุดการเชื่อมต่อไม่เสร็จ) -> ลองใหม่ใน wifiPoll() ไม่บล็อก loop
    gScanPending = true;
    gScanAt = now;
    Serial.println("[WIFI] radio busy, retrying scan");
    return true;
}

bool wifiScanning() { return gScanning || gScanPending; }
bool wifiScanFailed() { return gScanFailed; }
uint32_t wifiScanGeneration() { return gScanGen; }
int wifiScanCount() { return static_cast<int>(gNets.size()); }

const WifiNet &wifiScanGet(int i)
{
    return gNets.at(static_cast<size_t>(i));
}

void wifiScanClear()
{
    stopScan();
    WiFi.scanDelete();
    gNets.clear();
    gScanFailed = false;
    gScanGen++;
}

void wifiConnect(const char *ssid, const char *pass, bool save)
{
    gPendingSave = save;
    startConnect(ssid, pass == nullptr ? "" : pass, millis());
}

bool wifiConnectSaved()
{
    if (gSavedSsid.isEmpty()) return false;
    gPendingSave = false;
    startConnect(gSavedSsid, gSavedPass, millis());
    return true;
}

void wifiDisconnect()
{
    WiFi.disconnect();
    gPendingSave = false;
    gState = gSavedSsid.isEmpty() ? WifiState::NoConfig : WifiState::Disconnected;
    Serial.println("[WIFI] disconnected");
}

void wifiForget()
{
    WiFi.disconnect();
    gPendingSave = false;
    prefs().remove("wifi_ssid");
    prefs().remove("wifi_pass");
    gSavedSsid = "";
    gSavedPass = "";
    gTargetSsid = "";
    gState = WifiState::NoConfig;
    Serial.println("[WIFI] saved network forgotten");
}

WifiState wifiState() { return gState; }
bool wifiConnected() { return gState == WifiState::Connected; }
const String &wifiSavedSsid() { return gSavedSsid; }
const String &wifiTargetSsid() { return gTargetSsid; }
String wifiIp() { return WiFi.localIP().toString(); }
int wifiRssi() { return wifiConnected() ? WiFi.RSSI() : 0; }

const char *wifiStateName()
{
    switch (gState) {
        case WifiState::NoConfig: return "NO CONFIG";
        case WifiState::Disconnected: return "DISCONNECTED";
        case WifiState::Connecting: return "CONNECTING";
        case WifiState::Connected: return "CONNECTED";
        case WifiState::Failed: return "FAILED (retry)";
    }
    return "?";
}

}  // namespace hal
