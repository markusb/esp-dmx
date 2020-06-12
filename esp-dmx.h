/*
 * ESP-DMX
 */

#ifndef ESP_DMX_H
#define ESP_DMX_H

// Global universe buffer
struct globalStruct {
    uint16_t universe;
    uint16_t length;
    uint8_t sequence;
    uint8_t *data;
} ;

// Structure for configurable values
struct Config {
  String hostname;
  String fwURL;
  int universe;
  int channels;
  int delay;
  int holdsecs;
  int pOnShowCh1;
  int pOnShowNumCh;
};

#endif
