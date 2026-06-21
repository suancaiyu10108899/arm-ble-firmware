# arm-ble — nRF52840 VR 手柄蓝牙接收固件

> 项目: 假肢机械臂 — Arduino 蓝牙板（板①）
> 板子: Adafruit Feather nRF52840 Express
> SDK: PlatformIO + Arduino framework
> 固件版本: v2.9-final (2026-06-18)

## 功能

- **BLE Central** 扫描并连接 VR LOOKBON 手柄
- 接收手柄按键 Notify 数据，解析为 `BTN/X/Y`
- 通过 Serial1 UART TX（**D0=P0.25**, 115200 baud, 高驱动）发出 3 字节帧到③号板
- 帧格式: `0xAA` | 方向码 | `0xBB`

## 5 分钟上手（从零开始）

### 方式一：PlatformIO（推荐）

```powershell
# 1. 装 PlatformIO（仅第一次需要，需 Python 3.x）
pip install platformio

# 2. 克隆代码
git clone https://github.com/suancaiyu10108899/arm-ble-firmware.git
cd arm-ble-firmware

# 3. 插 MicroUSB 连板子，烧录（首次会自动下载依赖，约 2 分钟）
python -m platformio run --target upload

# 4. 硬件接线：D0(P0.25) → GX12 公头 → ③号板 RX
#                GND       → GX12 公头 → ③号板 GND

# 5. 上电验证：
#    - D4 蓝灯亮 = BLE 已连接手柄
#    - 按手柄 A/B/C/D → D3 红灯闪 = 发出 UART 帧
#    - 示波器接 D0：3 字节脉冲 0xAA / dir / 0xBB
```

> ⚠️ 烧录失败时按两下板子 Reset 按钮进入 Bootloader 模式重试。

### 方式二：Arduino IDE（备选）

```
1. 安装板子包：Arduino IDE → 文件 → 首选项 → 附加开发板管理器网址
   添加: https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
   → 开发板管理器搜索 "Adafruit nRF52" → 安装

2. 安装库：库管理器搜索 "Adafruit Bluefruit nRF52 Libraries" → 安装

3. 克隆代码后，用 Arduino IDE 打开 src/arm_ble.ino

4. 选择开发板: Adafruit Feather nRF52840 Express

5. 编译上传
```

> PlatformIO 方式零配置（无需手动装板子包和库），优先推荐。Arduino IDE 适合不熟悉命令行的同学。

### 串口监视（非必须，调试用）

```powershell
python -m platformio device monitor --project-dir=D:\Dev\arm-ble --baud 115200
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
=== ARM-BLE v2.9 (TX=D0/P0.25, 0xAA/0xBB framed) ===
[UART] Serial1 @ 115200 baud (TX=D0=P0.25 — default, A2 damaged)
[UART] TX -> GX12 -> 3号板 RX, 帧格式: 0xAA/dir/0xBB
[OK] v2.9 ready
[FOUND] LOOKBON 0D:CE:99:03:5D:D2
[CONNECT] conn=0
[OK] subscribed
[DATA] raw=0xA3 -> BTN=0x80 X=128 Y=128    ← 按键 B 单击
[UART] sent 3 bytes: AA 01 BB              ← A键→方向1
[.] conn=YES                                 ← 心跳，连接保持
```

## D0 TX 物理层验证 (2026-06-18)

| 测试 | 方法 | 结果 |
|:---:|------|:--:|
| 双引脚交替 (D0 + A4) | 寄存器直驱, 示波器 | ✅ D0 有规律 UART 方波 |
| 纯 TX (D0 only) | Serial1, 示波器 | ✅ 二进制序列 `0 01010101 1` = 0xAA |
| v2.9 全链路 | BLE + Serial1 + 示波器 | ✅ 0xAA dir BB 帧完整 |
| 空闲电平 | 万用表 DC (高驱动后) | ✅ 3.06V (>2.31V VIH) |

### 引脚最终分配

| 引脚 | 丝印 | 功能 | 状态 |
|:--:|:--:|------|:--:|
| **P0.25** | **D0** | **UART TX → GX12 → ③号板** | ✅ 示波器验证 |
| P0.30 | A2 | 原 UART TX | ❌ 5V 永久损坏 |
| P0.24 | RX/D1 | Serial1 RX (不读取) | ❌ 5V 输入管击穿 |
| P0.04 | A4 | Bluefruit LED_CONN | ⚠️ 被库占用 |
| P0.05 | A5 | 备用 | ❌ 5V 输入管击穿 |
| P1.15 | D3 | 红色 LED — UART TX 指示 | ✅ |
| P1.10 | D4 | Bluefruit LED_CONN (蓝) | ✅ |

## BLE 调试历程（关键发现）

| 阶段 | 问题 | 修复 |
|:---:|------|------|
| v1.0-v1.4 | `Bluefruit.begin(0,1)` 纯 Central 下 onScan 只触发一次 | `begin(1,1)` + loop 中被动扫描重启 |
| v1.6-v1.8 | Adafruit 库 GATT discover 与 LOOKBON 不兼容 | 跳过 discover，参照 Python 代码直写 CCCD |
| v2.0-v2.3 | onConnect 中 `delay(2000)` 阻塞 SoftDevice 中断 | 将 CCCD 写入移到 loop 中温和执行 |
| v2.5-v2.6 | 解析器误判 LOOKBON 为 ASCII 协议 | 发现是单字节编码（高 nibble=事件，低 nibble=按键） |
| v2.7 | `while (!Serial)` 死等导致电池供电时灭灯 | 改为 3 秒超时 |
| **v2.8** | **A2 TX 物理层无声** | **9 轮示波器调试 → A2 被 5V 损坏 → 切到 D0 + 高驱动 → 打通** |

## UART TX 物理层突破全过程 (2026-06-18)

```
7 轮失败 (6/17): Serial1/A2×3 + UARTE0/A2×1 + UARTE1/A2×1 + UARTE0/GPIO×2
    ↓
发现 A2 误焊到 5V 引脚 → 硬件损伤
    ↓
双引脚交替测试: D0(P0.25) + A4(P0.04) 寄存器直驱 → D0 有波形! ✅
    ↓
纯 TX 固件: Serial1 D0 → 示波器二进制序列 0xAA 正确 ✅
    ↓
回环验证 (D0 ┄ A4/D1/A5): 全部失败 → GPIO 输入管全部击穿
    ↓
v2.9 final: Serial1 D0 + 高驱动 (3.06V) → 全链路打通 ✅
    ↓
板1 报废: 12V 带电焊接灌入 VDD 轨 → 3V/GND 短路
```

## LOOKBON 协议（单字节编码）

| 字节 | 含义 |
|------|------|
| `0xA1`-`0xA7` | 单击 @/A/B/C/D/R/L |
| `0xB1`-`0xB7` | 长按 |
| `0xC1`-`0xC7` | 释放 |
| `0xD0` | 摇杆中位 |
| `0xD1`-`0xD8` | 摇杆 8 方向 |

## UART 帧格式（学长协议）

| byte[0] | byte[1] | byte[2] |
|---------|---------|---------|
| `0xAA` (帧头) | 方向码 | `0xBB` (帧尾) |

方向码：
- `0x01` = A 单击 → 舵机1 正转
- `0x02` = B 单击 → 舵机1 反转
- `0x03` = C 单击 → 舵机2 正转
- `0x04` = D 单击 → 舵机2 反转
- `0x05` = A 长按 → 舵机1 正转（持续顶到限位）
- `0x06` = B 长按 → 舵机1 反转（持续）
- `0x07` = C 长按 → 舵机2 正转（持续）
- `0x08` = D 长按 → 舵机2 反转（持续）

> 无按键时不发帧。引脚 D0(P0.25) → GX12 航空接头 → ③号板 RX。波特率 115200。

## 硬件接线

```
D0 (P0.25) ──飞线──▶ GX12 公头 ──▶ ③号板 RX
GND        ─────────▶ GX12 公头 ──▶ ③号板 GND
```

## 文件说明

| 文件 | 内容 |
|------|------|
| `src/main.cpp` | **v2.9 final** — 扫描/连接/CCCD/解析/UART TX(D0, 0xAA帧, 高驱动) |
| `src/handle_parser.h/cpp` | LOOKBON + CodexPad 协议解析器 |
| `src/arm_ble.ino` | Arduino IDE 入口文件 |
| `platformio.ini` | PlatformIO 配置 (含 NeoPixel 依赖) |
| `docs/devlog/` | 开发日志 |
| `docs/debug-log/` | 调试记录 |
| `docs/开发环境搭建指南.md` | Python/PlatformIO 安装与使用 |
| `docs/交接文档_20260613.md` | 给学长的板子信息/协议/待确认清单 |

### 备份

| 文件 | 说明 |
|------|------|
| `docs/main_v2.9_final.cpp` | 最终生产固件 |
| `docs/main_v2.9_prod_backup.cpp` | v2.9 第二备份 |
| `docs/main_20260618_uart_test_backup.cpp` | 双引脚交替 TX 测试 |

## 硬件事故记录

| # | 日期 | 事故 | 后果 | 教训 |
|:--:|------|------|------|------|
| 1 | 6/17 | TX(A2) 误焊到③号板 5V | A2 损坏 | 焊接后万用表验连通 |
| 2 | 6/17 | 5V 倒灌 VDD 轨 | 输入管全部击穿 | GPIO 输入端对过压敏感 |
| 3 | 6/18 | 12V 带电焊接灌入 | 板子报废 | 焊接必须断电 |

## 已验证的硬件

- 板子 MAC: `D3:52:88:3A:06:27` / 手柄 MAC: `0D:CE:99:03:5D:D2`
- BLE 扫描 + 连接 + Notify 数据接收 ✅
- LOOKBON 协议解析 (BTN/X/Y) ✅
- **UART TX (D0, 115200, 0xAA帧) — 示波器验证通过** ✅
- 电池供电自动启动 ✅
- D0 高驱动空闲电平 3.06V ✅

## ⚡ ESP32 替补方案

nRF52840 板1 已报废(12V 事故)。已有 **ESP32 v6-final 替补方案** 完成验证：

| 项目 | nRF52840 | ESP32 |
|------|:---:|:---:|
| BLE → UART | ✅ v2.9 | ✅ v6-final |
| TX 引脚 | D0(P0.25) | D17(GPIO17) |
| 项目路径 | `D:\Dev\arm-ble\` | `D:\Dev\arm-ble-esp32\` |
| README | 本文件 | `../arm-ble-esp32/README.md` |

> ESP32 方案已通过全链路验证（扫描→连接→GATT→Notify→UART TX 示波器），可直接用于③号板联调。

---

## 换新板后的操作

```
1. 插 MicroUSB
2. python -m platformio run --project-dir=D:\Dev\arm-ble --target upload
3. 飞线: D0 → GX12 → ③号板 RX
4. 上电, D4 蓝灯亮 = BLE 连接 OK
5. 按手柄 A/B/C/D → D3 红灯闪 + D0 示波器 0xAA dir BB
```

无需改任何代码。

## 相关文档

| 文档 | 路径 |
|------|------|
| 交接文档 | `docs/交接文档_20260613.md` |
| 开发环境搭建 | `docs/开发环境搭建指南.md` |
| UART TX 调试全记录 | `docs/debug-log/2026-06-17_UART_TX调试全记录.md` |
| AI 记忆管理 | `d:\假肢机械臂\09_项目管理\AI_MEMORY.md` |