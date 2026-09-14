# Hardware and wiring

The currently tested build uses:

- Freenove ESP32-S3 WROOM-1 FNK0084 N8R8
- ST7789V 2.0-inch 240x320 SPI display (GMT020-027P VER 1.3)
- MAX98357A I2S mono amplifier
- 3 W, 4 ohm speaker

The Freenove Basic Starter Kit includes the GPIO extension board, breadboard, jumper wires, and other basic components used for this prototype.

## Hardware links

> **Affiliate disclosure:** As an Amazon Associate I earn from qualifying purchases. Amazonのアソシエイトとして、rushter777は適格販売により収入を得ています。

| Part | Link | Compatibility status |
|---|---|---|
| Freenove ESP32-S3 Camera Board Basic Starter Kit | [Amazon](https://amzn.to/4A8Cczn) *(affiliate link)* | Tested kit |
| Xicoolee 2-inch 240x320 ST7789V SPI display | [Amazon](https://amzn.to/3UQ9XoW) *(affiliate link)* | Compatible alternative; the physical prototype uses GMT020-027P VER 1.3 |
| SATUY MAX98357A I2S 3 W mono amplifier, four-pack | [Amazon](https://amzn.to/4cF1FGv) *(affiliate link)* | Same amplifier type used by the prototype |
| 3 W, 4 ohm speakers with 2.54 mm connectors, two-pack | [Amazon](https://amzn.to/3UQXVvx) *(affiliate link)* | Tested electrical specification |

Amazon OneLink may redirect a visitor to a matching product or search results in their local Amazon marketplace. Product availability and board layout can vary, so confirm the controller, pin labels, voltage, impedance, and power rating before wiring.

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

The 3 W, 4 ohm speaker connects to the amplifier's two output terminals. Do not connect either speaker output terminal to ground.
