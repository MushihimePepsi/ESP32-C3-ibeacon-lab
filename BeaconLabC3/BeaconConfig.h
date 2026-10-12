#pragma once
#include <stdint.h>
#include <esp_bt.h>

// 调整区：修改本文件，然后重新编译、上传；与 BeaconLabC3.ino 放在同一目录。
namespace BeaconConfig {

// 射频发射功率请求（dBm），不是 iBeacon 的 Measured Power 字节。
// 支持档位：-24,-21,-18,-15,-12,-9,-6,-3,0,3,6,9,12,15,18,20。
constexpr int TX_POWER_DBM = 0;

constexpr uint32_t ADV_INTERVAL_MS = 100;  // 广播间隔：100～10240 ms。
constexpr uint32_t DWELL_MS = 2000;       // 每组驻留时间，至少一个广播间隔。

struct Profile {
  const char *uuid;        // 标准 36 字符格式；支持大小写十六进制。
  uint16_t major;          // 0～65535。
  uint16_t minor;          // 0～65535。
  int8_t measuredPower;   // -128～127；应填写自己实验信标的 1 米 RSSI 校准值。
};

// 两组实验载荷：UUID、major、minor、Measured Power。
constexpr Profile PROFILES[2] = {
  {"a1517af2-7aef-11ed-a1eb-0242ac120002", 675, 10004, -60},  // A
  {"5f904a31-48db-4270-8dda-9b9b3df2c585", 202, 2, -60}   // B
};

// 硬件设置：通常无需改；按你的实际板型核对。
constexpr uint8_t OLED_SDA = 5;
constexpr uint8_t OLED_SCL = 6;
constexpr uint8_t LAB_BOOT_PIN = 9;  // 按下为低电平。
constexpr uint32_t DEBOUNCE_MS = 35;

// 以下为换算和检查，不需要随配置修改。
static_assert((TX_POWER_DBM >= -24 && TX_POWER_DBM <= 18 && TX_POWER_DBM % 3 == 0) ||
              TX_POWER_DBM == 20, "TX_POWER_DBM: use one of the documented C3 power levels.");
static_assert(ADV_INTERVAL_MS >= 100 && ADV_INTERVAL_MS <= 10240,
              "ADV_INTERVAL_MS: use 100..10240 ms for this legacy beacon sketch.");
static_assert(DWELL_MS >= ADV_INTERVAL_MS,
              "DWELL_MS must be at least ADV_INTERVAL_MS.");
static_assert(DEBOUNCE_MS > 0, "DEBOUNCE_MS must be positive.");

// C3 indices 0..14 correspond to -24..18 dBm; index 15 corresponds to +20 dBm.
constexpr esp_power_level_t txPowerLevel() {
  return TX_POWER_DBM == 20 ? ESP_PWR_LVL_P20
                          : static_cast<esp_power_level_t>((TX_POWER_DBM + 24) / 3);
}
// Round an integer millisecond interval to the nearest 0.625 ms controller unit.
constexpr uint16_t ADV_INTERVAL_UNITS = (ADV_INTERVAL_MS * 8UL + 2UL) / 5UL;

}  // namespace BeaconConfig
