# StopWatch Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an ESP-IDF 6.0 StopWatch firmware that boots every board peripheral and exposes a compact LVGL diagnostic UI.

**Architecture:** Reuse the hardware sequences and MIT drivers from M5Stack's pinned StopWatch UserDemo, while removing its business applications and UI framework. `board_hal` owns hardware and reports a snapshot; `ui` only renders that snapshot and invokes explicit diagnostics.

**Tech Stack:** ESP-IDF 6.0, C++20, LVGL 9.5.0, M5GFX 0.2.27, M5IOE1 1.0.8, M5PM1 1.0.6, ESP codec-dev 1.5.4.

**Spec:** `docs/superpowers/specs/2026-08-19-bajji-stopwatch-ble-ip-bridge-design.md`

## Global Constraints

- Target is ESP32-S3R8 with 16 MB flash and 8 MB octal PSRAM.
- Build with `/Users/eki/esp/esp-idf` at version 6.0.
- UI uses LVGL and contains diagnostics only.
- Hardware errors remain visible in the status snapshot; display failure retains serial logs.
- Calibration constants for battery voltage, motor duty, microphone gain, and display brightness stay named constants.
- Git dependencies are pinned; generated dependency and build directories remain ignored.

---

## File Map

- `device/CMakeLists.txt`: ESP-IDF project entry and vendor component path.
- `device/sdkconfig.defaults`: ESP32-S3, PSRAM, LVGL and memory settings.
- `device/partitions.csv`: NVS and single factory application layout.
- `device/deps.lock.json`, `device/tools/fetch_deps.py`, `device/patches/*.patch`: reproducible upstream checkout.
- `device/main/app_main.cpp`: startup and diagnostic refresh loop.
- `device/components/board_hal/include/board_hal.hpp`: public status and action API.
- `device/components/board_hal/*.cpp`: I2C/PMIC/IOE, display/touch/LVGL, audio/motor, sensors/buttons.
- `device/components/board_hal/drivers/`: pinned CST820 and RX8130 drivers from M5Stack.
- `device/components/ui/include/diagnostics_ui.hpp`, `device/components/ui/diagnostics_ui.cpp`: LVGL screen.
- `device/tests/host/test_board_math.cpp`: host check for physical calibration helpers.
- `device/tests/host/run.sh`: framework-free host check runner reused by later phases.

### Task 1: Reproducible ESP-IDF shell

**Files:**
- Create: `device/CMakeLists.txt`
- Create: `device/sdkconfig.defaults`
- Create: `device/partitions.csv`
- Create: `device/deps.lock.json`
- Create: `device/tools/fetch_deps.py`
- Create: `device/patches/M5IOE1.patch`
- Create: `device/patches/M5PM1.patch`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: ESP-IDF in `/Users/eki/esp/esp-idf`.
- Produces: `python3 tools/fetch_deps.py` and an IDF project named `bajji_stopwatch`.

- [ ] **Step 1: Record exact upstream refs**

```json
{
  "M5GFX": ["https://github.com/m5stack/M5GFX.git", "93b480bb349749202c8a2a953065c8ae95f58320"],
  "lvgl": ["https://github.com/lvgl/lvgl.git", "85aa60d18b3d5e5588d7b247abf90198f07c8a63"],
  "M5IOE1": ["https://github.com/m5stack/M5IOE1.git", "37db04861687858b6748f5dbd1a84439a6635c46"],
  "M5PM1": ["https://github.com/m5stack/M5PM1.git", "8f1f1a60b3040088cd0ca2b9eb9d024ccc4d907f"],
  "BMI270_BMM150_Sensor": ["https://github.com/lbuque/BMI270_BMM150_Sensor.git", "ae8bc480bca638d3458e65d5e9a4fff4270c1430"]
}
```

- [ ] **Step 2: Implement idempotent dependency fetch**

```python
subprocess.run(["git", "clone", "--filter=blob:none", url, str(dst)], check=True)
subprocess.run(["git", "-C", str(dst), "checkout", "--detach", ref], check=True)
subprocess.run(["git", "-C", str(dst), "submodule", "update", "--init", "--recursive"], check=True)
subprocess.run(["git", "-C", str(dst), "apply", "--check", str(patch)], check=True)
subprocess.run(["git", "-C", str(dst), "apply", str(patch)], check=True)
```

The script rejects an existing checkout whose `HEAD` differs from the resolved lock ref, rather than silently updating it.
The two patches preserve the official UserDemo's I2C compatibility changes. M5GFX 0.2.27 is used directly because its IDF 6 driver split fixes have been verified locally.

- [ ] **Step 3: Add minimum IDF configuration**

```ini
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_FREERTOS_HZ=1000
CONFIG_LV_COLOR_DEPTH_16=y
```

- [ ] **Step 4: Verify dependency bootstrap and empty configuration**

Run: `cd device && python3 tools/fetch_deps.py && source /Users/eki/esp/esp-idf/export.sh && idf.py set-target esp32s3 && idf.py reconfigure`

Expected: command exits 0 and reports ESP-IDF v6.0.

- [ ] **Step 5: Commit**

```bash
git add .gitignore device
git commit -m "build(device): add reproducible ESP-IDF foundation"
```

### Task 2: Board model and calibration checks

**Files:**
- Create: `device/components/board_hal/CMakeLists.txt`
- Create: `device/components/board_hal/include/board_hal.hpp`
- Create: `device/components/board_hal/board_math.hpp`
- Create: `device/tests/host/test_board_math.cpp`
- Create: `device/tests/host/run.sh`

**Interfaces:**
- Produces: `bajji::BoardStatus`, `bajji::BoardHal::init()`, `snapshot()`, `set_brightness()`, `vibrate()`, `play_tone()`, `shutdown()`.

- [ ] **Step 1: Write the failing host check**

```cpp
#include "board_math.hpp"
#include <cassert>
int main() {
    assert(bajji::battery_percent(3300) == 0);
    assert(bajji::battery_percent(4200) == 100);
    assert(bajji::motor_duty(0) == 0);
    assert(bajji::motor_duty(100) == 100);
}
```

- [ ] **Step 2: Verify failure**

Run: `c++ -std=c++20 -Idevice/components/board_hal device/tests/host/test_board_math.cpp -o /tmp/bajji-board-test`

Expected: FAIL because `board_math.hpp` does not exist.

- [ ] **Step 3: Add bounded calibration helpers and public status types**

```cpp
constexpr uint8_t battery_percent(uint16_t mv) {
    constexpr uint16_t empty_mv = 3300;
    constexpr uint16_t full_mv = 4200;
    return mv <= empty_mv ? 0 : mv >= full_mv ? 100 :
        static_cast<uint8_t>((mv - empty_mv) * 100U / (full_mv - empty_mv));
}
constexpr uint8_t motor_duty(uint8_t strength) {
    return strength == 0 ? 0 : static_cast<uint8_t>(25U + (std::min<unsigned>(strength, 100U) * 75U) / 100U);
}
```

- [ ] **Step 4: Verify host check**

Run: `c++ -std=c++20 -Idevice/components/board_hal device/tests/host/test_board_math.cpp -o /tmp/bajji-board-test && /tmp/bajji-board-test`

Expected: exit 0.

`run.sh` compiles the same command with `-Wall -Wextra -Werror`, runs the binary from a temporary directory, and later phases append their checks to this runner.

- [ ] **Step 5: Commit**

```bash
git add device/components/board_hal device/tests/host
git commit -m "feat(device): define StopWatch board HAL"
```

### Task 3: Bring up StopWatch hardware

**Files:**
- Create: `device/components/board_hal/board_hal.cpp`
- Create: `device/components/board_hal/display_touch.cpp`
- Create: `device/components/board_hal/power_io.cpp`
- Create: `device/components/board_hal/audio_motor.cpp`
- Create: `device/components/board_hal/sensors_buttons.cpp`
- Create: `device/components/board_hal/drivers/cst820.{h,cpp}`
- Create: `device/components/board_hal/drivers/rx8130.{h,cpp}`

**Interfaces:**
- Consumes: exact StopWatch pins and IOE lines from the design spec.
- Produces: initialized `BoardHal` with per-subsystem `Health::{ok,error,unavailable}`.

- [ ] **Step 1: Port only the required MIT hardware paths**

Use M5Stack UserDemo commit `6b4aa125288b6fe9dca661f10159f6e1e5ee785c` as source. Preserve its SPDX/MIT notices. Keep CO5300 QSPI pins 39/40/38/41/42/46/45, system I2C pins 47/48, touch interrupt 13, keys 2/1, I2S pins 18/17/16/15/21, and IOE power/reset lines 3/4/5/8/9/10.

```cpp
Health BoardHal::init() {
    init_nvs();
    init_i2c();
    init_pmic();
    init_io_expander();
    init_display_touch();
    init_audio_motor();
    init_imu_rtc_buttons();
    return snapshot().required_health();
}
```

- [ ] **Step 2: Keep noncritical failures observable**

```cpp
if (ioe_result != M5IOE1_OK) {
    status_.io_expander = Health::error;
    ESP_LOGE(TAG, "M5IOE1 unavailable");
} else {
    status_.io_expander = Health::ok;
}
```

- [ ] **Step 3: Build with ESP-IDF 6.0**

Run: `cd device && source /Users/eki/esp/esp-idf/export.sh && idf.py build`

Expected: `Project build complete` with no compiler error.

- [ ] **Step 4: Commit**

```bash
git add device/components/board_hal
git commit -m "feat(device): bring up StopWatch peripherals"
```

### Task 4: LVGL diagnostic UI

**Files:**
- Create: `device/components/ui/CMakeLists.txt`
- Create: `device/components/ui/include/diagnostics_ui.hpp`
- Create: `device/components/ui/diagnostics_ui.cpp`
- Create: `device/main/CMakeLists.txt`
- Create: `device/main/idf_component.yml`
- Create: `device/main/app_main.cpp`

**Interfaces:**
- Consumes: `BoardHal::snapshot()` and explicit action methods.
- Produces: `DiagnosticsUI::create()` and `DiagnosticsUI::refresh(const BoardStatus&)`.

- [ ] **Step 1: Build one scrollable screen**

```cpp
void DiagnosticsUI::create() {
    root_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(root_, 466, 466);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    power_ = add_row(root_, "Power");
    display_touch_ = add_row(root_, "Display / Touch");
    audio_motor_ = add_row(root_, "Audio / Motor");
    imu_rtc_ = add_row(root_, "IMU / RTC");
    ble_ = add_row(root_, "BLE: waiting");
    bridge_ = add_row(root_, "Bridge: offline");
}
```

- [ ] **Step 2: Wire touch/buttons to diagnostics**

Button A plays a short tone and vibrates; Button B cycles brightness; the UI exposes shutdown and clear-bond buttons with a confirmation dialog.

```cpp
if (hal.button_a_pressed()) { hal.play_tone(880, 80); hal.vibrate(80, 60); }
if (hal.button_b_pressed()) { hal.set_brightness((hal.brightness() + 20) % 100); }
```

- [ ] **Step 3: Build complete foundation**

Run: `cd device && source /Users/eki/esp/esp-idf/export.sh && idf.py build`

Expected: exit 0 and firmware binaries in `device/build/`.

- [ ] **Step 4: Commit**

```bash
git add device
git commit -m "feat(device): add LVGL hardware diagnostics"
```
