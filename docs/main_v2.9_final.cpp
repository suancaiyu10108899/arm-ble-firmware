/**
 * ARM-BLE v2.9 — TX=D0(P0.25), 0xAA/0xBB framed
 *
 * v2.8 → v2.9 变更: TX 从 A2(P0.30) 改为 D0(P0.25) — A2 被 5V 损坏
 *
 * 状态机速览：
 *   扫描(10s窗口) → 扫到LOOKBON → CONNECTED
 *   CONNECTED → CCCD Phase1(2s后) → Phase2(4s后) → Phase3(6s后) → RECEIVING
 *   RECEIVING → 每帧手柄数据 → sendUartFrame (D0 TX, 3字节 0xAA/dir/0xBB)
 *   断连 → DISCONNECTED → 回到扫描
 */

#include <bluefruit.h>
#include <handle_parser.h>

static ParsedInput    g_input;
static volatile bool  g_newData      = false;

static uint16_t       g_connHandle   = BLE_CONN_HANDLE_INVALID;
static volatile bool  g_needRescan   = false;
static bool           g_scanPaused   = false;

static unsigned long  g_scanCount    = 0;
static unsigned long  g_lastLog      = 0;
static unsigned long  g_lastRestart  = 0;
static bool           g_restarting   = false;

static uint8_t        g_cccdPhase    = 0;
static unsigned long  g_cccdNextTime = 0;

static const uint16_t kCCCDHandles[] = {7, 67, 131};

static const unsigned long kUartBaud = 115200;
static const uint8_t     kLedRed     = 3;   // 红色 LED = D3 = P1.15

// ── BLE 事件回调 ──
static void onBLEEvent(ble_evt_t* evt)
{
    if (evt->header.evt_id == BLE_GAP_EVT_CONNECTED) {
        Serial.print("[EVT] CONNECTED conn="); Serial.println(evt->evt.common_evt.conn_handle);
    } else if (evt->header.evt_id == BLE_GAP_EVT_DISCONNECTED) {
        uint8_t r = evt->evt.gap_evt.params.disconnected.reason;
        Serial.print("[EVT] DISCONNECTED reason=0x"); Serial.println(r, HEX);
    }

    if (evt->header.evt_id != BLE_GATTC_EVT_HVX) return;
    ble_gattc_evt_hvx_t* hvx = &evt->evt.gattc_evt.params.hvx;
    if (hvx->type != BLE_GATT_HVX_NOTIFICATION) return;

    g_input   = parseLookbon(hvx->data, hvx->len);
    g_newData = true;

    Serial.print("[DATA] raw=0x");
    for (uint16_t i = 0; i < hvx->len; i++) {
        if (hvx->data[i] < 0x10) Serial.print('0');
        Serial.print(hvx->data[i], HEX);
    }
    Serial.print(" -> BTN=0x"); Serial.print(g_input.buttons, HEX);
    Serial.print(" X=");       Serial.print(g_input.joystickX);
    Serial.print(" Y=");       Serial.println(g_input.joystickY);
}

static void writeOneCCCD(uint16_t conn, uint16_t cccdHandle)
{
    uint16_t cccdVal = 0x0001;
    ble_gattc_write_params_t param;
    memset(&param, 0, sizeof(param));
    param.write_op = BLE_GATT_OP_WRITE_CMD;
    param.flags    = 0;
    param.handle   = cccdHandle;
    param.offset   = 0;
    param.len      = 2;
    param.p_value  = (uint8_t*)&cccdVal;

    uint32_t err = sd_ble_gattc_write(conn, &param);
    if (err != NRF_SUCCESS) {
        Serial.print("[CCCD-ERR] sd_ble_gattc_write=0x"); Serial.println(err, HEX);
    }
}

static void sendUartFrame(const ParsedInput& in)
{
    uint8_t dir = 0;
    if (in.buttons & 0x40)      dir = 0x01;
    else if (in.buttons & 0x80) dir = 0x02;
    else if (in.buttons & 0x10) dir = 0x03;
    else if (in.buttons & 0x20) dir = 0x04;

    if (dir) {
        uint8_t frame[3] = {0xAA, dir, 0xBB};
        size_t sent = Serial1.write(frame, 3);
        digitalWrite(kLedRed, HIGH);
        delay(2);
        digitalWrite(kLedRed, LOW);
        Serial.print("[UART] sent "); Serial.print(sent);
        Serial.print(" bytes: ");
        for (size_t i = 0; i < sent; i++) {
            if (frame[i] < 0x10) Serial.print('0');
            Serial.print(frame[i], HEX); Serial.print(' ');
        }
        Serial.println();
    }
}

static void onScan(ble_gap_evt_adv_report_t* report)
{
    g_scanCount++;
    uint8_t buf[32] = {0};
    String name = "(no name)";
    if (Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, buf, sizeof(buf)))
        name = String((char*)buf);
    else if (Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME, buf, sizeof(buf)))
        name = String((char*)buf);

    name.toLowerCase();
    if (name.indexOf("lookbon") >= 0) {
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
    unsigned long usbTimeout = millis() + 3000;
    while (!Serial && millis() < usbTimeout) delay(10);
    Serial.println("\n=== ARM-BLE v2.9 (TX=D0/P0.25, 0xAA/0xBB framed) ===");

    pinMode(kLedRed, OUTPUT);
    digitalWrite(kLedRed, LOW);

    // ── UART: 使用 Serial1 默认引脚: TX=D0(P0.25), RX=D1(P0.24) ──
    // A2(P0.30) 已被 5V 损坏, D0 示波器确认波形正常
    Serial1.begin(kUartBaud);
    Serial.print("[UART] Serial1 @ "); Serial.print(kUartBaud);
    Serial.println(" baud (TX=D0=P0.25 — default, A2 damaged)");
    Serial.println("[UART] TX -> GX12 -> 3号板 RX, 帧格式: 0xAA/dir/0xBB");

    // ── BLE ──
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

    g_lastLog     = millis();
    g_lastRestart = millis();
    Serial.println("[OK] v2.9 ready (TX=D0)");
}

void loop()
{
    unsigned long now = millis();

    if (g_cccdPhase >= 1 && g_cccdPhase <= 3 && (now - g_cccdNextTime > 2000)) {
        g_cccdNextTime = now;
        writeOneCCCD(g_connHandle, kCCCDHandles[g_cccdPhase - 1]);
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
        sendUartFrame(g_input);
    }
}