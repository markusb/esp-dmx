/*
 * ESP-DMX web ui
 * 
 * Webinterface related files
 * 
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> 
#include <WiFiUdp.h>
#include <FS.h>
#include "webui.h"
#include "favicon.h"
#include "dmx512.h"
#include "statusLED.h"
#include "esp-dmx.h"

extern ESP8266WebServer webServer;
extern Config config;
extern const char* build;
extern int version_mayor;
extern int version_minor;

extern int status;
extern char * status_text[];
extern unsigned long packetCounter;
extern unsigned long dmxUMatchCounter;
extern unsigned long artnetPacketCounter;
extern uint16_t seen_universe;
extern int last_rssi;
extern void setStatusLED(int,int);
extern float fps;
extern globalStruct global;
extern int temperature;
extern int fanspeed;
extern int dmxFrameCounter;

/*
 * Set default config on initial boot if there is no configuration yet
 */
void defaultConfig() {
    config.universe = 0;
    config.channels = 512;
    config.delay = 25;
    config.holdsecs = 5;
    config.hostname = "ESP-DMX-"+WiFi.macAddress().substring(9);
    config.hostname.replace(":","");
}


/*
 * Attempt loading the configuration from a file in SPIFFS
 */
bool loadConfig() {
    Serial.println("loadConfig: Loading config from /config.json");
  
    File configFile = SPIFFS.open("/config.json", "r");
    if (!configFile) {
        Serial.println("loadConfig: Failed to open config file /config.json");
        return false;
    }
  
    size_t size = configFile.size();
    if (size > 1024) {
        Serial.println("loadConfig: Config file size is too large");
        return false;
    }
  
    std::unique_ptr<char[]> buf(new char[size]);
    configFile.readBytes(buf.get(), size);
    configFile.close();
    char * bp = &buf[0];
  
    DynamicJsonDocument jsonDoc(1024);
    DeserializationError error = deserializeJson(jsonDoc, buf.get());
    if (error) {
        Serial.println("loadConfig: Failed to parse config file");
        return false;
    }
    if (jsonDoc.containsKey("hostname")) { String hn = jsonDoc["hostname"]; config.hostname = hn; };
    if (jsonDoc.containsKey("universe")) { config.universe = jsonDoc["universe"]; } 
    if (jsonDoc.containsKey("channels")) { config.channels = jsonDoc["channels"]; } 
    if (jsonDoc.containsKey("delay")) { config.delay = jsonDoc["delay"]; } 
    if (jsonDoc.containsKey("holdsecs")) { config.holdsecs = jsonDoc["holdsecs"]; } 
    return true;
}


/*
 * Attempt saving the configuration to a file in SPIFFS
 */
bool saveConfig() {
    Serial.println("saveConfig: ");
    DynamicJsonDocument jsonDoc(500);
  
    jsonDoc["hostname"] = config.hostname;
    jsonDoc["universe"] = config.universe;
    jsonDoc["channels"] = config.channels;
    jsonDoc["delay"] = config.delay;
    jsonDoc["holdsecs"] = config.holdsecs;
  
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
        Serial.println("saveConfig: Failed to open config file for writing");
        return false;
    }
    else {
        Serial.println("saveConfig: Writing to config file /config.json");
        serializeJson(jsonDoc, configFile);
        configFile.close();
        return true;
    }
}

/*
 * Restart the device after a sucessful upload of new firmware via webinterfac
 */
void ota_restart() {
    Serial.println("HTTP: ota_restart");
    Serial.print("hasError: ");
    Serial.println((Update.hasError()) ? "FAIL" : "OK");

    setStatusLED(LED_RED,500);

    String page = "<head><title>"+config.hostname+"</title><meta http-equiv='refresh' content='10;url=/'></head>\n";
    page += "<body><h1 style='text-align: center;'>"+config.hostname+"</h1>\n";
  
    page += "<table style='width:100%;border: 1px solid black; text-align: center;'>\n";
    page += "<tr><td><a href=/>Home<a></td><td><a href=/config>Config</a></td><td><a href=/restart>Restart</a></td><td><b>Update</b></td></tr>\n";
    page += "</table>\n";

    if (Update.hasError()) {
        page += "<p>Error: "+Update.getError();
    } else {
        page += "<p><h1>Update complete - rebooting</h1>";
    }
    
    webServer.sendHeader("Connection", "close");
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send(200, "text/html", page+http_foot());

    delay(1000);
    ESP.restart();
}

/*
 * Upload a firmware update via webinterface
 */
void ota_upload() {    
    HTTPUpload& upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
        Serial.setDebugOutput(true);
        WiFiUDP::stopAll();
        setStatusLED(LED_YELLOW,500);
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        Serial.printf("ota_upload: Upload start, filename: %s, space available: %u  ", upload.filename.c_str(),maxSketchSpace);
        if (!Update.begin(maxSketchSpace)) { //start with max available size
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        Serial.print(".");
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        Serial.println("ota_upload: Upload end");
        if (Update.end(true)) { //true to set the size to the current progress
            Serial.printf("ota_upload: Upload Success, totalSize=%u\n", upload.totalSize);
        } else {
            Serial.print("ota_upload: Error:");
            Update.printError(Serial);
        }
        Serial.setDebugOutput(false);
    }
    yield();
}

/*
 * Display the 404 error message for unknown URLs
 */
void http_error404() {
    Serial.println("HTTP: Error-404");
    String message = "Error 404: File Not Found\n\n";
    message += "URL: ";
    message += webServer.uri();
    message += "\nMethod: ";
    message += (webServer.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += webServer.args();
    message += "\n";
    for (uint8_t i = 0; i < webServer.args(); i++) {
        message += " " + webServer.argName(i) + ": " + webServer.arg(i) + "\n";
    }
    webServer.send(404, "text/plain", message);
}


/*
 * Format an IP address as string
 */
char* IP2String (IPAddress ip) {
    static char a[16];
    sprintf(a, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return a;
}


/*
 * Assemble the common footer string
 */
String http_foot() {
    String foot = "<p><hr style='width:100%; height:1px; border:none; color:red; background:black;'>";
    foot += "<p>ESP-DMX by Markus Baertschi, <a href=https://github.com/markusb>github.com/markusb/esp-dmx</a>\n";
    foot += "</body>\n";
    return foot;
}


/*
 * Assemble the main index and status page
 */
void http_index() {
    Serial.println("HTTP: Sending index page");
  
    String page = "<head><title>"+config.hostname+"</title></head>\n";
//    page += "<body><img src=/dmx512.png stype='width: 50px;'><h1 style='text-align: center;'>"+config.hostname+"</h1>\n";
    page += "<body><h1 style='text-align: center;'>"+config.hostname+"</h1>\n";
  
    page += "<table style='width:100%;border: 1px solid black; text-align: center;'>\n";
    page += "<tr><td><b>Home</b></td><td><a href=/config>Config</a></td><td><a href=/restart>Restart</a></td><td><a href=/update>Update</a></td></tr>\n";
    page += "</table>\n";
  
    page += "<p><table style='width:100%;border: 1px solid black;'>\n";
    page += "<tr><td>Hostname:</td><td>"+config.hostname+"</td></tr>\n";
    page += "<tr><td>Wifi SSID:</td><td>"+WiFi.SSID()+"</td></tr>\n";
    page += "<tr><td>RSSI (signal strngth):</td><td>"; page += last_rssi; page += "</td></tr>\n";
    page += "<tr><td>IP:</td><td>"; page += IP2String(WiFi.localIP()); page += "</td></tr>\n";
    page += "<tr><td>ESP-DMX version (build):</td><td>"; page += version_mayor; page += "."; page += version_minor; page += " ("; page += build; page += ")</td></tr>\n";
    page += "<tr><td>Universe:</td><td>"; page += config.universe; page += "</td></tr>\n";
    page += "<tr><td>Channels:</td><td>"; page += config.channels; page += "</td></tr>\n";
    page += "<tr><td>Delay:</td><td>"; page += config.delay; page += "</td></tr>\n";
    page += "<tr><td>Seconds to hold last frame after signal loss:</td><td>"; page += config.holdsecs; page += "</td></tr>\n";
    page += "<tr><td>FPS:</td><td>"; page += fps; page += "</td></tr>\n";
    page += "<tr><td>Artnet packets seen:</td><td>"; page += artnetPacketCounter; page += " (universe:";
    page += seen_universe; page += ")</td></tr>\n";
    page += "<tr><td>DMX frames sent:</td><td>"; page += dmxFrameCounter; page += "</td></tr>\n";
    page += "<tr><td>DMX packet length:</td><td>"; page += global.length; page += " (channels)</td></tr>\n";
    page += "<tr><td>Status:</td><td>"; page += status_text[status], page += "</td></tr>\n";
    page += "<tr><td>Device temperature:</td><td>"; page += temperature; page += "</td></tr>\n";
    page += "<tr><td>Fan speed (0-1024):</td><td>"; page += fanspeed; page += "</td></tr>\n";
    page += "<tr><td>Device uptime (s):</td><td>"; page += millis()/1000, page += "</td></tr>\n";
    page += "</table>\n";
    page += http_foot();
    
    webServer.send(200, "text/html", page);
}


/*
 * Assemble the configuration form
 * After saving the form is displayed again with the mention 'Configuration saved'
 * and allows to chnage the configuration again
 */
#define POST_REQUEST_SAVE 1
#define POST_REQUEST_FORMDEFAULTS 2
#define POST_REQUEST_WIFIDEFAULTS 3
#define POST_REQUEST_ALLDEFAULTS 4

void http_config() {
    int post_request = 0;
    
    Serial.print("\tHTTP: Config form");

    String head = "<head><title>"+config.hostname+"</title></head>\n";
    
    String body = "<body><h1 style='text-align: center;'>"+config.hostname+"</h1>\n";
    body += "<table style='width:100%;border: 1px solid black; text-align: center;'>\n";
    body += "<tr><td><a href=/>Home<a></td><td><b>Config</b></td><td><a href=/restart>Restart</a></td><td><a href=/update>Update</a></td></tr>\n";
    body += "</table>\n";

    String foot = http_foot();


    if (webServer.method() == HTTP_GET) {
        Serial.println("HTTP: config form GET");
    }
    if (webServer.method() == HTTP_POST) {
        Serial.println("POST: config form POST");
    
        String message = "HTTP POST Request: ";
        for (uint8_t i = 0; i < webServer.args(); i++) {
            message += " " + webServer.argName(i) + ": " + webServer.arg(i) + "\n";
            if (webServer.argName(i) == "hostname") { config.hostname = webServer.arg(i); }
            if (webServer.argName(i) == "universe") { config.universe = webServer.arg(i).toInt(); }
            if (webServer.argName(i) == "channels") { config.channels = webServer.arg(i).toInt(); }
            if (webServer.argName(i) == "delay")    { config.delay = webServer.arg(i).toInt(); }
            if (webServer.argName(i) == "holdsecs") { config.holdsecs = webServer.arg(i).toInt(); }
            if (webServer.argName(i) == "save")     { post_request = POST_REQUEST_SAVE; Serial.println("http_config: save"); }
            if (webServer.argName(i) == "formdefaults") { post_request = POST_REQUEST_FORMDEFAULTS; Serial.println("http_config: formdefaults"); }
            if (webServer.argName(i) == "wifidefaults") { post_request = POST_REQUEST_WIFIDEFAULTS; Serial.println("http_config: wifidefaults"); }
            if (webServer.argName(i) == "alldefaults") { post_request = POST_REQUEST_ALLDEFAULTS; Serial.println("http_config: alldefaults"); }
        }
        if (post_request == POST_REQUEST_SAVE) {
             saveConfig();
             Serial.println(message);
        
             body += "<p><div style='color:red;font-weight:bold;'>Configuration saved</div><p>\n";
        }
        if (post_request == POST_REQUEST_FORMDEFAULTS) {
             body += "<p><div style='color:red;font-weight:bold;'>Resetting to default settings, retainign wifi ... Rebooting !</div><p>\n";          
        }
        if (post_request == POST_REQUEST_WIFIDEFAULTS) {
             body += "<p><div style='color:red;font-weight:bold;'>Resetting wifi config ... Rebooting !</div><p>\n";          
        }
        if (post_request == POST_REQUEST_ALLDEFAULTS) {
             body += "<p><div style='color:red;font-weight:bold;'>Resetting to default settings including wifi ... Rebooting !</div><p>\n";          
        }
    }

    if (post_request <= POST_REQUEST_SAVE) {
        body += "<form id='config' method='post' action='/config'>";
        body += "<p><table style='width:100%;'>\n";
        body += "<tr><td>Hostname:</td><td><input type='text' id='hostname' name='hostname' value='"+config.hostname+"' required></td></tr>\n";
        body += "<tr><td>Universe configured:</td><td><input type='text' id='universe' name='universe' value='";
        body += config.universe;
        body += "' required></td></tr>\n";
        body += "<tr><td>Channels configured:</td><td><input type='text' id='channels' name='channels' value='";
        body += +config.channels;
        body += "' required></td></tr>\n";
        body += "<tr><td>Delay configured:</td><td><input type='text' id='delay' name='delay' value='";
        body += config.delay;
        body += "' required></td></tr>";
        body += "<tr><td>Seconds to hold last state after signal loss:</td><td><input type='text' id='holdsecs' name='holdsecs' value='";
        body += config.holdsecs;
        body += "' required></td></tr>";
        body += "<tr><td></td><td><button name='save' type='submit'>Save Config</button></td></tr>\n";
        body += "<tr><td colspan=2 align=center><button name='formdefaults' type='submit'>Reset config to defaults</button> ";
        body += "<button name='wifidefaults' type='submit'>Reset wifi config</button> ";
        body += "<button name='alldefaults' type='submit'>Reset config & wifi</button></td></tr>\n";
        body += "</table></form>\n";
    }
    webServer.send(200, "text/html", head+body+foot);

    if (post_request == POST_REQUEST_FORMDEFAULTS) {
        defaultConfig();
        saveConfig();
        delay(2000);
        ESP.restart();
    }
    if (post_request == POST_REQUEST_WIFIDEFAULTS) {
        WiFiManager wifiManager;
        wifiManager.resetSettings();
        delay(2000);
        ESP.restart();
    }
    if (post_request == POST_REQUEST_ALLDEFAULTS) {
        defaultConfig();
        saveConfig();
        WiFiManager wifiManager;
        wifiManager.resetSettings();
        delay(2000);
        ESP.restart();
    }
}


/*
 * Assemble the restart form
 * 
 * A get request display the form, a POST request restarts the device
 */
void http_restart () {
    Serial.print("HTTP: Restart page ");
    
    String head = "<head><title>"+config.hostname+"</title></head>\n";
    
    String body = "<body><h1 style='text-align: center;'>"+config.hostname+"</h1>\n";
    body += "<p><table style='width:100%;border: 1px solid black; text-align: center;'>\n";
    body += "<tr><td><a href=/>Home<a></td><td><a href=/config>Config</a></td><td><b>Restart</b></td><td><a href=/update>Update</a></td></tr>\n";
    body += "</table><p>\n";

    String foot = http_foot();

    if (webServer.method() == HTTP_GET) {
        Serial.println("GET (confirmation form)");
        body += "<form id='reset' method='post' action='/restart'>\n";
        body += "<p><table style='width:100%;text-align: center;'><tr><td><button type='submit'>Confirm Restart</button></td></tr></table></form><p>\n";
    }
    if (webServer.method() == HTTP_POST) {
        Serial.println("POST (reset)");
        Serial.println("Resetting device");
        setStatusLED(LED_RED,500); // red
        head = "<head><title>"+config.hostname+"</title><meta http-equiv='refresh' content='15;url=/'></head>\n";
        body += "<h1 style='align: center;'>Resetting ...</h1><p>\n";
        webServer.send(200, "text/html", head+body+foot);
        
        delay(5000);
        ESP.restart();
    }
    
    webServer.send(200, "text/html", head+body+foot);
}


/*
 * Display the firmware update form
 */
void http_update() {
    Serial.println("HTTP: Sending update form");

    String head = "<head><title>"+config.hostname+"</title></head>\n";
    
    String body = "<body><h1 style='text-align: center;'>"+config.hostname+"</h1>\n";
    body += "<table style='width:100%;border: 1px solid black; text-align: center;'>\n";
    body += "<tr><td><a href=/>Home<a></td><td><a href=/config>Config</a></td><td><a href=/restart>Restart</a></td><td><b>Update</b></td></tr>\n";
    body += "</table>\n";

    body += "<form id=\"config\" method=\"post\" action=\"/update\" enctype='multipart/form-data'><p><table>\n";
    body += "<tr><td>Current firmware:</td><td>"; body += build; body += "</td></tr>\n";
    body += "<tr><td>New firmware binary:</td><td><input type=\"file\" id=\"update\" name=\"update\" required></td></tr>\n";
    body += "<tr><td></td><td><button type=\"submit\">Update</button></td></tr>\n";
    body += "</table><p>\n";

    String foot = http_foot();

    webServer.send(200, "text/html", head+body+foot);
}


/*
 * Sends the favicon
 */
void http_favicon () {
    Serial.println("\tSend Favicon");
    webServer.send_P(200, favicon_ctype, favicon_ico, favicon_ico_len);
}


/*
 * Sends the dmx512 image
 */
void http_dmx512png () {
    Serial.println("\tSend DMX512png");
    webServer.send_P(200, dmx512_png_ctype, dmx512_png, dmx512_png_len);
}
