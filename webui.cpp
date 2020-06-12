/*
 * ESP-DMX web ui
 * 
 * Webinterface related files
 * 
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
//#include <WiFiClientSecure.h>
#include <ESP8266httpUpdate.h>
#include <WiFiManager.h> 
#include <WiFiUdp.h>
#include <FS.h>
#include "webui.h"
#include "favicon.h"
#include "dmx512.h"
#include "statusLED.h"
#include "esp-dmx.h"

//#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
//#include <esp_log.h>

extern ESP8266WebServer webServer;
extern Config config;
extern const char* build;
extern int version_mayor;
extern int version_minor;
extern long dmxloop;
extern int status;
extern statusLED LED;
extern char * status_text[];
extern unsigned long packetCounter;
extern unsigned long dmxUMatchCounter;
extern unsigned long artnetPacketCounter;
extern uint16_t seen_universe;
extern long dmxskip;
extern int last_rssi;
extern void powerOnShow(int,int);
extern globalStruct global;
extern int temperature;
extern int fanspeed;
extern int dmxFrameCounter;
extern long micros_dmxsend;
bool newFwAvailable;
String newFwURL;
extern long debugval;
String debugstring = "nothing yet";
int new_mayor = 0;
int new_minor = 0;

/*
 * Set default config on initial boot if there is no configuration yet
 */
void defaultConfig() {
    config.universe = 0;
    config.channels = 512;
    config.delay = 30;
    config.holdsecs = 30;
    config.hostname = "ESP-DMX-"+WiFi.macAddress().substring(9);
    config.hostname.replace(":","");
    config.fwURL = "http://"+WiFi.gatewayIP(); config.fwURL += "/";
    config.pOnShowCh1 = 0;
    config.pOnShowNumCh = 1;
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
    if (jsonDoc.containsKey("fwURL")) { String fw = jsonDoc["fwURL"]; config.fwURL = fw; } 
    if (jsonDoc.containsKey("pOnShowCh1")) { config.pOnShowCh1 = jsonDoc["pOnShowCh1"]; } 
    if (jsonDoc.containsKey("pOnShowNumCh")) { config.pOnShowNumCh = jsonDoc["pOnShowNumCh"]; } 
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
    jsonDoc["fwURL"] = config.fwURL;
    jsonDoc["pOnShowCh1"] = config.pOnShowCh1;
    jsonDoc["pOnShowNumCh"] = config.pOnShowNumCh;
  
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
 * Assemble the common html header string
 */
#define PAGE_INDEX 1
#define PAGE_CONFIG 2
#define PAGE_RESTART 3
#define PAGE_UPDATE 4
#define PAGE_RESTART 5
String http_head(int pageid) {
    String head =  F("<head><title>"); head += config.hostname; head += F("</title>");
    if (pageid == PAGE_RESTART) { head += F("<meta http-equiv='refresh' content='20;url=/'></head>\n"); } else { head += F("</head>\n"); }
           head += F("<body><h1 style='text-align: center;'>"); head += config.hostname; head += F("</h1>\n");
           head += F("<table style='width:100%;border: 1px solid black; text-align: center;'>\n<tr>");
    if (pageid == PAGE_INDEX)   { head += F("<td><b>Home</b></td>"); }    else { head += F("<td><a href='/'>Home</a></td>"); }
    if (pageid == PAGE_CONFIG)  { head += F("<td><b>Config</b></td>"); }  else { head += F("<td><a href='/config'>Config</a></td>"); }
    if (pageid == PAGE_RESTART) { head += F("<td><b>Restart</b></td>"); } else { head += F("<td><a href='/restart'>Restart</a></td>"); }
    if (pageid == PAGE_UPDATE)  { head += F("<td><b>Update</b></td>"); }  else { head += F("<td><a href='/update'>Update</a></td>"); }
           head += F("</tr>\n</table>\n");

    return head;
}


/*
 * Assemble the common html footer string
 */

String http_foot() {
    String foot =  F("<p><hr style='width:100%; height:1px; border:none; color:red; background:black;'>\n");
           foot += F("<p>ESP-DMX by Markus Baertschi, <a href=https://github.com/markusb>github.com/markusb/esp-dmx</a>\n");
           foot += F("</body>\n");
    return foot;
}


// #define FWHOST "https://raw.githubusercontent.com"
// #define FWDIR "/markusb/esp-dmx/master/"
// #define FWHOST "http://192.168.15.14"
#define FWDIR "/"
#define FWHOST "http://192.168.15.14"
#define FWVERSIONFILE "esp-dmx-release.txt"
#define FWBIN "esp-dmx-"
String fwBaseUrl = (String)FWHOST + (String)FWDIR;
String fwUpdateStatus;
String httpsHost;
#define CORE_DEBUG_LEVEL=5
using namespace BearSSL;
bool checkForNewVersion () {
    char lastSslError[200];
    HTTPClient httpClient;
    String versionURL = fwBaseUrl + (String)FWVERSIONFILE;
    int httpCode = 0;
    String newFWVersion;

    Serial.print("===== checkForNewVersion: URL=");
    Serial.println(versionURL);
    debugstring = "versionURL="+versionURL;
    
    if (versionURL.startsWith("https:")) {
        httpsHost = versionURL.substring(8);
        int i = httpsHost.indexOf("/");
        httpsHost.remove(i);
        debugstring += " host="; debugstring += httpsHost;
        BearSSL::WiFiClientSecure httpsClient;
        httpsClient.setInsecure();
        Serial.printf("Connecting to host %s, port 443\n",httpsHost.c_str());
        for (i = 512; i <= 4096; i=i*2) {
            Serial.printf("    Probing for smaller SSL buffer: %d ",i);
            if (httpsClient.probeMaxFragmentLength(httpsHost,443,i)) {
                Serial.println("OK");
                break;
            } else {
                Serial.println("Nope");
            }
        }
        if (i == 8196) {
            Serial.println("    Warning ! MFLN negotation failed, may get buffer overflow");
        }
        i=1024;
        Serial.printf("    Setting buffer size to %d\n",i);
        httpsClient.setBufferSizes(i,i);
        int e = httpsClient.connect(httpsHost,443);
        if (!e) {
            debugstring += " Connect failed";
            Serial.print("httpsClient.connect failed with error ");
            Serial.println(e);
            e = httpsClient.getLastSSLError(lastSslError,200);
            Serial.printf("LastSSLError: %d %s\n",e,lastSslError);
            httpCode = -1;
            newFwURL = "Failed SSL connect";
        } else {
            debugstring = " ssl connetc ok";
            Serial.printf("ESP free heap: %d\n", ESP.getFreeHeap());
    //        httpCode = httpsClient.GET();      
            String getRequest = "GET "; getRequest += FWDIR; getRequest += FWVERSIONFILE; getRequest += " HTTP/1.1\r\nHost: " + httpsHost + "\r\n" + "User-Agent: ESP8266 esp-dmx\r\n\r\n";
            Serial.println("Sending GET request: "); Serial.println(getRequest);
            httpsClient.print(getRequest);
    //        httpsClient.print(String("GET ") + FWVERSIONFILE + " HTTP/1.1\r\n" +
    //               "Host: " + httpsHost + "\r\n" +          
    //               "Connection: close\r\n\r\n");
            httpsClient.flush();
            Serial.println("Reading returned header");
            while (httpsClient.connected()) {
                String line = httpsClient.readStringUntil('\n');
                Serial.print("    Header: "); Serial.println(line.c_str());
                //    Header: HTTP/1.1 200 OK
                if (line.startsWith("HTTP")) {
                    int i=line.indexOf(" ");
                    String httpcode = line.substring(i+1,i+4);
                    httpCode=httpcode.toInt();
                    Serial.println("    -- Checking for http code: "+httpcode+" "+httpCode);
                }
                if (line == "\r") {  // empty line is end of header
                     break;
                }
            }
    //        newFWVersion = httpsClient.getString();
            Serial.println("Reading returned data");
            while(httpsClient.available()) {
                newFWVersion += httpsClient.readStringUntil('\n');  // Read Line by Line
//                Serial.println(newFWVersion); // Print response
            }
//            httpCode = 200;
        }
    } else {
        // http URL
        httpClient.begin( versionURL );
        httpCode = httpClient.GET();
        newFWVersion = httpClient.getString();
    }
    if( httpCode == 200 ) {
        debugstring += "Sucess: "+newFWVersion;
      
        Serial.printf( "Current firmware version: %d.%d\n",version_mayor, version_minor);

        // extract version number from downloaded file
        // Format: Latest-release: x.y
        int i = newFWVersion.lastIndexOf("Latest-release: ");
        newFWVersion.remove(0,i);
        i = newFWVersion.indexOf(".");
        String sminor = newFWVersion;
        String smayor = newFWVersion;
        sminor.remove(0,i+1);   // Remove everything up to the decimal point
        smayor.remove(i);       // Remove everything including and after the decimal point
        smayor.remove(0,16);    // Remove the text 'Latest-release: '
        new_mayor = smayor.toInt();
        new_minor = sminor.toInt();

        // extract filename from downloaded file
        // Format: Filename: <filename>
        i = newFWVersion.lastIndexOf("Filename: ");
        newFWVersion.remove(0,i);
        i = newFWVersion.lastIndexOf(" ");
        int j = newFWVersion.indexOf("\n");
        newFwURL = (String)FWHOST + (String)FWDIR + newFWVersion.substring(i+1,j); // + newFWVersion + String(i) + String(j);
        
        debugstring += " smayor="; debugstring += smayor; debugstring += " sminor="; debugstring += sminor;
        debugstring += " newVers="; debugstring += new_mayor; debugstring += "."; debugstring += new_minor;
        
        Serial.printf( "Firmware on server: %d.%d\n",new_mayor, new_minor);
        if (new_mayor > version_mayor) {
            newFwAvailable = true;
        } else if ( new_minor > version_minor ) {
            newFwAvailable = true;
        } else {
            newFwAvailable = false;
        }
        if (newFwAvailable) {
//            newFwURL = (String)FWHOST + (String)FWDIR + (String)FWBIN; newFwURL += new_mayor; newFwURL+= "."; newFwURL += new_minor; newFwURL += ".bin";
            debugstring += " new URL="; debugstring += newFwURL;
            Serial.printf("URL: %s\n",newFwURL.c_str());
            fwUpdateStatus = "new firmware "+new_mayor;
            fwUpdateStatus += "."+new_minor;
            fwUpdateStatus += " availabe at "+newFwURL;
        } else {
            fwUpdateStatus = "This the latest version available at "+fwBaseUrl;
        }
    } else {
        debugstring += " Error "; debugstring += httpCode; debugstring += " retrieving "; debugstring += (String)FWHOST + (String)FWDIR + (String)FWVERSIONFILE;
        fwUpdateStatus  = "Error ";
        fwUpdateStatus += String(httpCode);
        fwUpdateStatus += " checking for new firmware at ";
        fwUpdateStatus += fwBaseUrl;
        Serial.printf("Error %d retrieving ",httpCode);
        Serial.println((String)FWHOST + (String)FWDIR + (String)FWVERSIONFILE);
    }

}


/*
 * Restart the device after a sucessful upload of new firmware via webinterfac
 */
#define UPDATE_FILE 1
#define UPDATE_URL 2
void ota_restart() {
    Serial.println("HTTP: ota_restart");
    Serial.print("hasError: ");
    Serial.println((Update.hasError()) ? "FAIL" : "OK");

    int updatetype = 0;
    LED.setColor(LED_RED);

    String page = http_head(PAGE_RESTART);

    for (uint8_t i = 0; i < webServer.args(); i++) {
        if (webServer.argName(i) == "updatefile") { updatetype = UPDATE_FILE; }
        if (webServer.argName(i) == "updateurl")  { updatetype = UPDATE_URL;  }
    }

    if (updatetype == UPDATE_URL) {
        page += F("<p>Updating from URL: "); page += newFwURL;
        
//        t_httpUpdate_return ret = ESPhttpUpdate.update( (BearSSL::WiFiClientSecure)httpsClient, httpsHost, newFwURL );
        ESPhttpUpdate.rebootOnUpdate(false);
        t_httpUpdate_return ret = ESPhttpUpdate.update( newFwURL );
        switch(ret) {
            case HTTP_UPDATE_FAILED:
                page += F("<p>HTTP_UPDATE_FAILED Error: "); page += ESPhttpUpdate.getLastError();
                page += F(" Text: "); page += ESPhttpUpdate.getLastErrorString();
                Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s",  ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
                break;
            case HTTP_UPDATE_NO_UPDATES:
                page += F("<p>HTTP_UPDATE_NO_UPDATES");
                Serial.println("HTTP_UPDATE_NO_UPDATES");
                break;
            case HTTP_UPDATE_OK:
                page += F("<p>HTTP_UPDATE_OK");
                Serial.println("HTTP_UPDATE_OK");
                break;
        }
    }
    if (updatetype == UPDATE_FILE) {
        if (Update.hasError()) {
            page += F("<p>Error: ");
            page += Update.getError();
        } else {
            page += F("<p><h1>Upload from file complete - rebooting</h1>");
        }
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
        LED.setColor(LED_YELLOW);
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
 * Assemble the main index and status page
 */
void http_index() {
    Serial.println("HTTP: Sending index page");

    String page = http_head(PAGE_INDEX);
    page += F("<p><table style='width:100%; border:1px solid black;'>\n");
    page += F("<tr><td>Hostname:</td><td>"); page += config.hostname; page += F("</td></tr>\n");
    page += F("<tr><td>Wifi SSID:</td><td>"); page += WiFi.SSID(); page += F("</td></tr>\n");
    page += F("<tr><td>RSSI (signal strength):</td><td>"); page += last_rssi; page += F("</td></tr>\n");
    page += F("<tr><td>IP:</td><td>"); page += IP2String(WiFi.localIP()); page += F("</td></tr>\n");
    page += F("<tr><td>ESP-DMX version (build):</td><td>"); page += version_mayor; page += "."; page += version_minor; page += " ("; page += build; page += F(")</td></tr>\n");
    page += F("<tr style='border-top: 1px solid black;'><td>Universe:</td><td>"); page += config.universe; page += F("</td></tr>\n");
    page += F("<tr><td>Channels:</td><td>"); page += config.channels; page += F("</td></tr>\n");
    page += F("<tr><td>Delay:</td><td>"); page += config.delay; page += F("</td></tr>\n");
    page += F("<tr><td>Seconds to hold last frame after signal loss:</td><td>"); page += config.holdsecs; page += "</td></tr>\n";
    page += F("<tr style='border-top: 1px solid black;'><td>Artnet packets seen:</td><td>"); page += artnetPacketCounter; page += F(" (universe:");
    page += seen_universe; page += F(")</td></tr>\n");
    page += F("<tr><td>DMX frames sent:</td><td>"); page += dmxFrameCounter; page += F("</td></tr>\n");
    page += F("<tr><td>DMX packet length:</td><td>"); page += global.length; page += F(" (channels)</td></tr>\n");
    page += F("<tr><td>Status:</td><td>"); page += status_text[status], page += F("</td></tr>\n");
    page += F("<tr style='border-top: 1px solid black;'><td>Device temperature:</td><td>"); page += temperature; page += F("</td></tr>\n");
    page += F("<tr><td>Fan speed (0-1024):</td><td>"); page += fanspeed; page += F("</td></tr>\n");
    page += F("<tr><td>Device uptime (s):</td><td>"); page += millis()/1000, page += F("</td></tr>\n");
//    page += F("<tr><td>DMXloop:</td><td>"); page += dmxloop, page += F("</td></tr>\n");
//    page += F("<tr><td>DMX skipped:</td><td>"); page += dmxskip, page += F("</td></tr>\n");
//    page += F("<tr><td>micros DMX send:</td><td>"); page += micros_dmxsend, page += F("</td></tr>\n");
//    page += F("<tr><td>debugval:</td><td>"); page += debugval, page += F("</td></tr>\n");
//    page += F("<tr><td>debugstring:</td><td>"); page += debugstring, page += F("</td></tr>\n");
    page += F("</table>\n");
    page += http_foot();
    
    webServer.send(200, "text/html", page);
}


/*
 * Assemble the main index and status page
 */
void http_pos() {
    Serial.println("HTTP: Sending index page");

    String page = http_head(PAGE_INDEX);
    page += F("<p>PowerOnShow\n");
    page += http_foot();
    
    webServer.send(200, "text/html", page);

    powerOnShow(config.pOnShowCh1,config.pOnShowNumCh);
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

    String head = http_head(PAGE_CONFIG);
    String body; 
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
            if (webServer.argName(i) == "fwURL") { config.fwURL = webServer.arg(i); }
            if (webServer.argName(i) == "universe") { config.universe = webServer.arg(i).toInt(); }
            if (webServer.argName(i) == "channels") { config.channels = webServer.arg(i).toInt(); }
            if (webServer.argName(i) == "delay")    { config.delay = webServer.arg(i).toInt(); }
            if (webServer.argName(i) == "holdsecs") { config.holdsecs = webServer.arg(i).toInt(); }
            if (webServer.argName(i) == "pOnShowCh1") { config.pOnShowCh1 = webServer.arg(i).toInt(); }
            if (webServer.argName(i) == "pOnShowNumCh") { config.pOnShowNumCh = webServer.arg(i).toInt(); }
            if (webServer.argName(i) == "save")     { post_request = POST_REQUEST_SAVE; Serial.println("http_config: save"); }
            if (webServer.argName(i) == "formdefaults") { post_request = POST_REQUEST_FORMDEFAULTS; Serial.println("http_config: formdefaults"); }
            if (webServer.argName(i) == "wifidefaults") { post_request = POST_REQUEST_WIFIDEFAULTS; Serial.println("http_config: wifidefaults"); }
            if (webServer.argName(i) == "alldefaults") { post_request = POST_REQUEST_ALLDEFAULTS; Serial.println("http_config: alldefaults"); }
        }
        if (post_request == POST_REQUEST_SAVE) {
             saveConfig();
             Serial.println(message);
        
             body += F("<p><div style='color:red;font-weight:bold;'>Configuration saved</div><p>\n");
        }
        if (post_request == POST_REQUEST_FORMDEFAULTS) {
             body += F("<p><div style='color:red;font-weight:bold;'>Resetting to default settings, retainign wifi ... Rebooting !</div><p>\n");          
        }
        if (post_request == POST_REQUEST_WIFIDEFAULTS) {
             body += F("<p><div style='color:red;font-weight:bold;'>Resetting wifi config ... Rebooting !</div><p>\n");          
        }
        if (post_request == POST_REQUEST_ALLDEFAULTS) {
             body += F("<p><div style='color:red;font-weight:bold;'>Resetting to default settings including wifi ... Rebooting !</div><p>\n");          
        }
    }

    if (post_request <= POST_REQUEST_SAVE) {
        body += F("<form id='config' method='post' action='/config'>");
        body += F("<p><table style='width:100%;'>\n");
        body += F("<tr><td>Hostname:</td><td><input type='text' id='hostname' name='hostname' value='"); body += config.hostname; body += F("' required></td></tr>\n");
        body += F("<tr><td>Universe configured:</td><td><input type='text' id='universe' name='universe' value='");
        body += config.universe;
        body += F("' required></td></tr>\n");
        body += F("<tr><td>Channels configured:</td><td><input type='text' id='channels' name='channels' value='");
        body += +config.channels;
        body += F("' required></td></tr>\n");
        body += F("<tr><td>Delay configured:</td><td><input type='text' id='delay' name='delay' value='");
        body += config.delay;
        body += F("' required></td></tr>");
        body += F("<tr><td>Seconds to hold last state after signal loss:</td><td><input type='text' id='holdsecs' name='holdsecs' value='");
        body += config.holdsecs;
        body += F("' required></td></tr>");
        body += F("<tr><td>URL for updates:</td><td><input type='text' id='fwURL' name='fwURL' value='");
        body += config.fwURL;
        body += F("' required></td></tr>");
        body += F("<tr><td>PowerOnShow 1st channel:</td><td><input type='text' id='pOnShowCh1' name='pOnShowCh1' value='");
        body += config.pOnShowCh1;
        body += F("' required>(0=Off)</td></tr>");
        body += F("<tr><td>PowerOnShow mode/number of channels:</td><td><input type='text' id='pOnShowNumCh' name='pOnShowNumCh' value='");
        body += config.pOnShowNumCh;
        body += F("' required></td></tr>");
        body += F("<tr><td></td><td><button name='save' type='submit'>Save Config</button></td></tr>\n");
        body += F("<tr><td colspan=2 align=center><button name='formdefaults' type='submit'>Reset config to defaults</button> ");
        body += F("<button name='wifidefaults' type='submit'>Reset wifi config</button> ");
        body += F("<button name='alldefaults' type='submit'>Reset config & wifi</button></td></tr>\n");
        body += F("</table></form>\n");
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

    String head = http_head(PAGE_RESTART);
    String body;
    String foot = http_foot();

    if (webServer.method() == HTTP_GET) {
        Serial.println("GET (confirmation form)");
        body += F("<form id='reset' method='post' action='/restart'>\n");
        body += F("<p><table style='width:100%;text-align: center;'><tr><td><button type='submit'>Confirm Restart</button></td></tr></table></form><p>\n");
    }
    if (webServer.method() == HTTP_POST) {
        Serial.println("POST (reset)");
        Serial.println("Resetting device");
        LED.setColor(LED_RED); // red
//        head = F("<head><title>"); head += config.hostname; head += F("</title><meta http-equiv='refresh' content='15;url=/'></head>\n");
        body += F("<h1 style='align: center;'>Resetting ...</h1><p>\n");
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

    String head = http_head(PAGE_UPDATE);
    String body;
    body += F("<p><table style='width:100%;'>\n");
    body += F("<tr><td>ESP-DMX current version (build):</td><td>"); body += version_mayor; body += "."; body += version_minor; body += " ("; body += build; body += F(")</td></tr>\n");
    if (newFwAvailable) {
        body += F("<form id=\"updateonline\" method=\"post\" action=\"/update\" enctype='multipart/form-data'>");
        body += F("<tr><td>New online version available:</td><td>"); body += new_mayor; body += "."; body += new_minor; body += F("</td></tr>\n");
        body += F("<tr><td></td><td><button name='updateurl' type=\"submit\">Update from online</button>&nbsp;(");
        body += newFwURL += F(")</td></tr>");
        body += F("</form>");
    } else {
        body += F("<tr><td></td><td>"); body += fwUpdateStatus; body += F("</td></tr>\n");    
    }
    body += F("<form id=\"updatefile\" method=\"post\" action=\"/update\" enctype='multipart/form-data'>");
    body += F("<tr><td>New firmware image file:</td><td><input type=\"file\" id=\"update\" name=\"update\" required></td></tr>\n");
    body += F("<tr><td></td><td><button name='updatefile' type=\"submit\">Upload and update from File</button>");
    body += F("</td></tr>\n</form></table><p>\n");

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
