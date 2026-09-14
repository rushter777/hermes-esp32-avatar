# Hermes ESP32 Avatar

An experimental physical avatar for [Hermes Agent](https://github.com/NousResearch/hermes-agent). Hermes speech is sent over Wi-Fi to an ESP32-S3, played through an I2S amplifier and speaker, and represented by an animated face whose mouth follows the audio.

## Current status

Working prototype:

- Hermes desktop and file-based TTS can route to the ESP32 over Wi-Fi.
- The ESP32 falls back to an idle mouth when each utterance ends.
- Hermes falls back to Mac playback when the device is unavailable before speech begins.
- `/jin on`, `/jin off`, and `/jin status` control the currently installed personalized plugin.
- The ST7789V face, MAX98357A amplifier, and speaker have been tested together.

The repository folder has a generic name, while the current working plugin and avatar retain their prototype name, Jin. Generalizing command names, hostname, artwork, and pronunciation behavior is planned before a public release.

## Hardware

The prototype uses the Freenove ESP32-S3 Basic Starter Kit plus a display, an I2S amplifier, and a 3 W, 4 ohm speaker. The Freenove kit includes the GPIO extension board, breadboard, jumper wires, and other basic components needed for assembly.

> **Affiliate disclosure:** As an Amazon Associate I earn from qualifying purchases. Amazonのアソシエイトとして、rushter777は適格販売により収入を得ています。

- [Freenove ESP32-S3 Camera Board Basic Starter Kit](https://amzn.to/4A8Cczn) *(affiliate link; tested kit)*
- [Xicoolee 2-inch 240x320 ST7789V SPI display](https://amzn.to/3UQ9XoW) *(affiliate link; compatible alternative to the tested GMT020-027P display)*
- [SATUY MAX98357A I2S 3 W mono amplifier, four-pack](https://amzn.to/4cF1FGv) *(affiliate link; tested amplifier type)*
- [3 W, 4 ohm speakers with 2.54 mm connectors, two-pack](https://amzn.to/3UQXVvx) *(affiliate link; tested speaker specification)*

Amazon OneLink may redirect these links to a matching product or search results in the visitor's local Amazon marketplace. See [docs/HARDWARE.md](docs/HARDWARE.md) for the exact tested parts, electrical connections, and compatibility notes.

## Layout

- `firmware/hermes_esp32_avatar/` — current ESP32-S3 firmware
- `hermes-plugin/jin-esp32-bridge/` — exact working Hermes plugin snapshot
- `docs/HARDWARE.md` — tested parts and GPIO wiring

## Firmware setup

1. Copy `firmware/hermes_esp32_avatar/secrets.h.example` to `secrets.h` in the same directory.
2. Enter the Wi-Fi network name and password in `secrets.h`.
3. Install the Adafruit GFX and Adafruit ST7789 Arduino libraries.
4. Select `ESP32S3 Dev Module` in Arduino IDE and upload the sketch.

Private Wi-Fi credentials are excluded by `.gitignore`.

## Roadmap

- Generalize the plugin identity and `/jin` command while retaining optional avatar-name pronunciation aliases.
- Replace fixed GPIO and display settings with a documented configuration section.
- Confirm licenses and attribution for Hermes Agent and all included libraries.
