#pragma once

// ============================================================
// WiFi 名称和密码在 include/secrets.h，这个文件不进入 Git。
// 换电脑时复制 include/secrets.example.h 为 include/secrets.h 再填写。
// ============================================================
#include "secrets.h"

// ============================================================
// 接线（ESP32-C3 Super Mini  <->  TM1637 四位数码管）
//
//   数码管 CLK  ->  GPIO3（紫线）
//   数码管 DIO  ->  GPIO4（蓝线）
//   数码管 VCC  ->  3V3（绿线）
//   数码管 GND  ->  GND（黄线）
//
// 模块供电范围是 3.3V～5.5V，亮度 0～7 共 8 档。
// 接到 3V3，信号电平和 ESP32-C3 一致，最稳妥。
// 不要把 5V 接到板子的 3V3 脚。长期供电用 USB-C，或把 5V 电源接到 5V 脚。
// GPIO8 是板载蓝色 LED（低电平点亮），GPIO9 是 BOOT 键，都不要拿来接数码管。
// ============================================================
#define PIN_CLK 3
#define PIN_DIO 4
#define PIN_STATUS_LED 8

// 中国标准时间，无夏令时。POSIX 时区写法里 CST-8 表示 UTC+8。
#define TIMEZONE "CST-8"
#define NTP_SERVER_1 "ntp.aliyun.com"
#define NTP_SERVER_2 "ntp.tencent.com"
#define NTP_SERVER_3 "cn.pool.ntp.org"

// 一直通电，按这个间隔重复向 NTP 对时，抵消晶振漂移。
#define NTP_RESYNC_MS (60UL * 60UL * 1000UL)
#define WIFI_RETRY_MS 15000UL

// 还没拿到时间时使用的亮度，0 最暗，7 最亮。
#define BOOT_BRIGHTNESS 3

// 每天各播放一次，时长 30 秒。24 点就是 0 点。
// 上班动画用最亮；睡觉动画自己慢慢变暗，播完后午夜仍然关闭数码管。
#define ANIM_DURATION_MS 30000UL
#define WORK_ANIM_HOUR 9
#define WORK_ANIM_MINUTE 0
#define WORK_ANIM_BRIGHTNESS 7
#define SLEEP_ANIM_HOUR 0
#define SLEEP_ANIM_MINUTE 0

// 0 最暗但仍点亮，7 最亮。BRIGHTNESS_OFF 表示这段时间关闭数码管。
static const uint8_t BRIGHTNESS_OFF = 0xFF;

// 从该时刻起使用这一档，直到下一条。可按房间光线改数字。
struct BrightnessSlot {
  uint8_t hour;
  uint8_t minute;
  uint8_t level;
};

static const BrightnessSlot BRIGHTNESS_TABLE[] = {
    {0, 0, BRIGHTNESS_OFF},  // 00:00–02:00 关闭
    {2, 0, 0},               // 02:00 最暗，仍然点亮
    {6, 0, 1},               // 06:00 黎明
    {7, 0, 2},               // 07:00
    {8, 0, 4},               // 08:00 早晨
    {10, 0, 7},              // 10:00 白天最亮
    {17, 0, 5},              // 17:00 傍晚
    {19, 0, 3},              // 19:00
    {21, 0, 1},              // 21:00 夜间，仍然显示
};

static const size_t BRIGHTNESS_COUNT =
    sizeof(BRIGHTNESS_TABLE) / sizeof(BRIGHTNESS_TABLE[0]);
