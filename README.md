# Smart Greenhouse (ESP-IDF)

This is the ESP-IDF version of my ESP32-S3 smart greenhouse project, rewritten in C. It monitors environmental conditions and automatically controls the fan, grow light, and watering system.

Watch the project demo video on YouTube:
https://youtu.be/kshUbCskHOQ?si=70YcUaTW5qpkAsB5

## Features

- Climate control: Uses temperature, humidity, and gas readings to control fan speed.
- Smart lighting: Adjusts light output based on ambient light and a daily schedule.
- Automatic irrigation: Monitors soil moisture and controls the water pump.
- Wi-Fi and time sync: Uses Wi-Fi and SNTP for the lighting schedule.
- Indicator: Uses an RGB LED to show system events and errors.

## Architecture

The firmware is divided into separate components for sensors, controllers, actuators, and the indicator. FreeRTOS tasks handle independent operations, while callbacks and task notifications support communication between components.

For example, the DHT22 sensor runs in its own task, and the irrigation controller uses a task notification to start an irrigation cycle when the soil is dry. This design separates sensor reading, control logic, and hardware actions, making the firmware easier to maintain and extend.

I moved from the Arduino version to ESP-IDF when the project needed a clearer way to organize independent operations and respond to sensor readings.

## Hardware

- ESP32-S3 development board
- DHT22 - temperature and humidity
- BH1750 - ambient light
- MQ135 - gas level
- Capacitive soil moisture sensor
- MOSFET-driven fan, grow light, and water pump
- RGB indicator LED

## Gas Monitoring

The greenhouse air is not always checked by a person, so a low-cost MQ135 gas sensor provides a coarse, continuous indication of air-quality changes between visits. The MQ135 responds to several gases (e.g. ammonia, CO₂, smoke), is not selective, and its raw ADC output is not calibrated to a concentration. The firmware averages the last 20 samples (2 s) and compares the result against three thresholds; together with temperature and humidity, this sets the fan speed (the highest speed requested by any of the three inputs wins).

**Limitations:** the thresholds are fixed starting values, and the sensor needs a warm-up period and drifts with temperature and humidity, so they should be re-tuned for each installation. A more selective sensor would be needed for a real air-quality measurement.

## Build

1. Install [Visual Studio Code](https://code.visualstudio.com/) and the [Espressif IDF extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension).
2. Copy `components/wifi_manager/private/wifiSecrets.h.example` as `wifiSecrets.h` in the same directory, then configure your Wi-Fi credentials.
3. Set the target, build, and flash the firmware.

**CLI commands:**
```bash
idf.py set-target esp32s3
idf.py build flash monitor
```
## Related Project

See [SmartGreenhouse-Arduino](https://github.com/AminRazm/SmartGreenhouse-Arduino) for the earlier Arduino and C++ version.

The two repositories show my work with both C++ and C, and how the firmware architecture evolved as the project grew.

## Future Work

Remote monitoring and online control may be added in the future, potentially using MQTT.