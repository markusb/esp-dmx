/*
 * ESP-DMX
 * 
 * This is firmware for ESP8266 devices. It receives Artnet data
 * over wifi and sends the configured universe out over serial DMX.
 * 
 * The device status is shown on a color LED
 * Serial DMX output is sent via the second serial port
 * A webinterface serves to configure and update the device
 * 
 */

#include <ESP8266WiFi.h>          // https://github.com/esp8266/Arduino
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <ESP8266mDNS.h>          // For zeroconf
#include <WiFiClient.h>
#include <ArtnetnodeWifi.h>       // https://github.com/rstephan/ArtnetnodeWifi
#include <Adafruit_NeoPixel.h>    // Driver for the WS2812 color LED
//#define REMOTEDEBUG
#ifdef REMOTEDEBUG
#include "RemoteDebug.h"          // https://github.com/JoaoLopesF/RemoteDebug
#endif
#include <FS.h>
#include "webui.h"
#include "send_break.h"
#include "statusLED.h"
#include "esp-dmx.h"

#define MIN(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a < _b ? _a : _b; })
#define MAX(a,b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a > _b ? _a : _b; })

// wifi manager can be replaced during development with harcoded ssid/password during development
// as re-flashing from Arduino wipes out the SPIFFS and the wifi config
//#define WIFIMANAGER
#ifndef WIFIMANAGER
#include "wifi-credentials.h"
#endif

// Struct for configurable values
struct Config config;

// Global universe buffer
struct globalStruct global;

ESP8266WebServer webServer(80);

#ifdef REMOTEDEBUG
RemoteDebug Debug;
#endif

// Assemble build string from compile time date
#define BUILD_YEAR  __DATE__[7],__DATE__[8],__DATE__[9],__DATE__[10]
#define BUILD_MONTH __DATE__[0],__DATE__[1],__DATE__[2]
#define BUILD_DAY   ((__DATE__[4] >= '0') ? (__DATE__[4]) : '0'),__DATE__[5]
#define BUILD_TIME  __TIME__[0],__TIME__[1],__TIME__[3],__TIME__[4]
const char build_text[] = { BUILD_YEAR,BUILD_MONTH,BUILD_DAY,'-',BUILD_TIME,'\0' };
const char* build = &build_text[0];
int version_mayor = 1;
int version_minor = 4;

// Artnet settings
ArtnetnodeWifi artnetnode;

// counters to keep track of things, display statistics
unsigned long packetCounter = 0;
unsigned long dmxFrameCounter = 0;
unsigned long dmxUMatchCounter = 0;
unsigned long artnetPacketCounter = 0;
bool packetReceived = false;    // Artnet packet in buffer waiting to be sent as DMX, gets reset after sending DMX
unsigned long holdframe = 0;    // holding last valid frame for some time
unsigned long debugval = 0;

// keep track of the timing
long millis_web = 0;            // webinterface activity
long millis_artnetreceived=0;   // received dmx/artnet frame timestamp
long millis_dmxready=0;         // received matching artnet frame timestamp
long millis_serialstatus=0;     // timestamp for periodical status on serial port
long millis_dmxsend = 0;        // timestamp for limiting the dmx transmit rate
long millis_statusled = 0;      // for status led change
long millis_checkversion = 0;   // timestamp to check for new version
long dmxskip = 0;       // counter for artnet frames not sent as DMX
long millis_analogread = 0;
long dmxloop;
long micros_dmxsend = 0;
uint16_t seen_universe = 0;  // universe number of last seen artnet frame
int last_rssi;               // Wifi RSSI for display

int temperature = 0;  // temperature in deg c
int tempAdc = 0;      // temperature reading from ADC

//#define VERSIONCHECKINTERVAL (3600 * 24 * 7 * 1000)   // Check for new version once a week
#define VERSIONCHECKINTERVAL 10000

int fanspeed = 0;     // speed of the fan

// Status codes for display
int status;
char * status_text[] = { "", "Booting", "Config not found", "Config found", "Init complete", "Ready", "Serving webrequest", "DMX seen", "DMX received", "DMX holding" };
#define STATUS_BOOTING         1  // Red
#define STATUS_CONFIG_NOTFOUND 2  // Red
#define STATUS_CONFIG_FOUND    3  // Yellow
#define STATUS_INIT_COMPLETE   4
#define STATUS_READY           5  // Pink
#define STATUS_WEBREQUEST      6  // Blue
#define STATUS_DMX_SEEN        7  // Cyan
#define STATUS_DMX_RECEIVED    8  // Green
#define STATUS_DMX_HOLDING     9  // Green


#define PIN_DMX_OUT    2  // gpio2/D4
#define PIN_DMX_ENABLE 5  // gpio5/D1
#define PIN_ANALOG A0     // A0
#define PIN_FAN 16        // GPIO16/D0

// RGB LED as status display
#define PIN_NEOPIXEL 13 // GPIO13/D7
Adafruit_NeoPixel neoPixel = Adafruit_NeoPixel(1, PIN_NEOPIXEL, NEO_GRB);

statusLED LED = statusLED(neoPixel);

/*
 * Temperature measurements
 */

/*
 * Convert NTC resistance to temparature using beta function
 * https://www.jameco.com/Jameco/workshop/TechTip/temperature-measurement-ntc-thermistors.html
 * definitions here are for a TTC05104 
 */
const float R0 = 100000;  // Base resiatnce value at base temperature from data sheet
const float T0 = 298.15;  // base temperature 25°C in Kelvin (273.15 + 25)
const float B = 4400;     // Beta (from NTC data sheet)
float betaNTC(float R) {
    float T;
    T = 1/(1/T0 + 1/B*log(R/R0));
    T = T - 273.15;
    return T;
}

/*
 * read NTC and convert ADC reading to temperature
 * 1) Smoothe the ADC reading
 * 2) convert to resistance
 * 3) convert to temperature using betaNTC function
 */
int readTemperature () {
    float ntcRes;

    // only read the ADC every 500ms, otherwise Wifi breaks 
    if (millis() < (millis_analogread + 500)) { return 0; }
    millis_analogread = millis();
    
    // read the ADC
    int i = analogRead(PIN_ANALOG);

    // initialize the smooting on the first call
    if (tempAdc == 0) {
        tempAdc = i;
    } else {
        // smooth the ADC reading, the new reading get 10% weight
        tempAdc = ((tempAdc * 9) + i) / 10;
    }
    
    // calculate the NTC resistance
    ntcRes = 330000000/tempAdc-320000;

    // calculate temparature from NTC resistance
    temperature = int(betaNTC(ntcRes));
#ifdef REMOTEDEBUG    
    debugD("temp=%d ntcRes=%d i=%d",temperature,ntcRes,i);
#endif
    return temperature;
}


/*
 * Set fan speed
 * for now just on or off, analog (PWM) seems to be as noisy as full speed
 */
void setFan(int speed) {
    if (speed == 0) {
        digitalWrite(PIN_FAN,LOW);
    }
    if (speed > 0) {
        digitalWrite(PIN_FAN,HIGH);
//        analogWrite(PIN_FAN,speed);
    }
    fanspeed = speed;
}

/*
 * Control the fan with respect to temperature
 */
void fanControl () {
    if (fanspeed == 0) {
        if (temperature > 36) {
            setFan(550);   
        }
    } else {
        if (temperature < 32) {
            setFan(0);           
        }
    }
//    else if (temperature > 40) setFan(1024);
//    else setFan(0);
}


/*
 * Send DMX data from buffer out through serial port 1
 */
void sendDmxData(int delay) {
    dmxloop = millis() - millis_dmxsend;
    if ((millis() - millis_dmxsend ) >= delay) {
        millis_dmxsend  = millis();
        dmxFrameCounter++;
        micros_dmxsend = micros();
        sendBreak();
        Serial1.write(0); // Start-Byte
        // send out the value of the selected channels (up to 512)
        for (int i = 0; i < MIN(global.length, config.channels); i++) {
            Serial1.write(global.data[i]);
        }
        micros_dmxsend = micros()-micros_dmxsend;
    } else {
        dmxskip++;
    }
}

/*
 * Artnet packet routine
 * 
 * This routine is called for each received artnet packet
 * If the universe of the received packet matches the configured universe
 * we copy the dmx data to the dmx buffer
 *
 */
void onArtnetFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t * data) {
    seen_universe = universe;
    artnetPacketCounter++;
    millis_artnetreceived = millis();
    
    if (universe == config.universe) {
        packetReceived = true;
        // If the universe matches copy the data from the UDP packet over to the global universe buffer
        millis_dmxready = millis();
        dmxUMatchCounter++;
        global.universe = universe;
        global.sequence = sequence;
        if (length < 512) global.length = length;
        for (int i = 0; i < global.length; i++)
            global.data[i] = data[i];
    }
}


/*
 * Set fill dmx buffer to value every 8 channels  
 */
void setDmxBuf(int c, uint8_t v) {
    for (int i=0; i<=64; i++) {
        global.data[c+i*8]=v;
    }
}

uint8_t pOnPat[8][8] = {{ 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00 },
                        { 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff },
                        { 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 },
                        { 0xff, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00 },
                        { 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00 },
                        { 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff },
                        { 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00 },
                        { 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00 }};

/*
 * Show some light pattern after boot
 */
void powerOnShow (int ch1, int numch) {
#ifdef REMOTEDEBUG
    debugD("powerOnShow: ch1=%d, chnu=%d\n",ch1,numch);
#endif    
    if (ch1==0) { return; } // poweron show disabled

    // enable DMX driver
    digitalWrite(PIN_DMX_ENABLE, HIGH);

    // mode 0 = some pattern on all DMX channels
    if (numch==0) {
        for (int i=0; i<=60; i++) {
            for (int j=1; j<=8; j++) {
                setDmxBuf(ch1+j-2,abs(int(0xff*sin(i*PI/20+j*PI/8))));
//                global.data[ch1+j-2] = abs(int(0xff*sin(i*PI/20+j*PI/8)));
            }
            sendDmxData(0);
            delay(20); 
        }
        for (int i=1; i<=8; i++) {
            for (int j=1; j<=8; j++) {
                setDmxBuf(i,pOnPat[i][j]);
            }
            sendDmxData(0);
            delay(10); 
        }
        digitalWrite(PIN_DMX_ENABLE, LOW);
        return;
    }    

    // special pattern for single channel
    if (numch==1) {
#ifdef REMOTEDEBUG
        debugD("Pattern1: start");
#endif
        LED.setColor(LED_BLUE);
        delay(100);
        for (int i=0; i<60; i++) {
#ifdef REMOTEDEBUG          
            debugD("Pattern1: i%d, led=%d\n",i,0xff*(i%2));
#endif
            global.data[ch1-1] = abs(int(0xff*sin(i*PI/20)));
//            global.data[ch1-1] = 0xff*(i%2);
            sendDmxData(0);
            LED.setColor(LED_YELLOW);
            delay(20);
            LED.setColor(LED_BLUE);
            
//            delay(100);
        }
        digitalWrite(PIN_DMX_ENABLE, LOW);
        return;
    }

    // pattern for more than 1 channel
    for (int i=0; i<=60; i++) {
        for (int j=1; j<=numch; j++) {
            global.data[ch1+j-2] = abs(int(0xff*sin(i*PI/20+j*PI/numch)));
        }
        sendDmxData(0);
        delay(20); 
    }
    digitalWrite(PIN_DMX_ENABLE, LOW);
    return;
}

/*
 * Initialize the device during boot
 */
void setup() {
    // set up serial port and display boot message with version
    Serial.begin(115200);
    while (!Serial) { ; }
    Serial.printf("\nESP-DMX: Version %d.%d, Build %s Initializing ...\n",version_mayor,version_minor,build);

    // set up status LED(s)
    millis_statusled = millis();

    neoPixel.begin();

    pinMode(PIN_FAN, OUTPUT);
    setFan(1024);

    // Display booting status on LED
    LED.setColor(LED_RED);
    status = STATUS_BOOTING;
    
    // set up 2ns serial port for DMX output and a pin for the Max485 enable
    pinMode(PIN_DMX_ENABLE, OUTPUT);
    digitalWrite(PIN_DMX_ENABLE, LOW);   // disable the Max driver initially
    Serial1.begin(250000, SERIAL_8N2);

    // Set up DMX buffer
    global.universe = 0;
    global.sequence = 0;
    global.length = 512;
    global.data = (uint8_t *)malloc(512);
    for (int i = 0; i < 512; i++) global.data[i] = 0;

    // Start SPIFFS used for configuration
    Serial.println("ESP-DMX: initializing SPIFFS");
    SPIFFS.begin();

    // Attempt to get config from SPIFFS
    Serial.println("ESP-DMX: load config");
    if (loadConfig()) {
        Serial.println("ESP-DMX: config loaded");
        LED.setColor(LED_YELLOW);
        delay(200);
    } else {
        Serial.println("ESP-DMX: config not found, setting defaults");
        LED.setColor(LED_WHITE);
        defaultConfig();
        delay(200);
    }

    Serial.print("Hostname: ");Serial.println(config.hostname);
    Serial.print("Universe: ");Serial.println(config.universe);
    Serial.print("Channels: ");Serial.println(config.channels);
    Serial.print("Delay:    ");Serial.println(config.delay);

    // Start Wifi
#ifdef WIFIMANAGER
    Serial.println("ESP-DMX: starting wifiManager");
  
    WiFiManager wifiManager;
    // wifiManager.resetSettings();
    WiFi.hostname(config.hostname.c_str());
    wifiManager.setAPStaticIPConfig(IPAddress(192, 168, 1, 1), IPAddress(192, 168, 1, 1), IPAddress(255, 255, 255, 0));
    // wifiManager.autoConnect(host);
  
    wifiManager.autoConnect();
#else  
    Serial.print("ESP-DMX: connecting to wifi '"MYSSID"': ");
    WiFi.hostname(config.hostname.c_str());
    WiFi.begin(MYSSID, MYPASS);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(2000);
    }
    Serial.println();
#endif

    if (WiFi.status() != WL_CONNECTED) {
        LED.setColor(LED_RED);
        Serial.println("Wifi connection failed !!!!!");
    }

    Serial.println("Wifi connected");
    Serial.print("SSID:");
    Serial.println(WiFi.SSID());
    Serial.print("Hostname: ");
    Serial.println(WiFi.hostname());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

#ifdef REMOTEDEBUG
    // set up RemoteDebug
    Debug.begin(config.hostname);
    Debug.setResetCmdEnabled(true); // Enable the reset command
#endif
    
    // Set up webinterface
    Serial.println("ESP-DMX: setting up webserver");

    webServer.onNotFound(http_error404);
    
    webServer.on("/",            HTTP_GET, []()       { millis_web = millis(); http_index(); });
    webServer.on("/favicon.ico", HTTP_GET, []         { millis_web = millis(); http_favicon(); });
    webServer.on("/dmx512.png",  HTTP_GET, []         { millis_web = millis(); http_dmx512png(); });
    webServer.on("/config", webServer.method(), []()  { millis_web = millis(); http_config(); });
    webServer.on("/restart", webServer.method(), []() { millis_web = millis(); http_restart(); });
    webServer.on("/update",      HTTP_GET, []         { millis_web = millis(); http_update(); });
    webServer.on("/update",     HTTP_POST, ota_restart, ota_upload);
    webServer.on("/pos",         HTTP_GET, []         { http_pos(); });

    webServer.begin();

    // announce the hostname and web server through zeroconf
    Serial.println("ESP-DMX: enabling zeroconf for "+config.hostname+".local");
    MDNS.begin(config.hostname.c_str());
    MDNS.addService("http", "tcp", 80);

    // initialize artnet
    Serial.println("ESP-DMX: starting artnet");
    artnetnode.setName(config.hostname.c_str());
    artnetnode.setNumPorts(1);
    artnetnode.enableDMXOutput(0);
    artnetnode.begin();
    artnetnode.setArtDmxCallback(onArtnetFrame);
    
    // initialize timestamps
    millis_dmxsend  = millis()-config.delay;
    millis_analogread = millis();
    millis_web    = 0;
    millis_dmxready = 0;
    millis_checkversion = 0;
    Serial.println("ESP-DMX: setup done");
    
    powerOnShow(config.pOnShowCh1,config.pOnShowNumCh);   
} // setup

/*
 * Main loop for processing
 */
void loop() {
#ifdef REMOTEDEBUG
    Debug.handle();
#endif
    LED.handle();
    
    readTemperature();

    fanControl();
    

    // handle web service
    webServer.handleClient();
  
    // handle artnet
    artnetnode.read();

    // handle zeroconf
    MDNS.update();

    // Main processing here
    if (WiFi.status() != WL_CONNECTED) {
        // If no wifi, then show red LED
        LED.setColor(LED_ORANGE);
        Serial.println("ESP-DMX loop: No wifi connection !!!");
        delay(500);
    } else {
        if ((millis() - millis_web) < 1000) {
            //
            // If there was a web interaction, then show blue LED for 2 seconds, DMX sending is paused
            //
            status = STATUS_WEBREQUEST;
//            LED.setColor(LED_BLUE);
        }
        if ((millis() - millis_dmxready) < 2000) {
            //
            // There was a matching artnet frame in the last 2 seconds !
            //
            // Show the green LED and set status
            //
            LED.setColor(LED_GREEN);
            status = STATUS_DMX_RECEIVED;
            digitalWrite(PIN_DMX_ENABLE, HIGH);
            //
            // Send frame at configured framerate
            //
            packetReceived = false;
            sendDmxData(config.delay);
        } else if ((millis() - millis_dmxready) < config.holdsecs*1000) {
            //
            // There was matching artnet frame in the last config.holdsecs seconds !
            //
            // Continue to send DMX frames for holdsec seconds
            //
            LED.setColor(LED_GREEN,150);
            status = STATUS_DMX_HOLDING;
            digitalWrite(PIN_DMX_ENABLE, HIGH);
            sendDmxData(config.delay);
        } else if ((millis() - millis_artnetreceived) < 2000) {
            //
            // There was an artnet frame seen, show cyan LED
            //
            LED.setColor(LED_CYAN);
            status = STATUS_DMX_SEEN;
            digitalWrite(PIN_DMX_ENABLE, LOW);
        } else {
            //
            // No DMX received show redy state
            //
            status = STATUS_READY;
            LED.setColor(LED_GREEN,1000);
            digitalWrite(PIN_DMX_ENABLE, LOW);
        }
    }
    // Status line every 5 seconds
    if ((millis() - millis_serialstatus) > 5000) {
        last_rssi = WiFi.RSSI();
        millis_serialstatus = millis();
        Serial.printf("ESP-DMX loop: status = %s, RSSI=%i, dmxPacket=%d (u=%d), dmxUMatch=%d, u=%d, dmx sent=%d\n",
                       status_text[status],last_rssi,artnetPacketCounter,seen_universe,dmxUMatchCounter,config.universe,dmxFrameCounter);
#ifdef REMOTEDEBUG                       
        debugV("ESP-DMX: status = %s, RSSI=%i, dmxPacket=%d (u=%d), dmxUMatch=%d, u=%d, dmx sent=%d",
                       status_text[status],last_rssi,artnetPacketCounter,seen_universe,dmxUMatchCounter,config.universe,dmxFrameCounter);
#endif                       
    }      

    debugval = millis() - millis_checkversion;
    if ((millis() - millis_checkversion ) > VERSIONCHECKINTERVAL ) {
        millis_checkversion = millis();
        checkForNewVersion();
    }
    // limit loop speed
    delay(1);
} // loop
