#include "handle_parser.h"

// ============================================================
//  CodexPad-C10: 8 字节二进制帧
//  bytes[0-3] = 按键位掩码 (uint32 LE)
//  bytes[4]   = 左摇杆 X (0-255, 128=中位)
//  bytes[5]   = 左摇杆 Y (0-255, 128=中位)
//  bytes[6-7] = 右摇杆 X/Y (C10 无效)
// ============================================================
ParsedInput parseCodexPad(const uint8_t* data, size_t len)
{
    ParsedInput in;

    if (len >= 8) {
        // 按键状态 — little-endian 4字节
        in.buttons   = (uint32_t)data[0]
                     | ((uint32_t)data[1] << 8)
                     | ((uint32_t)data[2] << 16)
                     | ((uint32_t)data[3] << 24);

        in.joystickX = data[4];
        in.joystickY = data[5];
    }

    return in;
}

// ============================================================
//  VR LOOKBON: 单字节编码 (v2.5 修正)
//  高 nibble (bits 7-4): 事件类型
//    0xA = 单击, 0xB = 长按, 0xC = 释放, 0xD = 方向
//  低 nibble (bits 3-0): 按键/方向编号
//    0x1 = @, 0x2 = A, 0x3 = B, 0x4 = C, 0x5 = D, 0x6 = R, 0x7 = L
//    0x0-0x8 = 摇杆方向 (D系列)
// ============================================================
ParsedInput parseLookbon(const uint8_t* data, size_t len)
{
    ParsedInput in;  // 默认中位

    if (len < 1) return in;

    uint8_t byte  = data[0];
    uint8_t event = (byte >> 4) & 0x0F;  // 高 nibble
    uint8_t key   = byte & 0x0F;          // 低 nibble

    if (event == 0xD) {
        // 摇杆方向
        switch (key) {
        case 0x0: in.joystickX = 128; in.joystickY = 128; break;
        case 0x1: in.joystickX = 128; in.joystickY = 0;   break;
        case 0x2: in.joystickX = 128; in.joystickY = 255; break;
        case 0x3: in.joystickX = 0;   in.joystickY = 128; break;
        case 0x4: in.joystickX = 255; in.joystickY = 128; break;
        case 0x5: in.joystickX = 0;   in.joystickY = 0;   break;
        case 0x6: in.joystickX = 0;   in.joystickY = 255; break;
        case 0x7: in.joystickX = 255; in.joystickY = 0;   break;
        case 0x8: in.joystickX = 255; in.joystickY = 255; break;
        default: break;
        }
    } else if (event == 0xA || event == 0xB) {
        // 按键按下或长按
        // key: 1=@, 2=A, 3=B, 4=C, 5=D, 6=R, 7=L
        // lookupMap 索引 0 和 1 填 16 (bit16=@), 因为 key 从 1 开始永不访问索引 0
        static const uint8_t lookupMap[] = {16, 16, 6, 7, 4, 5, 11, 10};
        if (key >= 1 && key <= 7) {
            in.buttons = (uint32_t)1 << lookupMap[key];
        }
    }
    // event == 0xC: 释放，buttons 保持 0

    return in;
}

// ============================================================
//  统一入口
// ============================================================
ParsedInput parseInput(HandleType type, const uint8_t* data, size_t len)
{
    switch (type) {
    case HandleType::CodexPad_C10:
        return parseCodexPad(data, len);
    case HandleType::Lookbon_VR:
        return parseLookbon(data, len);
    default:
        return ParsedInput{};
    }
}