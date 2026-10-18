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
enum AnimKind { ANIM_NONE = 0, ANIM_WORK, ANIM_SLEEP };

static AnimKind animKind = ANIM_NONE;
static uint32_t animStart = 0;
static int workAnimYday = -1;
static int sleepAnimYday = -1;

static const uint8_t GLYPH_G = SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G;
static const uint8_t GLYPH_O = SEG_A | SEG_F | SEG_B | SEG_G;
static const uint8_t kStrokes[] = {SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F};
static const uint8_t STROKE_COUNT = 6;
static const uint8_t RING = SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F;
static const uint32_t STROKE_MS = 200;
static const uint32_t FLASH_SLOT_MS = 400;
static const uint8_t FLASH_TIMES = 3;
static const uint32_t SLEEP_BUILD_MS = 4UL * STROKE_COUNT * STROKE_MS;
static const uint32_t SLEEP_ROUND_MS = SLEEP_BUILD_MS + FLASH_TIMES * FLASH_SLOT_MS;

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
  if (level != BRIGHTNESS_OFF && level > 7) {
    level = 7;
  }
  return level;
}

static void applyBrightness(uint8_t level) {
  if (level == appliedBrightness) {
    return;
  }
  appliedBrightness = level;
  shownValue = -1;
  shownColon = -1;
  if (level == BRIGHTNESS_OFF) {
    display.setBrightness(0, false);
    display.clear();
    Serial.println("亮度档位: 关闭");
    return;
  }
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

static void paint(const uint8_t segs[4]) {
  shownValue = -1;
  shownColon = -1;
  display.setSegments(segs);
}

static void renderWorkFrame(uint32_t elapsed) {
  uint8_t segs[4] = {0, 0, 0, 0};
  display.setBrightness(WORK_ANIM_BRIGHTNESS, true);
  if ((elapsed / 500) % 2 == 0) {
    segs[0] = GLYPH_G;
    segs[1] = GLYPH_O | SEG_DP;
    segs[2] = GLYPH_G;
    segs[3] = GLYPH_O;
  }
  paint(segs);
}

static void renderSleepFrame(uint32_t elapsed) {
  const uint32_t cycle = elapsed % SLEEP_ROUND_MS;
  uint8_t segs[4] = {0, 0, 0, 0};
  display.setBrightness(4, true);

  if (cycle < SLEEP_BUILD_MS) {
    const uint32_t step = cycle / STROKE_MS;
    const uint8_t digit = step / STROKE_COUNT;
    const uint8_t strokesOn = step % STROKE_COUNT;
    uint8_t partial = 0;
    for (uint8_t i = 0; i <= strokesOn; i++) {
      partial |= kStrokes[i];
    }
    for (uint8_t d = 0; d < digit; d++) {
      segs[d] = RING;
    }
    segs[digit] = partial;
  } else if (((cycle - SLEEP_BUILD_MS) % FLASH_SLOT_MS) < FLASH_SLOT_MS / 2) {
    segs[0] = segs[1] = segs[2] = segs[3] = RING;
  }

  paint(segs);
}

static void beginAnim(AnimKind kind) {
  animKind = kind;
  animStart = millis();
  appliedBrightness = 0xFE;
  Serial.println(kind == ANIM_WORK ? "上班提醒" : "睡觉提醒");
  if (kind == ANIM_WORK) {
    renderWorkFrame(0);
  } else {
    renderSleepFrame(0);
  }
}

static bool startScheduledAnim(const struct tm &t) {
  if (animKind != ANIM_NONE) {
    return false;
  }
  if (t.tm_hour == WORK_ANIM_HOUR && t.tm_min == WORK_ANIM_MINUTE &&
      t.tm_yday != workAnimYday) {
    workAnimYday = t.tm_yday;
    beginAnim(ANIM_WORK);
    return true;
  }
  if (t.tm_hour == SLEEP_ANIM_HOUR && t.tm_min == SLEEP_ANIM_MINUTE &&
      t.tm_yday != sleepAnimYday) {
    sleepAnimYday = t.tm_yday;
    beginAnim(ANIM_SLEEP);
    return true;
  }
  return false;
}

static bool playScheduledAnim() {
  if (animKind == ANIM_NONE) {
    return false;
  }
  const uint32_t elapsed = millis() - animStart;
  if (elapsed >= ANIM_DURATION_MS) {
    animKind = ANIM_NONE;
    appliedBrightness = 0xFF;
    shownValue = -1;
    shownColon = -1;
    return false;
  }
  if (animKind == ANIM_WORK) {
    renderWorkFrame(elapsed);
  } else {
    renderSleepFrame(elapsed);
  }
  return true;
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
    if (playScheduledAnim() || startScheduledAnim(nowTm)) {
      delay(100);
      return;
    }
    const uint8_t level = brightnessFor(nowTm.tm_hour, nowTm.tm_min);
    applyBrightness(level);
    if (level != BRIGHTNESS_OFF) {
      showTime(nowTm, (nowTm.tm_sec % 2) == 0);
    }
  } else {
    setStatusLed((millis() / 400) % 2 == 0);
    applyBrightness(BOOT_BRIGHTNESS);
    showWaiting();
  }

  delay(100);
}
