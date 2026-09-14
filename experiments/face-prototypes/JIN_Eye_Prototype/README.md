# Jin eye comparison prototype

This sketch leaves `JIN_Avatar_V4` untouched. It presents static comparison views for:

- Candidate 3: rounded slots
- Candidate 4: four-cell segmented slots
- Candidate 6: wide shared trough

Each candidate appears at 0%, 35%, 70%, 100%, and 75/45 asymmetric aperture. Nothing moves automatically. Press the ESP32-S3 board's **BOOT** button once to advance to the next view; that view remains displayed indefinitely for photography. Send `BLINK` through Serial Monitor when you specifically want to inspect the blink.

Optional Serial Monitor commands at 115200 baud:

- `AUTO` starts the optional ten-second automatic cycle.
- `3`, `4`, or `6` selects one candidate.
- `0`, `35`, `70`, or `100` selects a symmetric aperture.
- `A` selects the 75/45 asymmetric test.
- `BLINK` performs a synchronized blink.
- `AMBER` displays the four amber brightness candidates.
- `PLATE` displays RGB565 plate colors `0x18E5`, `0x18E4`, and `0x18E3`.

The small labels are test instrumentation and are not part of Jin's final face.
