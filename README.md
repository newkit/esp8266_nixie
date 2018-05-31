# esp8266_nixie
ESP8266 implementation for Nixie Shield NCS314 from GRA AFCH: https://gra-afch.com/catalog/shield-nixie-clock-for-arduino/nixie-tubes-clock-arduino-shield-ncs314-for-xussr-in-14-nixie-tubes/

Tested on a WEMOS D1 (retired model)
```
PIN Layout Nixie Shield <-----> Wemos D1 ESP 8266
                    GND <-----> GND
                    VIN <-----> VIN
                    SCK <-----> D13/SCK/D5
                    MOSI<-----> D11/MOSI/D7
                    LE  <-----> D8
                    SDA <-----> D14/SDA
                    SCL <-----> D15/SCL
```
Not all functions of the shield (e.g, RBG LEDs, buzzer) are connected
