# Overview

This implements a Wifi Artnet receiver to receive Artnet frames and send them out over DMX.
The Idea is to fit the modules into DMX equipment and be able to use Wifi instead of DMX cabling.

The module uses WifiManager to connect to a Wifi network on initial startup. On subsequent boots,
it will reconnect to the same network again with, no configuration necessary. The initial hostname
is based on the hardware ID of the ESP8266. This name can be changed and saved.

The Artnet/DMX Universe can be configured, along with the max frame size and the update rate.

A WS2812 RGB LED is used to show the display status. The same status can be seen on the console
output or the webinterface.

The colors displayed are as follows:
- Red:    The device is booting
- Yellow: Connecting to Wifi
- Pink:   Device is ready and conneted to Wifi
- Blue:   Web-interface activity
- Cyan:   Artnet frames are detected, but ignored as the universe does not match
- Green:  Artnet frames are received and transmitted to DMX

Wherever there is activity on the web interface (configuration, monitoring), the LED turns blue. During web interface activity, the DMX512 output is silenced. A smooth web interface and smooth DMX signalling don't go together.

# Special features

- The MAX485 transmitter is switched off, when no frames are sent.
  This places the driver in high-Z mode and allows to use the DMX input with
  the ESP_DMX device still connected.
- Everything is self-contained in the binary. There is no need to upload/maintain
  data in the SPIFFS filesystem. SPIFFS is still used to hold the configuration data
  and as temporary storage for updating the firmware.

# Components

  - Wemos D1 mini
  - MAX485 module
  - WS2812 RGB LED

# Wiring diagram

                            +-----+ 
                       Rst  +  W  +  Tx
                        A0  +  E  +  Rx
                 D0/GPIO16  +  M  +  D1/GPIO5   -> RS422 En
        LED_B <- D5/GPIO14  +  O  +  D2/GPIO4   -> RS422 Tx
        LED_R <- D6/GPIO12  +  S  +  D3/GPIO0*
    NEO/LED_G <- D7/GPIO13  +     +  D4/GPIO2*
                 D8/GPIO15* +  D  +  Gnd
                       3V3  +  1  +  5V
                            +-----+ 


	MAX485:		1 Rx      vcc 8
			2 RxEn   outb 7
			3 TxEn   outa 6
			4 Tx      gnd 5

There is also a schematcics among the files

# Reference

The Sketch by Robert Oosterveld http://robertoostenveld.nl/art-net-to-dmx512-with-esp8266
has server with examples and some code.

