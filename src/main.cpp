/**
 * ARM-BLE v2.6 — 单字节解析器 + 连接后停扫描
 *
 * v2.5: parseLookbon 仍用旧的 ASCII 解析 → raw=乱码, BTN=0x0
 * v2.6: parseLookbon 改为单字节 (高 nibble=事件, 低 nibble=按键)
 *       + raw 用 hex 打印
 */

#include <bluefruit.h>
#include <Servo.h>
#include "handle_parser.h"

Servo myservo;
static const int kServoPin = A2;

static HandleType           g_handleType = HandleType::Lookbon_VR;
static volatile bool        g_newData    = false;
static ParsedInput          g_input;

static uint16_t          g_connHandle    = BLE_CONN_HANDLE_INVALID;
static uint16_t          g_notifyHandle  = 0;
static volatile bool     g_needRescan    = false;
static bool              g_scanPaused    = false;

static unsigned long g_scanCount     = 0;
static unsigned long g_lastLog       = 0;
static unsigned long g_lastRestart   = 0;
static bool          g_restarting    = false;

// CCCD 阶段机
static uint8_t       g_cccdPhase     = 0;
static unsigned long g_cccdNextTime  = 0;
static const uint16_t kCCCDHandles[] = {7, 67, 131};

// ── BLE 事件回调 ──
static void onBLEEvent(ble_evt_t* evt)
{
    if (evt->header.evt_id == 0x10) {
        Serial.print("[EVT] CONNECTED conn="); Serial.println(evt->evt.common_evt.conn_handle);
    } else if (evt->header.evt_id == 0x11) {
        uint8_t r = evt->evt.gap_evt.params.disconnected.reason;
        Serial.print("[EVT] DISCONNECTED reason=0x"); Serial.println(r, HEX);
    }

    if (evt->header.evt_id != BLE_GATTC_EVT_HVX) return;
    ble_gattc_evt_hvx_t* hvx = &evt->evt.gattc_evt.params.hvx;
    if (hvx->type != BLE_GATT_HVX_NOTIFICATION) return;

    // v2.6: hex 打印 + 单字节解析
    g_input   = parseLookbon(hvx->data, hvx->len);
    g_newData = true;

    Serial.print("[DATA] raw=0x");
    for (uint16_t i = 0; i < hvx->len; i++) {
        if (hvx->data[i] < 0x10) Serial.print('0');
        Serial.print(hvx->data[i], HEX);
    }
    Serial.print(" → BTN=0x"); Serial.print(g_input.buttons, HEX);
    Serial.print(" X=");       Serial.print(g_input.joystickX);
    Serial.print(" Y=");       Serial.println(g_input.joystickY);
}

// ── CCCD ──
static void writeOneCCCD(uint16_t conn, uint16_t charHandle)
{
    uint16_t cccdHandle = charHandle + 1;
    uint16_t cccdVal    = 0x0001;
    ble_gattc_write_params_t param;
    memset(&param, 0, sizeof(param));
    param.write_op = BLE_GATT_OP_WRITE_CMD;
    param.flags    = BLE_GATT_EXEC_WRITE_FLAG_PREPARED_WRITE;
    param.handle   = cccdHandle;
    param.offset   = 0;
    param.len      = 2;
    param.p_value  = (uint8_t*)&cccdVal;
    sd_ble_gattc_write(conn, &param);
}

// ── 扫描 ──
static void onScan(ble_gap_evt_adv_report_t* report)
{
    g_scanCount++;
    uint8_t buf[32] = {0};
    String name = "(no name)";
    if (Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, buf, sizeof(buf)))
        name = String((char*)buf);
    else if (Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME, buf, sizeof(buf)))
        name = String((char*)buf);

    if (name.indexOf("LOOKBON") >= 0 || name.indexOf("Lookbon") >= 0) {
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 report->peer_addr.addr[5], report->peer_addr.addr[4], report->peer_addr.addr[3],
                 report->peer_addr.addr[2], report->peer_addr.addr[1], report->peer_addr.addr[0]);
        Serial.print("[FOUND] LOOKBON "); Serial.println(mac);
        Bluefruit.Scanner.stop();
        g_scanPaused = true;
        Bluefruit.Central.connect(report);
    } else {
        g_needRescan = true;
    }
}

// ── 连接 ──
static void onConnect(uint16_t conn)
{
    Serial.print("[CONNECT] conn="); Serial.println(conn);
    g_connHandle  = conn;
    g_scanPaused  = true;
    g_cccdPhase   = 1;
    g_cccdNextTime = millis() + 2000;
}

static void onDisconnect(uint16_t conn, uint8_t reason)
{
    (void)conn;
    Serial.print("[DISCONN] reason=0x"); Serial.println(reason, HEX);
    g_connHandle   = BLE_CONN_HANDLE_INVALID;
    g_cccdPhase    = 0;
    g_scanPaused   = false;
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("\n=== ARM-BLE v2.6 (single-byte parser) ===");

    Bluefruit.begin(1, 1);
    Bluefruit.setName("ARM_BLE");
    Bluefruit.setEventCallback(onBLEEvent);
    Bluefruit.Central.setConnectCallback(onConnect);
    Bluefruit.Central.setDisconnectCallback(onDisconnect);
    Bluefruit.Scanner.setRxCallback(onScan);
    Bluefruit.Scanner.restartOnDisconnect(true);
    Bluefruit.Scanner.setInterval(32, 16);
    Bluefruit.Scanner.useActiveScan(false);
    Bluefruit.Scanner.start(0);

    myservo.attach(kServoPin);
    myservo.write(90);
    g_lastLog     = millis();
    g_lastRestart = millis();
    Serial.println("[OK] v2.6");
}

void loop()
{
    unsigned long now = millis();

    if (g_cccdPhase >= 1 && g_cccdPhase <= 3 && (now - g_cccdNextTime > 2000)) {
        g_cccdNextTime = now;
        writeOneCCCD(g_connHandle, kCCCDHandles[g_cccdPhase - 1]);
        g_notifyHandle = kCCCDHandles[g_cccdPhase - 1];
        g_cccdPhase++;
        if (g_cccdPhase > 3) Serial.println("[OK] subscribed");
    }

    if (!g_scanPaused) {
        if (g_needRescan) {
            g_needRescan = false; g_lastRestart = now;
            Bluefruit.Scanner.stop(); delay(50);
            g_scanCount = 0; Bluefruit.Scanner.start(0);
        }
        if (now - g_lastRestart > 10000 && !g_restarting) {
            g_restarting = true; g_lastRestart = now;
            Bluefruit.Scanner.stop(); delay(50);
            g_scanCount = 0; Bluefruit.Scanner.start(0);
            g_restarting = false;
        }
    }

    if (now - g_lastLog > 5000) {
        g_lastLog = now;
        Serial.print("[.] conn=");
        Serial.println(g_connHandle != BLE_CONN_HANDLE_INVALID ? "YES" : "NO");
    }

    if (g_newData) {
        g_newData = false;
        int angle = map(g_input.joystickX, 0, 255, 0, 180);
        myservo.write(angle);
    }
}