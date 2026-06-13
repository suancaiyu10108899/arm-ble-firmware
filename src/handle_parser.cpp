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
//  VR LOOKBON: 2 字节 ASCII 字符串
//  格式: [事件][编号]
//    'A'=单击, 'B'=长按, 'C'=释放, 'D'=方向
//    '1'=@, '2'=A, '3'=B, '4'=C, '5'=D, '6'=R, '7'=L
//    D0-D8 = 方向
// ============================================================
ParsedInput parseLookbon(const uint8_t* data, size_t len)
{
    ParsedInput in;  // 默认中位

    if (len < 2) return in;

    char event = (char)data[0];
    char key   = (char)data[1];

    if (event == 'D') {
        // 摇杆方向 → 映射到 X/Y 模拟值
        switch (key) {
        case '0': in.joystickX = 128; in.joystickY = 128; break;  // 中位
        case '1': in.joystickX = 128; in.joystickY = 0;   break;  // 上
        case '2': in.joystickX = 128; in.joystickY = 255; break;  // 下
        case '3': in.joystickX = 0;   in.joystickY = 128; break;  // 左
        case '4': in.joystickX = 255; in.joystickY = 128; break;  // 右
        case '5': in.joystickX = 0;   in.joystickY = 0;   break;  // 左上
        case '6': in.joystickX = 0;   in.joystickY = 255; break;  // 左下
        case '7': in.joystickX = 255; in.joystickY = 0;   break;  // 右上
        case '8': in.joystickX = 255; in.joystickY = 255; break;  // 右下
        default: break;
        }
    } else if (event == 'A' || event == 'B') {
        // 按键按下或长按 → 设置对应 bit
        // @=bit16, A=bit6, B=bit7, C=bit4, D=bit5, R=bit11(复用), L=bit10(复用)
        int btnIdx = key - '1';  // 0-6
        if (btnIdx >= 0 && btnIdx <= 6) {
            // 映射到通用按钮位
            // 0(@) → bit16, 1(A) → bit6, 2(B) → bit7,
            // 3(C) → bit4, 4(D) → bit5, 5(R) → bit11, 6(L) → bit10
            static const uint8_t lookupMap[] = {16, 6, 7, 4, 5, 11, 10};
            in.buttons = (uint32_t)1 << lookupMap[btnIdx];
        }
    }
    // 'C' 释放事件 — 清空按钮（buttons 保持 0 即可，因为每次收到全量替换）

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