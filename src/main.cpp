/**
 * ARM-BLE v1.0 — 正确参数顺序
 *
 * 根因：Bluefruit.begin(peripheralCount, centralCount)
 *       第一个参数是 Peripheral 数量，第二个才是 Central。
 *       v0.2-v0.8 全部写反了 begin(1,0) = 1 Peripheral + 0 Central。
 *
 * v1.0: begin(0, 1) = 纯主机模式，扫描器才能真正启动。
 */

#include <bluefruit.h>
#include <Servo.h>
#include "handle_parser.h"

Servo myservo;
static const int kServoPin = A2;

static HandleType           g_handleType = HandleType::None;
static volatile bool        g_newData    = false;
static ParsedInput          g_input;

static BLEClientService*        g_svc  = nullptr;
static BLEClientCharacteristic* g_char = nullptr;

static unsigned long g_scanCount   = 0;
static unsigned long g_lastLog     = 0;

// ── Notify 回调 ──
static void onNotify(BLEClientCharacteristic*, uint8_t* data, uint16_t len)
{
    g_input   = parseInput(g_handleType, data, len);
    g_newData = true;
    Serial.print("[DATA] BTN=0x"); Serial.print(g_input.buttons, HEX);
    Serial.print(" X=");           Serial.print(g_input.joystickX);
    Serial.print(" Y=");           Serial.println(g_input.joystickY);
}

// ── 扫描回调 ──
static void onScan(ble_gap_evt_adv_report_t* report)
{
    g_scanCount++;

    uint8_t buf[32] = {0};
    String name;
    if (Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, buf, sizeof(buf)))
        name = String((char*)buf);
    else if (Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME, buf, sizeof(buf)))
        name = String((char*)buf);

    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             report->peer_addr.addr[5], report->peer_addr.addr[4], report->peer_addr.addr[3],
             report->peer_addr.addr[2], report->peer_addr.addr[1], report->peer_addr.addr[0]);

    if (name.equalsIgnoreCase("LOOKBON")) {
        Serial.print(">>> LOOKBON found: "); Serial.println(mac);
        g_handleType = HandleType::Lookbon_VR;
        Bluefruit.Scanner.stop();
        Bluefruit.Central.connect(report);
    }
}

// ── 连接成功 ──
static void onConnect(uint16_t conn)
{
    Serial.println("[CONNECT] OK");
    g_svc  = new BLEClientService(BLEUuid("0000AE30-0000-1000-8000-00805F9B34FB"));
    g_char = new BLEClientCharacteristic(BLEUuid("0000AE02-0000-1000-8000-00805F9B34FB"));

    if (!g_svc->discover(conn))  { Serial.println("[ERR] service"); Bluefruit.disconnect(conn); return; }
    if (!g_char->discover())     { Serial.println("[ERR] char");    Bluefruit.disconnect(conn); return; }
    g_char->setNotifyCallback(onNotify);
    if (g_char->enableNotify())   Serial.println("[NOTIFY] OK");
    else                         { Serial.println("[ERR] notify"); Bluefruit.disconnect(conn); }
}

// ── 断连 ──
static void onDisconnect(uint16_t conn, uint8_t reason)
{
    (void)conn;
    Serial.print("[DISCONN] reason=0x"); Serial.println(reason, HEX);
    g_handleType = HandleType::None;
    delete g_svc;  g_svc  = nullptr;
    delete g_char; g_char = nullptr;
    Bluefruit.Scanner.start(0);
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("\n=== ARM-BLE v1.0 (correct begin params) ===");

    // begin(peripheralCount, centralCount) — 第一个是外设数!
    Bluefruit.begin(/*peripheral=*/0, /*central=*/1);
    Bluefruit.setName("ARM-BLE");
    Bluefruit.Central.setConnectCallback(onConnect);
    Bluefruit.Central.setDisconnectCallback(onDisconnect);
    Bluefruit.Scanner.setRxCallback(onScan);
    Bluefruit.Scanner.restartOnDisconnect(true);
    Bluefruit.Scanner.setInterval(160, 80);   // 标准窗口
    Bluefruit.Scanner.useActiveScan(true);
    Bluefruit.Scanner.start(10000);  // 扫 10 秒 → 触发 stopCallback → 自动重启

    // 扫描结束后自动重启
    Bluefruit.Scanner.setStopCallback([]() {
        Serial.println("[SCAN] restart");
        g_scanCount = 0;
        Bluefruit.Scanner.start(10000);
    });

    myservo.attach(kServoPin);
    myservo.write(90);

    g_lastLog = millis();
    Serial.println("[READY] pure Central, scanning...");
}

void loop()
{
    // 每秒报一次心跳
    if (millis() - g_lastLog > 1000) {
        g_lastLog = millis();
        Serial.print("[SCAN] count=");
        Serial.println(g_scanCount);
    }

    if (g_newData) {
        g_newData = false;
        int angle = map(g_input.joystickX, 0, 255, 0, 180);
        myservo.write(angle);
    }
}