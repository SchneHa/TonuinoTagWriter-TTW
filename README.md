# TonUINOTagWriter

Pocket device to read and write NFC tags for TonUINO project\r
You can find the TonUINO project at https://www.voss.earth\r
Forked from Daniel Wilhelm, debugging and improvements by Hans Schneider

Hardware:\r
ESP32 Dev Module\r

Arduino config:\r
4M (190KB SPIFFS with OTA)\r
240MHz\r

Version:\r
0.1.0 First stable working version\r
0.1.2 Bug: show right input options after read card\r
0.1.3 Ability to switch to German language in configuration 2022-02-29\r

ESP32 connections to NFC reader:\r
ESP32 <->  RC522:\r
3.3V  <->  3.3V\r
GND   <->  GND\r
D2    <->  RST\r
D18   <->  SCK\r
D19   <->  MISO\r
D21   <->  SDA\r
D23   <->  MOSI\r
D22   <->  IRQ // Currently not used\r

Used libraries:\r
FS.h\r
SPIFFS.h\r
ESPmDNS.h\r
WiFiManager.h\r
ArduinoJson.h (5.13.5, 6 not yet supported!)\r
TimeLib.h\r
SPI.h\r
MFRC522.h\r
ESP32httpUpdate.h\r
