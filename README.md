#Smart Greenhouse

An ESP32-S3 smart greenhouse built with ESP-IDF and C. It monitors environmental conditions 24/7 and automatically controls the fan, lights, and watering system.

##Features

- Climate control: Uses temperature, humidity, and gas sensor readings to adjust fan speed.
- Smart lighting: Adjusts light output based on measured light levels and a daily schedule.
- Automatic irrigation: Checks soil moisture and runs the water pump when needed.
- Wi-Fi and time sync: Connects to Wi-Fi and uses SNTP to keep the lighting schedule accurate.
- Modular firmware: Separate components for sensors, actuators, and control logic.

##Hardware

- ESP32-S3 development board
- DHT22 — temperature and humidity
- BH1750 — ambient light level
- MQ135 — gas sensor
- Capacitive soil moisture sensor v1.2
- MOSFET-driven fan, lights, and water pump

##Gas Monitoring

The MQ135 continuously monitors changes in gas levels. These readings may help flag unusual greenhouse conditions and provide an early warning of possible plant-health problems, sometimes before visible signs appear.

##Build

Configure Wi-Fi credentials before building:

Open "components/wifi_manager/private/wifiSecrets.h.example", Enter your Wi-Fi credentials, Remove the ".example" suffix by renaming and save the file.

Then build:

idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor

##Future Steps

Add remote monitoring and online control, potentially using MQTT.