# AI Passport UI host test

This test runs the production Passport UI against real LVGL with stubbed hardware and
services. It checks every visible object stays inside 240x320, displayed glyphs exist,
physical-button navigation reaches the menus, unpairing requires confirmation, portal
start/stop works, JPEG decode produces an image widget, and cover/fit render differently.
The synthetic JPEG fixture is generated for this repository and contains no external art.

After fetching `device/vendor`, run from the repository root:

```sh
cmake -S device/tests/passport_ui -B device/build-passport-ui -G Ninja
cmake --build device/build-passport-ui
./device/build-passport-ui/passport_ui_test /tmp/bajji-passport-ui
```

The output directory must be absolute. The executable writes PPM screen captures there.
These are host renders, not photographs of the device; ADC thresholds, DMA completion,
real display orientation, radio coexistence and runtime heap still require hardware.
