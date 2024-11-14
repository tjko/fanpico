# FANPICO-0804D PCB

PCB Size: 135mm x 81.5mm

![PCB Render](board.png)

## Change Log

v1.3
- Add QWIIC connector (to simplify connecting I2C sensors).
- Add 1-Wire connector
- Change J1 to a combination footprint (allows mounting either Floppy or DC connector).
- Slightly improved ESD protection (TVS diodes added for most connectors)

v1.2c
- Add support for using either SN74AHCT125 or SN74AHCT126 (as U1 and U2).

v1.2b
- C5 was not derated correctly. Replace with 25V part for proper derating.

v1.2
- Add alternate OLED display header with alternate pinout (J2)
- Change J18 header to 2x5 pins (with extra pins often needed for SPI displays)
- Change pull-ups to pull-downs on PWM signal pins on MBFAN inputs.

v1.1
- Add support for direct mounting either Pico or Pico W
- Remove unused footprints
- Fix errata from v1.0
- Errata:
  - JP11 Silkscreen incorrect: 10K and 100K labels should be the otherway around.

v1.0 
- First Prototye
- Based on 0804 v1.3, with added OLED (I2c) display module support
- Errata:
  - U7 pin 7 (VEE) should be grounded.
    Workaround: add solder bridge between pins 7 & 8.

