# TonUINOTagWriter

Pocket device to read and write NFC tags for TonUINO project

You can find the TonUINO project at https://www.voss.earth

Forked from Daniel Wilhelm, debugging and improvements by Hans Schneider

Daniel's project site: http://itse.homeip.net/projekte/13/

Reading and writing tags using an implemented webserver. You can also do a firmware update OTA (over the air), edit the hostname, use a NTP server to get correct timestamps and switch between English and German language. All in a handy small device with builtin battery and charging circuit.

Hardware:
- ESP32 Dev Module
- RFID-RC522 Reader
- TP4056 Li-Po charging module (you have to modify the module to charge with not more than max. current for your battery)
- Li-Po battery 3,7V / 500mAh or more
- Switch 2x 1-0-1
- Enclosure (can even be found on http://itse.homeip.net/projekte/13/)

Arduino config:
- 4M (190KB SPIFFS with OTA)
- 240MHz

Version:
- 0.1.0 First stable working version
- 0.1.2 Bug: show right input options after read card
- 0.1.3 Ability to switch to German language in configuration 2022-02-29

ESP32 connections to NFC reader:
- ESP32 <->  RC522:
- 3.3V  <->  3.3V
- GND   <->  GND
- D2    <->  RST
- D18   <->  SCK
- D19   <->  MISO
- D21   <->  SDA
- D23   <->  MOSI
- D22   <->  IRQ // Currently not used

Used libraries:
- FS.h
- SPIFFS.h
- ESPmDNS.h
- WiFiManager.h
- ArduinoJson.h (5.13.5, 6 not yet supported!)
- TimeLib.h
- SPI.h
- MFRC522.h
- ESP32httpUpdate.h



![TTW](https://user-images.githubusercontent.com/6528455/197866958-0fe1d69b-212d-4710-95c0-6898d25316dc.jpg)



After flashing firmware the first time using Arduino IDE the TonuinoTagWriter provides an access point named TTW_Config_xxxx where 'xxx' is the chipID of your board. You have to connect to that WiFi from your PC or mobile device. Then the TonuinoTagWriter is accessible using the IP-address 192.168.4.1. After that you can choose your own WiFi to connect to. Further you have access to TonuinoTagWriter using default hostname TTW-01 (you can change in configuration menu).


![IMG_2519](https://user-images.githubusercontent.com/6528455/197995794-340e663b-5043-4cc1-8fc7-312d7a458a4a.JPG)

![IMG_2520](https://user-images.githubusercontent.com/6528455/197995880-a14bb28e-01db-4cc0-a66e-723ca754ed19.JPG)

![IMG_2522](https://user-images.githubusercontent.com/6528455/197995983-5aec97e3-a39a-4459-9349-ef4cb79cd47a.JPG)

![IMG_2525](https://user-images.githubusercontent.com/6528455/197996077-55ff53b9-7bb1-4147-bc16-e94fd8c270cd.JPG)

![IMG_2523](https://user-images.githubusercontent.com/6528455/197996168-538c36b9-d9b7-47e0-87dd-239a7a970c85.JPG)

![IMG_2524](https://user-images.githubusercontent.com/6528455/197996297-4f98bebc-fe15-499a-8730-251126b0a820.JPG)


