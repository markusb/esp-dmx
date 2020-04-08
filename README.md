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
  the ESP-DMX device still connected.
- Everything is self-contained in the binary. There is no need to upload/maintain
  data in the SPIFFS filesystem. SPIFFS is used to hold the configuration data
  and as temporary storage for updating the firmware.

# Limitations

- My cheap PARs semm not to like frames shorter than 512 channels.

# Components / Hardware

Most ESP8266 modules or boards can be used. Personally I use the Wemos D1
with the protoboard shield. The circuit is built on the protoboard.

  - Wemos D1 mini
  - MAX485 module or chip
  - WS2812 RGB LED
  - NPN transistor (with enough current for your fan), optional
  - 100k NTC for temperature measurement, optional

# Wiring diagram

         Wemos D1:
                            +-----+ 
                       Rst  +  W  +  Tx
          NTC <-        A0  +  E  +  Rx
          Fan <- D0/GPIO16  +  M  +  D1/GPIO5   -> RS422 En
        LED_B <- D5/GPIO14  +  O  +  D2/GPIO4 
        LED_R <- D6/GPIO12  +  S  +  D3/GPIO0*
    NEO/LED_G <- D7/GPIO13  +     +  D4/GPIO2*  -> RS422 Tx
                 D8/GPIO15* +  D  +  Gnd
                       3V3  +  1  +  5V
                            +-----+ 


            MAX485:	1 Rx      vcc 8  -> Wemos D1 3V3 power
		        2 RxEn   outb 7  -> DMX fixture
	 Wemod D1 D1 <- 3 TxEn   outa 6  -> DMX fixture
	 Wemod D1 D4 <- 4 Tx      gnd 5  -> Wemos D1 Gnd

There is are [schematics](Schematics.png) and a [wiring diagram](Wiring-Diagram.png) among the files.

Some remarks:

- There is a prototype board for the D1 on which the Max485 fits nicely
- I don't use a 120 Ohm termination resistor as it is not necessary with the short
  cable length within the fixture itself and would require to be disconnected if
  the fixture cabled conventionally.

# ToDo

See the [todo list](engineering-notes/todo.txt)

# Reference

The Sketch by Robert Oosterveld http://robertoostenveld.nl/art-net-to-dmx512-with-esp8266
has served as starting point.

