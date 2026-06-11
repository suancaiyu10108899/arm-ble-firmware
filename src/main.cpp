/**
 * arm-ble — Blink 测试程序
 * 板子: Adafruit Feather nRF52840 Express
 * 用途: 验证 Arduino 蓝牙板的基本可编程性
 *
 * 成功标志: 板载红色 LED (pin 13) 以 1Hz 频率闪烁
 * 下一步: 替换为蓝牙遥控固件
 */

#include <Arduino.h>

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}