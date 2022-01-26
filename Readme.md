# APRS_IGate #

## Description ##

This software is for the APRS_IGate project. The hardware can be found at https://www.pi4raz.nl/index.php/products-page/kits/aprs-igate/ .
The iGate has an ESP32 and an Arduino. The ESP32 has to be programmed with APRS_IGate.ino sketch. The Arduino has a TNC function and needs to be programmed with the kissTnc.ino sketch (see kissTnc submodule). 

## Installation and programming ##

To install the ESP32 board in your Arduino IDE, follow these next instructions:

1. In your Arduino IDE, go to File> Preferences.
2. Enter https://dl.espressif.com/dl/package_esp32_index.json into the “Additional Board Manager URLs” field. Then, click the “OK” button.

Note: if you already have the ESP8266 boards URL, you can separate the URLs with a comma as follows:
https://dl.espressif.com/dl/package_esp32_index.json, http://arduino.esp8266.com/stable/package_esp8266com_index.json

3. Open the Boards Manager. Go to Tools > Board > Boards Manager…
4. Search for ESP32 and press install button for the “ESP32 by Espressif Systems“
6. Plug the ESP32 board to your computer.
7. Select your Board in Tools > Board menu (in my case it’s the DOIT ESP32 DEVKIT V1)
8. Select the Port (if you don’t see the COM Port in your Arduino IDE, you need to install the CP210x USB to UART Bridge VCP Drivers)
9. Open the APRS_IGate.ino sketch
10. Press the Upload button in the Arduino IDE. Wait a few seconds while the code compiles and uploads to your board.
11. If everything went as expected, you should see a “Done uploading.” message.
