# JIN AVATAR — Eye Comparison Prototype Spec (bounded)

Scope: eye architecture only. Everything else frozen. No prompts, no main-spec revision. Panel: 240 x 320 portrait, ST7789V.

---

## 1. Priority order

| Rank | Priority | Type |
|---|---|---|
| 1 | Recognisably present as a face | **Gate** |
| 2 | Resistance to the friendly-appliance look | **Decisive** |
| 3 | Emotional range | Ranked |
| 4 | Instrument identity | Ranked, means not end |
| 5 | Technical simplicity | Lowest |

**How conflicts resolve:**

- **#1 is a gate, not a score.** A candidate that does not read as a face within one second at desk distance is out before anything else is considered. This is the only hard pass/fail.
- **#2 is the tiebreaker among candidates that pass the gate.** Not because it outranks face-ness, but because every candidate that survives #1 will look acceptably face-like, and the one that looks least like a smart speaker is the one to build. If a shape is 10 percent less immediately face-like but completely loses the appliance read, take it.
- **#3 beats #4.** Emotional range is a structural requirement: a face with no range is a mask, and range is most of what makes the object read as a presence rather than a graphic. Instrument identity is the *style* answer that lets us satisfy #1 and #2 at the same time. Important, but it is a means.
- **#5 never wins.** The S3 with 8 MB PSRAM has plenty of headroom, so simplicity does not get to overrule presence. It only binds where it causes a real hardware failure (frame rate, shimmer), which in practice means: prefer shapes that are crisp without anti-aliasing.

---

## 2. Shared constants (frozen for this test)

| Element | Value |
|---|---|
| Canvas | 240 x 320, origin top-left |
| Faceplate | x 20..219 (200 px), y 48..267 (220 px), radius 28 |
| Plate fill | `0x18E4` (#191C21) |
| Background | `0x0021` (#000408) |
| Eye axis | y = 128 |
| Mouth (V4, frozen) | 5 bars, each 10 px wide, 8 px gaps, group 82 px, x 79..160, baseline y 232, resting heights 5,7,9,7,5 (tops at 228,226,224,226,228) |

Only the eye architecture below changes between candidates.

---

## 3. Candidate 3 — Rounded Slot

| Part | Coordinates |
|---|---|
| Recess L | x 44..105 (62 px), y 118..137 (20 px), radius 7 |
| Recess R | x 134..195 (62 px), y 118..137 (20 px), radius 7 |
| Gap between recesses | 28 px (x 106..133) |
| Plate margins | 24 px each side (44-20 and 219-195) |
| Lit pane L | x 47..102 (56 px), y 121..134 (14 px), radius 2 |
| Lit pane R | x 137..192 (56 px), y 121..134 (14 px), radius 2 |
| Pane inset | 3 px inside the recess on all sides |

Aperture = lit height, 0..14 px, anchored to the pane bottom (y 134) and growing upward. Corner radius 7 is the tunable: 7 reads as equipment, 10 (full pill) reads as an appliance.

---

## 4. Candidate 4 — Segmented Slot

Recess, pane footprint, inset and aperture behaviour are **byte-identical to candidate 3**. The only change is that the lit pane is masked into 4 cells.

Left eye: cells x **52..61**, **64..73**, **76..85**, **88..97** (10 px each), gaps x 62..63, 74..75, 86..87 (2 px each). Pane span 52..97 = 46 px, centred in 47..102, leaving 5 px margin each side.

Right eye: cells x **142..151**, **154..163**, **166..175**, **178..187**, 10 px each, gaps x 152..153, 164..165, 176..177. Pane span 142..187 = 46 px, centred in 137..192, 5 px margin each side.

**Aperture changes vertical fill only.** All four cells fill together from the bottom. Cell gaps are structural and always dark. Lit-cell count never changes. Cell brightness never changes. This is what prevents the LED marquee / equalizer read: the eye is a segmented *window* over a fill, not four lamps.

---

## 5. Candidate 6 — Wide Shared Trough

| Part | Coordinates |
|---|---|
| Trough recess | x 44..195 (152 px), y 120..135 (16 px), radius 8 (pill ends) |
| Subject L | x 58..97 (40 px wide), max height 10 px, radius 2 |
| Subject R | x 142..181 (40 px wide), max height 10 px, radius 2 |
| Gap between subjects | 44 px (x 98..141) |
| Margin inside trough | 14 px each end (58-44 and 195-181) |
| Subject horizontal centres | L = 77.5, R = 161.5 (symmetric about x 120) |

**Aperture = subject height only**, 0..10 px, anchored to the trough's vertical centre at y 128. Rows: `top = 128 - floor(h/2)`, `bottom = top + h - 1`.

Aperture changes **height and nothing else**. Subject width (40 px) and subject horizontal position are fixed during the aperture test. They are separate channels, reserved as follows:

- **Gaze** = horizontal offset of both subjects within the trough. Out of scope for this test.
- **Bloom** = width, used only above 90 percent aperture (see Surprised).

Keeping width and position out of the aperture definition is what makes it one understandable variable in all three candidates.

---

## 6. Aperture test table

Formula: `h_px = round(pct x H)`, round-half-up, and `h_px = 0` if and only if `pct = 0`.

| Aperture | Cand 3 / 4 (H = 14) | Rows | Cand 6 (H = 10) | Rows |
|---|---|---|---|---|
| 0% | 0 px | none | 0 px | none |
| 35% | 5 px | y 130..134 | 4 px | y 126..129 |
| 70% | 10 px | y 125..134 | 7 px | y 125..131 |
| 100% | 14 px | y 121..134 | 10 px | y 123..132 |

Render each candidate at all four values, on the shared plate, and view at desk distance before judging anything else.

---

## 7. Independent left/right control

**Yes, and the instrument metaphor is why.** A stereo VU meter has two needles sitting at different positions and still reads as one instrument. Asymmetry between the two channels is native to the metaphor, not opposed to it. What preserves the single-instrument read is not equal apertures, it is a **shared axis**:

- Aperture (height) may differ freely between left and right.
- Vertical centre must stay identical. Both subjects stay centred on y 128.
- Width must stay identical.
- Horizontal position of the pair stays symmetric about x 120.

Break any of the last three and the trough stops reading as one instrument and starts reading as two unrelated blobs.

- **Amused:** L 75%, R 45%. Asymmetry alone is the whole effect. No geometry change.
- **Concerned:** L 55%, R 55%, with the pair's vertical centre moved to y 130 (+2 px). A shared-axis change, so the single-instrument read holds, and it reads as a lowered gaze rather than a squint.
- **Tired:** L 40%, R 40%, with the outer 3 px of each subject's bottom edge extending 2 px lower. Applied symmetrically to both, so again the shared axis holds.

In code: separate left and right aperture values always, even when equal. The blink is a **multiplier**, never a value, so it cannot overwrite an expression.

---

## 8. Blink

Timings as specified: closing 100 ms, fully closed 120 to 160 ms, reopening 100 to 140 ms. Total 320 to 400 ms.

`effective_aperture = round(expression_pct x blink_scalar)`, `blink_scalar` running 1 -> 0 -> 1.

Because it is a multiplier, Amused holds its 75/45 ratio through the blink and both eyes reach zero together. Independent expression values survive the blink unchanged.

**Closed state per candidate:**

| Candidate | Closed appearance |
|---|---|
| 3 / 4 | Fill 0 px, plus a 1 px seam at y 128 across the pane width (x 47..102 and 137..192) in `0x20A0` |
| 6 | Fill 0 px, plus a 1 px seam at y 128 across **only the two subject widths** (x 58..97 and 142..181) in `0x20A0` |

The seam stays because an entirely dark slot can read as a dead pixel or a render fault, and because it gives a cheap discrimination: **blink keeps the seam and the recess; Offline removes the seam and dims the recess to background.** The trough seam is deliberately not full width: a 152 px line across the face reads as a mouth.

---

## 9. Permanent motion

**Choice: a stationary brightness pulse in the eyes. Not a travelling highlight.**

The lit element's brightness is modulated by a gentle envelope at 1 Hz (60 BPM), excursion plus or minus 6 percent, applied on top of the existing 0.2 Hz breathing. Combined peak-to-peak excursion stays under 10 percent.

**Why this stays tolerable in peripheral vision for hours:** peripheral retina is tuned to motion and to changes in position, and is comparatively insensitive to slow luminance change on an element that is already lit. This pulse introduces no moving edge, no new shape, and no new element anywhere on the plate. Total luminance excursion under 10 percent and a frequency at or below 1 Hz keep it under the level that pulls attention; above roughly 10 percent or above 1.5 Hz it starts to read as a flicker. It also sits on the eyes, which are the element you are already reading, rather than adding motion somewhere you are not looking.

**Fallback if it is still noticed after a full day:** drop to one pulse every fourth beat (15 BPM) at the same 6 percent, and if that is still noticeable, remove the pulse entirely and keep the clock only for count-in and state transitions.

**Rejected: the travelling highlight.** Motion in the eye band is the single most attention-capturing event available, it is the Cylon / Knight Rider visor cliché, and it competes with the mouth for the same glance.

---

## 10. Segmentation decision (if candidate 4 survives)

| Parameter | Value |
|---|---|
| Cells per eye | 4 |
| Cell width | 10 px |
| Gap width | 2 px |
| Pane width | 46 px |
| Aperture effect | Vertical fill of the whole pane |
| Cell brightness | Fixed, never varies |
| Lit-cell count | Fixed at 4, never varies |

Four cells rather than five, deliberately: five would match the mouth's five bars and put fifteen independently moving lit elements on the plate during speech, which is the equalizer look we are trying to avoid. The mouth should stay the busier element. Gaps stay at 2 px because below that they shimmer; at 2 px and integer-aligned they are crisp.

---

## 11. Calibration pointer

| Parameter | Value |
|---|---|
| Size | 3 px wide x 6 px tall |
| Location | x 189..191, y 125..130 |
| Colour | Background `0x0021`, so it reads as a physical cut through the recess wall |
| Which eye | Outer end of the **right** recess / trough only |

**Function: none.** It carries no information. It does not track aperture, gaze or state. Say so plainly rather than inventing a meaning for it.

It stays anyway, on structural grounds: it breaks perfect bilateral symmetry, which is the single cheapest and strongest anti-appliance device in the whole design, and it costs 18 pixels.

**Omit criterion:** if at normal desk distance it reads as a dead pixel, a defect, or dirt rather than a deliberate mark, delete it. A mark that reads as damage is worse than symmetry.

---

## 12. Resting illumination (RGB565)

| Element | Value | 8-bit |
|---|---|---|
| Recess / cavity | `0x0862` | #0A0D10 |
| Recess (darker alt) | `0x0841` | #05070A |
| Closed seam, 12% | `0x20A0` | #1D1404 |
| Closed seam, 18% | `0x28E1` | #2C1E06 |
| Hot core, 1 px centre row above 85% aperture | `0xFE8F` | #FFD37A |

**Fill brightness ladder.** Render all four as a strip and select on the panel. Scott prefers brighter than the original spec, so Level 3 is the current working assumption:

| Level | Value | 8-bit | Note |
|---|---|---|---|
| 1 | `0xABA3` | #B0771A | 72 percent, dim / Night mode |
| 2 | `0xF524` | #F5A524 | Original spec value |
| 3 | `0xFD87` | #FFB43A | Working assumption, brighter |
| 4 | `0xFE09` | #FFC24D | Brightest acceptable, ceiling |

Ceiling remains #FFE9C4. Never #FFFFFF.

---

## 13. Expression table (wide trough, H = 10)

Aperture in percent, and the pixel heights for the trough (H = 10) and for the slots (H = 14) so the same table drives all three candidates.

| Expression | L% | R% | hL/hR @H10 | hL/hR @H14 | Permitted geometry change |
|---|---|---|---|---|---|
| Neutral | 70 | 70 | 7 / 7 | 10 / 10 | none |
| Happy | 85 | 85 | 9 / 9 | 12 / 12 | pair vertical centre to y 127 (-1 px) |
| Amused | 75 | 45 | 8 / 5 | 11 / 6 | none. Asymmetry is the effect. |
| Curious | 90 | 90 | 9 / 9 | 13 / 13 | 1 px vertical split at each subject's horizontal centre (x 78 and x 162), full subject height, 400 ms on entry then closes |
| Concerned | 55 | 55 | 6 / 6 | 8 / 8 | pair vertical centre to y 130 (+2 px) |
| Surprised | 100 | 100 | 10 / 10 | 14 / 14 | width 40 -> 46 px, expanding from each subject centre (L to x 55..100, R to x 139..184), permitted only above 90% |
| Annoyed | 30 | 30 | 3 / 3 | 4 / 4 | width 40 -> 34 px (L to x 61..94, R to x 145..178) |
| Tired | 40 | 40 | 4 / 4 | 6 / 6 | outer 3 px of each subject's bottom edge extends 2 px lower |

For candidates 3 and 4 the width and vertical-centre modifiers do not apply (the recess is the recess). The split, the droop and a 2 px pair offset remain available.

Rounding is round-half-up throughout. 85 percent of 10 is 8.5, rendered as 9.

---

## 14. Selection criteria (maximum five)

Judge all three candidates on the real panel, from normal desk seating distance (roughly 40 to 60 cm) in normal room light, at all four aperture values.

1. **Does it read as a face before you consciously decide?** First glance, no analysis. This is the gate.
2. **Does it read as an appliance, a smart speaker, a camera lens, or a status panel?** The decisive test. One honest answer beats a test sheet.
3. **Is 0 versus 70 versus 100 percent obvious without being told which is which?** Aperture must be legible unaided.
4. **At 75 / 45, do the two eyes still read as one face?** Asymmetry tolerance.
5. **Is the lit element clean at desk distance?** Solid fill, no shimmer, no visible seam, no cell gaps that have collapsed or bled.

**Automatic disqualifier:** if at any of the four aperture values the element reads as a **mouth, a smile, or a text character**, the candidate is out immediately regardless of the other criteria. This is not a preference, it is the proven failure mode from V2, where three lower-face elements collapsed into competing mouth reads. One element per semantic slot, and the eyes must never suggest they are a second mouth.

**Method note:** photograph the test once, with all candidates and all four apertures in the same frame, and judge by relative comparison. A phone photo of a TFT cannot give reliable absolute colour, but relative comparison within one photo is trustworthy.
