# SmartGreenhouse-ESP-IDF

An ESP32-S3 smart greenhouse built with ESP-IDF and C. This version uses a modular firmware architecture with FreeRTOS tasks to monitor environmental conditions and control climate, lighting, and irrigation.

## Features

- **Climate control:** Uses temperature, humidity, and gas readings to control fan speed.
- **Smart lighting:** Adjusts light output based on ambient light and a daily schedule.
- **Automatic irrigation:** Monitors soil moisture and controls the water pump.
- **Wi-Fi and time sync:** Uses Wi-Fi and SNTP to keep the lighting schedule accurate.
- **Modular firmware:** Separates sensors, controllers, actuators, and the RGB indicator.

## Architecture

The firmware is divided into separate components for sensors, controllers, actuators, Wi-Fi, and the indicator. FreeRTOS tasks handle independent operations, while callbacks and task notifications support communication between components.

For example, the DHT22 sensor runs in its own task, and the irrigation controller uses a task notification to start an irrigation cycle when the soil is dry. Separating sensor reading, control logic, and hardware actions makes the firmware easier to maintain and extend.

I rewrote the earlier [Arduino and C++ version](https://github.com/AminRazm/SmartGreenhouse-Arduino) when the project needed a clearer way to organize independent operations and respond to sensor readings.

## Hardware

- ESP32-S3 development board
- DHT22 — temperature and humidity
- BH1750 — ambient light level
- MQ135 — gas level
- Capacitive soil moisture sensor
- MOSFET-driven fan, grow light, and water pump
- RGB indicator LED

## Gas Monitoring

The MQ135 monitors changes in gas levels. These readings can help flag unusual greenhouse conditions, but they do not directly detect plant diseases.

## Build

1. Open `components/wifi_manager/private/wifiSecrets.h.example`.
2. Enter your Wi-Fi credentials.
3. Rename the file to `wifiSecrets.h` by removing the `.example` suffix. Do not publish this private file.
4. Build and flash the firmware.

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with your serial port.

## Future Work

Remote monitoring and online control may be added in the future, potentially using MQTT.
