# ESP32 iBeacon Lab

[English](README.md) | **简体中文**

ESP32-C3 Super Mini + 72×40 OLED iBeacon 实验固件。

Arduino 草图：`BeaconLabC3/BeaconLabC3.ino`；可调模块：`BeaconLabC3/BeaconConfig.h`。下文提供两组独立实验身份作为配置示例，用于另一台设备上的 BLE 扫描和解析实验。实际广播内容以配置模块为准。两个文件必须保存在同一草图目录中。

<img src="docs/images/board.jpg" alt="ESP32 和 OLED 运行画面" width="640">

## Arduino IDE

1. 开发板管理器安装 **esp32 by Espressif Systems**。本机检查版本为 **3.3.12**。
2. 库管理器安装 **U8g2 by oliver**。本机检查版本为 **2.36.19**。BLE 使用开发板包内置库。
3. 打开 `BeaconLabC3/BeaconLabC3.ino`；开发板选择 **ESP32C3 Dev Module**，**USB CDC On Boot → Enabled**，**Flash Mode → DIO**。其余保留默认，选择你的板子端口后编译、上传。
4. 上电后自动轮播 A、B，每组约 2 秒。**程序运行后短按 BOOT 停止，再短按恢复**；恢复时从停止前的组重新计时。按键有 35 ms 非阻塞消抖，按住只切换一次，稳定松开后才接受下一次按下。串口监视器设为 **115200**，发送 `s` 停止、`r` 恢复，发送 `i` 查看当前状态、两组配置及错误原因。程序不等待串口监视器；日志缓冲区满时丢弃日志。
5. 在手机的通用 BLE 扫描器或你自己的接收程序中，核对下面两组 UUID 和 major/minor。观察至少 10 秒，并启用重复广播/实时数据更新；按地址去重并只显示首次载荷的扫描器可能看不到切换。断电停止；再次上电恢复默认的自动广播行为。

## 调整功率和内容

在 Arduino IDE 的 **BeaconConfig.h** 标签中编辑配置，然后重新编译、上传。它是编译时配置模块，BOOT 仍负责开关广播。

```cpp
constexpr int TX_POWER_DBM = -6;           // 射频功率请求，示例：降低至 -6 dBm
constexpr uint32_t ADV_INTERVAL_MS = 100;  // 每组内部的广播间隔
constexpr uint32_t DWELL_MS = 2000;        // 每组驻留时间

constexpr Profile PROFILES[2] = {
  {"a1517af2-7aef-11ed-a1eb-0242ac120002", 675, 10004, -60}, // A, 这是有效的
  {"5f904a31-48db-4270-8dda-9b9b3df2c585", 202, 2, -60}  // B
};
```

本次交付默认射频设置仍为 **0 dBm**；上面 −6 dBm 是修改示例。

| 配置 | 含义与范围 |
|---|---|
| `TX_POWER_DBM` | 射频功率请求档位：−24、−21、−18、−15、−12、−9、−6、−3、0、3、6、9、12、15、18、20 |
| `PROFILES` 中第 1 项 | UUID 字符串，标准 `8-4-4-4-12` 格式；支持大小写，屏幕统一显示小写 |
| 第 2、3 项 | Major、Minor，各为 0～65535 |
| 第 4 项 | Measured Power 校准字节，−128～127；默认 −59 是示例值 |
| `ADV_INTERVAL_MS` | 100～10240 ms，换算到最接近的 0.625 ms 控制器单位 |
| `DWELL_MS` | 每组驻留时间，至少等于广播间隔 |

不支持的功率档位、超范围的间隔或字段会在编译时报错。UUID 格式错误会在上电初始化时显示 ERROR，并通过串口指出 A/B；不会启动广播。屏幕和二进制载荷都从同一配置生成，修改后无需另改显示文字。

射频功率通过 advertising 专用接口设置，若 API 返回失败，会显示 ERROR 并停止。功率设置是控制器请求，实际射频输出与接收 RSSI 仍需实测；C3 的功率有 3 dBm 步长，部分请求会被量化。接口依据：[Espressif 发射功率说明](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/bluetooth/controller_vhci.html)。Measured Power 独立表示 1 米 RSSI 校准值，调整它不会改变射频输出功率。

OLED 按 **SSD1306 72×40、SDA=GPIO5、SCL=GPIO6** 编写；这只是针对常见板型的假设，尚未核对你的板子原理图。代码自动探测 I²C 地址 `0x3C`、`0x3D`。引脚常量在 `BeaconConfig.h` 的硬件设置部分。屏幕无应答时 BLE 仍然继续，串口会打印原因。如果 I²C 有应答但显示错位或空白，需要核对实际控制器及板型。

BOOT 按常见 SuperMini 的 **GPIO9、按下为低电平** 接法编写，使用 `INPUT_PULLUP`；板型若不同，修改 `BeaconConfig.h` 中的 `LAB_BOOT_PIN`。GPIO9 在复位时还控制启动模式：正常使用开关功能，应等程序运行后再按；上电或复位时按住 BOOT 可能进入下载模式。依据：[NuttX SuperMini 按键说明](https://nuttx.incubator.apache.org/docs/latest/platforms/risc-v/esp32c3/boards/esp32c3-supermini/index.html#board-buttons)、[Espressif 启动模式说明](https://docs.espressif.com/projects/esptool/en/latest/esp32c3/advanced-topics/boot-mode-selection.html)。本次未实测你这块板子的按键接线。

## OLED 信息

六行同时显示当前状态和完整载荷身份，例如 A：

```text
A ON 100ms
59275ba7-ef3
d-41b2-84d4-
f3b34e493c5b
Maj101 Min1
MP:-59 Tx:0dBm
```

第一行的 A/B 是当前组，ON/OFF 是广播开关状态，100 ms 是配置的广播间隔。中间三行拼接为完整的 UUID；`Maj`/`Min` 为 major/minor。`MP` 是 iBeacon 校准功率字节，`Tx` 是请求的射频发射功率，两者都不是接收端测量的 RSSI。停止后显示 OFF，并保留最后一组信息供核对；切换到 B 时所有身份字段一同更新。

启动过程为 `CFG LOAD` → `A CFG OK` → `A BLE WAIT` → `A AD SETUP` → `A ON`。阶段可能很短，不一定能逐一看到。配置未就绪时只显示提示，不显示全零身份；配置校验完成后，启动阶段也显示真实字段。UUID 格式错误显示 `CFG ERROR`。蓝牙初始化失败显示 `BLE ERROR`，超过 10 秒显示 `BLE TIMEOUT`；其余错误显示 `ERROR`，串口 `i` 可查看具体原因。蓝牙超时后停在错误状态，不自动重试或开始广播。

## 接口来源

- [Espressif BLEAdvertising 接口](https://github.com/espressif/arduino-esp32/blob/master/libraries/BLE/src/BLEAdvertising.h)
- [Espressif BLEDevice 接口](https://github.com/espressif/arduino-esp32/blob/master/libraries/BLE/src/BLEDevice.h)
- [Espressif ESP32-C3 广播与扫描说明](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-guides/ble/get-started/ble-device-discovery.html)
- [U8g2 SSD1306 72×40 构造器](https://github.com/olikraus/u8g2/wiki/u8g2setupcpp#ssd1306-72x40_er)

开发依据还包括本机已安装的 Arduino-ESP32 **3.3.12** 头文件及 U8g2 **2.36.19**。

## 能不能再给力一点?

拥有ESP32的门槛还是太高了, 有没有仅需手机的方法🥺?

当然可以, 鼠子提供以下链接, 觉得好用的话可以关注收藏本仓库请小鼠喝杯咖啡喵^^: https://pan.baidu.com/s/1ED7q8PYqjpq8LYqcWpaskg?pwd=2333 
