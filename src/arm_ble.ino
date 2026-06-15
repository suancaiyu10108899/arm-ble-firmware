/**
 * Arduino IDE 入口文件
 *
 * 使用方法：
 *   1. 将整个 src/ 目录作为 Arduino 项目文件夹打开
 *   2. 安装 "Adafruit nRF52 Boards" 板子包
 *   3. 选择开发板 "Adafruit Feather nRF52840 Express"
 *   4. 库管理器安装 "Adafruit Bluefruit nRF52 Libraries"
 *   5. 编译上传
 *
 * 注意：推荐使用 PlatformIO 命令行（见 docs/开发环境搭建指南.md）
 */

// 实测：Arduino IDE 能自动识别同目录下 main.cpp 的 setup()/loop()
// 以及 handle_parser.h/cpp，直接打开本文件编译即可。
// 需要先安装 "Adafruit nRF52 Boards" 板子包和 "Adafruit Bluefruit nRF52 Libraries"。
