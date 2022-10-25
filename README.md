# TonuinoTagWriter

Pocket device to read and write NFC tags for TonUINO project
You can find the TonUINO project here: https://www.voss.earth

Forked from Daniel Wilhelm, debugging and improvements by Hans Schneider

Hardware: ESP32 Dev Module

Arduino config:
4M (190KB SPIFFS with OTA)
240MHz

Version:
0.1.0 First stable working version
0.1.2 Bug: show right input options after read card
0.1.3 Ability to switch to German language in configuration 2022-02-29

ESP32 connections to NFC reader:
ESP32 <->  RC522:
3.3V  <->  3.3V
GND   <->  GND
D2    <->  RST
D18   <->  SCK
D19   <->  MISO
D21   <->  SDA
D23   <->  MOSI
D22   <->  IRQ // Currently not used

Used libraries:
FS.h
SPIFFS.h
ESPmDNS.h
WiFiManager.h
ArduinoJson.h (5.13.5, 6 not yet supported!)
TimeLib.h
SPI.h
MFRC522.h
ESP32httpUpdate.h
