# Hardware and wiring

The currently tested build uses:

- Freenove ESP32-S3 WROOM-1 FNK0084 N8R8
- ST7789V 2.0-inch 240x320 SPI display (GMT020-027P VER 1.3)
- MAX98357A I2S mono amplifier
- Passive speaker

## Display

| Display pin | ESP32-S3 GPIO |
|---|---:|
| CS | 41 |
| DC | 42 |
| RST | 47 |
| SDA / MOSI | 21 |
| SCL / SCLK | 14 |
| VCC | 3.3V |
| GND | GND |

## Amplifier

| MAX98357A pin | ESP32-S3 connection |
|---|---:|
| BCLK | GPIO38 |
| LRC | GPIO39 |
| DIN | GPIO40 |
| VIN | 5V |
| GND | GND |

The speaker connects to the amplifier's two output terminals. Do not connect either speaker output terminal to ground.
