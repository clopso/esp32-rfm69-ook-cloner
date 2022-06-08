# ESP32 - RFM69

## Installation (Original)

```Shell
git clone https://github.com/nopnop2002/esp-idf-rf69
cd esp-idf-rf69
idf.py set-target {esp32/esp32s2/esp32s3/esp32c3}
idf.py menuconfig
idf.py flash
```

## Installation (Modified)

```Shell
git clone https://github.com/clopso/RFM69.git
cd RFM69
idf.py set-target {esp32/esp32s2/esp32s3/esp32c3}
idf.py menuconfig
idf.py flash
```

## Wirering

|RFM69||ESP32|
|:-:|:-:|:-:|
|MISO|--|GPIO19|
|SCK|--|GPIO18|
|MOSI|--|GPIO23|
|CSN|--|GPIO5|
|RESET|--|GPIO4|
|GND|--|GND|
|VCC|--|3.3V|

## Software to help

[sdrsharp](https://www.scivision.dev/sdr-sharp-ubuntu/)

[URH](https://github.com/jopohl/urh)

### Ref Links

[RF 433.92MHz OOK frame cloner](https://github.com/texane/ooklone)

[OOK transceiver library](https://github.com/kobuki/RFM69OOK)
