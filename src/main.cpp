/**
 * ARM-BLE — 双手柄接收固件
 *
 * 架构:
 *   nRF52840 同时担任:
 *     1. BLE Central — 扫描/连接手柄 (CodexPad-C10 或 VR LOOKBON)
 *     2. BLE Peripheral — NUS 服务给 arm-ble-gui 监控
 *
 * 支持的手柄:
 *   - CodexPad-C10: Service 0xFFA0, Char 0xFFA1 Notify, 8字节二进制帧
 *   - VR LOOKBON:   Service AE30, Char AE02 Notify, 2字节 ASCII 帧
 *
 * 输出:
 *   - 舵机 PWM (A2 引脚)
 *   - 串口打印 (115200, 调试用)
 *   - NUS BLE 转发 (给 GUI)
 *   - UART 转发 (给 ③ 号 STM32 板, 待确认波特率)
 *
 * 基于 sketch_feb3a.bak 改造:
 *   - 扫描过滤 UUID 从 0x1820 改为手柄 UUID
 *   - 数据解析从弯曲传感器格式改为手柄格式
 *   - 增加双角色 (Central + Peripheral)
 *   - 增加手柄类型自动检测
 */

#include <bluefruit.h>
#include <Servo.h>

#include "handle_parser.h"

// ── 舵机 ──
Servo myservo;
static const int kServoPin = A2;  ///< 舵机信号线接 A2

// ── 手柄连接状态 ──
static HandleType  g_handleType = HandleType::None;
static volatile bool g_newData  = false;
static ParsedInput g_input;           ///< 最新解析后的手柄输入

// ── BLE Client 对象 (Central 角色 — 连接手柄) ──
// 用指针 + 动态分配，因为不同手柄的 UUID 不同，且 BLEClientService
// 不支持赋值操作（Bluefruit 库限制）。
static BLEClientService*        g_handleService = nullptr;
static BLEClientCharacteristic* g_handleChar    = nullptr;

// ── BLE Peripheral 对象 (给 GUI 的 NUS 服务) ──
static BLEUart g_bleuart;  ///< Nordic UART Service — GUI 通过这个监控

// ============================================================
//  Notify 回调 — 手柄数据到达
// ============================================================
static void handleNotifyCallback(BLEClientCharacteristic* chr,
                                  uint8_t* data, uint16_t len)
{
    g_input   = parseInput(g_handleType, data, len);
    g_newData = true;

    // 调试打印
    Serial.print("BTN:0x");
    Serial.print(g_input.buttons, HEX);
    Serial.print(" X:");
    Serial.print(g_input.joystickX);
    Serial.print(" Y:");
    Serial.println(g_input.joystickY);
}

// ============================================================
//  BLE 扫描回调
// ============================================================
static void scanCallback(ble_gap_evt_adv_report_t* report)
{
    // 获取广播名
    uint8_t nameBuf[32] = {0};
    String name = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, nameBuf, sizeof(nameBuf))
                  ? String((char*)nameBuf) : String();

    Serial.print("发现设备: ");
    Serial.println(name.length() > 0 ? name : "(无名称)");

    // 自动识别手柄类型
    if (name.startsWith("CodexPad-")) {
        g_handleType = HandleType::CodexPad_C10;
        Serial.println("  → 识别为 CodexPad-C10");
        Bluefruit.Central.connect(report);
    } else if (name == "LOOKBON") {
        g_handleType = HandleType::Lookbon_VR;
        Serial.println("  → 识别为 VR LOOKBON");
        Bluefruit.Central.connect(report);
    }
}

// ============================================================
//  连接成功回调
// ============================================================
static void connectCallback(uint16_t conn_handle)
{
    Serial.println("已连接");

    // 根据手柄类型创建对应的 Service/Characteristic 对象
    if (g_handleType == HandleType::Lookbon_VR) {
        g_handleService = new BLEClientService(BLEUuid("0000AE30-0000-1000-8000-00805F9B34FB"));
        g_handleChar    = new BLEClientCharacteristic(BLEUuid("0000AE02-0000-1000-8000-00805F9B34FB"));
    } else {
        // CodexPad-C10 (默认)
        g_handleService = new BLEClientService(0xFFA0);
        g_handleChar    = new BLEClientCharacteristic(0xFFA1);
    }

    // 发现服务
    if (!g_handleService->discover(conn_handle)) {
        Serial.println("未找到输入服务");
        Bluefruit.disconnect(conn_handle);
        return;
    }

    // 发现特征
    if (!g_handleChar->discover()) {
        Serial.println("未找到输入特征");
        Bluefruit.disconnect(conn_handle);
        return;
    }

    // 设置 Notify 回调
    g_handleChar->setNotifyCallback(handleNotifyCallback);

    // 启用 Notify
    if (g_handleChar->enableNotify()) {
        Serial.println("Notify 已启用 — 等待手柄数据...");
    } else {
        Serial.println("Notify 启用失败");
        Bluefruit.disconnect(conn_handle);
    }
}

// ============================================================
//  断连回调
// ============================================================
static void disconnectCallback(uint16_t conn_handle, uint8_t reason)
{
    (void)conn_handle;
    Serial.print("已断开, reason = 0x");
    Serial.println(reason, HEX);

    g_handleType = HandleType::None;

    // 释放旧的 Service/Char 对象
    if (g_handleService) { delete g_handleService; g_handleService = nullptr; }
    if (g_handleChar)    { delete g_handleChar;    g_handleChar    = nullptr; }

    // 重新开始扫描
    Bluefruit.Scanner.start(0);
}

// ============================================================
//  setup()
// ============================================================
void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("\n=== ARM-BLE 双手柄接收固件 v0.2 ===");

    // ── 1. 初始化 BLE (双角色: Central + Peripheral) ──
    Bluefruit.begin(/*central=*/1, /*peripheral=*/1);
    Bluefruit.setName("ARM-BLE");

    // ── 2. Peripheral 角色: NUS 服务 (给 GUI 监控) ──
    g_bleuart.begin();
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(g_bleuart);
    Bluefruit.Advertising.start(0);
    Bluefruit.autoConnLed(true);

    // ── 3. Central 角色: 扫描手柄 ──
    Bluefruit.Central.setConnectCallback(connectCallback);
    Bluefruit.Central.setDisconnectCallback(disconnectCallback);
    Bluefruit.Scanner.setRxCallback(scanCallback);
    Bluefruit.Scanner.restartOnDisconnect(true);
    Bluefruit.Scanner.setInterval(160, 80);  // 快速扫描
    // 不设 UUID 过滤 — 在 scanCallback 里按广播名识别手柄类型
    Bluefruit.Scanner.useActiveScan(false);
    Bluefruit.Scanner.start(0);

    // ── 4. 舵机 ──
    myservo.attach(kServoPin);
    myservo.write(90);  // 初始中位

    Serial.println("就绪 — 正在扫描手柄...");
    Serial.println("  NUS MAC: D3:52:88:3A:06:27");
    Serial.println("  CodexPad Service: 0xFFA0");
}

// ============================================================
//  loop()
// ============================================================
void loop()
{
    // ── 处理新到手柄数据 ──
    if (g_newData) {
        g_newData = false;

        // 1. 映射摇杆 → 舵机角度
        //    joystickX: 0(左) ~ 128(中) ~ 255(右) → 0° ~ 90° ~ 180°
        int servoAngle = map(g_input.joystickX, 0, 255, 0, 180);
        myservo.write(servoAngle);

        // 2. 通过 NUS 发给 GUI (结构化数据)
        //    格式: [X][Y][buttons_LE4]
        uint8_t outBuf[6];
        outBuf[0] = g_input.joystickX;
        outBuf[1] = g_input.joystickY;
        outBuf[2] = (g_input.buttons >> 0)  & 0xFF;
        outBuf[3] = (g_input.buttons >> 8)  & 0xFF;
        outBuf[4] = (g_input.buttons >> 16) & 0xFF;
        outBuf[5] = (g_input.buttons >> 24) & 0xFF;
        if (Bluefruit.connected()) {
            g_bleuart.write(outBuf, sizeof(outBuf));
        }

        // 3. UART 发给 ③ 号板 (TODO: 确认波特率后用 Serial1)
        // Serial1.write(outBuf, sizeof(outBuf));
    }

    // ── NUS 回环 (GUI → 板子 → 串口, 调试用) ──
    while (g_bleuart.available()) {
        char c = g_bleuart.read();
        Serial.write(c);
    }
}