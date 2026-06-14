/**
 * ARM-BLE v2.7 — UART TX 打通测试
 *
 * v2.6: BLE扫描/GAP/CCCD/Notify/解析 全通
 * v2.7: 新增 Serial1 UART TX 输出 (D0=P0.25)
 *       + 启动时 UART 自检 (loopback 测试)
 *       + 手柄数据通过 UART 发出
 */

#include <bluefruit.h>
#include <handle_parser.h>

static HandleType     g_handleType   = HandleType::Lookbon_VR;
static volatile bool  g_newData      = false;
static ParsedInput    g_input;

static uint16_t       g_connHandle   = BLE_CONN_HANDLE_INVALID;
static uint16_t       g_notifyHandle = 0;
static volatile bool  g_needRescan   = false;
static bool           g_scanPaused   = false;

static unsigned long  g_scanCount    = 0;
static unsigned long  g_lastLog      = 0;
static unsigned long  g_lastRestart  = 0;
static bool           g_restarting   = false;

static uint8_t        g_cccdPhase    = 0;
static unsigned long  g_cccdNextTime = 0;
static const uint16_t kCCCDHandles[] = {7, 67, 131};

// ── UART 配置 ──
static const unsigned long kUartBaud  = 115200;  // 先默认 115200，等学长确认

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

    g_input   = parseLookbon(hvx->data, hvx->len);
    g_newData = true;

    // USB 串口打印
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

// ── UART 自检（启动时跑一次） ──
static bool uartSelfTest()
{
    Serial.println("[UART-SELFTEST] starting...");

    // 检查 Serial1 是否可用
    if (!Serial1) {
        Serial.println("[UART-SELFTEST] FAIL: Serial1 not available");
        return false;
    }

    // 发送测试帧
    uint8_t testFrame[] = {0xAA, 0x55, 0x00, 0xFF};
    Serial1.write(testFrame, 4);
    Serial.flush();
    delay(10);

    Serial.print("[UART-SELFTEST] sent 4 bytes on D0(P0.25) @ ");
    Serial.print(kUartBaud);
    Serial.println(" baud");

    // 打印引脚信息
    Serial.println("[UART-SELFTEST] pin mapping:");
    Serial.println("  D0(TX) = P0.25 → to ③号板 RX");
    Serial.println("  D1(RX) = P0.24 → from ③号板 TX");
    Serial.println("  GND → to ③号板 GND (共地)");

    // 如果 D0 和 D1 用杜邦线短接了（loopback），读回验证
    Serial1.flush();
    delay(20);
    if (Serial1.available() > 0) {
        uint8_t rxBuf[4] = {0};
        size_t cnt = Serial1.readBytes(rxBuf, 4);
        if (cnt == 4 && rxBuf[0] == 0xAA && rxBuf[1] == 0x55) {
            Serial.println("[UART-SELFTEST] LOOPBACK PASS ✅ — D0→D1 shorted, received echo");
        } else {
            Serial.print("[UART-SELFTEST] received "); Serial.print(cnt);
            Serial.println(" bytes (loopback not shorted or wrong baud)");
        }
    } else {
        Serial.println("[UART-SELFTEST] no loopback detected (D0-D1 not shorted, this is OK)");
    }

    Serial.println("[UART-SELFTEST] complete");
    return true;
}

// ── 通过 UART 发送手柄数据 ──
static void sendUartFrame(const ParsedInput& in)
{
    // v2.7: 固定 4 字节帧 (等学长规划具体格式后再改)
    // byte[0] = joystickX
    // byte[1] = joystickY
    // byte[2] = buttons (低 8 位)
    // byte[3] = buttons (高 8 位)  — 预留
    uint8_t frame[4];
    frame[0] = in.joystickX;
    frame[1] = in.joystickY;
    frame[2] = in.buttons & 0xFF;
    frame[3] = (in.buttons >> 8) & 0xFF;

    size_t sent = Serial1.write(frame, 4);
    Serial.print("[UART] sent "); Serial.print(sent);
    Serial.print(" bytes: ");
    for (size_t i = 0; i < sent; i++) {
        if (frame[i] < 0x10) Serial.print('0');
        Serial.print(frame[i], HEX); Serial.print(' ');
    }
    Serial.println();
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

// ── setup ──
void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("\n=== ARM-BLE v2.7 (UART TX test) ===");

    // ── UART 初始化 ──
    Serial1.begin(kUartBaud);
    Serial.print("[UART] Serial1 started @ "); Serial.print(kUartBaud);
    Serial.println(" baud (D0=TX=P0.25, D1=RX=P0.24)");

    // ── UART 自检 ──
    delay(500);
    uartSelfTest();
    delay(500);

    // ── BLE 初始化 ──
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
    Serial.println("[OK] v2.7 ready");
}

// ── loop ──
void loop()
{
    unsigned long now = millis();

    // ── CCCD 阶段机 ──
    if (g_cccdPhase >= 1 && g_cccdPhase <= 3 && (now - g_cccdNextTime > 2000)) {
        g_cccdNextTime = now;
        writeOneCCCD(g_connHandle, kCCCDHandles[g_cccdPhase - 1]);
        g_notifyHandle = kCCCDHandles[g_cccdPhase - 1];
        g_cccdPhase++;
        if (g_cccdPhase > 3) Serial.println("[OK] subscribed");
    }

    // ── 扫描管理 ──
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

    // ── 心跳 ──
    if (now - g_lastLog > 5000) {
        g_lastLog = now;
        Serial.print("[.] conn=");
        Serial.println(g_connHandle != BLE_CONN_HANDLE_INVALID ? "YES" : "NO");
    }

    // ── 手柄数据 → UART TX ──
    if (g_newData) {
        g_newData = false;
        sendUartFrame(g_input);
    }
}