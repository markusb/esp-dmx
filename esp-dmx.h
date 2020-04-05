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

#endif
