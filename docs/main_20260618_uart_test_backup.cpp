/**
 * UART_TX_DUAL — 双引脚交替测试 (无 SoftDevice)
 *
 * D0(P0.25) 和 A4(P0.04) 交替发 0xAA，每 500ms 切换一次引脚
 * LED 闪法区分当前引脚：
 *   D0 轮: LED 闪 1 次
 *   A4 轮: LED 闪 2 次
 *
 * 示波器依次量 D0 和 A4，看有无 UART 波形
 */

#include <Arduino.h>

static const uint8_t kLedPin = LED_BUILTIN;

// 两个干净的测试引脚
static const uint32_t kPinD0  = 25;   // P0.25
static const uint32_t kPinA4  = 4;    // P0.04

static void flashLed(int times)
{
    for (int i = 0; i < times; i++) {
        digitalWrite(kLedPin, HIGH); delay(80);
        digitalWrite(kLedPin, LOW);  delay(150);
    }
}

static void initUart(uint32_t txPin)
{
    NRF_UARTE0->ENABLE = 0;
    NRF_UARTE0->PSEL.TXD = txPin;
    NRF_UARTE0->PSEL.RXD = 0xFFFFFFFF;
    NRF_UARTE0->BAUDRATE = 0x01D7E854;
    NRF_UARTE0->CONFIG = 0;
    NRF_UARTE0->ENABLE = 8;
}

static void sendByte(uint8_t data)
{
    NRF_UARTE0->TXD.PTR    = (uint32_t)&data;
    NRF_UARTE0->TXD.MAXCNT = 1;
    NRF_UARTE0->EVENTS_ENDTX = 0;
    NRF_UARTE0->TASKS_STARTTX = 1;
    unsigned long t0 = micros();
    while (!NRF_UARTE0->EVENTS_ENDTX) {
        if (micros() - t0 > 5000) break;
    }
}

void setup()
{
    pinMode(kLedPin, OUTPUT);
    digitalWrite(kLedPin, LOW);

    // 启动确认: LED 快闪 3 次
    flashLed(3);
}

void loop()
{
    // ── D0 阶段 (1 秒) ──
    flashLed(1);  // 闪 1 次 = D0
    initUart(kPinD0);
    for (unsigned long t = millis(); millis() - t < 1000; ) {
        sendByte(0xAA);
        delayMicroseconds(1200);  // ~800 Hz burst
    }

    // ── 停顿 + 信号 ──
    delay(300);
    flashLed(2);  // 闪 2 次 = A4

    // ── A4 阶段 (1 秒) ──
    initUart(kPinA4);
    for (unsigned long t = millis(); millis() - t < 1000; ) {
        sendByte(0xAA);
        delayMicroseconds(1200);
    }

    delay(300);
}