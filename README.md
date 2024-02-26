# Acid-lead battery charging monitor

Acid-lead battery charging monitor registers electrical parameters during process of charging or discharging. Data is saved in CSV file on MicroSD card, and (or) sent via WiFi to LAN MQTT broker.

Configuration for WiFi and MQTT is loaded from MicroSD card [asdasd](config file).

![Block schema of connection to battery and charger, and data reporting flows.](https://raw.githubusercontent.com/malipek/battery_charging_monitor/master/assets/arduino-battery-charger-monitor.png)

Registered parameters (average from 1 minute, measured every second):

* voltage,
* current.

Displayed parameters (measured every second):

* running time,
* voltage,
* current,
* power,
* calculated reserved capacity.

![Photo of connected battery charging monitor for 80Ah deep charge Ca-Ca Acid-lead battery](https://raw.githubusercontent.com/malipek/battery_charging_monitor/master/assets/arduino_charging_monitor_photo.png)

Data is collected in two modes:

* continuous mode - from manual start to power down
* auto - form manual start until the current is lower then 0.2 during 5 minutes

## Modules

![Solution divided into functional modules](https://raw.githubusercontent.com/malipek/battery_charging_monitor/master/assets/arduino-battery-charger-monitor-blocks.png)

* NodeMCUv3 ESP8266-based board,
* INA 219 breakout board (removed 0.1Ω on-board shunt),
* 20A 75mV shunt,
* HD44780-compatible LCD 20x4 display,
* LCM1602 I²C I/O expander for HD44780 displays,
* MicroSD SPI bus card slot,
* D24V5F3 DC/DC step-down converter (3.3V),
* AMSRO-7805-NZ DC/DC step-down converter (-5V),
* 3-key membrane keyboard,
* 15A fuse, wiring and terminals able to handle 30A of current.

