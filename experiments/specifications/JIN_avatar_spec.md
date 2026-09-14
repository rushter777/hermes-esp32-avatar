# JIN — Physical Avatar: Visual Identity & Behavior Specification

Target hardware: Freenove ESP32-S3 WROOM-1 (FNK0084, N8R8) + GMT020-027P v1.3 TFT (ST7789V), 240 x 320 portrait, MAX98357A + speaker, camera, INMP441 later.

Framebuffer budget: full RGB565 frame = 153,600 bytes. With 8 MB PSRAM, 2 to 4 full framebuffers plus overlay buffers fit easily. Rendering constraint is SPI bandwidth, not RAM.

---

## 0. Design thesis

Jin is not a mascot, not a hologram, not a pair of eyes in a dark box. Jin is an **instrument that happens to have a face**: a rack-unit faceplate, a VU meter that looks back at you. Level, warm, direct. A precision tool with a sense of time.

One line: **a graphite instrument panel with amber shutter eyes and a metered mouth, running on a drummer's clock.**

Two rules generate everything else:

1. **Everything is a meter.** Eyes are shutters. The mouth is a 5-bar VU meter. The bottom band is a speaker grille that pulses with real output. Jin measures; Jin never wears a drawn-on smile.
2. **Everything is on a clock.** A beat line sweeps the faceplate at 60 BPM at rest. Jin is a drummer. Time is always running somewhere on the face.

---

## 1. Overall appearance

**Category:** instrumentation, not character. Closest label: *precision instrument panel* with an abstract, non-human face. Not a robot, not humanlike, not a military computer, not an anime mask.

**Head shape.** A single rounded-rectangle faceplate, the front panel of a 3U device: 200 x 220 px, centred, top edge at y=48, bottom at y=268, corner radius 28 px. Physical depth is implied, not drawn: a 1 px lighter bevel along the top and left edges (#2A3441) and a 1 px darker shadow along the bottom and right (#05070A). No neck, no ears, no hair, no jaw. The plate *is* the head.

**Eyes.** Two horizontal shutter slots, not circles. Left slot x 44 to 106, right slot x 134 to 196, both y 118 to 138 (62 x 20 px, rounded ends). Each is a dark recess (#0A0D10) containing a glowing amber bar whose height is the aperture. The bar is drawn on the sub-pixel-free grid: a 1 px hot core (#FFD37A) inside a 3 px mid-toned bloom (#F5A524) inside a 6 px soft halo at 25 percent alpha. Aperture open percentage is the primary expressive channel in the entire design.

**Mouth.** A 5-bar horizontal meter, 84 px wide, centred, y 196 to 232. Bar width 10 px, gap 8 px, rounded caps. It sits exactly where a mouth would, and it *is* the mouth. Never a lips graphic, never a curve-and-two-dots smile.

**Faceplate lower band.** y 236 to 262: a speaker grille of 3 horizontal slots, 6 px tall, 120 px wide, dim grey (#39424E) at rest. These pulse with audio output. The screen's grille and the physical MAX98357A speaker behind it agree with each other.

**Background.** Deep graphite (#0B0E11), flat. Over it, very low contrast horizontal scanlines, 1 px every 3 px at 6 percent alpha (#1A2027), drifting downward 1 px per 2 s. Optional faint amber ember bloom behind the faceplate at 8 percent alpha, radius 90 px. No gradients steeper than that, they band on 16-bit colour.

**Lighting.** Self-luminous. The faceplate emits; there is no external light source. Any glow is radial and falls off within about 12 px. No drop shadows, no specular highlights beyond the edge bevel.

**Texture.** Anodised aluminium and dark glass: matte plate, glossy recessed slots, hard-edged emissive elements. Clean geometry, no noise, no grain, no grunge.

**Artistic style.** Flat vector / industrial product illustration. Crisp silhouettes, restrained palette, no outlines thicker than 1 px, no gradients beyond the emissive falloff, no photorealism. The reference feel is a high-end studio VU meter or an avionics annunciator panel, rendered for a 240 px wide screen where every pixel is deliberate.

---

## 2. Personal identity

Six details that make this Jin and not a generic assistant:

1. **Amber, not cyan.** Primary is signal amber (#F5A524, hot core #FFD37A). Deliberately avoids the cyan-on-black hologram cliché and the matrix-green terminal cliché. Amber is a VU meter, an annunciator, a warm instrument light. This is the strongest identity carrier on the whole display.
2. **The beat line.** A 1 px amber line that sweeps left to right along the bottom edge of the faceplate, one sweep per beat. It never stops, at rest or not. Tempo is Jin's emotional temperature: 45 patient, 60 level, 72 curious, 84 concerned, 120 sharp and working.
3. **The index notch.** A 4 px vertical detent cut into the outer end of the *right* eye slot only. Asymmetry. Small enough to read as engineering, deliberate enough to be recognisable in a glance.
4. **The grille.** Three slots at the bottom of the plate, always present, always tied to real audio.
5. **The count-in.** Jin ticks before speaking (see section 9). A drummer's move, and a useful state transition.
6. **Instrument vocabulary, never face vocabulary.** No eyebrows, no cheeks, no lips, no teeth. Everything expressive is an aperture, a bar height, a tempo, or a direction of motion.

---

## 3. Default appearance (idle and ready)

**Expression: Neutral, level, available.** Not blank, not smiling. The expression of a colleague who has finished reading your message and is waiting for you.

- Eyes: amber shutters at 70 percent aperture. Slightly hooded, deliberately. Reads as direct and unbothered, never wide-eyed and needy.
- Mouth: Frame 0, a single 2 px dim amber line at 15 percent brightness. A settled line, just barely alive.
- Colour: amber at full identity saturation.
- Beat line: 60 BPM, low brightness, 20 percent.

**Idle motion, all of it subtle:**

- **Breathing:** overall emissive brightness breathes plus or minus 1.5 percent at 0.2 Hz (one cycle per 5 s), with the faceplate bevel shifting by the same amount. Slow and shallow.
- **Blink:** shutters close to 0 percent over 60 ms, hold 40 ms, open over 90 ms. Every 4 to 7 s, randomised. Roughly one blink in eight is a double blink. Blinks do not interrupt the beat.
- **Micro-saccade:** every 3 to 9 s both shutter apertures shift 1 px horizontally and return over 200 ms. Never during speech.
- **Beat line:** continuous sweep, one sweep per beat, never resets, never stutters.
- **Scanlines:** drift downward 1 px per 2 s.
- **Ember:** the faint amber bloom pulses in phase with breathing.
- **Rare idle event:** every 30 to 90 s, one extra: a slow single 1 px dot orbiting the inner faceplate edge once (5 s), or the index notch catching a 200 ms brightness lift. Prevents the face reading as dead without adding clutter.

---

## 4. Emotional expressions (initial set: 8)

The mouth meter never carries emotion. Emotion lives in **eye aperture, eye geometry, tempo, and tint**. That keeps the system consistent, honest, and cheap to animate.

| # | Name | Eyes | Mouth (meter bars) | Colour / tempo | Extra |
|---|---|---|---|---|---|
| 1 | **Neutral** | 70%, level | Frame 0, flat, dim | Amber, 60 BPM | Default state |
| 2 | **Happy** | 85%, lower edge of each slot lifts 2 px into a shallow upward arc | Frame 2, and the bar envelope curves into a shallow U | Amber warms to #FFC24D, 60 BPM | One soft mint (#46E0A0) downbeat pulse on entry, 300 ms |
| 3 | **Amused** | Asymmetric: right slot narrows to 45%, left holds 75% | Frame 2 but lopsided: bar heights 2-3-2-4-1 | Amber, 66 BPM | One short unsynced flicker of the beat line. Dry. Never giggling. |
| 4 | **Curious** | 90%, each slot splits with a 1 px gap at its centre (a focus split) | Frame 1, stepped | Amber cools 10% toward ice, 72 BPM | One 1 px dot orbits the inner edge once, 3 s |
| 5 | **Concerned** | 55%, slots tilt 3 degrees with inner corners lower | Frame 1 sagging into a shallow inverted arc | Amber desaturates to #C98A2E, 84 BPM | Slow crimson (#FF4D4D) edge glow at 0.3 Hz, 12 percent |
| 6 | **Surprised** | 100%, shutter fully retracted, 2 px hot-white core | Frame 4, snaps to full for 180 ms then falls to Frame 2 | Amber to near-white, **beat skips one beat** | One 60 ms full-face luminance lift. The only expression that breaks the clock. |
| 7 | **Annoyed** | 30%, both slots flatten to near-lines | Frame 2 compressed to a single flat bar with one centre spike | Hard saturated #FF8A00, 120 BPM | Grille band flickers once |
| 8 | **Tired** | 40%, 1 px droop on the outer edge of each slot | Frame 1, slow, 2 bars breathing | Muted #B07C28, 48 BPM | Scanlines slow to half speed, beat line dims to 10% |

Deliberately excluded for now: Excited (Jin does not perform enthusiasm), Confused (that is a *Processing* state, not a feeling), Tired is included as the low-energy floor.

---

## 5. Operational states (11)

States are visually distinct from emotions by **colour temperature, motion direction, and beat tempo**. Emotional reads happen "inside" a state.

| State | Appearance | Animation |
|---|---|---|
| **Starting** | No face at all for the first second. | Four amber ticks along the bottom edge at 120 BPM (2.0 s total), brighter on the downbeat. Then the plate powers on like a CRT: a 1 px horizontal amber line at y=158 expands vertically to full plate height over 350 ms. Shutter apertures open in 3 steps (0, 40, 70%). Then one blink. Total ~2.5 s. |
| **Ready** | Neutral (section 3). | Full idle motion set. |
| **Listening** | Colour shifts to ice (#6FD8FF). Apertures 80%. Mouth Frame 0, dimmed to 10%. A live input meter appears on the bottom edge, growing from the left, driven by INMP441 level. | Concentric arcs sweep **inward** from the screen edges toward the two eye slots, one every 700 ms, brightness following mic level. Unmistakable and quiet. |
| **Processing** | Amber. Apertures narrow to 35% (a squint, concentration). Mouth becomes an indeterminate 3-bar scanner running left to right on a 900 ms loop. | Beat line doubles to 120 BPM. Grille band still. Distinct from Listening by colour (amber vs ice), aperture (narrow vs wide) and motion (outward scan vs inward ripples). |
| **Speaking** | Section 6. Apertures 85%, 1 px vertical bob on each beat downbeat. | Mouth meter driven by real audio envelope. |
| **Camera active** | A 24 px aperture glyph at the plate's top-right irises open from 0 to 100% over 250 ms. Plate edge glows ice-white. | One 1 px recognition line sweeps top-to-bottom across the plate once. Then a **persistent 4 px amber-red dot** in the top-right corner for as long as the camera is live. It cannot be hidden by any state or emotion. |
| **Waiting** | Apertures 60%, amber dimmed 30%. Mouth Frame 0. | Beat slows to 45 BPM. A single dim amber dot orbits the inner plate edge slowly, 8 s per lap. Patient, not stalled. |
| **Task complete** | Mint (#46E0A0) for 400 ms. | One bright downbeat tick plus a second quieter tick 120 ms later (a double tick). Apertures rise 1 px and settle. No fanfare, no celebration animation. |
| **Warning** | Plate edge alternates amber to crimson at 2 Hz. Apertures 50%, tilted inward. | Beat 90 BPM. A small crimson caret appears at the plate's top centre. |
| **Error** | Plate dims to 25% overall. Apertures collapse to two thin fractured lines, each with a 6 px break at its centre. Crimson only. | **The beat stops.** Silence is the alarm. Three crimson vignette pulses at 1 Hz, then hold. |
| **Offline / shutdown** | Apertures iris closed left to right over 600 ms. Plate dims. | Beat decelerates from 60 to 0 over 2 s, then one amber pixel at screen centre, then true black. Nothing left on screen. |

**Global safety cap:** overall field luminance never changes faster than 2 Hz, and no full-field flash exceeds 60 percent white. Full-white (#FFFFFF) is never used as a field colour on this panel; ceiling is #FFE9C4.

---

## 6. Mouth and speech animation

**Decision: no literal mouth image. The mouth is a 5-bar VU meter.** It combines the mouth, the waveform, and the identity in one element, and it is the only approach that stays consistent across every emotion and every speaking variant without redrawing art.

**Five discrete frames** (used for the sprite sheet and for low-effort fallback rendering). Bar heights in px, left to right:

- **Frame 0 — Rest:** one continuous 2 px line across the full 84 px, dim amber at 30 percent. The closed mouth.
- **Frame 1 — Low:** 4, 8, 4, 8, 4. Speech onset, quiet syllables.
- **Frame 2 — Mid:** 8, 14, 8, 20, 8. Normal conversational volume.
- **Frame 3 — Open:** 14, 22, 14, 30, 14. Stressed syllables.
- **Frame 4 — Wide:** 22, 32, 22, 38, 22, with the 1 px hot core (#FFD37A) on the outer bars. Peaks only.

**Live drive.** Frame selection comes from a **50 Hz (20 ms per step)** RMS envelope computed on the PCM buffer feeding the MAX98357A, **not** from a microphone. We already know the sample values; reading them is free and perfectly in sync. Attack 40 ms, release 120 ms, which at 20 ms per step is two steps to open and six to close. (A 30 ms attack would be shorter than a single update step and therefore meaningless, so the attack is quantised to two whole frames.) Envelope is normalised per speaker with a rolling 2 s peak so quiet and loud voices both drive full range.

**Pauses between words.** The meter returns to Frame 0 over 120 ms rather than snapping. The beat line keeps running through the pause and the grille band stays dimly lit. Silence still reads as live, not as broken.

**Do the eyes move while speaking?** Yes, minimally: apertures open about 15 percent relative to the current expression (speech wants attention), and there is a 1 px narrower read on syllable onsets for accent. Nothing else. No head shake, no eyebrow substitutes.

**Do emotions need their own speaking variants?** No separate art. The mouth meter is identical across every emotion, because it is the honest output meter. Speaking variants are produced by **eye shape and tint only**: Happy speaks with lifted lower lids, Annoyed speaks with flattened slots and 120 BPM, Surprised speaks with retracted shutters. This is both cheaper and more coherent than a matrix of redrawn mouths.

**Screen and speaker agree.** The 3-slot grille band at the bottom of the plate is driven by the same envelope, so the on-screen mouth and the real audio match.

---

## 7. Listening versus thinking animation

These must be readable at a glance from across a room. They are separated on four axes at once, which is why they cannot be confused:

| Axis | Listening | Processing |
|---|---|---|
| Colour | Ice #6FD8FF | Amber #F5A524 |
| Aperture | Wide, 80% | Narrow squint, 35% |
| Motion direction | Arcs sweep **inward** from the edges | 3-bar scanner sweeps **left to right** |
| Beat tempo | 60 BPM, steady | 120 BPM, doubled |
| Mouth | Frame 0, dim 10% | Indeterminate 3-bar scanner |
| Bottom edge | Live input level meter growing from the left | Still |

Listening says *I am taking it in*. Processing says *I am working on it, hold on*. If a user cannot tell them apart in one glance at 240 px, the design has failed.

---

## 8. Information shown on screen

**Default: the face only.** No name, no clock, no stats, no permanent HUD. Jin protects attention; a face with a dashboard on it is noise.

Three exceptions, all minimal and all event-driven:

1. **Ticker.** A single line at y 296 to 316, 8 to 10 px cap height, maximum about 30 characters, monospaced, dim amber on graphite. Used for a short status or a one-line reply preview. Fades in over 150 ms, holds 4 s, fades out. Idle time is zero. If there is nothing to say, it is not on screen.
2. **Persistent micro-indicators** (never larger than 6 px, never central):
   - Camera live: 4 px amber-red dot, top right of the plate. Cannot be hidden.
   - Mic muted: a 3 px amber diagonal slash across the centre of the mouth meter. Immediately legible as "voice off", and it never moves.
   - Connection lost: the whole face dims to 30 percent and the grille band goes dark. No icon needed; the machine visibly stops responding.
3. **State text** is never used. States are carried by colour, motion and tempo. Text is reserved for content, not chrome.

Never on screen: a permanent "JIN" label, uptime, CPU load, IP address, scrolling logs, animated loading spirals.

---

## 9. Signature behavior: The Count-In

**Before Jin speaks, Jin counts the band in.** Amber ticks sweep the bottom edge, one per beat, the first brighter and thicker, then speech begins on the downbeat. It is a drummer's reflex, an honest Processing to Speaking transition, and a short warning that a reply is coming.

The count-in runs at **its own tempo, decoupled from the mood tempo of the beat line**, because four beats at 60 BPM is four seconds and would stall the conversation. Three lengths, arithmetic shown:

| Count-in | Ticks | Tempo | Beat interval | Total |
|---|---|---|---|---|
| **Quick** | 2 | 300 BPM | 200 ms | 400 ms |
| **Standard** (default) | 3 | 200 BPM | 300 ms | 900 ms |
| **Full** | 4 | 120 BPM | 500 ms | 2.0 s |

**Quick** is for short replies, acknowledgements and retorts. **Standard** is the default for a normal conversational reply. **Full** is reserved for a substantial or deliberate answer, and it is the only case where the pause is long enough for the user to feel the count.

During the count-in the beat line snaps to the count-in tempo, so the two never disagree, then hands back to the mood tempo once speech begins. The downbeat tick is 1 px tall at full brightness; the remaining ticks are 1 px and dimmer.

Secondary signature: the beat line itself, always sweeping, tempo as mood. Together they mean that even a still, silent Jin is visibly a working clock.

---

## 10. Visual boundaries (avoid)

**Palette and light**
- No cyan-on-black hologram look as the dominant palette (this is the single most generic AI-avatar signal).
- No matrix-green falling code, no terminal green as primary.
- No neon purple / magenta synthwave.
- No pure white (#FFFFFF) as a field colour; ceiling #FFE9C4.
- No gradient steep enough to band in RGB565, no dithering, no noise or grain.
- No full-field flashes above 2 Hz or above 60 percent white.

**Face and character**
- No robot smiley clichés: two dot eyes plus a curved line mouth.
- No humanlike anime face, no blush, no heart eyes, no eyelashes, no eyebrows.
- No camera-lens irises. Eye slots are shutters, never apertures that read as a webcam watching you.
- No teeth, no lips, no tongue.
- No skin tones or human skin texture anywhere.
- No emoji, ever.

**Interface**
- No circular Siri/Jarvis audio orb, no pulsing ring.
- No permanent "JIN" nameplate, no permanent stats bar.
- No spinning or indeterminate spinners other than the deliberate Processing scanner.
- No text on the face beyond the one-line event ticker.
- No drop shadows, bevels thicker than 1 px, or heavy outlines.
- No red used as general decoration; crimson means warning or error only.
- No detail smaller than 1 px or ornament that vanishes at 240 px wide.

**Behaviour**
- No bouncing, bopping, or cute idle motion. Breathing is 1.5 percent, not a squash-and-stretch.
- No celebration animation on task completion. A double tick and 400 ms of mint is the entire vocabulary.

---

## 11. Future expansion

**Emotions to add:** Skeptical (one slot narrows, mouth flattens to one bar), Relieved (apertures 65%, one slow long exhale fade, tempo drops to 50), Proud (apertures 75%, one sustained mint beat line for 2 s), Bored (tempo 40, aperture 45%, beat line drifts slightly out of sync on purpose), Focused (aperture 50%, mouth locked to Frame 0, beat line hidden entirely).

**Special modes:**
- **Focus mode:** near-black plate, beat line only, no eyes, no mouth, no ticker. For long unattended work.
- **Night mode:** whole palette shifts amber to deep red and luminance ceiling drops 40 percent. Preserves dark adaptation.
- **Metronome / practice:** the beat line becomes a real, user-settable metronome with tap tempo. Jin as an actual instrument. Uses the speaker.
- **Directional listening (requires a second microphone):** one INMP441 is mono. It reports level and nothing else, so a single mic cannot tell you which side a sound came from. Two INMP441 modules wired as a stereo pair on the same I2S bus (the LR pin on each selects which half of the frame it drives) with roughly 10 to 15 cm of physical spacing give a usable left/right estimate from the inter-channel level difference, with better angular resolution at wider spacing. From that, bias the aperture widths left or right so the face turns toward the speaker with no moving parts. With one mic this mode is simply unavailable, and the Listening state uses level only.
- **Voice greeting:** recognise a known voice and respond with the double tick and a 40 percent aperture lift. A nod, not a wave.

**Cosmetic finishes** (recolour only, geometry unchanged): Graphite (default), Brass (warm patina plate, #8A6A32), Carbon (matte black plate, low bloom), Field (olive #3A4030 plate, an honest nod to issue hardware).

**Environments:** background layer gains a faint scene at long idle only, for example a dark studio window grid, a rack of out-of-focus equipment silhouettes, or a night city grid. Maximum 12 percent alpha. The faceplate stays the focal point.

**Hardware extensions:**
- The Freenove board's onboard addressable RGB LED becomes an **ambient room light** carrying the same identity colour and the same beat. Same state, second surface, no extra wiring.
- A small servo neck for 10 degree head turns, driven by the stereo pair's left/right estimate.
- An RGB LED ring behind the panel for a soft throw onto the desk, synced to the meter.

---

## 12. Image-generation prompts

Practical note first: generate at 768 x 1024 or 960 x 1280 (3:4, matching 240 x 320), then downscale with a good box filter. Do not generate at 240 x 320 directly; the model will produce mush. Downscale in one step, not iteratively.

### 12a. Portrait prompt (Nano Banana / Grok Image)

```
Front-facing character portrait of an abstract AI interface entity called Jin, designed for a 240 x 320 portrait embedded display, 3:4 vertical composition. Industrial precision-instrument aesthetic, not a robot, not humanoid, not anime, not a cartoon.

Head: a single rounded-rectangle graphite faceplate, 200 x 220 px proportions, corner radius 28 px, floating centered in the frame with the top edge at about 15 percent from the top. Matte anodised aluminium surface in very dark graphite #12171C on a deep near-black background #0B0E11. A thin 1px lighter bevel runs along the top and left edges of the plate in #2A3441; a 1px darker shadow runs along the bottom and right in #05070A. No neck, no ears, no hair, no jaw. The plate is the entire head.

Eyes: two horizontal recessed shutter slots, each about 62 x 20 px, at roughly 40 percent down the plate. Left slot centered left, right slot centered right, generous space between them. Each slot is a dark recess #0A0D10 containing a glowing amber horizontal bar, aperture open about 70 percent. The bar is a 1px hot core of #FFD37A inside a 3px band of #F5A524 inside a soft 6px halo at low opacity. Slightly hooded, level, direct gaze. The right slot has a small 4px vertical index notch cut into its outer end for asymmetry.

Mouth: not a mouth. A horizontal 5-bar audio meter 84px wide centered in the lower middle of the plate, five evenly spaced rounded vertical bars 10px wide with 8px gaps, all at a low resting height forming an almost flat line, glowing dim amber at 30 percent brightness. It reads as a meter that functions as a mouth.

Lower plate: three horizontal speaker grille slots 6px tall and 120px wide, dim grey #39424E, near the bottom of the plate.

Signal: a single 1px amber line sweeping the bottom edge of the plate, low brightness. Very faint amber ember glow behind the plate. Faint horizontal scanlines across the whole background at 6 percent opacity, subtle, not distracting.

Colour palette: signal amber #F5A524 and hot core #FFD37A as the only saturated colours, dark graphite #0B0E11 and #12171C, dim steel line #39424E.

Lighting: fully self-luminous, the plate emits its own light, glow falls off within about 12 pixels, no external light source, no drop shadows, no specular highlights beyond the 1px edge bevel.

Style: flat vector industrial product illustration, crisp silhouettes, high contrast, hard-edged emissive elements, clean geometry, zero grain, zero grunge, zero photorealism. Reference feel: a high-end studio VU meter or an avionics annunciator panel.

Requirements: strong readable facial features that survive being scaled down to 240 x 320 pixels. High contrast between the amber elements and the dark plate. Consistent, symmetrical, centred proportions. Clean uncluttered background with nothing behind the head. Absolutely no tiny details, no text, no lettering, no numbers, no logos, no emoji, no interface chrome. No cyan, no green, no purple, no pure white. Background must be a flat, clean, solid field.
```

### 12b. Sprite-sheet prompt

Grid: **4 columns x 4 rows, 16 cells.** Cell art 512 x 683 each (3:4, same as 240 x 320). Gutter and outer margin 24 px of pure solid background between every cell so frames slice cleanly. Target sheet size 2168 x 2852; if the model only emits standard sizes, accept the nearest and keep the grid and gutters exact, do not accept a squeezed or overlapping layout.

Cell layout, reading left to right, top to bottom:

| Row | Col 1 | Col 2 | Col 3 | Col 4 |
|---|---|---|---|---|
| 1 | Neutral | Happy | Amused | Curious |
| 2 | Concerned | Surprised | Annoyed | Tired |
| 3 | Mouth Frame 0 (rest line) | Mouth Frame 1 (low) | Mouth Frame 2 (mid) | Mouth Frame 3 (open) |
| 4 | Mouth Frame 4 (wide) | Listening (ice blue) | Processing (amber squint) | Starting (boot, eyes opening) |

```
Sprite sheet of the same abstract AI interface character Jin, repeated as 16 identical-proportion head frames arranged in a strict 4 column by 4 row grid, on a flat solid near-black background #0B0E11, with a 24 pixel solid pure-background gutter between every cell and a 24 pixel outer margin. Every cell contains the exact same head, same position, same scale, same lighting, same colours, same proportions, same camera angle, front facing, centered in its cell. Nothing overlaps between cells.

The character: a rounded-rectangle graphite faceplate, matte anodised aluminium #12171C, corner radius 28, 1px lighter bevel top and left in #2A3441, 1px darker shadow bottom and right in #05070A. No neck, hair, ears or jaw. Two horizontal recessed shutter eye slots, 62 x 20 px, dark recess #0A0D10 containing a glowing amber bar with a 1px hot core #FFD37A inside #F5A524. The right slot has a 4px vertical index notch at its outer end. A 5-bar horizontal audio meter in place of a mouth, 10px rounded bars with 8px gaps. Three dim grey speaker grille slots near the bottom of the plate. A 1px amber sweep line along the bottom edge of the plate.

Row 1, four frames left to right: NEUTRAL, eyes open 70 percent, mouth meter almost flat at low height, amber. HAPPY, eyes open 85 percent with the lower edge of each eye slot lifting into a shallow upward arc, mouth meter curved into a shallow upward U, amber slightly warmer. AMUSED, asymmetric eyes, the right slot narrowed to about 45 percent while the left stays at 75 percent, mouth meter lopsided with uneven bar heights, amber. CURIOUS, eyes open 90 percent with a 1px split gap at the centre of each slot, mouth meter in a small stepped pattern, amber cooled slightly toward pale blue.

Row 2, four frames left to right: CONCERNED, eyes at 55 percent with the slots tilted three degrees and the inner corners lower, mouth meter sagging into a shallow inverted arc, amber desaturated and duller, faint crimson rim glow at the plate edge. SURPRISED, eyes fully open, shutter retracted, brighter 2px core, mouth meter at maximum height, amber nearly white-hot. ANNOYED, eyes squinted to 30 percent near-lines, mouth meter compressed to an almost flat bar with a single centre spike, hard saturated orange amber. TIRED, eyes at 40 percent with a 1px droop at the outer edges, mouth meter low and slow, colour drained to muted brown amber.

Row 3, four frames left to right, all in the neutral expression and posture, differing ONLY in the mouth meter, which is a 5-bar horizontal audio meter: FRAME 0 rest, one continuous flat low line across all five bar positions. FRAME 1 low, bar heights in pixels left to right 4 8 4 8 4. FRAME 2 mid, heights 8 14 8 20 8. FRAME 3 open, heights 14 22 14 30 14.

Row 4: FRAME 4 wide, mouth meter at heights 22 32 22 38 22 with hot cores on the outer bars, neutral eyes. LISTENING, the entire character shifts to ice blue #6FD8FF with amber removed, eyes open 80 percent, mouth meter flat and dimmed, faint concentric arcs sweeping inward from the frame edges toward the eyes. PROCESSING, back to amber, eyes squinted to 35 percent in concentration, mouth meter replaced by an indeterminate three-bar scanning pattern mid-sweep. STARTING, the boot frame, eyes open only about 40 percent as if switching on, mouth meter flat and dark, plate slightly dimmer overall.

Requirements: identical head position, scale, proportions, lighting and design in all 16 cells, absolutely no design drift between frames. Evenly sized cells in a clearly defined grid, generous even separation so each cell can be extracted individually. Plain solid background, no gradient, no texture, no vignette. No text, no labels, no numbers, no captions, no borders overlapping the artwork, no watermark. No cyan or green or purple anywhere except the deliberate ice blue in the listening cell. No pure white. No tiny details that vanish when scaled down. High contrast, crisp flat vector industrial illustration style, hard-edged emissive elements, zero photorealism.
```

---

## Implementation specification (condensed)

- **Visual style:** flat vector industrial product illustration; precision instrument faceplate. Reference feel: studio VU meter / avionics annunciator panel. No photorealism, no grunge.
- **Main colours:** signal amber #F5A524, hot core #FFD37A. Ice #6FD8FF (listening). Mint #46E0A0 (complete). Crimson #FF4D4D (warning/error). Lines and grille #39424E. Luminance ceiling #FFE9C4, never #FFFFFF.
- **Background:** graphite #0B0E11 flat, plus 1 px scanlines every 3 px at 6 percent alpha drifting down 1 px per 2 s, plus an 8 percent amber ember behind the plate.
- **Face structure:** one 200 x 220 px rounded-rect faceplate (radius 28) centred at y 48 to 268. Two 62 x 20 px shutter eye slots at y 118 to 138. A 5-bar meter as the mouth, 84 x 36 px at y 196 to 232. Three grille slots at y 236 to 262.
- **Signature features:** amber-not-cyan palette; the continuously sweeping beat line; the index notch on the right eye slot; the audio-driven speaker grille; the count-in before speech.
- **Default expression:** Neutral, apertures 70 percent, mouth Frame 0 dim, 60 BPM, breathing at 0.2 Hz, blink every 4 to 7 s.
- **Initial emotions (8):** Neutral, Happy, Amused, Curious, Concerned, Surprised, Annoyed, Tired.
- **Operational states (11):** Starting, Ready, Listening, Processing, Speaking, Camera active, Waiting, Task complete, Warning, Error, Offline.
- **Mouth frames: 5.** 0 rest (flat line), 1 low (4-8-4-8-4), 2 mid (8-14-8-20-8), 3 open (14-22-14-30-14), 4 wide (22-32-22-38-22). Discrete frames for sprite/fallback use, interpolated by envelope when rendering live.
- **Listening animation:** ice #6FD8FF, apertures 80 percent, inward-sweeping arcs at 700 ms, live mic input meter growing from the left along the bottom edge, mouth flat and dim, beat steady at 60. Level only; one mono mic provides no direction.
- **Thinking animation:** amber, apertures 35 percent, three-bar indeterminate scanner sweeping left to right on a 900 ms loop, beat doubled to 120 BPM.
- **Speaking animation:** mouth from the 50 Hz (20 ms per step) RMS envelope of the PCM stream (attack 40 ms, release 120 ms, 2 s peak normalisation), apertures plus 15 percent, 1 px downbeat bob, 120 ms return to rest during pauses, grille band driven by the same envelope, count-in before onset (default 3 ticks at 200 BPM = 900 ms).
- **Information normally displayed:** face only. Event ticker at y 296 to 316, max about 30 characters, 4 s hold, otherwise hidden. Micro-indicators only: camera live dot (cannot be hidden), mic-muted slash, dimmed face on connection loss.
- **Signature behaviour:** The Count-In at its own tempo (quick 400 ms / standard 900 ms / full 2.0 s), plus the permanent beat line whose tempo encodes mood.
- **Avoid:** cyan hologram palette, matrix green, neon purple, pure white fields, banding gradients, dither, dot-eye smiley robots, anime faces, camera-lens irises, teeth or lips, skin tones, emoji, Siri-style audio orbs, permanent nameplates or stat bars, spinners, drop shadows, red as decoration, sub-pixel ornament, cute idle bouncing, celebration animations.
- **Future expansion:** extra emotions (Skeptical, Relieved, Proud, Bored, Focused); Focus and Night modes; a real tap-tempo metronome mode; a two-microphone stereo pair on one I2S bus for left/right directional listening; voice greeting; cosmetic finishes (Brass, Carbon, Field); idle environments at low alpha; onboard RGB LED as synced ambient light; servo neck for 10 degree head turns.

### Rendering note

Render **procedurally in code** as the primary path, not from the sprite sheet. At 240 x 320 the entire face is roughly ten primitives (two rounded rects, two recessed slots with two bars each, five meter bars, three grille slots, two lines). Procedural drawing gives perfect frame-to-frame consistency, sub-pixel-free crispness, zero flash storage, free expression interpolation, and free audio-reactive mouth heights. Use the sprite sheet as the design reference and as a bitmap fallback, not as the runtime art source. Budget two PSRAM framebuffers plus a dirty-rect update path; the only real limit is SPI throughput on the ST7789, so keep redraws to changed regions rather than full-frame blits.

### Hardware notes that constrain the above

- **Panel mismatch.** The kit's documented module is a JMD-IPS130-V2.0 at 240 x 240. The GMT020-027P v1.3 at 240 x 320 is a different geometry on the same ST7789V driver, so any existing TFT_eSPI or LovyanGFX config needs width, height, rotation and possibly a y-offset (0 or 80, glass dependent) updated. Confirm those against the physical panel before writing layout code; every coordinate in this document assumes portrait 240 wide x 320 tall with origin top-left.
- **Microphones.** One INMP441 gives level only. Direction needs two, spaced, on one I2S bus.
