# jin-esp32-bridge

Routes Hermes TTS to the **Jin** ESP32-S3 avatar (`jin.local:3333`) so Jin's voice comes out of the
device's speaker with the on-screen mouth following the audio. Adds `/jin on|off|status`, suppresses
Mac playback while the device speaks, and respells the name **Jin** so Edge pronounces it correctly.

Verified against the firmware build physically tested on the device.

---

## Install

```bash
# 1. copy the staged plugin into the plugins directory
mkdir -p ~/.hermes/plugins
cp -R ~/Downloads/jin-esp32-bridge ~/.hermes/plugins/jin-esp32-bridge

# 2. enable it explicitly
hermes plugins enable jin-esp32-bridge

# 3. confirm it is recognised
hermes plugins list | grep jin-esp32-bridge

# 4. restart the relevant Hermes session (or the gateway, if you use one)
#    Plugins load at session start. A running session will not pick this up.
```

Then, in the new session:

```
/jin status
```

You should see `jin-esp32-bridge: ON`, the device address, and `(reachable)`.

### First conversation test

Say something to Jin and confirm: the Mac stays silent, the ESP32 speaks, and the mouth follows.
Then `/jin off` and confirm the Mac speaks again.

---

## Commands

| Command | Effect |
|---|---|
| `/jin on` | Send TTS to the ESP32, mute Mac playback. Persisted. |
| `/jin off` | Normal Mac playback. Persisted. |
| `/jin status` | Enabled state, device address, reachability, transport, fallback, pronunciation. |

`/jin on|off` takes effect immediately *and* writes to config, so the choice survives a restart. If
the write fails (read-only profile) the runtime change still applies and the reply says so.

---

## Settings

All configurable in `~/.hermes/config.yaml` under the plugin's own entry — no source edits:

```yaml
plugins:
  entries:
    jin-esp32-bridge:
      settings:
        host: jin.local        # hostname or IP of the ESP32
        host_ip: ""            # optional: pin the IP to skip name resolution
        port: 3333             # TCP port the firmware listens on
        esp32_output: true     # master switch; same value /jin on|off writes
        suppress_local: true   # mute the Mac while the device speaks
        mac_fallback: true     # play on the Mac if the device can't be reached
        jin_spelling: Jinn     # respelling for the name in TTS input only ("" disables)
```

| Key | Type | Default | Meaning |
|---|---|---|---|
| `host` | str | `jin.local` | Device address. Use the IP if mDNS is unreliable. |
| `host_ip` | str | `""` | Optional. Pin the device IP to skip name resolution entirely. |
| `port` | int | `3333` | Firmware's listening port. |
| `esp32_output` | bool | `true` | Off = the plugin is inert and the Mac behaves normally. |
| `suppress_local` | bool | `true` | Off = both play (only sensible for debugging). |
| `mac_fallback` | bool | `true` | Off = if the device is unreachable, nothing plays at all. |
| `jin_spelling` | str | `Jinn` | See *Pronunciation* below. |

To make the device permanently off by default, set `esp32_output: false`.

### If speech intermittently comes out of the Mac instead

mDNS is unreliable on macOS. `jin.local` can fail to resolve with
`[Errno 8] nodename nor servname provided` while the device is up and reachable seconds later. When
that happens before any audio, the plugin correctly falls back to the Mac, so the symptom is
occasional replies on the wrong speaker rather than an error.

Two defences, and the second is the durable one:

1. The plugin **remembers the last address that actually accepted a connection** and tries it before
   the name, so a subsequent flake recovers on its own. This is in-memory and per-process, so it
   starts empty after a restart and is learned on the first successful connect.
2. Give the device a **static DHCP lease** and set `host_ip` to that address. It is tried first, so
   name resolution is skipped entirely and the failure mode disappears.

`/jin status` prints `address  last reached <ip>` so you can see what it has learned. If that line
says `(none yet)` and speech is landing on the Mac, the name has never resolved in this process.

---

## Disable and remove

```bash
# stop using it, keep the files
hermes plugins disable jin-esp32-bridge
# restart the session

# remove it entirely
hermes plugins remove jin-esp32-bridge
# or, if you copied it in by hand:
rm -rf ~/.hermes/plugins/jin-esp32-bridge
```

Removing the plugin restores the original playback functions via `ctx.on_unload`, so the Mac plays
normally again. You can also delete the `plugins.entries.jin-esp32-bridge` block from
`~/.hermes/config.yaml`; leftover settings are inert.

---

## Transport (unchanged from the build that passed on the device)

| Property | Value |
|---|---|
| Format | 16 kHz, signed int16, little-endian, **mono** PCM |
| Block | **512 bytes** = 256 samples = **16.000 ms** |
| Pacing | every block against a monotonic deadline `t0 + n * period`, so no drift |
| Socket | `TCP_NODELAY` (62.5 small writes/second would otherwise be Nagle-coalesced) |
| Framing | one `JIN1` magic + uint32 LE sample rate header, **once per sentence file** |
| Connection | one per sentence file, **closed at end of file** |

The connection is closed at end of file deliberately: the firmware uses closure to return the mouth
to its flat resting state and reset its sample timeline. No connection is ever held idle between
sentences.

A final partial block is padded with silence so every block on the wire carries a full 16 ms. A
0.500 s clip therefore sends 32 blocks (0.512 s).

---

## Playback policy and fallback

With `esp32_output` and `suppress_local` both on (the defaults), the Mac is muted and the send runs
**synchronously** inside `play_audio_file`, wrapped in `mark_audio_output_active(True/False)`.
Because the send is paced to realtime, that window equals the true audio duration, so
`is_audio_output_active()` stays honest and Jin's own speech does not register as barge-in.

| Situation | Behaviour |
|---|---|
| Device reachable | Audio goes to the ESP32, Mac silent. |
| Device unreachable **before** any audio is sent | Mac plays it (`mac_fallback: true`), or nothing plays if false. |
| Send fails **after** audio started | **No local replay.** The head already played on the device; replaying locally would double-speak. |
| `/jin off` | Original Mac playback, untouched. |

`stop_playback` is also patched, so a barge-in cuts an in-flight send at the next 16 ms block
boundary rather than letting it run to completion.

---

## Pronunciation of "Jin"

Edge `en-US-GuyNeural` reads the literal spelling **"Jin" as "Jean"** (/ʒiːn/). Measured by
synthesizing a carrier phrase and transcribing it locally with faster-whisper, so this is what the
engine actually produces, not a guess:

| Spelling sent to Edge | What the ASR heard | Correct? |
|---|---|---|
| `Jin` | "The name is **Jean**." | ✗ |
| `gin` | "The name is **Gin**." | ✓ |
| `Jinn` | "The name is **Gin**." | ✓ |
| `Jyn` | "The name is **Gin**." | ✓ |

**Fix:** the token `Jin` is respelled to `Jinn` on the way to the speech engine only. Your visible
spelling is never touched, and no other sentence text is altered.

* Only a capitalised, word-bounded `Jin` is replaced. `jin.local`, `JIN1`, `jin-esp32-bridge` and
  any lowercase token are left alone.
* `Jin's` → `Jinn's`, so possessives work.
* Applied at `tools.tts_tool._generate_edge_tts`, the single entry point for every Edge synthesis
  path. Switching TTS provider turns the fix off (it is Edge-specific).

The patch is on `tools/tts_tool._generate_edge_tts` rather than the defining module because
`tts_tool.py` binds that name at import time and calls *that* binding — patching
`tools.tts_tool_providers` would silently do nothing.

To use a different respelling, set `jin_spelling` (e.g. `gin`). To disable, set it to `""`.

---

## Desktop read-replies

Desktop "read replies" (`voice.auto_tts`) speaks through
`WS /api/audio/speak-stream`, which streams raw int16 PCM to the Electron renderer. It never calls
`play_audio_file`, so the file/CLI tap does not cover it.

**Two facts matter here, and the second is easy to miss.**

1. The tap sits on the **PCM egress only**, so every upstream behaviour is preserved untouched:
   `SentenceChunker` sentence boundaries, the idle-flush cadence, the stop/barge-in latch, provider
   resolution, the `WebSocketDisconnect`/`RuntimeError` handling, and the final close. None of it is
   re-implemented.

2. **With `tts.provider: edge` the WebSocket is never actually used.** `tools/tts_streaming._REGISTRY`
   registers elevenlabs/openai/gemini/xai only; edge has no chunked PCM API, so
   `resolve_streaming_provider` returns `None`, the endpoint sends `{"type":"fallback"}`, and the
   client POSTs `/api/audio/speak` for base64 audio into the renderer instead. A socket-only tap
   would be a **silent no-op**.

So the plugin also registers an **edge streamer** — the extension point that module documents
("Adding a streamer is `@register("name")` on a subclass") — and only while `/jin on` is active.
Edge's chunked MP3 is decoded incrementally through ffmpeg, which doubles as the resampler.

`/jin off` removes the streamer, so the desktop returns to exactly the stock fallback path it uses
today. That is what makes `/jin off` a true revert rather than an approximation.

| Aspect | Behaviour |
|---|---|
| Where the tap lives | `starlette.websockets.WebSocket.send_json` / `send_bytes` / `close`, armed per-socket by the `{"type":"start","sample_rate":N}` handshake |
| Why the class, not the endpoint | FastAPI copies the endpoint into the route at include time, so patching the module function or the route object depends on load order; the class method is what runs regardless |
| Device reachable | PCM resampled statefully to 16 kHz, buffered into exact 512-byte blocks, paced at 16 ms, sent on one connection, closed at end of utterance |
| Device unreachable **before** any frame | Decided before anything is withheld; the renderer receives the **real** audio, so the Mac path is retained |
| Forwarding fails **mid-utterance** | Remaining audio is dropped and never replayed locally |
| Barge-in / cancel | `close()` is the universal finalizer, because no `{"type":"end"}` is sent on stop; the socket closes so the mouth returns to rest |

**Suppression sends equal-length silence rather than dropping frames.** The client settles
`'fallback'` when a stream produced no audio, and the caller would then re-speak the same text
through `/api/audio/speak` — two voices, the exact thing being prevented. Equal-length silence keeps
the client's stream state and speaking indicator honest while staying inaudible.

The resampler carries its fractional phase and previous input sample across frames, so feeding a
signal in many small frames yields byte-identical output to feeding it in one piece. Resampling each
frame independently would restart the phase every frame: discontinuities plus drift.

---

## Implementation notes

The plugin patches seven module attributes and restores all seven on unload:

| Target | Why it needs its own patch |
|---|---|
| `tools.voice_mode.play_audio_file` | The macOS playback choke point. |
| `tools.voice_mode.stop_playback` | Lets a barge-in cut the send mid-file. |
| `hermes_cli.voice.play_audio_file` | `voice.py` binds it at **module** level, so its `speak_text` path bypasses the swap above. |
| `tools.tts_tool._generate_edge_tts` | Bound at import time in `tts_tool.py`; the defining module is not where production reads. |
| `starlette.WebSocket.send_json` | Arms the desktop tap from the speak-stream `start` handshake, so only speech sockets are affected. |
| `starlette.WebSocket.send_bytes` | The PCM egress: forward to the device, send equal-length silence to the renderer. |
| `starlette.WebSocket.close` | The universal finalizer, since barge-in never sends `end`. |

* **Unload is conditional.** Each attribute is restored only while it still points at our
  replacement, so a later patch by something else is never clobbered.
* **Reloads do not stack.** Re-registering cleans up first, so there is always exactly one
  replacement layer. Both playback bindings receive the *same* wrapper object, preserving the
  pre-patch identity relationship between them.
* **No audio-carrying hooks exist** in Hermes (`VALID_HOOKS` has no speech entries), so the function
  seam is the only integration point. This is why the plugin patches rather than hooks.
* Local playback is suppressed *without* collapsing the audio-output-active window, which is what
  keeps barge-in working.
* The dashboard's `WS /api/audio/speak-stream` path (what desktop read-replies uses) never calls
  `play_audio_file`; it has its own tap, documented above. `/jin status` reports the two paths
  separately because they fail independently.

---

## Verification performed before install

* `hermes plugins validate` — manifest parses, config schema valid, `register()` ran in isolation.
* 51 local checks passed, with every local playback call intercepted so nothing was audible:
  registration, all four patch targets, the `/jin` command, settings resolution and coercion,
  the respelling (including that it reaches the engine), all fallback branches, a real ffmpeg decode
  and send to a throwaway local TCP sink (header, 512-byte blocks, realtime pacing), no-replay on
  mid-send failure, unload restoration, conditional restore, and reload safety.
* 51 local checks on the file/CLI path, and 49 on the desktop streaming path, all passing with every
  local playback call intercepted so nothing was audible. The desktop suite drives the **real
  Starlette WebSocket class methods** against a throwaway local TCP sink, rather than a mock of them.
  Covered there: reachability fallback before the first frame, mid-utterance failure with no local
  replay, cancellation via `close`, byte-identical chunked-vs-one-shot resampling, exact 512-byte
  blocks, realtime pacing, `/jin off` restoring the stock fallback, and unload restoration.
* The device probe used earlier sent **silence only**.

The pronunciation result is machine-verified (Edge synthesizes, local Whisper transcribes what it
heard). Whether `Jinn` *sounds* right to you is a listen-and-confirm step, since that is a judgement
no tool here can make.
