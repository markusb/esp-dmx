/*
 * Stuff for the status LED display
 */

#ifndef _STATUSLED_
#define _STATUSLED_

#include <Adafruit_NeoPixel.h>    // Driver for the WS2812 color LED

// Color codes for LED, Format 0xrrggbb
#define LED_OFF    0x0000000
#define LED_RED    0x0660000  // Initial boot
#define LED_ORANGE 0x0553300  // Wifi lost
#define LED_YELLOW 0x0666600  // Setting up Wifi
#define LED_PINK   0x0660066  // Connected to wifi, ready for DMX
#define LED_CYAN   0x0006666  // Observing DMX, but other universe
#define LED_GREEN  0x0006600  // Receiving and transmitting DMX
#define LED_BLUE   0x0000066  // Processing webrequest
#define LED_WHITE  0x0666666  // Config not found, using defaults

class statusLED {
    public:
        statusLED(const Adafruit_NeoPixel&);

        void setColor(int);
        void setColor(int, int);
        void handle();

    private:
        void setPixel(int);
        Adafruit_NeoPixel LED;
        int  blink_color;
        int  blink_interval;
        long blink_millis;
        bool blink_on;
};
#endif
