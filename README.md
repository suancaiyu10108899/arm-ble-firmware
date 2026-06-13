# arm-ble — nRF52840 VR 手柄蓝牙接收固件

> 项目: 假肢机械臂 — Arduino 蓝牙板（板①）
> 板子: Adafruit Feather nRF52840 Express
> SDK: PlatformIO + Arduino framework
> 固件版本: v2.6（2026-06-13）

## 功能

- **BLE Central** 扫描并连接 VR LOOKBON 手柄（MAC: `0D:CE:99:03:5D:D2`）
- 接收手柄按键/摇杆 Notify 数据，解析为 `BTN/X/Y`
- 输出 PWM 舵机控制信号（A2 引脚）

## 快速开始

### 烧录

```powershell
python -m platformio run --project-dir=d:\Dev\arm-ble --target upload
```

烧录失败时按一下板子 Reset 按钮进入 Bootloader 模式重试。

### 串口监视

```powershell
python -m platformio device monitor --project-dir=d:\Dev\arm-ble --baud 115200 --port COM6
```

## 预期运行效果

```
=== ARM-BLE v2.6 (single-byte parser) ===
[OK] v2.6
[FOUND] LOOKBON 0D:CE:99:03:5D:D2
[CONNECT] conn=0
[OK] subscribed
[DATA] raw=0xA3 → BTN=0x80 X=128 Y=128    ← 按键 B 单击
[DATA] raw=0xB3 → BTN=0x80 X=128 Y=128    ← 按键 B 长按
[DATA] raw=0xD1 → BTN=0x0 X=128 Y=0       ← 摇杆向上
[DATA] raw=0xD7 → BTN=0x0 X=255 Y=0       ← 摇杆右上
[.] conn=YES                                ← 心跳，连接保持
```

## BLE 调试历程（关键发现）

| 阶段 | 问题 | 修复 |
|:---:|------|------|
| v1.0-v1.4 | `Bluefruit.begin(0,1)` 纯 Central 下 onScan 只触发一次 | `begin(1,1)` + loop 中被动扫描重启 |
| v1.6-v1.8 | Adafruit 库 GATT discover 与 LOOKBON 不兼容 | 跳过 discover，参照 Python 代码直写 CCCD |
| v2.0-v2.3 | onConnect 中 `delay(2000)` 阻塞 SoftDevice 中断 | 将 CCCD 写入移到 loop 中温和执行 |
| v2.5-v2.6 | 解析器误判 LOOKBON 为 ASCII 协议 | 发现是单字节编码（高 nibble=事件，低 nibble=按键） |

## LOOKBON 协议（单字节编码）

| 字节 | 含义 |
|------|------|
| `0xA1`-`0xA7` | 单击 @/A/B/C/D/R/L |
| `0xB1`-`0xB7` | 长按 |
| `0xC1`-`0xC7` | 释放 |
| `0xD0` | 摇杆中位 |
| `0xD1`-`0xD8` | 摇杆 8 方向 |

## 文件说明

| 文件 | 内容 |
|------|------|
| `src/main.cpp` | v2.6 固件 — 扫描/连接/CCCD/解析/舵机 |
| `src/handle_parser.h/cpp` | LOOKBON + CodexPad 协议解析器 |
| `docs/devlog/` | 开发日志 |

## 验证过的硬件

- MAC: `D3:52:88:3A:06:27`（板子）/ `0D:CE:99:03:5D:D2`（手柄）
- 板子 BLE 通信 ✅
- 手柄扫描 + 连接 + Notify 数据接收 ✅

## 相关文档

| 文档 | 路径 |
|------|------|
| 交接文档 | `docs/交接文档_20260613.md` |
| 开发日志 | `docs/devlog/2026-06-13_手柄调试图鉴.md` |
