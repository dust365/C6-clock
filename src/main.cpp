#include <Arduino.h>
#include <TM1637Display.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

#include "config.h"

TM1637Display display(PIN_CLK, PIN_DIO);

static bool ntpStarted = false;
static uint32_t wifiAttemptAt = 0;
static uint8_t appliedBrightness = 0xFF;
static int shownValue = -1;
static int shownColon = -1;

static void setStatusLed(bool on) {
  digitalWrite(PIN_STATUS_LED, on ? LOW : HIGH);
}

static uint8_t brightnessFor(uint8_t hour, uint8_t minute) {
  const uint16_t nowMin = hour * 60 + minute;
  uint8_t level = BRIGHTNESS_TABLE[0].level;
  for (size_t i = 0; i < BRIGHTNESS_COUNT; i++) {
    const uint16_t slot =
        BRIGHTNESS_TABLE[i].hour * 60 + BRIGHTNESS_TABLE[i].minute;
    if (nowMin >= slot) {
      level = BRIGHTNESS_TABLE[i].level;
    }
  }
  if (level > 7) {
    level = 7;
  }
  return level;
}

static void applyBrightness(uint8_t level) {
  if (level == appliedBrightness) {
    return;
  }
  appliedBrightness = level;
  display.setBrightness(level, true);
  Serial.printf("亮度档位: %u\n", level);
}

static void showTime(const struct tm &t, bool colonOn) {
  const int value = t.tm_hour * 100 + t.tm_min;
  const int colon = colonOn ? 1 : 0;
  if (value == shownValue && colon == shownColon) {
    return;
  }
  shownValue = value;
  shownColon = colon;
  // 第二位的小数点在这块模块上就是中间的冒号。
  display.showNumberDecEx(value, colonOn ? 0x40 : 0x00, true);
}

static void showWaiting() {
  uint8_t segs[4] = {SEG_G, SEG_G, SEG_G, SEG_G};
  if ((millis() / 500) % 2 == 0) {
    segs[1] |= 0x80;
  }
  shownValue = -1;
  shownColon = -1;
  display.setSegments(segs);
}

static void onTimeSync(struct timeval *tv) {
  (void)tv;
  Serial.println("NTP 校时成功");
}

static void startNtp() {
  if (ntpStarted) {
    return;
  }
  sntp_set_time_sync_notification_cb(onTimeSync);
  configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  sntp_set_sync_interval(NTP_RESYNC_MS);
  ntpStarted = true;
  Serial.println("已启动 NTP，之后每小时自动校时");
}

static void connectWifi() {
  Serial.printf("连接 WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiAttemptAt = millis();
}

static void maintainWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    startNtp();
    return;
  }
  const uint32_t now = millis();
  if (wifiAttemptAt != 0 && now - wifiAttemptAt < WIFI_RETRY_MS) {
    return;
  }
  connectWifi();
}

static bool readTime(struct tm &out) {
  const time_t now = time(nullptr);
  // 2023-11-14 之前视为还没有完成校时。
  if (now < 1700000000) {
    return false;
  }
  localtime_r(&now, &out);
  return true;
}

static void startupSweep() {
  const uint8_t full[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  for (uint8_t level = 0; level <= 7; level++) {
    display.setBrightness(level, true);
    display.setSegments(full);
    delay(160);
  }
  appliedBrightness = 0xFF;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("ESP32-C3 Super Mini 时钟启动");

  pinMode(PIN_STATUS_LED, OUTPUT);
  setStatusLed(true);

  display.clear();
  startupSweep();
  applyBrightness(BOOT_BRIGHTNESS);
  showWaiting();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname("esp32c3-clock");

  if (strcmp(WIFI_SSID, "你的WiFi名称") == 0) {
    Serial.println("请先修改 include/config.h 中的 WIFI_SSID 和 WIFI_PASSWORD");
  }
  connectWifi();
}

void loop() {
  maintainWifi();

  struct tm nowTm;
  if (readTime(nowTm)) {
    setStatusLed(false);
    applyBrightness(brightnessFor(nowTm.tm_hour, nowTm.tm_min));
    showTime(nowTm, (nowTm.tm_sec % 2) == 0);
  } else {
    setStatusLed((millis() / 400) % 2 == 0);
    applyBrightness(BOOT_BRIGHTNESS);
    showWaiting();
  }

  delay(100);
}
