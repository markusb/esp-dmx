#include "statusLED.h"
//#include "RemoteDebug.h"
//extern RemoteDebug Debug;

statusLED::statusLED(const Adafruit_NeoPixel& LED) {
    this->LED = LED;
}

void statusLED::handle() {
    if (this->blink_interval > 0) {
        if (millis() > (this->blink_millis + this->blink_interval)) {
//            debugD("blinking on=%d interval=%d blink_millis=%ld t=%ld millis=%ld", this->blink_on, this->blink_interval, this->blink_millis, t, m);
            this->blink_millis = millis();
            if (this->blink_on) {
                this->setPixel(0);
            } else {
                this->setPixel(this->blink_color);
            }
        }
    }
}

void statusLED::setColor(int color) {
    this->blink_interval = 0;
    this->setPixel(color);
}

void statusLED::setColor(int color, int interval) {
    if ((color == this->blink_color) && (interval == blink_interval)) return;
    this->blink_interval = interval;
    this->blink_millis = millis();
    this->blink_color = color;
    this->setPixel(color);
}

void statusLED::setPixel(int color) {
    if (color>0) { this->blink_on = true; }
    else { this->blink_on = false; }
//    debugD("on=%d color=%x",this->blink_on,color);
    this->LED.setPixelColor(0,color);
    this->LED.show();
}
