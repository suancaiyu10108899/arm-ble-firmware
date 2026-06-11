/**
 * ARM-BLE — 极简 BLE 广播测试
 * 不依赖串口 COM，只靠 NeoPixel 灯指示状态
 *
 * 成功: 蓝色呼吸灯 + 手机 nRF Connect 扫描到 "ARM-BLE"
 */

#include <bluefruit.h>

BLEUart bleuart;  // BLE UART 服务对象

void setup()
{
  // BLE 初始化
  Bluefruit.begin();
  Bluefruit.setName("ARM-BLE");

  // 配置 BLE UART 服务
  bleuart.begin();

  // 开始广播
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.Advertising.start(0);

  // NeoPixel 蓝色 = 等待连接
  Bluefruit.autoConnLed(true);

  // 开启串口（用于打印收到的蓝牙数据）
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("ARM-BLE Ready");
}

void loop()
{
  // 手机 → 板子 → 电脑串口
  while (bleuart.available()) {
    char c = bleuart.read();
    Serial.write(c);
  }

  // 电脑串口 → 板子 → 手机
  while (Serial.available()) {
    char c = Serial.read();
    bleuart.write(c);
  }
}
