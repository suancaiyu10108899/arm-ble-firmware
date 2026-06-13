/**
 * ARM-BLE v2.0 — 原始事件回调 + 快速扫描 + 直写 CCCD
 *
 * v1.9: GAP ✅, CCCD write ✅, 但 Notify 回调不触发（fake chr 未关联 SoftDevice）
 * v2.0: Bluefruit.setEventCallback() 捕获 BLE_GATTC_EVT_HVX 原始事件
 *       + setInterval(32,16) 快速扫描 + 精简日志
 */

#include <bluefruit.h>
#include <Servo.h>
#include "handle_parser.h"

Servo myservo;
static const int kServoPin = A2;

static HandleType           g_handleType = HandleType::None;
static volatile bool        g_newData    = false;
static ParsedInput          g_input;

static uint16_t          g_connHandle    = BLE_CONN_HANDLE_INVALID;
static uint16_t          g_notifyHandle  = 0;  // 成功订阅的 handle

static unsigned long g_scanCount     = 0;
static unsigned long g_lastLog       = 0;
static unsigned long g_lastRestart   = 0;
static bool          g_restarting    = false;

// ── 原始 SoftDevice 事件回调 (接收所有 BLE 事件) ──
static void onBLEEvent(ble_evt_t* evt)
{
    // 只看 HVX (Handle Value Notification/Indication) 事件
    if (evt->header.evt_id != BLE_GATTC_EVT_HVX) return;

    ble_gattc_evt_hvx_t* hvx = &evt->evt.gattc_evt.params.hvx;
    if (hvx->type != BLE_GATT_HVX_NOTIFICATION) return;

    // 只处理我们订阅的 handle
    if (hvx->handle != g_notifyHandle && g_notifyHandle != 0) return;

    // 收到手柄数据！
    g_input   = parseInput(g_handleType, hvx->data, hvx->len);
    g_newData = true;

    Serial.print("[DATA] handle="); Serial.print(hvx->handle);
    Serial.print(" len=");         Serial.print(hvx->len);
    Serial.print(" raw=");
    for (uint16_t i = 0; i < hvx->len; i++) {
        if (hvx->data[i] < 0x10) Serial.print('0');
        Serial.print(hvx->data[i], HEX);
    }
    Serial.print(" → BTN=0x"); Serial.print(g_input.buttons, HEX);
    Serial.print(" X=");       Serial.print(g_input.joystickX);
    Serial.print(" Y=");       Serial.println(g_input.joystickY);
}

// ── 直写 CCCD，跳过 discover ──
static bool writeCCCD(uint16_t conn, uint16_t charHandle)
{
    uint16_t cccdHandle = charHandle + 1;
    uint16_t cccdVal    = 0x0001;  // enable notification

    ble_gattc_write_params_t param;
    memset(&param, 0, sizeof(param));
    param.write_op = BLE_GATT_OP_WRITE_CMD;
    param.flags    = BLE_GATT_EXEC_WRITE_FLAG_PREPARED_WRITE;
    param.handle   = cccdHandle;
    param.offset   = 0;
    param.len      = 2;
    param.p_value  = (uint8_t*)&cccdVal;

    uint32_t err = sd_ble_gattc_write(conn, &param);
    if (err != NRF_SUCCESS) {
        Serial.print("[CCCD-ERR] sd_ble_gattc_write err=0x");
        Serial.println(err, HEX);
        return false;
    }
    Serial.print("[CCCD] wrote 0x0001 to handle ");
    Serial.print(cccdHandle);
    Serial.print(" (char=");
    Serial.print(charHandle);
    Serial.println(")");
    return true;
}

// ── 扫描回调 (精简日志) ──
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
        Serial.print(">>> LOOKBON "); Serial.println(mac);
        g_handleType = HandleType::Lookbon_VR;
        Bluefruit.Scanner.stop();
        Bluefruit.Central.connect(report);
    }
}

// ── 连接成功 ──
static void onConnect(uint16_t conn)
{
    Serial.print("[CONNECT] conn="); Serial.println(conn);
    g_connHandle = conn;

    // v2.0: 等 2 秒，写入 CCCD，注册事件回调
    delay(2000);

    static const uint16_t kHandles[] = {7, 67, 131};
    for (uint8_t i = 0; i < 3; i++) {
        if (writeCCCD(conn, kHandles[i])) {
            g_notifyHandle = kHandles[i];
            Serial.println("[READY] waiting for data... press buttons now!");
            return;
        }
    }
    Serial.println("[FAIL] all 3 CCCD handles failed, disconnecting");
    Bluefruit.disconnect(conn);
}

// ── 断连 ──
static void onDisconnect(uint16_t conn, uint8_t reason)
{
    (void)conn;
    Serial.print("[DISCONN] reason=0x"); Serial.println(reason, HEX);
    g_handleType   = HandleType::None;
    g_connHandle   = BLE_CONN_HANDLE_INVALID;
    g_notifyHandle = 0;
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("\n=== ARM-BLE v2.0 (raw event callback) ===");

    Bluefruit.begin(/*peripheral=*/1, /*central=*/1);
    Bluefruit.setName("ARM_BLE");

    // v2.0: 注册原始 SoftDevice 事件回调
    Bluefruit.setEventCallback(onBLEEvent);

    Bluefruit.Central.setConnectCallback(onConnect);
    Bluefruit.Central.setDisconnectCallback(onDisconnect);
    Bluefruit.Scanner.setRxCallback(onScan);
    Bluefruit.Scanner.restartOnDisconnect(true);
    Bluefruit.Scanner.setInterval(32, 16);   // v2.0: 最快扫描窗口
    Bluefruit.Scanner.useActiveScan(false);
    Bluefruit.Scanner.start(0);

    myservo.attach(kServoPin);
    myservo.write(90);

    g_lastLog     = millis();
    g_lastRestart = millis();
    Serial.println("[READY] fast scan, auto-restart every 10s");
}

void loop()
{
    unsigned long now = millis();

    // ── 扫描器 10 秒重启 ──
    if (now - g_lastRestart > 10000 && !g_restarting) {
        g_restarting = true;
        g_lastRestart = now;
        Serial.print("[RESTART] count="); Serial.println(g_scanCount);
        Bluefruit.Scanner.stop();
        delay(500);
        g_scanCount = 0;
        Bluefruit.Scanner.start(0);
        g_restarting = false;
    }

    // 每 5 秒心跳
    if (now - g_lastLog > 5000) {
        g_lastLog = now;
        Serial.print("[OK] scan="); Serial.print(g_scanCount);
        Serial.print(" conn=");     Serial.println(g_connHandle != BLE_CONN_HANDLE_INVALID ? "YES" : "NO");
    }

    if (g_newData) {
        g_newData = false;
        int angle = map(g_input.joystickX, 0, 255, 0, 180);
        myservo.write(angle);
    }
}