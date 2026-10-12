/*
 * Beacon Lab: ESP32-C3 + SSD1306 72x40 OLED.
 * Two LAB identities alternate on ONE BLE advertiser/address.
 * Edit BeaconConfig.h to adjust RF power, payload identities and timing.
 * Arduino-ESP32 3.3.12 / built-in NimBLE; U8g2 2.36.19.
 * BOOT short press = toggle; Serial: 115200 baud, s = stop, r = resume, i = info.
 * OLED shows state, full UUID, major/minor and power settings.
 * Verify reception on a second device.
 */
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <atomic>
#include <stdarg.h>
#include "BeaconConfig.h"

#if !defined(CONFIG_NIMBLE_ENABLED)
#error "This sketch targets the ESP32-C3 NimBLE backend in Arduino-ESP32 3.3.12."
#endif

namespace cfg = BeaconConfig;

U8G2_SSD1306_72X40_ER_F_HW_I2C oled(
    U8G2_R0, U8X8_PIN_NONE, cfg::OLED_SCL, cfg::OLED_SDA);

struct LabProfile {
  uint8_t uuid[16];
  uint16_t major;
  uint16_t minor;
  int8_t measuredPower;
};

// Validated binary profiles derived from the text configuration at startup.
LabProfile profiles[2] = {};

uint8_t payloads[2][30];
BLEAdvertising *advertising = nullptr;
bool oledPresent = false;
bool profilesReady = false;
bool running = false;
const char *displayState = "BOOT";
const char *lastError = nullptr;
std::atomic<int> bleInitResult{0};  // 0 = pending, 1 = success, -1 = failure.
constexpr uint32_t BLE_INIT_TIMEOUT_MS = 10000;
uint8_t activeProfile = 0;
uint32_t dwellStarted = 0;
uint8_t bootRawLevel = HIGH;
uint8_t bootStableLevel = HIGH;
uint32_t bootRawChanged = 0;
bool bootArmed = false;

void printDiagnostics();
void fatalError(const char *reason, const char *screenCode = "ERROR", bool cleanupBle = true);

// USB logs are optional. A host that is not reading must not stall the OLED,
// button handling or advertising. Drop a whole message if the TX queue is full.
void logPrintf(const char *format, ...) {
  if (!Serial) return;
  char message[192];
  va_list arguments;
  va_start(arguments, format);
  const int length = vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  if (length <= 0 || size_t(length) >= sizeof(message)) return;
  if (Serial.availableForWrite() < length + 8) return;  // Ring-buffer overhead.
  Serial.write(reinterpret_cast<const uint8_t *>(message), size_t(length));
}

void formatUuid(uint8_t id, char *out) {
  const char hex[] = "0123456789abcdef";
  uint8_t position = 0;
  for (uint8_t i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) out[position++] = '-';
    out[position++] = hex[profiles[id].uuid[i] >> 4];
    out[position++] = hex[profiles[id].uuid[i] & 0x0f];
  }
  out[position] = '\0';
}

void drawStatus(const char *state) {
  displayState = state;
  if (!oledPresent) return;
  char line[24];
  char uuid[37];
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x7_tf);
  if (!profilesReady) {
    oled.drawStr(0, 7, state);
    oled.setFont(u8g2_font_4x6_tf);
    oled.drawStr(0, 19, lastError ? "Check config" : "Loading config");
    oled.drawStr(0, 31, "Not advertising");
    oled.sendBuffer();
    return;
  }
  formatUuid(activeProfile, uuid);
  if (strcmp(state, "ON") == 0 || strcmp(state, "OFF") == 0) {
    snprintf(line, sizeof(line), "%c %s %ums", 'A' + activeProfile, state,
             unsigned(cfg::ADV_INTERVAL_MS));
  } else {
    snprintf(line, sizeof(line), "%c %s", 'A' + activeProfile, state);
  }
  if (oled.getStrWidth(line) > 72) oled.setFont(u8g2_font_4x6_tf);
  oled.drawStr(0, 7, line);
  // Six rows fit the 72x40 panel. The complete 36-character UUID uses 3 rows.
  oled.setFont(u8g2_font_4x6_tf);
  for (uint8_t row = 0; row < 3; ++row) {
    memcpy(line, uuid + row * 12, 12);
    line[12] = '\0';
    oled.drawStr(0, 13 + row * 6, line);
  }
  snprintf(line, sizeof(line), "Maj%u Min%u",
           profiles[activeProfile].major, profiles[activeProfile].minor);
  oled.drawStr(0, 31, line);
  snprintf(line, sizeof(line), "MP:%d Tx:%ddBm",
           profiles[activeProfile].measuredPower, cfg::TX_POWER_DBM);
  oled.drawStr(0, 37, line);
  oled.sendBuffer();
}

void initOled() {
  Wire.begin(cfg::OLED_SDA, cfg::OLED_SCL);
  Wire.setTimeOut(100);
  for (uint8_t addr = 0x3c; addr <= 0x3d; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      oled.setI2CAddress(addr << 1);  // U8g2 uses the 8-bit I2C address.
      oled.setBusClock(100000);
      oled.begin();
      oledPresent = true;
      drawStatus("CFG LOAD");
      logPrintf("OLED found at 0x%02X, SDA=%u SCL=%u\n", addr, cfg::OLED_SDA, cfg::OLED_SCL);
      return;
    }
  }
  logPrintf("No OLED ACK at 0x3C/0x3D. BLE will still run; check pins/controller.\n");
}

void fatalError(const char *reason, const char *screenCode, bool cleanupBle) {
  lastError = reason;
  running = false;
  drawStatus(screenCode);  // Display the error before logging or BLE cleanup.
  logPrintf("ERROR: %s. Reset after fixing the error.\n", reason);
  // A timed-out initialization task may still own BLE resources. Let it finish;
  // never deinitialize concurrently with it, and never start an advertiser.
  if (cleanupBle && BLEDevice::getInitialized()) BLEDevice::deinit();
  while (true) {
    while (Serial.available() > 0) {
      const char command = Serial.read();
      if (command == 'i' || command == 'I') printDiagnostics();
    }
    delay(20);
  }
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseUuid(const char *text, uint8_t *out) {
  if (text == nullptr || strlen(text) != 36) return false;
  uint8_t byte = 0;
  for (uint8_t position = 0; position < 36;) {
    if (position == 8 || position == 13 || position == 18 || position == 23) {
      if (text[position++] != '-') return false;
      continue;
    }
    const int high = hexNibble(text[position]);
    const int low = hexNibble(text[position + 1]);
    if (high < 0 || low < 0) return false;
    out[byte++] = uint8_t((high << 4) | low);
    position += 2;
  }
  return byte == 16;
}

void loadProfiles() {
  for (uint8_t id = 0; id < 2; ++id) {
    profiles[id].major = cfg::PROFILES[id].major;
    profiles[id].minor = cfg::PROFILES[id].minor;
    profiles[id].measuredPower = cfg::PROFILES[id].measuredPower;
    if (!parseUuid(cfg::PROFILES[id].uuid, profiles[id].uuid)) {
      activeProfile = id;
      logPrintf("Profile %c: UUID must have format xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx.\n", 'A' + id);
      fatalError("Invalid UUID in BeaconConfig.h", "CFG ERROR");
    }
  }
  profilesReady = true;
  drawStatus("CFG OK");
}

void bleInitTask(void *) {
  const bool ok = BLEDevice::init("");
  bleInitResult.store(ok ? 1 : -1, std::memory_order_release);
  vTaskDelete(nullptr);
}

void initBleWithTimeout() {
  drawStatus("BLE WAIT");
  // The installed NimBLE implementation waits for host synchronization inside
  // init(). Keep the UI task responsive enough to report a missing sync.
  if (xTaskCreate(bleInitTask, "beacon-ble-init", 6144, nullptr, 1, nullptr) != pdPASS) {
    fatalError("Cannot create BLE initialization task", "BLE TASK", false);
  }
  const uint32_t started = millis();
  while (bleInitResult.load(std::memory_order_acquire) == 0) {
    if (uint32_t(millis() - started) >= BLE_INIT_TIMEOUT_MS) {
      fatalError("BLE initialization exceeded 10 seconds", "BLE TIMEOUT", false);
    }
    delay(20);
  }
  if (bleInitResult.load(std::memory_order_acquire) < 0) {
    fatalError("BLE initialization failed", "BLE ERROR", false);
  }
}

void configureTxPower() {
  // Use the advertising-specific setter so failures can be reported explicitly.
  const esp_err_t result = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, cfg::txPowerLevel());
  if (result != ESP_OK) {
    logPrintf("RF power request %d dBm: %s\n", cfg::TX_POWER_DBM, esp_err_to_name(result));
    fatalError("Cannot configure advertising RF power");
  }
  logPrintf("RF power request: %d dBm | interval: %lu ms (%u x 0.625 ms) | dwell: %lu ms\n",
                cfg::TX_POWER_DBM, (unsigned long)cfg::ADV_INTERVAL_MS,
                cfg::ADV_INTERVAL_UNITS, (unsigned long)cfg::DWELL_MS);
}

void buildPayload(uint8_t id) {
  uint8_t *out = payloads[id];
  const uint8_t prefix[9] = {0x02, 0x01, 0x06, 0x1a, 0xff, 0x4c, 0x00, 0x02, 0x15};
  memcpy(out, prefix, sizeof(prefix));
  memcpy(out + 9, profiles[id].uuid, 16);
  out[25] = profiles[id].major >> 8;
  out[26] = profiles[id].major & 0xff;
  out[27] = profiles[id].minor >> 8;
  out[28] = profiles[id].minor & 0xff;
  out[29] = uint8_t(profiles[id].measuredPower);
}

void printDiagnostics() {
  if (profilesReady) {
    char uuidA[37], uuidB[37];
    formatUuid(0, uuidA);
    formatUuid(1, uuidB);
    // Keep the complete configuration in one message, small enough for the
    // default 256-byte USB TX queue. A later 'i' recovers skipped startup logs.
    logPrintf("State=%s Profile=%c Run=%u Tx=%d\nA %s Maj%u Min%u MP%d\nB %s Maj%u Min%u MP%d\n",
              displayState, 'A' + activeProfile, unsigned(running), cfg::TX_POWER_DBM,
              uuidA, profiles[0].major, profiles[0].minor, profiles[0].measuredPower,
              uuidB, profiles[1].major, profiles[1].minor, profiles[1].measuredPower);
  } else {
    logPrintf("State=%s Profile=%c | configuration not ready\n", displayState, 'A' + activeProfile);
  }
  if (lastError) logPrintf("ERROR: %s\n", lastError);
}

void startProfile(uint8_t id) {
  activeProfile = id;
  BLEAdvertisementData data;
  // Supply an explicit length: UUIDs and other fields can contain zero bytes.
  data.addData(reinterpret_cast<char *>(payloads[id]), sizeof(payloads[id]));
  if (!advertising->setAdvertisementData(data)) fatalError("Set payload failed");
  if (!advertising->start()) fatalError("Start advertising failed");
  if (!advertising->isAdvertising()) fatalError("Advertiser is inactive after start");
  running = true;
  dwellStarted = millis();
  drawStatus("ON");
  logPrintf("LAB %c active (host/controller startup confirmed).\n", 'A' + id);
}

void stopAdvertising() {
  if (!advertising->stop()) fatalError("Stop advertising failed");
  if (advertising->isAdvertising()) fatalError("Advertiser is still active after stop");
  running = false;
  drawStatus("OFF");
  logPrintf("Advertising stopped.\n");
}

void initBootButton() {
  pinMode(cfg::LAB_BOOT_PIN, INPUT_PULLUP);
  bootRawLevel = digitalRead(cfg::LAB_BOOT_PIN);
  bootStableLevel = bootRawLevel;
  bootRawChanged = millis();
  bootArmed = (bootStableLevel == HIGH);  // A held button must be released first.
}

void updateBootButton() {
  const uint32_t now = millis();
  const uint8_t raw = digitalRead(cfg::LAB_BOOT_PIN);
  if (raw != bootRawLevel) {
    bootRawLevel = raw;
    bootRawChanged = now;
  }
  if (uint32_t(now - bootRawChanged) < cfg::DEBOUNCE_MS || raw == bootStableLevel) return;
  bootStableLevel = raw;
  if (bootStableLevel == HIGH) {
    bootArmed = true;
  } else if (bootArmed) {
    bootArmed = false;  // One toggle per press, even if the button stays held.
    logPrintf("BOOT: toggle advertising\n");
    if (running) stopAdvertising();
    else startProfile(activeProfile);
  }
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);  // USB CDC writes must never wait for the host.
#endif
  delay(800);  // Bounded power/USB settling; no wait for a terminal connection.
  logPrintf("\nESP32-C3 Beacon Lab | ONE address, A/B alternating | BOOT=toggle s=stop r=resume i=info\n");
  initBootButton();
  initOled();
  loadProfiles();
  buildPayload(0);
  buildPayload(1);
  initBleWithTimeout();
  drawStatus("AD SETUP");
  logPrintf("BLE address: %s\n", BLEDevice::getAddress().toString().c_str());
  configureTxPower();
  advertising = BLEDevice::getAdvertising();
  if (advertising == nullptr) fatalError("Cannot allocate advertiser");
  advertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
  advertising->setScanResponse(false);
  advertising->setMinInterval(cfg::ADV_INTERVAL_UNITS);
  advertising->setMaxInterval(cfg::ADV_INTERVAL_UNITS);
  startProfile(0);
  printDiagnostics();
}

void loop() {
  updateBootButton();
  while (Serial.available() > 0) {
    const char command = Serial.read();
    if ((command == 's' || command == 'S') && running) stopAdvertising();
    if ((command == 'r' || command == 'R') && !running) startProfile(activeProfile);
    if (command == 'i' || command == 'I') printDiagnostics();
  }
  if (running && !advertising->isAdvertising()) fatalError("Unexpected advertising stop");
  if (running && uint32_t(millis() - dwellStarted) >= cfg::DWELL_MS) {
    if (!advertising->stop()) fatalError("Stop before profile switch failed");
    running = false;
    startProfile(activeProfile ^ 1);
  }
  delay(20);
}
