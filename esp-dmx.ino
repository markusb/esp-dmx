/*
 * ESP-DMX
 * 
 * This is firmware for an SP8266 devices. It receives Artnet date
 * over wifi and sends the configured universe out over serial DMX.
 * 
 * It display the device status on a color LED, the serial output
 * and a webinterface. The webinterface also serves to configure
 * and update the device.
 * 
 */

#include <ESP8266WiFi.h>         // https://github.com/esp8266/Arduino
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager
#include <ESP8266mDNS.h>         // For zeroconf
#include <WiFiClient.h>
// #include <ArtnetWifi.h>          // https://github.com/rstephan/ArtnetWifi
#include <ArtnetnodeWifi.h>          // https://github.com/rstephan/ArtnetnodeWifi
#include <Adafruit_NeoPixel.h>
#include <FS.h>
#include "webui.h"
#include "send_break.h"
#include "statusLED.h"
#include "esp-dmx.h"

#define MIN(x,y) (x<y ? x : y)
//#define MAX(x,y) (x>y ? x : y)
#define ENABLE_MDNS

// wifi manager is optional during development as re-flashing from Arduino
// wipes out the SPIFFS and the wifi config
#define WIFIMANAGER
#define MYSSID "ssid"
#define MYPASS "pass"

Config config;
ESP8266WebServer webServer(80);

#define PIN_DMX_OUT    2  // gpio2/D4
#define PIN_DMX_ENABLE 5  // gpio5/D1

// Assemble version/build from compile time date
#define BUILD_YEAR  __DATE__[7],__DATE__[8],__DATE__[9],__DATE__[10]
#define BUILD_MONTH __DATE__[0],__DATE__[1],__DATE__[2]
#define BUILD_DAY   ((__DATE__[4] >= '0') ? (__DATE__[4]) : '0'),__DATE__[5]
#define BUILD_TIME  __TIME__[0],__TIME__[1],__TIME__[3],__TIME__[4]
const char version_text[] = { BUILD_YEAR,BUILD_MONTH,BUILD_DAY,'-',BUILD_TIME,'\0' };
const char* version = &version_text[0];

// Artnet settings
ArtnetnodeWifi artnetnode;

// Global universe buffer
struct globalStruct global;

// counters to keep track of things, display statistics
unsigned long packetCounter = 0;
unsigned long frameCounter = 0;
unsigned long dmxUMatchCounter = 0;
unsigned long dmxPacketCounter = 0;
bool packetReceived = false;
float fps = 0;

//#define ANALYZEJITTER
#ifdef ANALYZEJITTER
#define DMXSENDHISTLEN 256
unsigned long dmxSendHist[DMXSENDHISTLEN];
int dmxSendIdx = 0;
#endif

// keep track of the timing
long tic_loop = 0;   // loop timing
long tic_fps = 0;    // calculate fps
long tic_web = 0;    // webinterface activity
long tic_artnet=0;   // received dmx/artnet frame timestamp
long tic_umatch=0;   // received matching artnet frame timestamp
long tic_status=0;   // timestamp for periodical status on serial port
long tic_looplat=0;  // timestamp to determine loop latency (how much spare time we have)
long last_looplat=0; // saved loop latency for display on serial port

uint16_t seen_universe = 0;  // universe number of last seen artnet frame
int last_rssi;               // Wifi RSSI for display

int temperature = 0;  // temperature in deg c
int tempAdc = 0;      // temperature reading from ADC
#define ANALOGPIN A0  //

#define NEOPIXEL

// Status codes for display
int status;
char * status_text[] = { "", "Booting", "Config not found", "Config found", "Init complete", "Ready", "Serving webrequest", "DMX seen", "DMX received" };
#define STATUS_BOOTING         1  // Red
#define STATUS_CONFIG_NOTFOUND 2  // Red
#define STATUS_CONFIG_FOUND    3  // Yellow
#define STATUS_INIT_COMPLETE   4
#define STATUS_READY           5  // Pink
#define STATUS_WEBREQUEST      6  // Blue
#define STATUS_DMX_SEEN        7  // Cyan
#define STATUS_DMX_RECEIVED    8  // Green

// RGB LED as status display
#ifdef NEOPIXEL
#define PIN_NEOPIXEL 13 // GPIO13/D7
Adafruit_NeoPixel statusLED = Adafruit_NeoPixel(1, PIN_NEOPIXEL, NEO_GRB);
#else
#define PIN_LED_B 14   // GPIO16/D5
#define PIN_LED_G 13   // GPIO13/D7
#define PIN_LED_R 12   // GPIO12/D6
#endif

/*
 * Set the status LED(s) to a specified color
 */
void setStatusLED(int color) {
#ifdef NEOPIXEL
    statusLED.setPixelColor(0,color);
    statusLED.show();
#else
    if (color && 0x0ff0000) digitalWrite(PIN_LED_R, LOW); else digitalWrite(PIN_LED_R, HIGH);
    if (color && 0x000ff00) digitalWrite(PIN_LED_G, LOW); else digitalWrite(PIN_LED_G, HIGH);
    if (color && 0x00000ff) digitalWrite(PIN_LED_B, LOW); else digitalWrite(PIN_LED_B, HIGH);
#endif
}


/*
 * Temperature measurements
 */
 
// https://www.jameco.com/Jameco/workshop/TechTip/temperature-measurement-ntc-thermistors.html
// definitions here for a TTC05104 
const float R0 = 100000;  // Base resiatnce value at base temperature from data sheet
const float B = 4400;     // Beta (from ntc data sheet)
const float T0 = 298.15;  // base temperature 25°C in Kelvin (273.15 + 25)
float betaNTC(float R) {
    float T;
    T = 1/(1/T0 + 1/B*log(R/R0));
    T = T - 273.15;
    return T;
}

float ntcRes;
int readTemperature () {
    // read the ADC
    int i = analogRead(ANALOGPIN);

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
}


/*
 * Artnet packet routine
 * 
 * This routine is called for each received artnet packet
 * If the universe of the received packet matched the configured universe
 * for our device we copy the dmx data to our dmx buffer
 *
 */
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t * data) {
  seen_universe = universe;
  dmxPacketCounter++;
  tic_artnet = millis();

  // print some feedback
/*  if ((millis() - tic_fps) > 1000 && frameCounter > 100) {
    // don't estimate the FPS too frequently
    fps = 1000 * frameCounter / (millis() - tic_fps);
    tic_fps = millis();
    frameCounter = 0;
    Serial.print("onDmxPacket: packetCounter = ");
    Serial.print(packetCounter++);
    Serial.print(",  FPS = ");
    Serial.print(fps);
    Serial.println();
  }
*/

  if (universe == config.universe) {
    packetReceived = true;
    // If the universe matches copy the data from the UDP packet over to the global universe buffer
    tic_umatch = millis();
    dmxUMatchCounter++;
    global.universe = universe;
    global.sequence = sequence;
    if (length < 512) global.length = length;
    for (int i = 0; i < global.length; i++)
        global.data[i] = data[i];
  }
} // onDmxpacket


/*
 * Initialize the device during boot
 */
void setup() {
    // set up serial port and display boot message with version
    Serial.begin(115200);
    while (!Serial) { ; }
    Serial.print("\nESP-DMX: Version: ");
    Serial.print(version);
    Serial.println(" Intializing");

    // set up status LED(s)
#ifdef NEOPIXEL
    statusLED.begin();
#else
    pinMode(PIN_LED_R, OUTPUT);
    pinMode(PIN_LED_G, OUTPUT);
    pinMode(PIN_LED_B, OUTPUT);
#endif

    // Display booting status on LED
    setStatusLED(LED_RED);
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
      setStatusLED(LED_YELLOW);
      delay(200);
    } else {
      Serial.println("ESP-DMX: config not found, setting defaults");
      setStatusLED(LED_WHITE);
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
        setStatusLED(LED_RED);
        Serial.println("Wifi connection failed !!!!!");
    }

    Serial.println("Wifi connected");
    Serial.print("SSID:");
    Serial.println(WiFi.SSID());
    Serial.print("Hostname: ");
    Serial.println(WiFi.hostname());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // Set up webinterface
    Serial.println("ESP-DMX: setting up webserver");

    webServer.onNotFound(http_error404);
    
    webServer.on("/",            HTTP_GET, []()      { tic_web = millis(); http_index(); });
    webServer.on("/favicon.ico", HTTP_GET, []        { tic_web = millis(); http_favicon(); });
    webServer.on("/dmx512.png",  HTTP_GET, []        { tic_web = millis(); http_dmx512png(); });
    webServer.on("/config", webServer.method(), []() { tic_web = millis(); http_config(); });
    webServer.on("/restart", webServer.method(), []()      { tic_web = millis(); http_restart(); });
    webServer.on("/update",      HTTP_GET, []        { tic_web = millis(); http_update(); });
    webServer.on("/update",     HTTP_POST, ota_restart, ota_upload);

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
    artnetnode.setArtDmxCallback(onDmxFrame);

    // initialize timestamps
    tic_loop   = millis();
    tic_fps    = millis();
    tic_web    = 0;
    tic_umatch = 0;
    tic_looplat = 0;
  
    Serial.println("ESP-DMX: setup done");
} // setup

/*
 * Main loop for processing
 */
void loop() {
    readTemperature();
    
    // handle web service
    webServer.handleClient();
  
    // handle artnet
    artnetnode.read();

    // handle zeroconf
    MDNS.update();

    // Main processing here
    if (WiFi.status() != WL_CONNECTED) {
        // If no wifi, then show red LED
        setStatusLED(LED_ORANGE);
        Serial.println("ESP-DMX loop: No wifi connection !!!");
        delay(100);
    } else {
        // Status line every 5 seconds
        if ((millis() - tic_status) > 5000) {
            last_rssi = WiFi.RSSI();
            tic_status=millis();
            Serial.printf("ESP-DMX loop: status = %s, RSSI=%i, dmxPacket=%d (u=%d), dmxUMatch=%d, u=%d, dmx sent=%d, looplat=%d\n",
                           status_text[status],last_rssi,dmxPacketCounter,seen_universe,dmxUMatchCounter,config.universe,frameCounter,last_looplat);
        }      
        if ((millis() - tic_web) < 2000) {
            //
            // If there was a web interaction, then show blue LED for 2 seconds, DMX sending is paused
            //
            status = STATUS_WEBREQUEST;
            setStatusLED(LED_BLUE);
            delay(10);
        } else if ((millis() - tic_umatch) < 5000) {
            //
            // There was a matching artnet frame in the last 5 seconds !
            //
            // Show the green LED and set status
            //
            setStatusLED(LED_GREEN);
            status = STATUS_DMX_RECEIVED;
            digitalWrite(PIN_DMX_ENABLE, HIGH);
            //
            // Send frame at configured framerate
            //
            if (packetReceived || ((millis() - tic_loop) > config.delay)) {
                // this section gets executed at a maximum rate of around 40Hz
                packetReceived = false;
                tic_loop = millis();
                last_looplat = tic_looplat;
                frameCounter++;
                sendBreak();
                Serial1.write(0); // Start-Byte
                // send out the value of the selected channels (up to 512)
                for (int i = 0; i < MIN(global.length, config.channels); i++) {
                    Serial1.write(global.data[i]);
                }
#ifdef ANALYZEJITTER                
                dmxSendHist[dmxSendIdx++]=millis();
                if (dmxSendIdx>=DMXSENDHISTLEN) {
                    // start new history cycle
                    dmxSendIdx=0;
                    // analyze history
                    long hdiff = dmxSendHist[DMXSENDHISTLEN-1] - dmxSendHist[0]; // time between 1st and last frame
                    long hmean = hdiff/DMXSENDHISTLEN;                           // mean time between each frame
                    long jmean = 0;
                    long jmax = 0;
                    long j = 0;
                    for (int i = 1; i<DMXSENDHISTLEN; i++) {
                        j = dmxSendHist[i-1]+hmean - dmxSendHist[i];    // time between consecutive frames from mean time
                        jmean += abs(j);
                        if (j>jmax) jmax=j;
                    }
                    jmean = jmean/DMXSENDHISTLEN;
                    Serial.printf("ESP-DMX jitter(ms): hdiff=%u, hmean=%u jmean=%u, jmax=%u\n",hdiff, hmean,jmean,jmax);
                }
#endif                    
            } else {
                tic_looplat = millis() - tic_loop;
            }
        } else if ((millis() - tic_artnet) < 5000) {
            //
            // There was an artnet frame seen, show cyan LED
            //
            setStatusLED(LED_CYAN);
            status = STATUS_DMX_SEEN;
            digitalWrite(PIN_DMX_ENABLE, LOW);
        } else {
            //
            // No DMX received show redy state
            //
            status = STATUS_READY;
            setStatusLED(LED_PINK);
            digitalWrite(PIN_DMX_ENABLE, LOW);
        }
    }
    
    // limit loop speed
    delay(1);
} // loop
