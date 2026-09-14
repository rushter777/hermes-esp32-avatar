# Hermes integration

How Hermes speech reaches the ESP32 avatar, as implemented and verified in the current working
build. This documents behaviour that was measured, not the design that was intended.

Scope: the host side. Firmware internals are described only where they define the contract.
Wiring is in `HARDWARE.md`. Terminology: the *host* is the Hermes process, the *device* is the
ESP32-S3, an *utterance* is one sentence-file or one desktop speech stream.

---

## 1. Speech paths the plugin handles

Hermes has more than one place where speech becomes audible. Three matter here.

| Path | Seam the plugin patches | Status |
|---|---|---|
| File / CLI TTS | `tools.voice_mode.play_audio_file` | Routed to device |
| File / CLI TTS via the CLI voice module | `hermes_cli.voice.play_audio_file` | Routed to device |
| Barge-in for the file path | `tools.voice_mode.stop_playback` | Cuts the in-flight send |
| Desktop automatic read-replies | `starlette.websockets.WebSocket.send_bytes` | Routed to device |
| Desktop, arming the stream tap | `starlette.websockets.WebSocket.send_json` | Taps the PCM handshake |
| Desktop, ending the stream tap | `starlette.websockets.WebSocket.close` | Finalises the utterance |
| Edge voice identity on all Edge paths | `tools.tts_tool._generate_edge_tts` | Applies name respelling |

**File / CLI TTS.** `play_audio_file` is the single macOS playback choke point for the `tts` tool,
the CLI sentence speaker pipeline, and voice mode. Every call site imports it inside the function,
so replacing the module attribute is picked up immediately. `hermes_cli/voice.py` is the exception:
it binds the name at module level (line 112) for its `speak_text` path, so it needs its own swap.
Pre-patch those two names refer to the *same* function object, so the plugin installs the *same*
replacement object at both, preserving that relationship.

**Desktop automatic read-replies.** `voice.auto_tts` streams PCM to the Electron renderer over a
WebSocket. That path never calls `play_audio_file`, so it needs its own interception. See section 2.

### Paths that still bypass the plugin

These are confirmed by reading the code, not assumed:

- **`POST /api/audio/speak`.** Returns base64 audio that the renderer plays itself. This is the
  desktop's fallback when no streaming provider is available. With `/jin on` and `provider: edge`
  the plugin registers an Edge streamer so the desktop uses the WebSocket instead, but with a
  *different* non-chunked provider configured the desktop falls back to this endpoint and the audio
  bypasses the plugin entirely. Treat it as "routed only when the streamer is registered".
- **Anything the renderer plays from a base64 payload.** The plugin controls Python-side egress,
  not the Electron audio stack.
- **Other Hermes processes.** A gateway, a TUI on another machine, or any second backend is a
  separate process and needs its own plugin load. Nothing is forwarded between processes.
- **Microphone input.** There is no listening path at all. The plugin is output-only; the
  `INMP441` is not read by anything on the host side.

---

## 2. Desktop interception point

### Where

`starlette.websockets.WebSocket` class methods: `send_json`, `send_bytes`, `close`.

The tap is armed per socket by the speak-stream handshake
`{"type": "start", "sample_rate": N, "channels": 1}`. That handshake is unique to this protocol,
so no other WebSocket in the process (pty, console, pub, events) is affected. Sockets that never
receive it are untouched.

### Why it was required

The endpoint `hermes_cli/web_routers/audio.py::speak_stream_ws` streams raw int16 PCM frames
straight to the renderer, which plays them through Web Audio. It never calls `play_audio_file`.
A call-site audit confirms this: `play_audio_file(` appears only in `tools/voice_mode.py`,
`tools/tts_tool_speaker.py`, `hermes_cli/cli_voice_mixin.py`, and `hermes_cli/voice.py` — nothing
in `web_routers/audio.py`.

### Why the class, and not the endpoint

FastAPI captures `route.endpoint` when the router is included in the app. Patching the endpoint
function, or `router.routes[i].endpoint`, therefore depends on whether the patch lands before or
after the app is built. Plugin discovery in Hermes is lazy and idempotent, so it can run at any
point in process life. The class method is what executes at send time regardless of load order.

### Why an Edge streamer is registered as well

This is the easy thing to miss. `tools/tts_streaming._REGISTRY` ships streamers for `elevenlabs`,
`openai`, `gemini` and `xai` only. **Edge has no chunked-PCM API and no streamer**, so with
`tts.provider: edge`:

1. `resolve_streaming_provider()` returns `None`,
2. the endpoint sends `{"type":"fallback"}`,
3. the client settles on `fallback` and POSTs `/api/audio/speak` for base64 audio instead.

A socket-only tap would therefore have been a silent no-op for the default provider. The plugin
registers its own Edge streamer in `_REGISTRY["edge"]` while the avatar is enabled, which is the
extension point `tts_streaming` documents, and removes it when disabled. `stream()` synthesises
with `edge_tts`, decoding the chunked MP3 incrementally through `ffmpeg`, which doubles as the
resampler. The feeder runs on its own thread so `ffmpeg`'s stdout is drained concurrently and the
pipe cannot deadlock.

### Suppression by substitution

While the device is being fed, the renderer is sent **silence of the same length** rather than
nothing at all.

Dropping the frames is the obvious approach and it is wrong: a stream that produced no audio makes
the client settle on `fallback`, and the caller then re-speaks the same text through
`/api/audio/speak`. That is two voices, which is the exact failure this plugin exists to prevent.
Equal-length silence keeps the client's stream state and speaking indicator honest while staying
inaudible.

---

## 3. Audio flow

### Source format

Two source shapes, depending on the path:

| Path | Source | Device-bound |
|---|---|---|
| File / CLI | Edge MP3, 24 kHz mono 16-bit | ffmpeg decodes once to 16 kHz int16 LE mono |
| Desktop | Edge streamer, declared `sample_rate = 24000`, int16 LE mono PCM | Stateful resampler converts to 16 kHz |

For other streaming providers the desktop path resamples from whatever rate the provider declares;
that is read from the `start` handshake rather than assumed.

### Conversion to 16 kHz int16 LE mono

`_Resampler` is a linear interpolator that carries two pieces of state across calls: the
fractional phase and the previous input sample. Feeding a signal in many small frames therefore
produces **byte-identical** output to feeding it in one piece. Resampling each frame independently
would restart the phase every frame, giving discontinuities plus cumulative drift. The distinction
is asserted directly in the test suite.

### 512-byte blocks

Converted output is buffered and emitted in exact 512-byte blocks: 256 samples, 16.000 ms at
16 kHz. Only the final partial block is padded, with zeros. No other block is ever short, because
the device reconstructs its sample timeline from a fixed block size and its own clock.

### Pacing

A worker thread drains the block queue against an absolute deadline of `t0 + n × 16 ms`, so error
cannot accumulate over a long utterance. `TCP_NODELAY` is set on the socket; 62.5 small writes per
second is otherwise a textbook Nagle case.

### `JIN1` framing

`struct.Struct("<4sI")`, 8 bytes: ASCII `JIN1` followed by a little-endian uint32 sample rate.

Sent **once per utterance**, not per block. The declared rate is always 16000.

### Connection and utterance boundaries

One TCP connection and one header per utterance. The connection is closed at end of utterance, and
the device treats that closure as the end of speech: it writes a short run of silence, forces the
mouth back to its flat resting frame, and resets its sample timeline.

**The device validates the header and rejects on mismatch.** It reads 8 bytes, requires the `JIN1`
magic *and* `rate == 16000`, and closes the connection otherwise. A wrong rate is a hard reject,
not a degraded mode.

The device buffers until at least 512 bytes are available before reading a block, and accepts any
even byte count up to 512.

### Latency compensation

The device timestamps each mouth frame from the sample position in the stream, not from packet
arrival time, and offsets playback by a calibrated output latency. This keeps the mouth locked to
the audio even when TCP delivery is irregular.

---

## 4. Fallback rules

The send path returns three states rather than a boolean, because the correct action depends on
whether anything already reached the device.

| Outcome | Meaning | Action |
|---|---|---|
| `sent` | Whole utterance delivered | Done |
| `unavailable` | **Nothing was sent** | Local playback, if enabled |
| `aborted` | Stopped or failed **after** audio began | No local playback |

### Failure before audio begins

Covers: the device unreachable, name resolution failure, `ffmpeg` missing, or the header failing
to send. With `mac_fallback: true` (default) the Mac plays the utterance. With it false, nothing
plays and the reason is logged.

For the desktop path the decision is made at the `start` handshake, **before any frame is
withheld**. On failure the renderer receives the real PCM frames untouched, so the stock Web Audio
playback proceeds normally.

### Failure after partial delivery

Deliberately no local playback. The head of the utterance already played on the device; replaying
it on the Mac would double-speak. For the desktop path the remaining frames are still substituted
with silence, so the renderer does not restart the utterance either.

### Disconnected device

Indistinguishable from "unreachable before audio begins": the connect fails and the fallback
applies. Because no connection was established, the device's own state is unaffected; a device that
was mid-utterance when the link dropped returns its mouth to rest on its own when the socket
closes.

### mDNS resilience

`jin.local` can fail to resolve intermittently on macOS (observed live: `[Errno 8] nodename nor
servname provided` while the device was up and answered immediately afterwards). Connection
attempts try, in order:

1. `host_ip`, if configured,
2. the last address that actually accepted a connection,
3. the configured `host`.

A successful connect re-learns the address, so a DHCP change self-corrects. The cache is
**in-memory and per process**: it starts empty after a restart, and if the very first lookup after
a restart fails, that one utterance goes to the Mac. A static DHCP lease plus `host_ip` removes the
failure mode entirely.

### `/jin on` and `/jin off`

| | `/jin on` | `/jin off` |
|---|---|---|
| Edge streamer | Registered | Removed |
| Desktop path | WebSocket streaming, device-bound | Stock `fallback` then `POST /api/audio/speak` |
| File / CLI path | Device-bound | Original Mac sink |
| Mac playback | Suppressed while the device speaks | Normal |
| Persistence | Written to plugin settings | Written to plugin settings |

`/jin off` is a true revert rather than an approximation: with the Edge streamer removed,
`resolve_streaming_provider()` returns `None` again and the desktop behaves exactly as it does
without the plugin installed. This is asserted in the test suite.

`/jin status` reports both integration paths separately, because they fail independently, and
reports the last address reached.

---

## 5. Cancellation and barge-in

**File / CLI path.** `stop_playback` is patched to set the stop event before delegating to the
original, so an in-flight send cuts at the next 16 ms block boundary while the original still
terminates local playback. Sixteen-millisecond granularity, finer than killing a subprocess.

**Desktop path.** Barge-in is signalled by the endpoint setting its own stop latch. The producer
thread exits, and the endpoint closes the socket **without** sending `{"type": "end"}`. The plugin
therefore uses the presence of the `end` frame to distinguish the two cases:

| At `close()` | Interpretation | Action |
|---|---|---|
| `{"type":"end"}` was seen | Utterance completed | Pad the tail, drain the queue, then close |
| No `end` frame | Barge-in or client disconnect | Cut immediately and close |

This matters: draining on a barge-in would keep the device talking after the interruption.
Measured on a barge-in mid-utterance: 0 of 63 queued blocks were sent.

`close()` is the universal finaliser. Socket closure is what returns the device mouth to rest, so
it must happen on every path, including errors and disconnects.

---

## 6. Plugin lifecycle

### Loading

Hermes discovers plugins lazily and idempotently. `discover_and_load(force=False)` does not rescan
once a process has discovered, so:

**A plugin change needs a process restart, not merely a new session.** This was confirmed
empirically: the desktop backend performed full discovery passes at two separate times
("Plugin discovery complete: 61 found, 55 enabled"), each printing the plugin's own install line.
A new conversation inside an already-running backend would not pick up an updated plugin.

### Enabling

1. Copy the plugin directory into `~/.hermes/plugins/`.
2. `hermes plugins enable <plugin-name>`.
3. Restart the relevant Hermes session, or the gateway if gateway-originated speech is wanted.

Enabling adds the plugin to the `plugins.enabled` list in `~/.hermes/config.yaml`. `hermes
plugins enable` also prompts about the **tool-override** privilege; this plugin does not override
built-in tools and the privilege should stay declined.

### Disabling and removing

```bash
hermes plugins disable <plugin-name>   # keep the files, stop using it; restart to apply
hermes plugins remove  <plugin-name>   # or: rm -rf ~/.hermes/plugins/<plugin-name>/
```

Leftover `plugins.entries.<name>` settings are inert after removal.

### Unloading and guarded restoration

Cleanup is registered through `ctx.on_unload(...)`. On unload the plugin:

1. sets the global stop event so any in-flight file send halts,
2. removes the Edge streamer, so the desktop immediately returns to the stock fallback,
3. aborts every live desktop forwarder and clears the socket map,
4. restores every patched attribute,
5. clears runtime overrides and status flags.

Restoration is **conditional**: each attribute is restored only while it still points at the
plugin's replacement. If something else has since patched the same name, that patch is left alone
and the fact is logged. This is what makes unload safe in a process with other plugins.

Patches are applied in a recorded list and restored in reverse order. `register()` calls cleanup
first if patches are already live, so a reload cannot stack layers.

### Patch targets

Seven attributes across four modules:

| Target | Why it needs its own patch |
|---|---|
| `tools.voice_mode.play_audio_file` | The macOS playback choke point |
| `tools.voice_mode.stop_playback` | Lets barge-in cut the send mid-file |
| `hermes_cli.voice.play_audio_file` | Bound at module level there; bypasses the swap above |
| `tools.tts_tool._generate_edge_tts` | Bound at import time in `tts_tool.py`; the defining module is not where production reads |
| `starlette.WebSocket.send_json` | Arms the desktop tap from the handshake |
| `starlette.WebSocket.send_bytes` | The PCM egress |
| `starlette.WebSocket.close` | The universal finaliser |

Two of these exist because a name is bound at import time somewhere other than its definition.
Patching the defining module would compile, run, and silently do nothing. Any future patch to
Hermes internals should locate the call site first, not the definition.

---

## 7. Automated tests

Three local suites, all runnable without a device and without producing any audio: every local
playback call is intercepted by a recording stub, and the device is replaced by a throwaway local
TCP sink.

| Suite | Checks | What it proves |
|---|---:|---|
| File / CLI path | 51 | Patching, the `/jin` command, settings resolution and coercion, name respelling (including that it reaches the engine), all fallback branches, real ffmpeg decode and send, no-replay on mid-send failure, unload restoration, conditional restore, reload safety |
| Desktop streaming | 50 | The tap arms only on the handshake; PCM reaches the device while the renderer receives equal-length silence; every device block is 512 bytes; realtime pacing; chunked-vs-one-shot resampling is byte-identical; unreachable-before-audio keeps the Mac path; mid-stream failure causes no replay; barge-in cuts instead of draining; `/jin off` restores the stock path; unload restoration |
| mDNS resilience | 9 | An unresolvable name fails cleanly; a remembered address is used when the name fails; a pinned address skips resolution; a successful connect learns the address; the fallback still applies with nothing cached |

Test classes worth naming, because each rules out a different failure mode:

- **Continuity**: chunked vs one-shot resampling must be identical. Rules out per-frame resampling
  drift, which would sound like a click per frame and drift out of sync over a long utterance.
- **Protocol equivalence**: with `/jin off`, the renderer must receive the real frames byte for
  byte. Rules out "looks disabled but quietly changed something".
- **No-replay**: after a partial send, no local playback may occur. Rules out double-speech.
- **Cut vs drain**: barge-in must send far fewer blocks than the utterance contains. Rules out the
  device talking over the user.
- **Restoration identity**: after unload, each patched attribute must be the original object, and a
  foreign patch must survive. Rules out stacking and clobbering.

The desktop suite drives the **real Starlette WebSocket class methods** against the sink, not a
mock of them, so the tap is exercised through the same code path the server uses.

**These suites are not in the repository yet** — see "Gaps" below.

---

## 8. Known limitations and risks

### Confirmed behaviour

- The plugin only intercepts the process it is loaded in. Gateways and remote backends need their
  own load and their own restart.
- `POST /api/audio/speak` bypasses the plugin whenever no streaming provider is registered.
- The address cache is in-memory and per process.
- The LAN hop is **plain TCP with no authentication and no encryption**. Anyone who can reach the
  device port can make the avatar speak, and the audio is readable on the wire. Acceptable on a
  trusted home network; not acceptable beyond one.
- The wire protocol carries audio only. There is **no channel for expression or operational
  state**, so the eyes cannot follow what Hermes is actually doing yet. Adding one is a protocol
  change agreed with the firmware, not a configuration change.
- No microphone input path exists on the host side.
- The mouth follows a broadband amplitude envelope, not phonemes, so it indicates speech activity
  rather than specific sounds. This is a deliberate trade: a few well-behaved frames beat many
  inconsistent ones.
- The streaming path is verified only with the Edge streamer the plugin registers. The other
  chunked providers were not exercised.

### Assumptions and unverified risks

- **Barge-in detection is heuristic.** It relies on the endpoint *not* sending
  `{"type":"end"}` on a stop. That is true of the current source and was verified by reading the
  endpoint, but it is not a documented contract. If Hermes ever sends `end` on a barge-in, the
  plugin would drain instead of cutting.
- **The egress assumption is code-read, not test-enforced.** Nothing fails if a future Hermes
  release adds another PCM egress for this endpoint; the audio would simply bypass the tap.
- The device's tolerance of reads smaller than 512 bytes and any even size was read from the
  firmware source, not exercised.
- **Untested on real hardware:** multiple concurrent utterances, very long utterances, and packet
  loss or roaming mid-stream on Wi-Fi. The pacing and buffering are designed for these but have
  not been observed under them.
- Latency compensation uses a single calibrated offset. It has not been re-measured against
  different Wi-Fi conditions or buffer configurations.

---

## 9. Currently specific to Jin

Everything below embeds the prototype's name and must become configurable before a public release.

| Area | Where it lives | Note |
|---|---|---|
| Plugin identity | `plugin.yaml` `name: jin-esp32-bridge`, directory name, logger name `hermes_plugins.jin_esp32_bridge` | Logger name is derived from the plugin module, so renaming the directory handles it |
| Slash command | `ctx.register_command("jin", ...)`, `_cmd_jin` | Command name, usage text, and the `run /jin status` instructions |
| Hostname | `DEFAULTS["host"] = "jin.local"`, `jin_send.py` default | Also the firmware's `MDNS.begin("jin")`, which is where the name actually originates |
| Pronunciation | `DEFAULTS["jin_spelling"] = "Jinn"`, `_NAME_RE = re.compile(r"\bJin\b")` | Specific to both the name and to Edge's behaviour, so it should be data, not code |
| Wire magic | `MAGIC = b"JIN1"` and the firmware's byte-by-byte check | The protocol magic encodes the avatar's name |
| Visible names | Plugin `description`, `author: "Scott + Jin"`, README prose | Needs a neutral author identity for publication |
| Firmware strings | Serial output: "Connecting Jin to Wi-Fi", "Jin IP:", "Jin V5 ... ready" | Cosmetic but public-facing |
| Development tooling | `jin_send.py` name and its `--say` / probe messaging | |
| Experiment filenames | `JIN_Avatar_V*`, `JIN_Eye_*`, `JIN_avatar_spec.md`, `JIN_eye_prototype_spec.md` | Historical; renaming them would destroy the record, so leaving them is defensible |
| Docs | This file and `HARDWARE.md` reference "Jin" in places | |

Note the distinction: the **folder and repository name are already generic**, and the firmware
directory is already `hermes_esp32_avatar`. The name is baked into the plugin identity, the
command, the mDNS name, the protocol magic, and prose — not into the project's structure.

---

## 10. Proposed generic configuration design

Not implemented. This is a design for the generalization pass, recorded here so the reasoning is
not lost.

### Guiding constraint

The wire contract is the one thing that cannot be changed casually: it is implemented on both sides
and the device is already flashed. Identity (names, command, prose) must be configurable without
touching the protocol, and the protocol must be able to evolve independently of identity.

### Host-side configuration

A single settings block, all of it optional with the current values as defaults:

```yaml
plugins:
  entries:
    esp32-avatar:
      settings:
        avatar_name: ""            # used for prose and default substitution; empty = generic
        command: avatar            # slash command name
        command_aliases: []        # e.g. ["jin"] to keep a personal alias working
        host: avatar.local         # hostname or IP
        host_ip: ""                # pin the address, skipping mDNS
        port: 3333
        device_rate: 16000         # declared in the JIN1 header
        magic: JIN1                # accepted for compatibility with flashed devices
        pronunciation: {}          # {"Jin": "Jinn"} — token map, applied to TTS input only
        suppress_local: true
        mac_fallback: true
```

Design points:

- **Pronunciation becomes data.** A map of token to respelling, applied to the TTS-bound string
  only, with the current measured evidence kept in the docs rather than hardcoded. An empty map
  disables it. Generic by construction: any name whose pronunciation a provider gets wrong can be
  corrected, and nothing about it is Jin-specific except the entry.
- **Command name is configured, not literal.** The handler registers under `command`, with
  `command_aliases` so a personalized name can coexist without a second code path. The usage and
  status strings derive from the configured name.
- **The magic is a setting.** Flashed devices keep working if the default changes later, and a
  deployment can pin the old value.
- **Voice stays in Hermes' own config.** The plugin already reads the provider's voice and speed
  from `tts.edge` rather than assuming them, which is the right separation: voice is a Hermes
  concern, not an avatar concern.
- **`avatar_name` empty disables substitution entirely** rather than defaulting to a name, so a
  generic install has no invisible text transform.

### Protocol evolution

The current magic encodes one avatar's name and carries no version. Two options for the
generalization pass, and this is a real decision rather than a detail:

1. **Keep `JIN1` as an opaque protocol identifier forever.** Cheapest, but permanently ties a
   public protocol to one person's avatar.
2. **Move to a neutral magic plus a version byte**, accepting `JIN1` as a deprecated alias so
   already-flashed devices continue to work.

Option 2 is the better long-term shape, and the cost is lowest **now**, while exactly one device
exists in the world. Doing it later means either a firmware update for every deployment or
permanent support for a legacy magic.

The same version field would carry the future expression/state channel, which is the next
capability the protocol needs: audio alone cannot drive the eyes.

### Firmware-side configuration

Recommendation: the firmware keeps owning all pins and timing in one place, and the plugin never
learns about GPIO. Concretely:

- A single `config.h` (checked in with defaults, overridable) holding display pins, I2S pins,
  audio rate, output-latency offset, mDNS name, and port.
- `secrets.h` stays separate and gitignored, holding only credentials.
- A short documented list of required values, so a new board is a config change rather than a code
  change.

This keeps the boundary clean: **firmware owns hardware and timing, host owns transport and
policy.** The only shared surface is the wire contract, which is why it should be versioned and
documented on its own.

### Separation worth considering

The wire contract now serves two independent consumers (firmware and host), and the desktop tap is
the most intricate part of the host side. That argues for splitting the host plugin into a small
transport layer (encode, pace, send, fall back) and the Hermes-specific patching layer, so the
transport can be tested and reused without a running Hermes. The current single file is fine for a
prototype; it is not the shape a public repository should ship.

---

## 11. Gaps in the current snapshot

Not part of this document's scope, recorded because they block publication:

- **The automated suites are not in the repository.** The checks described in section 7 live
  outside the project. They are the strongest evidence this integration works and they should move
  in, ideally before any refactor, so the refactor can be proven safe.
- **No `LICENSE` file,** though the README lists it as a to-do.
- **No attribution for dependencies.** The firmware uses Adafruit GFX and Adafruit ST7789; the host
  side depends on Hermes Agent, `edge_tts`, and `ffmpeg`. Licences need checking and recording.
- **No protocol document.** The wire contract is shared by two independent consumers and currently
  exists only as code plus this file's summary. It deserves its own versioned document.
- **No firmware versioning.** The plugin declares a version; the firmware does not, so a flashed
  device cannot be identified.
- **No security note.** The unauthenticated plaintext LAN port should be stated plainly, with the
  intended deployment boundary.
- **No reproducible build pinning.** No PlatformIO configuration and no recorded Arduino core or
  library versions.
- **No example Hermes config snippet** in the repository; it currently lives only inside the
  plugin's own README.
- **No test for the bypass paths.** Section 1 lists what still bypasses the plugin; nothing asserts
  that list stays accurate as Hermes evolves.
