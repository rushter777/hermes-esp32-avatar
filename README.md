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

## Before publishing

- Generalize the plugin identity and `/jin` command while retaining optional avatar-name pronunciation aliases.
- Replace fixed GPIO and display settings with a documented configuration section.
- Add reproducible plugin and firmware tests.
- Confirm licenses and attribution for Hermes Agent and all included libraries.
- Add a project license chosen by the repository owner.
