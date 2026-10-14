# Smart Greenhouse (ESP-IDF)

This is the ESP-IDF version of my ESP32-S3 smart greenhouse project, rewritten in C. It monitors environmental conditions and automatically controls the fan, grow light, and watering system.

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

The MQ135 monitors changes in gas levels. These readings can help identify unusual greenhouse conditions, but they do not directly detect plant diseases.

## Build

1. Configure Wi-Fi credentials in `components/wifi_manager/private/wifiSecrets.h.example`.
2. Rename the file to `wifiSecrets.h` and enter your credentials.
3. Set the target, build, and flash the firmware.
```Bash
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```
Replace `PORT` with your serial port.

## Related Project

See [SmartGreenhouse-Arduino](https://github.com/AminRazm/SmartGreenhouse-Arduino) for the earlier Arduino and C++ version.

The two repositories show my work with both C++ and C, and how the firmware architecture evolved as the project grew.

## Future Work

Remote monitoring and online control may be added in the future, potentially using MQTT.