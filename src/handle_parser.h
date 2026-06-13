#pragma once

#include <cstdint>
#include <cstddef>

/**
 * @brief 手柄解析后的标准化输入
 *
 * 无论连接的是 CodexPad-C10 还是 VR LOOKBON，
 * 最终都归一到这个结构体，后续处理（舵机映射/UART 发送/GUI 显示）只认 ParsedInput。
 */
struct ParsedInput {
    uint8_t  joystickX = 128;   ///< 摇杆 X 轴 (0=左, 128=中位, 255=右)
    uint8_t  joystickY = 128;   ///< 摇杆 Y 轴 (0=上, 128=中位, 255=下)
    uint32_t buttons   = 0;     ///< 按键位掩码 (bit0=上, bit1=下, bit2=左, bit3=右, bit6=A)
};

/// 手柄类型
enum class HandleType {
    None,
    CodexPad_C10,   ///< Service 0xFFA0, Char 0xFFA1 Notify, 8字节二进制帧
    Lookbon_VR,     ///< Service AE30, Char AE02 Notify, 2字节 ASCII 字符串
};

/**
 * @brief 解析 CodexPad-C10 手柄的 8 字节 Notify 数据
 * @param data 原始字节数组（必须 8 字节）
 * @return 标准化输入
 */
ParsedInput parseCodexPad(const uint8_t* data, size_t len);

/**
 * @brief 解析 VR LOOKBON 手柄的 2 字节 Notify 数据
 * @param data 原始字节数组（必须 2 字节）
 * @return 标准化输入
 */
ParsedInput parseLookbon(const uint8_t* data, size_t len);

/**
 * @brief 根据手柄类型调用对应解析器
 * @param type 手柄类型
 * @param data 原始数据
 * @param len 数据长度
 * @return 标准化输入
 */
ParsedInput parseInput(HandleType type, const uint8_t* data, size_t len);