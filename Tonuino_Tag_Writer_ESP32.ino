// Tonuino Tag Writer (TTW)
// Daniel Wilhelm
// Hans Schneider (debugging and improvements)
//
// Hardware: ESP32 Dev Module
// 
// Arduino IDE 1.8.15
// 
// ESP32 2.0.1
// 
// WIFIManager (https://github.com/tzapu/WiFiManager)
// 
// ArduinoJSON 5.13.5 (6 not yet supported!)
// 
// Arduino Config
// 4M (190KB SPIFFS with OTA); 240MHz;
// 882513 Bytes (67%) of memory used, max. memory is 1310720 bytes
// 39148 bytes (11%) of dynamic memory used by global variables, 288532 bytes remaining for local variables, max. is 327680 bytes
//
// ToDo
// - NTP timezone selection
// - Clear config also clear WiFi
//
// Bugs
// - byteToHexStringRange: last byte ist etwas irreführend
// - card must be represented after each read/write
//
// 0.1.0 First stable working version
// 0.1.2 Bug: show right input options after read card
// 0.1.3 Ability to switch to German language in configuration 2022-02-29
//
// NodeMCU connection to external devices
// ESP32 <->  RC522:
// 3.3V  <->  3.3V
// GND   <->  GND
// D2    <->  RST
// D18   <->  SCK
// D19   <->  MISO
// D21   <->  SDA
// D23   <->  MOSI
// D22   <->  IRQ // Currently not used


// **************************************************************************
//  Includes
// **************************************************************************
#include <FS.h>
#include "SPIFFS.h"
#include <ESPmDNS.h>
#include <WiFiManager.h>                // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ArduinoJson.h>                // Config file handling
#include <TimeLib.h>                    // https://github.com/PaulStoffregen/Time
#include <SPI.h>
#include <MFRC522.h>
//#include <HTTPUpdateServer.h>
#include <ESP32httpUpdate.h>

WebServer httpServer(80);
//HTTPUpdateServer httpUpdater;

WiFiUDP Udp;                            // Needed for NTP
unsigned int NtpLocalPort = 123;        // local port to listen for UDP packets (NTP)
  
// **************************************************************************
//  Defines
// **************************************************************************
#define DEBUG                           // Define for Debug output on serial interface  
#define CLEAR_BTN               4       // GPIO4 LOW during PowerUp will clear the json config file
#define RST_PIN                 2       // GPIO2 MFRC522
#define SS_PIN                  21      // GPIO21 MFRC522
#define IRQ_PIN                 22      // GPIO22 MFRC522

MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance

const String FIRMWARE_NAME  = "TTW";
const String VERSION        = "v0.1.3";


// Number of known default keys (hard-coded)
// NOTE: Synchronize the NR_KNOWN_KEYS define with the defaultKeys[] array
#define NR_KNOWN_KEYS   8
// Known keys, see: https://code.google.com/p/mfcuk/wiki/MifareClassicDefaultKeys
byte knownKeys[NR_KNOWN_KEYS][MFRC522::MF_KEY_SIZE] =  {
    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, // FF FF FF FF FF FF = factory default
    {0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5}, // A0 A1 A2 A3 A4 A5
    {0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5}, // B0 B1 B2 B3 B4 B5
    {0x4d, 0x3a, 0x99, 0xc3, 0x51, 0xdd}, // 4D 3A 99 C3 51 DD
    {0x1a, 0x98, 0x2c, 0x7e, 0x45, 0x9a}, // 1A 98 2C 7E 45 9A
    {0xd3, 0xf7, 0xd3, 0xf7, 0xd3, 0xf7}, // D3 F7 D3 F7 D3 F7
    {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}, // AA BB CC DD EE FF
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}  // 00 00 00 00 00 00
};

byte dataBlock[]    = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

String strDataBlock = "00000000000000000000000000000000";



// **************************************************************************
// **************************************************************************
//  DO NOT CHANGE ANYTHING BELOW THIS LINE
// **************************************************************************
// **************************************************************************

// **************************************************************************
//  Debug
// **************************************************************************
#ifdef DEBUG
#define DEBUG_PRINT(x)  Serial.print (x)
#define DEBUG_PRINTLN(x)  Serial.println (x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif


// **************************************************************************
//  Variables
// **************************************************************************
// config.json
char host_name[20] = "";
char ntpserver[30] = "";

// NTP
bool ntp_enabled = true;                             // Set to false to disable querying for the time
char poolServerName[30] = "europe.pool.ntp.org";     // default NTP Server when not configured in config.json
char boottime[20] = "";                              // Boot Time
const int timeZone = 1;                              // Central European Time
const int NTP_PACKET_SIZE = 48;                      // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE];                  // Buffer to hold incoming & outgoing packets

// Webserver
String javaScript;
String htmlHeader;
String htmlFooter;

// Other
bool shouldSaveConfig = false;                       // Flag for saving data
bool de_enabled = false;                             // Set language to English


// **************************************************************************
// JSON Configuration Management
// **************************************************************************
bool loadConfig() {
  if (SPIFFS.exists("/config.json")) {
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        DEBUG_PRINTLN("[0155] opened config file");
        size_t size = configFile.size();
        if (size > 1024) {
          DEBUG_PRINTLN("[0158] Config file size is too large");
          return false;
        }
        std::unique_ptr<char[]> buf(new char[size]);            // Allocate a buffer to store contents of the file.
        configFile.readBytes(buf.get(), size);                  // Input Buffer (needed by ArduinoJson)
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        #ifdef DEBUG
          json.printTo(Serial);
        #endif
        if (json.success()) {
          DEBUG_PRINTLN("[0169] \nparsed json");
          if (json.containsKey("hostname")) strncpy(host_name, json["hostname"], 20);
          if (json.containsKey("ntpserver")) strncpy(ntpserver, json["ntpserver"], 30);
          if (json.containsKey("ntpenabled")) ntp_enabled = json["ntpenabled"];
          if (json.containsKey("deenabled")) de_enabled = json["deenabled"];
        }
        else {
          DEBUG_PRINTLN("[0176] Failed to parse config file");
          return false;
        }
      }
      else {
        DEBUG_PRINTLN("[0181] Failed to open config file");
        return false;
      }
  }
  return true;
}


// **************************************************************************
//  Callback notifying us of the need to save config
// **************************************************************************
void saveConfigCallback()
{
  DEBUG_PRINTLN("[0194] Should save config");
  shouldSaveConfig = true;
}


// **************************************************************************
//  Gets called when WiFiManager enters configuration mode
// **************************************************************************
void configModeCallback (WiFiManager *myWiFiManager)
{
  DEBUG_PRINTLN("[0204] Entered config mode");
  DEBUG_PRINTLN(WiFi.softAPIP());
  DEBUG_PRINTLN(myWiFiManager->getConfigPortalSSID());    // if you used auto generated SSID, print it
}


// **************************************************************************
//  IP Address to String
// **************************************************************************
String ipToString(IPAddress ip)
{
  String s = "";
  for (int i = 0; i < 4; i++)
    s += i  ? "." + String(ip[i]) : String(ip[i]);
  return s;
}


// **************************************************************************
//  NTP Time Syncronization
// **************************************************************************
time_t getNtpTime()
{
  IPAddress ntpServerIP;                            // NTP server's ip address

  while (Udp.parsePacket() > 0) ;                   // Discard any previously received packets
  DEBUG_PRINTLN("[0230] NTP: Transmit NTP Request");
  WiFi.hostByName(ntpserver, ntpServerIP);          // Lookup IP from Hostname
  DEBUG_PRINT("[0232] NTP: ");
  DEBUG_PRINT(ntpserver);
  DEBUG_PRINT(" [0234] IP: ");
  DEBUG_PRINTLN(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      DEBUG_PRINTLN("[0241] NTP: Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);      // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + (timeZone * 60 * 60); // NTP Time - 70 Years + TimeZone Hours
    }
  }
  DEBUG_PRINTLN("[0252] NTP: No NTP Response :-(");
  return 0;                                          // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  memset(packetBuffer, 0, NTP_PACKET_SIZE);                 // set all bytes in the buffer to 0
  packetBuffer[0] = 0b11100011;                             // LI (Clock is unsynchronized), Version (4), Mode (Client)
  packetBuffer[1] = 0;                                      // Stratum, or type of clock (Unspecified or invalid)
  packetBuffer[2] = 6;                                      // Polling Interval (log2 seconds)
  packetBuffer[3] = 0xEC;                                   // Peer Clock Precision (log2 seconds)
                                                            // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;                                    // 
  packetBuffer[13] = 0x4E;                                  //
  packetBuffer[14] = 49;                                    //
  packetBuffer[15] = 52;                                    //
  Udp.beginPacket(address, 123);                            // NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}


// **************************************************************************
//  HTML Header Template
// **************************************************************************
void buildHeader()
{
  htmlHeader="<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.0 Strict//DE' 'http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd'>\n";
  htmlHeader+="<html xmlns='http://www.w3.org/1999/xhtml' xml:lang='de'>\n";
  htmlHeader+="  <head>\n";
  htmlHeader+="    <meta name='viewport' content='width=device-width, initial-scale=.75' />\n";
  htmlHeader+="    <link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css' />\n";
  htmlHeader+="    <style>@media (max-width: 991px) {.nav-pills>li {float: none; margin-left: 0; margin-top: 5px; text-align: center;}}</style>\n";
  htmlHeader+="    <title>" + FIRMWARE_NAME + " - " + VERSION + "</title>\n";
  htmlHeader+="  </head>\n";
  htmlHeader+="  <body>\n";
  htmlHeader+="    <div class='container'>\n";
  htmlHeader+="      <h1>" + FIRMWARE_NAME + " - " + VERSION + "</h1>\n";
  htmlHeader+="      <div class='row'>\n";
  htmlHeader+="        <div class='col-md-12'>\n";
  htmlHeader+="          <ul class='nav nav-pills'>\n";
  htmlHeader+="            <li class='active'>\n";
  htmlHeader+="              <a href='/'>Hostname <span class='badge'>" + String(host_name) + "</span></a></li>\n";
  htmlHeader+="            <li class='active'>\n";
  htmlHeader+="              <a href='http://" + ipToString(WiFi.localIP()) + ":80'>Local IP<span class='badge'>" + ipToString(WiFi.localIP()) + "</span></a></li>\n";
  htmlHeader+="            <li class='active'>\n";
  htmlHeader+="              <a href='/'>MAC <span class='badge'>" + String(WiFi.macAddress()) + "</span></a></li>\n";
  htmlHeader+="            <li class='active'>\n";
  if (de_enabled) {
    htmlHeader+="              <a href='/config'>Konfiguration</a></li>\n";
  }
  else {
    htmlHeader+="              <a href='/config'>Configuration</a></li>\n";
  }
  htmlHeader+="            <li class='active'>\n";
  htmlHeader+="              <a href='/tonuino'>Tonuino</a></li>\n";
  htmlHeader+="            <li class='active'>\n";
  htmlHeader+="              <a href='/ota'>FW-Update</a></li>\n";
  htmlHeader+="            <li class='active'>\n";
  htmlHeader+="              <a href='/'><span class='glyphicon glyphicon-signal'></span> "+ String(WiFi.RSSI()) + " dBm</a></li>\n";
  htmlHeader+="          </ul>\n";
  htmlHeader+="        </div>\n";
  htmlHeader+="      </div><hr />\n";
}


// **************************************************************************
//  HTML Footer Template
// **************************************************************************
void buildFooter()
{
  if (de_enabled) {
    htmlFooter="      <div class='row'><div class='col-md-12'><em>Tonuino Tag Writer (TTW), Aktuelle Zeit: "+ String(hour()) + ":" + String(minute()) + ":" + String(second()) +"</em></div></div>\n";
  }
  else {
    htmlFooter="      <div class='row'><div class='col-md-12'><em>Tonuino Tag Writer (TTW), Current time: "+ String(hour()) + ":" + String(minute()) + ":" + String(second()) +"</em></div></div>\n";
  }
  htmlFooter+="    </div>\n";
  htmlFooter+="  </body>\n";
  htmlFooter+="</html>\n";
}


// **************************************************************************
//  HTML JavaScript Template
// **************************************************************************
void buildJavaScript()
{
  javaScript="<script type='text/javascript'>\n";
  javaScript+="    function einblenden(){\n";
  javaScript+="      var selectcard = document.getElementById('select_card').selectedIndex;\n";
  javaScript+="      var selectadmincard = document.getElementById('select_admincard').selectedIndex;\n";
  javaScript+="      document.getElementById('input_titleA1').style.display = 'none';\n";
  javaScript+="      document.getElementById('input_titleB1').style.display = 'none';\n";
  javaScript+="      document.getElementById('input_titleA2').style.display = 'none';\n";
  javaScript+="      document.getElementById('input_titleB2').style.display = 'none';\n";
  javaScript+="      document.getElementById('input_title1').style.display = 'none';\n";
  javaScript+="      document.getElementById('input_title2').style.display = 'none';\n";
  javaScript+="      if(selectcard == 0) {\n";
  javaScript+="        document.getElementById('hoerspiel').style.display = 'inline';\n";
  javaScript+="      }\n";
  javaScript+="      else {\n";
  javaScript+="        document.getElementById('hoerspiel').style.display = 'none';\n";
  javaScript+="      }\n";
  javaScript+="      if(selectcard == 1) {\n";
  javaScript+="        document.getElementById('album').style.display = 'inline';\n";
  javaScript+="      }\n";
  javaScript+="      else {\n";
  javaScript+="        document.getElementById('album').style.display = 'none';\n";
  javaScript+="      }\n";
  javaScript+="      if(selectcard == 2) {\n";
  javaScript+="        document.getElementById('party').style.display = 'inline';\n";
  javaScript+="      }\n";
  javaScript+="      else {\n";
  javaScript+="        document.getElementById('party').style.display = 'none';\n";
  javaScript+="      }\n";
  javaScript+="      if(selectcard == 3) {\n";
  javaScript+="        document.getElementById('einzel').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_title1').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_title2').style.display = 'inline';\n";
  javaScript+="      }\n";
  javaScript+="      else {\n";
  javaScript+="        document.getElementById('einzel').style.display = 'none';\n";
  javaScript+="      }\n";
  javaScript+="      if(selectcard == 4) {\n";
  javaScript+="        document.getElementById('hoerbuch').style.display = 'inline';\n";
  javaScript+="      }\n";
  javaScript+="      else {\n";
  javaScript+="        document.getElementById('hoerbuch').style.display = 'none';\n";
  javaScript+="      }\n";
  javaScript+="      if(selectcard == 5) {\n";
  javaScript+="        document.getElementById('hoerspielvonbis').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_titleA1').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_titleB1').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_titleA2').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_titleB2').style.display = 'inline';\n";
  javaScript+="      }\n";
  javaScript+="      else {\n";
  javaScript+="        document.getElementById('hoerspielvonbis').style.display = 'none';\n";
  javaScript+="      }\n";
  javaScript+="      if(selectcard == 6) {\n";
  javaScript+="        document.getElementById('albumvonbis').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_titleA1').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_titleB1').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_titleA2').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_titleB2').style.display = 'inline';\n";
  javaScript+="      }\n";
  javaScript+="      else {\n";
  javaScript+="        document.getElementById('albumvonbis').style.display = 'none';\n";
  javaScript+="      }\n";
  javaScript+="      if(selectcard == 7) {\n";
  javaScript+="        document.getElementById('partyvonbis').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_titleA1').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_titleB1').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_titleA2').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_titleB2').style.display = 'inline';\n";
  javaScript+="      }\n";
  javaScript+="      else {\n";
  javaScript+="        document.getElementById('partyvonbis').style.display = 'none';\n";
  javaScript+="      }\n";
  javaScript+="      if(selectcard == 8) {\n";
  javaScript+="        document.getElementById('Sonder').style.display = 'inline';\n";
  javaScript+="        document.getElementById('select_admincard').style.display = 'inline';\n";
  javaScript+="      }\n";
  javaScript+="      else {\n";
  javaScript+="        document.getElementById('Sonder').style.display = 'none';\n";
  javaScript+="        document.getElementById('select_admincard').style.display = 'none';\n";
  javaScript+="      }\n";
  javaScript+="      if(selectcard == 8 && selectadmincard == 1) {\n";
  javaScript+="        document.getElementById('input_time1').style.display = 'inline';\n";
  javaScript+="        document.getElementById('input_time2').style.display = 'inline';\n";
  javaScript+="      }\n";
  javaScript+="      else {\n";
  javaScript+="        document.getElementById('input_time1').style.display = 'none';\n";
  javaScript+="        document.getElementById('input_time2').style.display = 'none';\n";
  javaScript+="      }\n";
  javaScript+="    }\n";
  javaScript+="</script>\n";
}


// **************************************************************************
//  HTML Page for ESP Reboot
// **************************************************************************
void Handle_Reboot()
{
  if (de_enabled) {
    httpServer.sendHeader("Verbindung", "geschlossen");
    httpServer.send(200, "text/html", F("<body>Neustart OK, bitte &#246;ffne die <a href='/'>Startseite</a>.</body>"));
  }
  else { 
    httpServer.sendHeader("Connection", "close");
    httpServer.send(200, "text/html", F("<body>Reboot OK, please open the <a href='/'>main page</a>.</body>"));
  }
  delay(500);
  ESP.restart();
}


// **************************************************************************
//  HTML Page for ESP Clear Json config-file
// **************************************************************************
void Handle_ClearConfig()
{
  if (de_enabled) {
    httpServer.sendHeader("Verbindung", "geschlossen");
    httpServer.send(200, "text/html", F("<body>Config-Datei gelöscht, ESP Neustart, bitte &#246;ffne die <a href='/'>Startseite</a>.</body>"));
  }
  else {
    httpServer.sendHeader("Connection", "close");
    httpServer.send(200, "text/html", F("<body>Config file deleted, ESP reboot, please open the <a href='/'>main page</a>.</body>"));
  }
  SPIFFS.remove("/config.json");
  delay(500);
  ESP.restart();
}


// **************************************************************************
//  HTML Page for ESP configuration
// **************************************************************************
void Handle_config()
{ 
  if (httpServer.method() == HTTP_GET) {
    DEBUG_PRINTLN("[0477] WEB: Connection established - /config");
    sendConfigPage("", "", 0, 200);
  } 
  else {
    DEBUG_PRINTLN("[0481] WEB: Connection established - /config (save)");
    }
  if (de_enabled) {
    sendConfigPage("Settings erfolgreich gesichert! Bitte neu starten!", "Erfolg!", 1, 200);
    }
  else {
    sendConfigPage("Settings saved! Please reboot!", "Success!", 1, 200);
  }
}

void sendConfigPage(String message, String header, int type, int httpcode)
{
  char host_name_conf[20] = "";
  char ntpserver_conf[30] = "";
  bool ntpenabled_conf;
  bool deenabled_conf;

  if (type == 1){                                              // Type 1 -> save data
    String message = "[0499] WEB: Number of args received: ";
    message += String(httpServer.args()) + "\n";
    for (int i = 0; i < httpServer.args(); i++) {
      message += "[0502] Arg " + (String)i + " â€“> ";
      message += httpServer.argName(i) + ":" ;
      message += httpServer.arg(i) + "\n";
    }
    
    strncpy(host_name_conf, httpServer.arg("host_name_conf").c_str(), 20);
    strncpy(ntpserver_conf, httpServer.arg("ntpserver_conf").c_str(), 30);
    if (httpServer.hasArg("ntpenabled")) {ntpenabled_conf = true;} else {ntpenabled_conf = false;}
    if (httpServer.hasArg("deenabled")) {deenabled_conf = true;} else {deenabled_conf = false;}

    DEBUG_PRINT("[0512]");
    DEBUG_PRINTLN(message);

    // validate values before saving
    bool validconf;
    if (httpServer.args() != 0) {
      validconf = true;
    }
    else {
      validconf = false;
    }
    if (validconf)
    {
      DEBUG_PRINTLN("[0525] SPI: save config.json...");
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();
      json["hostname"] = String(host_name_conf);
      json["ntpserver"] = String(ntpserver_conf);
      json["ntpenabled"] = String(ntpenabled_conf);
      json["deenabled"] = String(deenabled_conf);

      File configFile = SPIFFS.open("/config.json", "w");
      if (!configFile) {
        DEBUG_PRINTLN("[0535] SPI: failed to open config file for writing");
      }
      #ifdef DEBUG
        json.printTo(Serial);
      #endif
      DEBUG_PRINTLN("[0540]");
      json.printTo(configFile);
      configFile.close();
      // end save
    }
  } else {                                // Type 0 -> load data
    if (SPIFFS.begin())
    {
      DEBUG_PRINTLN("[0548] SPI: mounted file system");
      if (SPIFFS.exists("/config.json"))
      {
        // file exists, reading and loading
        DEBUG_PRINTLN("[0552] SPI: reading config file");
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile) {
          DEBUG_PRINTLN("[0555] SPI: opened config file");
          size_t size = configFile.size();
          // Allocate a buffer to store contents of the file.
          std::unique_ptr<char[]> buf(new char[size]);

          configFile.readBytes(buf.get(), size);
          DynamicJsonBuffer jsonBuffer;
          JsonObject& json = jsonBuffer.parseObject(buf.get());
          DEBUG_PRINT("[0563] JSO: ");
          #ifdef DEBUG
            json.printTo(Serial);
          #endif
          if (json.success()) {
            DEBUG_PRINTLN("\n[0568] JSO: parsed json");
            if (json.containsKey("hostname")) strncpy(host_name_conf, json["hostname"], 20);
            if (json.containsKey("ntpserver")) strncpy(ntpserver_conf, json["ntpserver"], 30);
            if (json.containsKey("ntpenabled")) ntpenabled_conf = json["ntpenabled"];
            if (json.containsKey("deenabled")) deenabled_conf = json["deenabled"];
          } else {
            DEBUG_PRINTLN("[0574] JSO: failed to load json config");
          }
        }
      }
    } else {
      DEBUG_PRINTLN("[0579] SPI: failed to mount FS");
    }
  }
  String htmlDataconf;        // Hold the HTML Code
  buildHeader();
  buildFooter();
  htmlDataconf=htmlHeader;
  
  if (type == 1)
    htmlDataconf+="      <div class='row'><div class='col-md-12'><div class='alert alert-success'><strong>" + header + "</strong> " + message + "</div></div></div>\n";
  if (type == 2)
    htmlDataconf+="      <div class='row'><div class='col-md-12'><div class='alert alert-warning'><strong>" + header + "</strong> " + message + "</div></div></div>\n";
  if (type == 3)
    htmlDataconf+="      <div class='row'><div class='col-md-12'><div class='alert alert-danger'><strong>" + header + "</strong> " + message + "</div></div></div>\n";
  htmlDataconf+="      <div class='row'>\n";
  htmlDataconf+="<form method='post' action='/config'>";
  htmlDataconf+="        <div class='col-md-12'>\n";
  if (de_enabled) {
    htmlDataconf+="          <h3>Konfiguration</h3>\n";
  } 
  else {
    htmlDataconf+="          <h3>Configuration</h3>\n";
  }
  htmlDataconf+="          <table class='table table-striped' style='table-layout: fixed;'>\n";
  if (de_enabled) {
    htmlDataconf+="            <thead><tr><th>Option</th><th>Aktueller Wert</th><th>Neuer Wert</th></tr></thead>\n";
    } 
  else {
    htmlDataconf+="            <thead><tr><th>Option</th><th>Current value</th><th>New value</th></tr></thead>\n";
    }
  htmlDataconf+="            <tbody>\n";
  htmlDataconf+="            <tr class='text-uppercase'><td>Hostname</td><td><code>" + ((host_name_conf[0] == 0 ) ? String("(" + String(host_name) + ")") : String(host_name_conf)) + "</code></td><td><input type='text' id='host_name_conf' name='host_name_conf' value='" + String(host_name_conf) + "'></td></tr>\n";
  htmlDataconf+="            <tr class='text-uppercase'><td>NTP Server</td><td><code>" + ((ntpserver_conf[0] == 0 ) ? String("(" + String(poolServerName) + ")") : String(ntpserver_conf)) + "</code></td><td><input type='text' id='ntpserver_conf' name='ntpserver_conf' value='" + String(ntpserver_conf) + "'></td></tr>\n";
  if (de_enabled) {
    htmlDataconf+="            <tr class='text-uppercase'><td>NTP ein?</td><td><code>" + (ntpenabled_conf ? String("Ja") : String("Nein")) + "</code></td><td><input type='checkbox' id='ntpena' name='ntpenabled' " + (ntpenabled_conf ? String("checked") : String("")) + "></td></tr>";
    htmlDataconf+="            <tr class='text-uppercase'><td>Sprache Deutsch ein?</td><td><code>" + (deenabled_conf ? String("Ja") : String("Nein")) + "</code></td><td><input type='checkbox' id='deenab' name='deenabled' " + (deenabled_conf ? String("checked") : String("")) + "></td></tr>";
//    htmlDataconf+=" <tr><td colspan='5' class='text-center'><em><a href='/reboot' class='btn btn-sm btn-danger'>Neustart</a>  <a href='/update' class='btn btn-sm btn-warning'>Update</a>  <button type='submit' class='btn btn-sm btn-success'>Sichern</button>  <a href='/' class='btn btn-sm btn-primary'>Abbruch</a></em></td></tr>";
    htmlDataconf+=" <tr><td colspan='5' class='text-center'><em><a href='/reboot' class='btn btn-sm btn-danger'>Neustart</a>  <button type='submit' class='btn btn-sm btn-success'>Sichern</button>  <a href='/' class='btn btn-sm btn-primary'>Abbruch</a></em></td></tr>";
  }
  else {
    htmlDataconf+="            <tr class='text-uppercase'><td>NTP on?</td><td><code>" + (ntpenabled_conf ? String("Yes") : String("No")) + "</code></td><td><input type='checkbox' id='ntpena' name='ntpenabled' " + (ntpenabled_conf ? String("checked") : String("")) + "></td></tr>";
    htmlDataconf+="            <tr class='text-uppercase'><td>German language on?</td><td><code>" + (deenabled_conf ? String("YES") : String("No")) + "</code></td><td><input type='checkbox' id='deenab' name='deenabled' " + (deenabled_conf ? String("checked") : String("")) + "></td></tr>";
//    htmlDataconf+=" <tr><td colspan='5' class='text-center'><em><a href='/reboot' class='btn btn-sm btn-danger'>Roboot</a>  <a href='/update' class='btn btn-sm btn-warning'>Update</a>  <button type='submit' class='btn btn-sm btn-success'>Save</button>  <a href='/' class='btn btn-sm btn-primary'>Cancel</a></em></td></tr>";
    htmlDataconf+=" <tr><td colspan='5' class='text-center'><em><a href='/reboot' class='btn btn-sm btn-danger'>Roboot</a>  <button type='submit' class='btn btn-sm btn-success'>Save</button>  <a href='/' class='btn btn-sm btn-primary'>Cancel</a></em></td></tr>";
  }
  htmlDataconf+="            </tbody></table>\n";
  htmlDataconf+="          </div></div>\n";
  
  htmlDataconf+=htmlFooter;

  httpServer.send(httpcode, "text/html; charset=UTF-8", htmlDataconf);
  httpServer.client().stop();
}


// **************************************************************************
//  Convert a Decimal String to a Hex String
// **************************************************************************
String dectohex(String dec) {
  String hex;
  hex = String(dec.toInt(), HEX);
  return hex;
}


// **************************************************************************
//  Convert a Hex String to a Decimal String
// **************************************************************************
String hextodec(String hex) {
  String dec;
  dec = String(strtol(hex.c_str(), NULL, 16));
  return dec;
}


// **************************************************************************
//  HTML create/change Tonuino ChipCard Page
// **************************************************************************
void Handle_tonuino(){
  if (httpServer.method() == HTTP_GET) {
    DEBUG_PRINTLN("[0659] WEB: Connection established - /tonuino");

    // Look for new cards

    bool card = 0;
    strDataBlock = "00000000000000000000000000000000";
    hexCharacterStringToBytes(dataBlock, strDataBlock.c_str());
    if (mfrc522.PICC_IsNewCardPresent()) 
    {
      card = 1;
    }
    // Select one of the cards
    if (mfrc522.PICC_ReadCardSerial()) 
    {
      card = 1;
    }

    if (card == 0) {
      strDataBlock = "00000000000000000000000000000000";
      hexCharacterStringToBytes(dataBlock, strDataBlock.c_str());
      DEBUG_PRINTLN("[0679] RFID: No card presented!");
      if (de_enabled) {
        sendControlPage("Keine Karte aufgelegt! Werte unten stammen nicht von einer Karte!", "Warnung!", 2, 200);
      }
      else {
        sendControlPage("No Card presented! Values do not come from a card!", "Warning!", 2, 200);
      }
    }
    else {
      readblock(1,0);
      // Halt PICC
      mfrc522.PICC_HaltA();
      // Stop encryption on PCD
      mfrc522.PCD_StopCrypto1();
      if (de_enabled) {
        sendControlPage("Werte (" + byteToHexString(dataBlock,sizeof(dataBlock)) + ") gelesen von Karte!", "Erfolg!", 1, 200);
      }
      else {
      sendControlPage("Values (" + byteToHexString(dataBlock,sizeof(dataBlock)) + ") read from a Card!", "Success!", 1, 200);
      }
    }  
  } 
  else {
    DEBUG_PRINTLN("[0702] WEB: Connection established - /tonuino (write card)");
    // generate the Tonuino MiFare sector 1 block 0
    strDataBlock = httpServer.arg(0);
    if (httpServer.arg(1).length() == 1) strDataBlock += "0";     // "1" -> "01"
    strDataBlock += httpServer.arg(1);  // Version
    if (httpServer.arg(3) == "8") {     // Modifier Karte
      strDataBlock += "00";
    }
    else {
      if (dectohex(httpServer.arg(2)).length() == 1) strDataBlock += "0";
      strDataBlock += dectohex(httpServer.arg(2));      
    }

    if (httpServer.arg(3) == "0") {     // Hörspiel
      strDataBlock += "01";
    }

    if (httpServer.arg(3) == "1") {     // Album
      strDataBlock += "02";
    }

    if (httpServer.arg(3) == "2") {     // Party
      strDataBlock += "03";
    }

    if (httpServer.arg(3) == "3") {     // Einzel
      strDataBlock += "04";
      if (dectohex(httpServer.arg(4)).length() == 1) strDataBlock += "0";
        strDataBlock += dectohex(httpServer.arg(4));
    }

    if (httpServer.arg(3) == "4") {     // Hörbuch
      strDataBlock += "05";
    }

    if (httpServer.arg(3) == "5") {     // Hörspiel von bis
      strDataBlock += "07";
      if (dectohex(httpServer.arg(6)).length() == 1) strDataBlock += "0";
        strDataBlock += dectohex(httpServer.arg(6));
      if (dectohex(httpServer.arg(8)).length() == 1) strDataBlock += "0";
        strDataBlock += dectohex(httpServer.arg(8));
    }

    if (httpServer.arg(3) == "6") {     // Album von bis
      strDataBlock += "08";
      if (dectohex(httpServer.arg(6)).length() == 1) strDataBlock += "0";
        strDataBlock += dectohex(httpServer.arg(6));
      if (dectohex(httpServer.arg(8)).length() == 1) strDataBlock += "0";
        strDataBlock += dectohex(httpServer.arg(8));
    }

    if (httpServer.arg(3) == "7") {     // Party von bis
      strDataBlock += "09";
      if (dectohex(httpServer.arg(6)).length() == 1) strDataBlock += "0";
        strDataBlock += dectohex(httpServer.arg(6));
      if (dectohex(httpServer.arg(8)).length() == 1) strDataBlock += "0";
        strDataBlock += dectohex(httpServer.arg(8));
    }

    if (httpServer.arg(3) == "8" && httpServer.arg(5) == "0") { // Modifier Karte - Admin Menü
      strDataBlock += "00";
    }

    if (httpServer.arg(3) == "8" && httpServer.arg(5) == "1") { // Modifier Karte - Einschlaf Modus
      strDataBlock += "01";
      if (dectohex(httpServer.arg(7)).length() == 1) strDataBlock += "0";
      strDataBlock += dectohex(httpServer.arg(7));
    }  

    if (httpServer.arg(3) == "8" && httpServer.arg(5) == "2") { // Modifier Karte - Pausetanz Modus
      strDataBlock += "02";
    }

    if (httpServer.arg(3) == "8" && httpServer.arg(5) == "3") { // Modifier Karte - Sperre Modus
      strDataBlock += "03";
    }

    if (httpServer.arg(3) == "8" && httpServer.arg(5) == "4") { // Modifier Karte - Kleinkind Modus
      strDataBlock += "04";
    }

    if (httpServer.arg(3) == "8" && httpServer.arg(5) == "5") { // Modifier Karte - Kindergarten Modus
      strDataBlock += "05";
    }

    if (httpServer.arg(3) == "8" && httpServer.arg(5) == "6") { // Modifier Karte - Titel wiederholen Modus
      strDataBlock += "06";
    }

    if (httpServer.arg(3) == "8" && httpServer.arg(5) == "7") { // Modifier Karte - Audio Feedback
      strDataBlock += "07";
    }

    strDataBlock += "00";
    
    // convert the hex har array to hex byte array
    hexCharacterStringToBytes(dataBlock, strDataBlock.c_str());
    
    // Look for new cards
    bool card = 0;
    strDataBlock = "00000000000000000000000000000000";
    hexCharacterStringToBytes(dataBlock, strDataBlock.c_str());
    if (mfrc522.PICC_IsNewCardPresent()) 
    {
      card = 1;
    }
    // Select one of the cards
    if (mfrc522.PICC_ReadCardSerial()) 
    {
      card = 1;
    }

    if (card == 0) {
      DEBUG_PRINTLN("[0815] RFID: No card presented!");
      if (de_enabled) {
        sendControlPage("Keine Karte aufgelegt!", "Fehler!", 3, 200);
      }
      else {
        sendControlPage("No card presented!", "Error!", 3, 200);
      }
      strDataBlock = "00000000000000000000000000000000";
      hexCharacterStringToBytes(dataBlock, strDataBlock.c_str());
    }
    else {
      // Write the block to the card
      writeblock(1, 0);        
      // Halt PICC
      mfrc522.PICC_HaltA();
      // Stop encryption on PCD
      mfrc522.PCD_StopCrypto1();
      if (de_enabled) {
        sendControlPage("Neue Werte (" + byteToHexString(dataBlock,sizeof(dataBlock)) + ") auf die Karte geschrieben!", "Erfolg!", 1, 200);
      }
      else {
        sendControlPage("New values (" + byteToHexString(dataBlock,sizeof(dataBlock)) + ") written to card!", "Success!", 1, 200);
      }
      strDataBlock = "00000000000000000000000000000000";
      hexCharacterStringToBytes(dataBlock, strDataBlock.c_str());
    }
  }
}


void sendControlPage(String message, String header, int type, int httpcode)
{
  if (type == 1) {                                               // Type 1
//    String message = "WEB: Neue Werte gesetzt über die control page: ";
    String message = "[0849] WEB: Set new values using control page: ";
    message += String(httpServer.args()) + "\n";
    for (int i = 0; i < httpServer.args(); i++) {
      message += "Arg " + (String)i + " -> ";
      message += httpServer.argName(i) + ":" ;
      message += httpServer.arg(i) + "\n";
    }
    DEBUG_PRINTLN(message);
  }
  
  String htmlDataconf;                  // Hold the HTML Code
  
  buildHeader();
  buildFooter();
  buildJavaScript();
  
  htmlDataconf=htmlHeader;
  htmlDataconf+=javaScript;
  
  if (type == 1)
    htmlDataconf+="      <div class='row'><div class='col-md-12'><div class='alert alert-success'><strong>" + header + "</strong> " + message + "</div></div></div>\n";
  if (type == 2)
    htmlDataconf+="      <div class='row'><div class='col-md-12'><div class='alert alert-warning'><strong>" + header + "</strong> " + message + "</div></div></div>\n";
  if (type == 3)
    htmlDataconf+="      <div class='row'><div class='col-md-12'><div class='alert alert-danger'><strong>" + header + "</strong> " + message + "</div></div></div>\n";  
  
  htmlDataconf+="      <div class='row'>\n";
  htmlDataconf+="       <form method='post' action='/tonuino'>\n";
  htmlDataconf+="        <div class='col-md-12'>\n";
  if (de_enabled) {
    htmlDataconf+="          <h3>Erzeuge / Ändere Tonuino Karte</h3>\n";
    htmlDataconf+="          <table class='table table-striped' style='table-layout: fixed;'>\n";
    htmlDataconf+="            <thead><tr><th>Option</th><th colspan='3'>Werte</th><th>Kommentare</th></tr></thead>\n";
    htmlDataconf+="            <tbody>\n";
    htmlDataconf+="              <tr><td>Magic Number</td><td><input type='text' id='magicnumber_set' name='magicnumber_set' size='6' maxlength='8' value='" + byteToHexStringRange(dataBlock,0,4) + "'></td><td></td><td></td><td>Nummer muss mit dem Tonuino übereinstimmen, default ist 1337B347</td></tr>\n";
    htmlDataconf+="              <tr><td>Version</td><td>";
    htmlDataconf+="                <select name='version_set' size='1'>\n";
    htmlDataconf+="                  <option " + ((byteToHexStringRange(dataBlock,4,5) == "01" ) ? String("selected") : String("")) + ">1</option>\n";
    htmlDataconf+="                  <option " + ((byteToHexStringRange(dataBlock,4,5) == "02" ) ? String("selected") : String("")) + ">2</option>\n";
    htmlDataconf+="                </select>\n";
    htmlDataconf+="               </td><td></td><td></td><td>V1 ist für Tonuino 2.0.x<br>V2 ist für Tonuino 2.1.x</td></tr>\n";
    htmlDataconf+="              <tr><td>Ordner Nummer</td><td><input type='text' id='folder_set' name='folder_set' size='2' maxlength='2' value='" + ((byteToHexStringRange(dataBlock,5,6) == "00") ? String("01") : hextodec(byteToHexStringRange(dataBlock,5,6))) + "'> (1-99)</td><td></td><td></td><td></td></tr>\n";
    htmlDataconf+="              <tr><td>Kartentyp</td><td>\n";
    htmlDataconf+="                <select id='select_card' onchange='einblenden()' name='cardtype' size='1'>\n";
    htmlDataconf+="                  <option value='0' " + ((byteToHexStringRange(dataBlock,6,7) == "01" ) ? String("selected") : String("")) + ">Hörspiel</option>\n";
    htmlDataconf+="                  <option value='1' " + ((byteToHexStringRange(dataBlock,6,7) == "02" ) ? String("selected") : String("")) + ">Album</option>\n";
    htmlDataconf+="                  <option value='2' " + ((byteToHexStringRange(dataBlock,6,7) == "03" ) ? String("selected") : String("")) + ">Party</option>\n";
    htmlDataconf+="                  <option value='3' " + ((byteToHexStringRange(dataBlock,6,7) == "04" ) ? String("selected") : String("")) + ">Einzel</option>\n";
    htmlDataconf+="                  <option value='4' " + ((byteToHexStringRange(dataBlock,6,7) == "05" ) ? String("selected") : String("")) + ">Hörbuch</option>\n";
    htmlDataconf+="                  <option value='5' " + ((byteToHexStringRange(dataBlock,6,7) == "07" ) ? String("selected") : String("")) + ">Hörspiel von-bis</option>\n";
    htmlDataconf+="                  <option value='6' " + ((byteToHexStringRange(dataBlock,6,7) == "08" ) ? String("selected") : String("")) + ">Album von-bis</option>\n";
    htmlDataconf+="                  <option value='7' " + ((byteToHexStringRange(dataBlock,6,7) == "09" ) ? String("selected") : String("")) + ">Party von-bis</option>\n";
    htmlDataconf+="                  <option value='8' " + ((byteToHexStringRange(dataBlock,5,6) == "00" ) ? String("selected") : String("")) + ">Sonderkarte</option>\n";
    htmlDataconf+="                </select>\n";
    htmlDataconf+="              </td><td>\n";
    htmlDataconf+="                      <input type='text' id='input_title1' name='title' value='" + hextodec(byteToHexStringRange(dataBlock,7,8)) + "' size='2' maxlength='3' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "04" ) ? String("inline") : String("none")) +  ";'> <div id='input_title2' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "04" ) ? String("inline") : String("none")) +  ";'>(1-255)</div>\n";
    htmlDataconf+="                    <select id='select_admincard' name='admincardtype' style='display: " + ((byteToHexStringRange(dataBlock,5,6) == "00" ) ? String("inline") : String("none")) + ";' onchange='einblenden()'>\n";
    htmlDataconf+="                      <option value='0' " + ((byteToHexStringRange(dataBlock,5,7) == "0000") ? String("selected") : String("")) + ">Admin Menü</option>\n";
    htmlDataconf+="                      <option value='1' " + ((byteToHexStringRange(dataBlock,5,7) == "0001") ? String("selected") : String("")) + ">Einschlaf Modus</option>\n";
    htmlDataconf+="                      <option value='2' " + ((byteToHexStringRange(dataBlock,5,7) == "0002") ? String("selected") : String("")) + ">Pausetanz Modus</option>\n";
    htmlDataconf+="                      <option value='3' " + ((byteToHexStringRange(dataBlock,5,7) == "0003") ? String("selected") : String("")) + ">Sperre</option>\n";
    htmlDataconf+="                      <option value='4' " + ((byteToHexStringRange(dataBlock,5,7) == "0004") ? String("selected") : String("")) + ">Kleinkind Modus</option>\n";
    htmlDataconf+="                      <option value='5' " + ((byteToHexStringRange(dataBlock,5,7) == "0005") ? String("selected") : String("")) + ">Kindergarten Modus</option>\n";
    htmlDataconf+="                      <option value='6' " + ((byteToHexStringRange(dataBlock,5,7) == "0006") ? String("selected") : String("")) + ">Titel wiederholen</option>\n";
    htmlDataconf+="                      <option value='7' " + ((byteToHexStringRange(dataBlock,5,7) == "0007") ? String("selected") : String("")) + ">Audio-Feedback</option>\n";
    htmlDataconf+="                    </select>\n";
    htmlDataconf+="                    <input type='text' id='input_titleA1' name='titlefrom' value='" + hextodec(byteToHexStringRange(dataBlock,7,8)) + "' size='2' maxlength='3' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "07" ) || (byteToHexStringRange(dataBlock,6,7) == "08" ) || (byteToHexStringRange(dataBlock,6,7) == "09" ) ? String("inline") : String("none")) +  ";'> <div id='input_titleA2' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "07" ) || (byteToHexStringRange(dataBlock,6,7) == "08" ) || (byteToHexStringRange(dataBlock,6,7) == "09" ) ? String("inline") : String("none")) +  ";'>(1-255)</div>\n";
    htmlDataconf+="                  </td><td>\n";
    htmlDataconf+="                    <input type='text' id='input_time1' name='sleeptime' value='" + hextodec(byteToHexStringRange(dataBlock,7,8)) + "' size='1' maxlength='2' style='display: " + ((byteToHexStringRange(dataBlock,5,7) == "0001" ) ? String("inline") : String("none")) + ";'> <div id='input_time2' style='display: " + ((byteToHexStringRange(dataBlock,5,7) == "0001" ) ? String("inline") : String("none")) + ";'>(1-99 min)</div>\n";
    htmlDataconf+="                    <input type='text' id='input_titleB1' name='titletill' value='" + hextodec(byteToHexStringRange(dataBlock,8,9)) + "' size='1' maxlength='3' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "07" ) || (byteToHexStringRange(dataBlock,6,7) == "08" ) || (byteToHexStringRange(dataBlock,6,7) == "09" ) ? String("inline") : String("none")) +  ";'> <div id='input_titleB2' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "07" ) || (byteToHexStringRange(dataBlock,6,7) == "08" ) || (byteToHexStringRange(dataBlock,6,7) == "09" ) ? String("inline") : String("none")) +  ";'>(1-255)</div>\n";
    htmlDataconf+="                  </td><td>\n";
    htmlDataconf+="                        <div id='hoerspiel' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "01" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Ein zufälliger Titel aus dem gewählten Ordner wird abgespielt.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='album' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "02" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Spielt alle Titel des Ordners in numerischer Reihenfolge.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='party' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "03" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Spielt zufällige Titel des Ordners in Endlosschleife.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='einzel' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "04" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Spielt Datei xxx.mp3 ab.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='hoerbuch' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "05" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Spielt alle Titel des Ordners in numerischer Reihenfolge und speichert den Fortschritt, so dass beim nächsten Verwenden der Karte beim aktuellen Titel begonnen wird.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='hoerspielvonbis' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "07" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Ein zufälliger Titel im gewählten Ordner zwischen von und bis wird abgespielt.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='albumvonbis' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "08" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Spielt alle Titel des Ordners zwischen von und bis in numerischer Reihenfolge.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='partyvonbis' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "09" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Spielt zufällige Titel des Ordners zwischen von und bis in Endlosschleife.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='Sonder' style='display: " + ((byteToHexStringRange(dataBlock,5,6) == "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Dies ist eine Sonderkarte\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                  </td></tr>\n";
    htmlDataconf+="            <tr><td colspan='5' class='text-center'><em> <button type='submit' class='btn btn-sm btn-success'>Schreibe Karte</button> <a href='/tonuino' class='btn btn-sm btn-warning'>Lese Karte</a>  <a href='/' class='btn btn-sm btn-primary'>Abbruch</a></em></td></tr>\n";
  }
  else {
    htmlDataconf+="          <h3>Generate / Change Tonuino card</h3>\n";
    htmlDataconf+="          <table class='table table-striped' style='table-layout: fixed;'>\n";
    htmlDataconf+="            <thead><tr><th>Option</th><th colspan='3'>Values</th><th>Comments</th></tr></thead>\n";
    htmlDataconf+="            <tbody>\n";
    htmlDataconf+="              <tr><td>Magic Number</td><td><input type='text' id='magicnumber_set' name='magicnumber_set' size='6' maxlength='8' value='" + byteToHexStringRange(dataBlock,0,4) + "'></td><td></td><td></td><td>Number must match the Tonuino, default is 1337B347</td></tr>\n";
    htmlDataconf+="              <tr><td>Version</td><td>";
    htmlDataconf+="                <select name='version_set' size='1'>\n";
    htmlDataconf+="                  <option " + ((byteToHexStringRange(dataBlock,4,5) == "01" ) ? String("selected") : String("")) + ">1</option>\n";
    htmlDataconf+="                  <option " + ((byteToHexStringRange(dataBlock,4,5) == "02" ) ? String("selected") : String("")) + ">2</option>\n";
    htmlDataconf+="                </select>\n";
    htmlDataconf+="               </td><td></td><td></td><td>V1 is for Tonuino 2.0.x<br>V2 is for Tonuino 2.1.x</td></tr>\n";
    htmlDataconf+="              <tr><td>Ordner Nummer</td><td><input type='text' id='folder_set' name='folder_set' size='2' maxlength='2' value='" + ((byteToHexStringRange(dataBlock,5,6) == "00") ? String("01") : hextodec(byteToHexStringRange(dataBlock,5,6))) + "'> (1-99)</td><td></td><td></td><td></td></tr>\n";
    htmlDataconf+="              <tr><td>Kartentyp</td><td>\n";
    htmlDataconf+="                <select id='select_card' onchange='einblenden()' name='cardtype' size='1'>\n";
    htmlDataconf+="                  <option value='0' " + ((byteToHexStringRange(dataBlock,6,7) == "01" ) ? String("selected") : String("")) + ">Radioplay</option>\n";
    htmlDataconf+="                  <option value='1' " + ((byteToHexStringRange(dataBlock,6,7) == "02" ) ? String("selected") : String("")) + ">Album</option>\n";
    htmlDataconf+="                  <option value='2' " + ((byteToHexStringRange(dataBlock,6,7) == "03" ) ? String("selected") : String("")) + ">Party</option>\n";
    htmlDataconf+="                  <option value='3' " + ((byteToHexStringRange(dataBlock,6,7) == "04" ) ? String("selected") : String("")) + ">Single</option>\n";
    htmlDataconf+="                  <option value='4' " + ((byteToHexStringRange(dataBlock,6,7) == "05" ) ? String("selected") : String("")) + ">Audio book</option>\n";
    htmlDataconf+="                  <option value='5' " + ((byteToHexStringRange(dataBlock,6,7) == "07" ) ? String("selected") : String("")) + ">Radioplay from-to</option>\n";
    htmlDataconf+="                  <option value='6' " + ((byteToHexStringRange(dataBlock,6,7) == "08" ) ? String("selected") : String("")) + ">Album from-to</option>\n";
    htmlDataconf+="                  <option value='7' " + ((byteToHexStringRange(dataBlock,6,7) == "09" ) ? String("selected") : String("")) + ">Party from-to</option>\n";
    htmlDataconf+="                  <option value='8' " + ((byteToHexStringRange(dataBlock,5,6) == "00" ) ? String("selected") : String("")) + ">Special card</option>\n";
    htmlDataconf+="                </select>\n";
    htmlDataconf+="              </td><td>\n";
    htmlDataconf+="                      <input type='text' id='input_title1' name='title' value='" + hextodec(byteToHexStringRange(dataBlock,7,8)) + "' size='2' maxlength='3' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "04" ) ? String("inline") : String("none")) +  ";'> <div id='input_title2' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "04" ) ? String("inline") : String("none")) +  ";'>(1-255)</div>\n";
    htmlDataconf+="                    <select id='select_admincard' name='admincardtype' style='display: " + ((byteToHexStringRange(dataBlock,5,6) == "00" ) ? String("inline") : String("none")) + ";' onchange='einblenden()'>\n";
    htmlDataconf+="                      <option value='0' " + ((byteToHexStringRange(dataBlock,5,7) == "0000") ? String("selected") : String("")) + ">Admin Menü</option>\n";
    htmlDataconf+="                      <option value='1' " + ((byteToHexStringRange(dataBlock,5,7) == "0001") ? String("selected") : String("")) + ">Sleep mode</option>\n";
    htmlDataconf+="                      <option value='2' " + ((byteToHexStringRange(dataBlock,5,7) == "0002") ? String("selected") : String("")) + ">Stop dance</option>\n";
    htmlDataconf+="                      <option value='3' " + ((byteToHexStringRange(dataBlock,5,7) == "0003") ? String("selected") : String("")) + ">Lock</option>\n";
    htmlDataconf+="                      <option value='4' " + ((byteToHexStringRange(dataBlock,5,7) == "0004") ? String("selected") : String("")) + ">Toddler mode</option>\n";
    htmlDataconf+="                      <option value='5' " + ((byteToHexStringRange(dataBlock,5,7) == "0005") ? String("selected") : String("")) + ">Kindergarten mode</option>\n";
    htmlDataconf+="                      <option value='6' " + ((byteToHexStringRange(dataBlock,5,7) == "0006") ? String("selected") : String("")) + ">Repeat title</option>\n";
    htmlDataconf+="                      <option value='7' " + ((byteToHexStringRange(dataBlock,5,7) == "0007") ? String("selected") : String("")) + ">Audio-Feedback</option>\n";
    htmlDataconf+="                    </select>\n";
    htmlDataconf+="                    <input type='text' id='input_titleA1' name='titlefrom' value='" + hextodec(byteToHexStringRange(dataBlock,7,8)) + "' size='2' maxlength='3' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "07" ) || (byteToHexStringRange(dataBlock,6,7) == "08" ) || (byteToHexStringRange(dataBlock,6,7) == "09" ) ? String("inline") : String("none")) +  ";'> <div id='input_titleA2' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "07" ) || (byteToHexStringRange(dataBlock,6,7) == "08" ) || (byteToHexStringRange(dataBlock,6,7) == "09" ) ? String("inline") : String("none")) +  ";'>(1-255)</div>\n";
    htmlDataconf+="                  </td><td>\n";
    htmlDataconf+="                    <input type='text' id='input_time1' name='sleeptime' value='" + hextodec(byteToHexStringRange(dataBlock,7,8)) + "' size='1' maxlength='2' style='display: " + ((byteToHexStringRange(dataBlock,5,7) == "0001" ) ? String("inline") : String("none")) + ";'> <div id='input_time2' style='display: " + ((byteToHexStringRange(dataBlock,5,7) == "0001" ) ? String("inline") : String("none")) + ";'>(1-99 min)</div>\n";
    htmlDataconf+="                    <input type='text' id='input_titleB1' name='titletill' value='" + hextodec(byteToHexStringRange(dataBlock,8,9)) + "' size='1' maxlength='3' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "07" ) || (byteToHexStringRange(dataBlock,6,7) == "08" ) || (byteToHexStringRange(dataBlock,6,7) == "09" ) ? String("inline") : String("none")) +  ";'> <div id='input_titleB2' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "07" ) || (byteToHexStringRange(dataBlock,6,7) == "08" ) || (byteToHexStringRange(dataBlock,6,7) == "09" ) ? String("inline") : String("none")) +  ";'>(1-255)</div>\n";
    htmlDataconf+="                  </td><td>\n";
    htmlDataconf+="                        <div id='hoerspiel' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "01" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        A random track from the selected folder is played.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='album' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "02" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Plays all tracks in the folder in numerical order.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='party' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "03" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Plays random tracks in the folder in an endless loop.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='einzel' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "04" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Plays file xxx.mp3.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='hoerbuch' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "05" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Plays all the tracks in the folder in numerical order and saves progress so that the next time you use the card, it will start from the current track.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='hoerspielvonbis' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "07" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        A random track in the selected folder between from and to is played.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='albumvonbis' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "08" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Plays all tracks in the folder between from and to in numerical order.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='partyvonbis' style='display: " + ((byteToHexStringRange(dataBlock,6,7) == "09" ) && (byteToHexStringRange(dataBlock,5,6) != "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        Plays random tracks in the folder between from and to in an endless loop.\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                        <div id='Sonder' style='display: " + ((byteToHexStringRange(dataBlock,5,6) == "00" ) ? String("inline") : String("none")) + ";'>\n";
    htmlDataconf+="                        This is a special card\n";
    htmlDataconf+="                        </div>\n";
    htmlDataconf+="                  </td></tr>\n";
    htmlDataconf+="            <tr><td colspan='5' class='text-center'><em> <button type='submit' class='btn btn-sm btn-success'>Write Card</button> <a href='/tonuino' class='btn btn-sm btn-warning'>Read card</a>  <a href='/' class='btn btn-sm btn-primary'>Cancel</a></em></td></tr>\n";    
  }    
  htmlDataconf+="            </tbody></table>\n";
  htmlDataconf+="          </div></div>\n";  
  htmlDataconf+=htmlFooter;
  
  httpServer.send(200, "text/html; charset=UTF-8", htmlDataconf);
  httpServer.client().stop(); 
}


// **************************************************************************
//  OTA Update Page
// **************************************************************************
void Handle_ota(){
  
  String htmlDataMain;        // Hold the HTML Code
  buildHeader();
  buildFooter();
  htmlDataMain=htmlHeader;
  
  htmlDataMain+="        <div class='col-md-12'>\n";
  htmlDataMain+="          <h3>TTW Firmware Update</h3><br>\n";
  htmlDataMain+="          <form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form><br>\n";
  htmlDataMain+="          </div>\n";
  
  htmlDataMain+=htmlFooter;

  httpServer.send(200, "text/html; charset=UTF-8", htmlDataMain);
  httpServer.client().stop();  
}


// **************************************************************************
//  HTML Main Page
// **************************************************************************
void Handle_welcome(){
  
  String htmlDataMain;        // Hold the HTML Code
  buildHeader();
  buildFooter();
  htmlDataMain=htmlHeader;
  
  htmlDataMain+="        <div class='col-md-12'>\n";
  htmlDataMain+="          <h3>Tonuino Tag Writer</h3><br>\n";
  htmlDataMain+="          MFRC522 Firmware: " + getmfrc522fwversion() + "<br>";
  if (de_enabled) {  
    htmlDataMain+="           Mögliche URL-Pfade:<br>\n"; // Bezeichner?
  }
  else {
    htmlDataMain+="           Possible URL paths:<br>\n"; // identifier?
  }
  htmlDataMain+="           <a href='http://" + ipToString(WiFi.localIP()) + ":80/reboot'>/reboot</a><br>\n";
//  htmlDataMain+="           /clearconfig<br>\n";
  htmlDataMain+="           <a href='http://" + ipToString(WiFi.localIP()) + ":80/freemem'>/freemem</a><br>\n";
  htmlDataMain+="           <a href='http://" + ipToString(WiFi.localIP()) + ":80/config'>/config</a><br>\n";
  htmlDataMain+="           <a href='http://" + ipToString(WiFi.localIP()) + ":80/dump'>/dump</a><br>\n";
  htmlDataMain+="           <a href='http://" + ipToString(WiFi.localIP()) + ":80/tonuino'>/tonuino</a><br>\n";
  htmlDataMain+="          </div>\n";
  
  htmlDataMain+=htmlFooter;

  httpServer.send(200, "text/html; charset=UTF-8", htmlDataMain);
  httpServer.client().stop();  
}


// **************************************************************************
//  HTML Dump Page
// **************************************************************************
void Handle_dump(){

  String htmlDataMain;        // Hold the HTML Code
  bool card = 0;
  buildHeader();
  buildFooter();
  htmlDataMain=htmlHeader;
  
  htmlDataMain+="        <div class='col-md-12'>\n";
  htmlDataMain+="          <h3>Card Dump</h3><br>\n";
  htmlDataMain+="          MFRC522 Firmware: " + getmfrc522fwversion() + "<br>\n";
  
  // Look for new cards
  if (mfrc522.PICC_IsNewCardPresent()) 
  {
    card = 1;
  }
  // Select one of the cards
  if (mfrc522.PICC_ReadCardSerial()) 
  {
    card = 1;
  }

  if (card == 0) {
    if (de_enabled) {
      htmlDataMain+="           Keine Karte aufgelegt!<br>";
    }
    else {
      htmlDataMain+="           No card presented!<br>";
    }
  }
  else {
    htmlDataMain+="           Card Type: " + getchipcardtype() + "<br>";
    htmlDataMain+="           Card Dump:<br>";
    readblock(0,0);
    htmlDataMain+="           Sector 0 Block 0: " + byteToHexString(dataBlock,sizeof(dataBlock)) + "<br>";
    readblock(0,1);
    htmlDataMain+="           Sector 0 Block 1: " + byteToHexString(dataBlock,sizeof(dataBlock)) + "<br>";
    readblock(0,2);
    htmlDataMain+="           Sector 0 Block 2: " + byteToHexString(dataBlock,sizeof(dataBlock)) + "<br>";
    readblock(0,3);
    htmlDataMain+="           Sector 0 Block 3: " + byteToHexString(dataBlock,sizeof(dataBlock)) + "<br>";
    readblock(1,0);
    htmlDataMain+="           Sector 1 Block 0: " + byteToHexString(dataBlock,sizeof(dataBlock)) + "<br>";
    readblock(1,1);
    htmlDataMain+="           Sector 1 Block 1: " + byteToHexString(dataBlock,sizeof(dataBlock)) + "<br>";
    readblock(1,2);
    htmlDataMain+="           Sector 1 Block 2: " + byteToHexString(dataBlock,sizeof(dataBlock)) + "<br>";
    readblock(1,3);
    htmlDataMain+="           Sector 1 Block 3: " + byteToHexString(dataBlock,sizeof(dataBlock)) + "<br>";
    htmlDataMain+="           Card UID: " + getchipcarduid() + "<br>";
    htmlDataMain+="          </div><br><br>";
  }
  
  htmlDataMain+=htmlFooter;
  
  // Halt PICC
  mfrc522.PICC_HaltA();
  // Stop encryption on PCD
  mfrc522.PCD_StopCrypto1();

  httpServer.send(200, "text/html; charset=UTF-8", htmlDataMain);
  httpServer.client().stop();  
}


// **************************************************************************
//  Get RFID Reader Firmware Version
// **************************************************************************
String getmfrc522fwversion() {
  String s = "";
  // Get the MFRC522 firmware version
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  // Lookup which version
  switch(v) {
    case 0x88: s = "(clone)";  break;
    case 0x90: s = "v0.0";     break;
    case 0x91: s = "v1.0";     break;
    case 0x92: s = "v2.0";     break;
    case 0x12: s = "counterfeit chip";     break;
    default:   s = "(unknown)";
  }
  // If 0x00 or 0xFF is returned, communication probably failed
  if ((v == 0x00) || (v == 0xFF))
    if (de_enabled) {
      s = "WARNUNG: Kommunikation fehlgeschlagen, ist der MFRC522 Leser korrekt verbunden?";
    }
    else {
      s = "WARNING: Communication failure, is the MFRC522 reader properly connected?";
    }
  
  return s;
}


// **************************************************************************
//  Get RFID Chipcard Type
// **************************************************************************
String getchipcardtype() {
  String s = "";
  // Get the chipcard type
  byte v = mfrc522.PICC_GetType(mfrc522.uid.sak);
  // Lookup which type
  switch(v) {
    case 0x00: s = "UNKNOWN";  break;
    case 0x01: s = "ISO/IEC 14443-4";     break;
    case 0x02: s = "ISO/IEC 18092";     break;
    case 0x03: s = "MIFARE Classic 320B";     break;
    case 0x04: s = "MIFARE Classic 1KB";     break;
    case 0x05: s = "MIFARE Classic 4KB";     break;
    case 0x06: s = "MIFARE Ultralight/C";     break;
    case 0x07: s = "MIFARE Plus";     break;
    case 0x08: s = "MIFARE DESFire";     break;
    case 0x09: s = "TNP3XXX";     break;
    case 0x0A: s = "NOT_COMPLETE";     break;
    default:   s = "UNKNOWN";
  }
  return s;
}


// **************************************************************************
//  Get RFID Chipcard UID
// **************************************************************************
String getchipcarduid() {
    String content= "";
    for (byte i = 0; i < mfrc522.uid.size; i++) 
    {
       content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
       content.concat(String(mfrc522.uid.uidByte[i], HEX));
    }
    content.toUpperCase();
  return content.substring(1);
}


// **************************************************************************
//  Read RFID Sector
// **************************************************************************
void readsector(int sector) {
}


// **************************************************************************
//  Read RFID Block
// **************************************************************************
void readblock(int sector, int block) {
  MFRC522::StatusCode status;
  MFRC522::MIFARE_Key key;
  byte buffer[18];
  byte size = sizeof(buffer);

  for (byte k = 0; k < NR_KNOWN_KEYS; k++) {
    // Copy the known key into the MIFARE_Key structure
    for (byte i = 0; i < MFRC522::MF_KEY_SIZE; i++) {
      key.keyByte[i] = knownKeys[k][i];
    }
      status = (MFRC522::StatusCode) mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, sector*4+3, &key, &(mfrc522.uid));
      if (status != MFRC522::STATUS_OK) {
      }
      else {
        // Read requested Block
        status = (MFRC522::StatusCode) mfrc522.MIFARE_Read(sector*4+block, buffer, &size);
        if (status != MFRC522::STATUS_OK) {
          
        }
        else {
          for(int i = 0; i < sizeof(dataBlock); i++) dataBlock[i] = buffer[i];
          break;
        }
      }
      // http://arduino.stackexchange.com/a/14316
  }
}


// **************************************************************************
//  Write RFID Block
// **************************************************************************
void writeblock(int sector, int block) {
  MFRC522::StatusCode status;
  MFRC522::MIFARE_Key key;
  // Authenticate using key A
  for (byte k = 0; k < NR_KNOWN_KEYS; k++) {
    // Copy the known key into the MIFARE_Key structure
    for (byte i = 0; i < MFRC522::MF_KEY_SIZE; i++) {
      key.keyByte[i] = knownKeys[k][i];
    }
    status = (MFRC522::StatusCode) mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, sector*4+3, &key, &(mfrc522.uid));
    if (status != MFRC522::STATUS_OK) {
      DEBUG_PRINTLN("[1280] RFID: wrong write key");
      DEBUG_PRINTLN(mfrc522.GetStatusCodeName(status));
    }
    else {
      DEBUG_PRINTLN("[1284] RFID: key accepted");
      // Write data to the block
      status = (MFRC522::StatusCode) mfrc522.MIFARE_Write(sector*4+block, dataBlock, 16);
      if (status != MFRC522::STATUS_OK) {
        DEBUG_PRINTLN("[1288] RFID: data can not be written");
      }
      else {
        DEBUG_PRINTLN("[1291] RFID: data written");
        break;
      }
    }
  }
}


String byteToHexStringRange(byte* data, byte firstbyte, byte lastbyte)
{
  char str[(lastbyte-firstbyte) * 2 + 1];
  str[(lastbyte-firstbyte) * 2] = '\0';
  byte digit;
  int j = 0;
  
  for (int i = firstbyte; i < lastbyte; i++)
  {
    digit = data[i] / 16;
    str[j * 2] = getHexDigit(digit);
    digit = data[i] % 16;
    str[j * 2 + 1] = getHexDigit(digit);
    j++;
  }
  DEBUG_PRINT("[1314] byteToHexStringRange: ");
  DEBUG_PRINTLN(str);
  return String(str);
}


String byteToHexString(byte* data, byte length)
{
  char str[length * 2 + 1];
  str[length * 2] = '\0';
  byte digit;

  for (int i = 0; i < length; i++)
  {
    digit = data[i] / 16;
    str[i * 2] = getHexDigit(digit);
    digit = data[i] % 16;
    str[i * 2 + 1] = getHexDigit(digit);
  }

  return String(str);
}


char getHexDigit(byte digit)
{
  char c;
  if (digit >= 0 && digit <= 9)
    c = digit + '0';
  else if (digit >= 0xA && digit <= 0xF)
    c = digit + 'A' - 10;
  else
    c = '0';

  return c;
}


void hexCharacterStringToBytes(byte *byteArray, const char *hexString)
{
  bool oddLength = strlen(hexString) & 1;

  byte currentByte = 0;
  byte byteIndex = 0;

  for (byte charIndex = 0; charIndex < strlen(hexString); charIndex++)
  {
    bool oddCharIndex = charIndex & 1;

    if (oddLength)
    {
      // If the length is odd
      if (oddCharIndex)
      {
        // odd characters go in high nibble
        currentByte = nibble(hexString[charIndex]) << 4;
      }
      else
      {
        // Even characters go into low nibble
        currentByte |= nibble(hexString[charIndex]);
        byteArray[byteIndex++] = currentByte;
        currentByte = 0;
      }
    }
    else
    {
      // If the length is even
      if (!oddCharIndex)
      {
        // Odd characters go into the high nibble
        currentByte = nibble(hexString[charIndex]) << 4;
      }
      else
      {
        // Odd characters go into low nibble
        currentByte |= nibble(hexString[charIndex]);
        byteArray[byteIndex++] = currentByte;
        currentByte = 0;
      }
    }
  }
}


byte nibble(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';

  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;

  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;

  return 0;  // Not a valid hexadecimal character
}


// **************************************************************************
//  Setup
// **************************************************************************
void setup(void){

  #ifdef DEBUG
    Serial.begin(115200);
    Serial.println();
    Serial.println("Booting ...");
  #endif

  SPI.begin();
  
  pinMode(CLEAR_BTN, INPUT_PULLUP);
//  pinMode(IRQ_PIN, INPUT_PULLUP);

  Serial.println(F("End setup"));


  DEBUG_PRINTLN("[1433] configured pinModes");

  WiFiManager wifiManager;
  if (digitalRead(CLEAR_BTN) == LOW) wifiManager.resetSettings();   // Clear WIFI data if CLEAR Button is pressed during boot

  wifiManager.setAPCallback(configModeCallback);                    // set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setSaveConfigCallback(saveConfigCallback);            // set config save notify callback
  
  if (SPIFFS.begin(true)) {                                         // Mounting File System, true because of formatOnFail
    DEBUG_PRINTLN("[1442] mounted file system");
    if (!loadConfig()) {
      DEBUG_PRINTLN("[1444] Failed to load config");
    } else {
      DEBUG_PRINTLN("[1446] Config loaded");
    }
  }
  else {
    DEBUG_PRINTLN("[1450] failed to mount FS");
  }

  // if (host_name[0] == 0 ) sprintf(host_name, "IPC-%d",ESP.getChipId());     //set default hostname when not set!
  if (host_name[0] == 0 ) sprintf(host_name, "TTW-01");     //set default hostname when not set!

  // Configure some additional 
  WiFiManagerParameter custom_hostname("hostname", "Hostname:", host_name, 20);
  wifiManager.addParameter(&custom_hostname);
  WiFiManagerParameter custom_ntpserver("ntpserver", "NTP Server (optional):", ntpserver, 30);
  wifiManager.addParameter(&custom_ntpserver);

  // String autoconf_ssid = "TTW_Config_"+String(ESP.getChipId());
  String autoconf_ssid = "TTW_Config_";
  wifiManager.setConnectTimeout(60);                                  // Workaround Test for reconnect issue
  wifiManager.setConfigPortalTimeout(120);                            // Timeout for SoftAP, try connect again to stored wlan
  wifiManager.autoConnect(autoconf_ssid.c_str());                     // Use IPC_Config_+Chip ID as AP-name with 192.168.4.1 as IP
  
  // Read and save the new values from AP config page
  strncpy(host_name, custom_hostname.getValue(), 20);
  strncpy(ntpserver, custom_ntpserver.getValue(), 30);

  DEBUG_PRINTLN("[1472] WiFi connected! IP: " + ipToString(WiFi.localIP()) + " Hostname: " + String(host_name) + " NTP-Server: " + String(ntpserver));

  // save the custom parameters to FS
  if (shouldSaveConfig) {
    DEBUG_PRINTLN(" [1476] config...");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["hostname"] = host_name;
    json["ntpserver"] = ntpserver;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      DEBUG_PRINTLN("[1484] SPI: failed to open config file for writing config");
    }
    #ifdef DEBUG
      json.printTo(Serial);
    #endif
    DEBUG_PRINTLN("[1489]");
    json.printTo(configFile);
    configFile.close();
  }

  // if (host_name[0] == 0 ) sprintf(host_name, "IPC-%d",ESP.getChipId());             // set default hostname when not set!
  if (host_name[0] == 0 ) sprintf(host_name, "TTW-01");
  if (ntpserver[0] == 0 ) strncpy(ntpserver, poolServerName, 30);                   // set default ntp server when not set!
  
  // Enable the Free Memory Page
  httpServer.on("/freemem", []() {
    DEBUG_PRINTLN("[1500] WEB: Connection established: /freemem : ");
    DEBUG_PRINT(ESP.getFreeSketchSpace());
    httpServer.sendHeader("Connection", "close");
    httpServer.send(200, "text/plain", String(ESP.getFreeSketchSpace()).c_str());
  });

  httpServer.on("/reboot", Handle_Reboot);              // Reboot the ESP Controller

  httpServer.on("/clearconfig", Handle_ClearConfig);    // Delete the Json config file on Filesystem

  httpServer.on("/config", Handle_config);              // Show the configuration page

  httpServer.on("/dump", Handle_dump);                  // Show card dump
  
  httpServer.on("/tonuino", Handle_tonuino);            // Create / Change Tonuino ChipCard Page
  
  httpServer.on("/ota", Handle_ota);                    // Show OTA upload page

  httpServer.on("/", Handle_welcome);                   // Show the main page

  httpServer.on("/update", HTTP_POST, []() {            // OTA Update
    httpServer.sendHeader("Connection", "close");
    httpServer.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = httpServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
      // Serial.setDebugOutput(true);
      // Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin()) { // start with max available size
        // Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        // Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { // true to set the size to the current progress
        // Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        // Update.printError(Serial);
      }
      // Serial.setDebugOutput(false);
    } else {
      DEBUG_PRINTLN("[1544] Update Failed Unexpectedly (likely broken connection)");
    }
  });
    
  httpServer.begin();                                   // Enable the WebServer
  
  // setTime(1514764800);  // 1.1.2018 00:00 Initialize time
  if (ntp_enabled) {
      DEBUG_PRINTLN("[1552] NTP: Starting UDP");
      Udp.begin(NtpLocalPort);
      DEBUG_PRINT("[1554] NTP: Local port: ");
      // DEBUG_PRINTLN(Udp.localPort());
      DEBUG_PRINTLN("[1556] NTP: waiting for sync");
      setSyncProvider(getNtpTime);                       // set the external time provider
      setSyncInterval(3600);                             // set the number of seconds between re-sync
      // String boottimetemp = printDigits2(hour()) + ":" + printDigits2(minute()) + " " + printDigits2(day()) + "." + printDigits2(month()) + "." + String(year());
      // strncpy(boottime, boottimetemp.c_str(), 20);           // If we got time set boottime
  }

  mfrc522.PCD_Init();   // Init MFRC522
  delay(4);             // Wait for MFRC522 Board

  DEBUG_PRINTLN("[1566] Startup completed ...");
}


// **************************************************************************
//  Main Loop
// **************************************************************************
void loop(void){
    httpServer.handleClient();
}
