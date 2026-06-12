# arm-ble — nRF52840 蓝牙板固件

> 项目: 假肢机械臂 — Arduino 蓝牙板（板①）
> 板子: Adafruit Feather nRF52840 Express
> SDK: PlatformIO + Arduino framework

## 快速开始

### 1. 环境
- PlatformIO 6.x (pip 安装): `python -m platformio --version`
- MicroUSB 数据线（4芯，能传数据的那种）
- nRF52840 板子

### 2. 烧录

```powershell
cd D:\Dev\arm-ble
python -m platformio run --project-dir=. --target upload
```

如果烧录失败（COM 口被占用 / 找不到板子）：
- 快按一下板子 Reset 按钮进入 Bootloader 模式
- Bootloader 只在几秒内可用，platformio 会自动检测

### 3. 串口监视

```powershell
python -m platformio device monitor --project-dir=.
```

波特率 115200，看到 `ARM-BLE Ready` = 固件跑起来了。

## 固件说明

| 文件 | 用途 | BLE角色 |
|------|------|:---:|
| `src/main.cpp` | **当前固件**: BLE UART 转发器 (NUS) | 外设 Peripheral |
| `src/sketch_feb3a.bak` | 备份: Bend Labs 弯曲传感器固件 | 主机 Central |

### main.cpp — BLE UART 转发器
- 广播名 `ARM-BLE`
- NUS UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- 功能: 手机/App → BLE → 板子 → 串口打印（双向透明转发）
- NeoPixel 蓝灯呼吸 = 广播中，常亮 = 已连接

### sketch_feb3a.bak — 弯曲传感器固件（备考）
- nRF52840 作为 BLE Central 连接 Bend Labs 传感器
- 接收角度数据 → IIR低通(20Hz) + 死区(0.5°) → 舵机 PWM (A2引脚)
- 标定: 串口发 `0`/`9`/`b` 等单字符命令

## 验证过的硬件

- MAC: `D3:52:88:3A:06:27`
- NUS 服务在 nRF Connect 中可被发现、连接、双向通信 ✅
- 中文 UTF-8 透明传递 ✅

## 相关项目

- 上位机调试工具: [arm-ble-gui](https://github.com/suancaiyu10108899/arm-ble-gui)
- 文档仓库: [prosthetic-robotic-arm](https://github.com/suancaiyu10108899/prosthetic-robotic-arm)