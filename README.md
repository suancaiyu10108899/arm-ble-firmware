# arm-ble — nRF52840 VR 手柄蓝牙接收固件

> 项目: 假肢机械臂 — Arduino 蓝牙板（板①）
> 板子: Adafruit Feather nRF52840 Express
> SDK: PlatformIO + Arduino framework
> 固件版本: v2.8（2026-06-15）

## 功能

- **BLE Central** 扫描并连接 VR LOOKBON 手柄（MAC: `0D:CE:99:03:5D:D2`）
- 接收手柄按键 Notify 数据，解析为 `BTN/X/Y`
- 通过 Serial1 UART TX（A2=P0.30, 115200 baud）发出 3 字节帧到③号板

## 快速开始

### 烧录

```powershell
python -m platformio run --project-dir=d:\Dev\arm-ble --target upload
```

烧录失败时按两下板子 Reset 按钮进入 Bootloader 模式重试。

### 串口监视

```powershell
python -m platformio device monitor --project-dir=d:\Dev\arm-ble --baud 115200
```

## 供电方式

| 方式 | 电压 | 接哪里 |
|------|------|--------|
| MicroUSB（调试） | 5V | MicroUSB 口 |
| 锂电池（成品） | 3.7-4.2V | 背面 JST 或 BAT 引脚 |
| 外部电源 | 4-6V | BAT 引脚 |

> ⚠️ **不能 3.3V 接 BAT**：板载 AP2112 LDO 有 250mV 压差，3.3V 输入 → 芯片只能拿到 ~3.0V，BLE 射频一开就欠压复位。

## 预期运行效果

```
=== ARM-BLE v2.8 (UART TX on A2, 0xAA/0xBB framed) ===
[UART] Serial1 @ 115200 baud (TX=A2=P0.30, RX=D1=P0.24)
[UART] A2 -> GX12 -> ③号板 RX, 帧格式: 0xAA/dir/0xBB
[OK] v2.8 ready
[FOUND] LOOKBON 0D:CE:99:03:5D:D2
[CONNECT] conn=0
[OK] subscribed
[DATA] raw=0xA3 -> BTN=0x80 X=128 Y=128    ← 按键 B 单击
[UART] sent 3 bytes: AA 01 BB              ← A键→方向1
[.] conn=YES                                 ← 心跳，连接保持
```

## BLE 调试历程（关键发现）

| 阶段 | 问题 | 修复 |
|:---:|------|------|
| v1.0-v1.4 | `Bluefruit.begin(0,1)` 纯 Central 下 onScan 只触发一次 | `begin(1,1)` + loop 中被动扫描重启 |
| v1.6-v1.8 | Adafruit 库 GATT discover 与 LOOKBON 不兼容 | 跳过 discover，参照 Python 代码直写 CCCD |
| v2.0-v2.3 | onConnect 中 `delay(2000)` 阻塞 SoftDevice 中断 | 将 CCCD 写入移到 loop 中温和执行 |
| v2.5-v2.6 | 解析器误判 LOOKBON 为 ASCII 协议 | 发现是单字节编码（高 nibble=事件，低 nibble=按键） |
| v2.7 | `while (!Serial)` 死等导致电池供电时灭灯 | 改为 3 秒超时 |

## LOOKBON 协议（单字节编码）

| 字节 | 含义 |
|------|------|
| `0xA1`-`0xA7` | 单击 @/A/B/C/D/R/L |
| `0xB1`-`0xB7` | 长按 |
| `0xC1`-`0xC7` | 释放 |
| `0xD0` | 摇杆中位 |
| `0xD1`-`0xD8` | 摇杆 8 方向 |

## UART 帧格式（v2.8 — 学长协议）

| byte[0] | byte[1] | byte[2] |
|---------|---------|---------|
| `0xAA` (帧头) | 方向码 | `0xBB` (帧尾) |

方向码：
- `0x01` = A键 → 舵机1 正转
- `0x02` = B键 → 舵机1 反转
- `0x03` = C键 → 舵机2 正转
- `0x04` = D键 → 舵机2 反转

> 无按键时不发帧。引脚 A2(P0.30) → GX12 航空接头 → ③号板 RX。波特率 115200。

## 硬件接线

```
A2 (P0.30) ──飞线──▶ GX12 公头 ──▶ ③号板 RX
GND        ─────────▶ GX12 公头 ──▶ ③号板 GND
```

## 文件说明

| 文件 | 内容 |
|------|------|
| `src/main.cpp` | v2.8 固件 — 扫描/连接/CCCD/解析/UART TX (0xAA帧) |
| `src/handle_parser.h/cpp` | LOOKBON + CodexPad 协议解析器 |
| `src/arm_ble.ino` | Arduino IDE 入口文件 |
| `docs/devlog/` | 开发日志 |
| `docs/开发环境搭建指南.md` | Python/PlatformIO 安装与使用 |
| `docs/交接文档_20260613.md` | 给学长的板子信息/协议/待确认清单 |

## 验证过的硬件

- MAC: `D3:52:88:3A:06:27`（板子）/ `0D:CE:99:03:5D:D2`（手柄）
- 板子 BLE 通信 ✅
- 手柄扫描 + 连接 + Notify 数据接收 ✅
- UART TX (A2, 115200, 0xAA帧) ✅ 编译通过，待接③号板实测
- 电池供电自动启动 ✅

## 相关文档

| 文档 | 路径 |
|------|------|
| 交接文档 | `docs/交接文档_20260613.md` |
| 开发环境搭建 | `docs/开发环境搭建指南.md` |
| 开发日志 | `docs/devlog/2026-06-13_手柄调试图鉴.md` |