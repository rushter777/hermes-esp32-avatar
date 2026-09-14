# Plugin verification suite

110 checks covering the Hermes side of the avatar. Run from the project root:

```bash
python3 tests/hermes-plugin/run_tests.py
```

No pytest, no third-party packages, no configuration files. Standard library only.

## Interpreter and Hermes

The checks exercise the plugin against real Hermes modules, so they need a Hermes checkout that
the interpreter can import. The runner looks for it in this order:

1. `HERMES_AGENT_PATH`, if set and valid,
2. an already-importable `tools.voice_mode`,
3. `~/.hermes/hermes-agent` (Hermes' default install location).

System `python3` is often older than Hermes requires. When the current interpreter cannot import
Hermes but a Hermes virtualenv exists, each module re-executes itself once with that interpreter's
Python, so the single command above works without further setup.

To run against a specific checkout:

```bash
HERMES_AGENT_PATH=/path/to/hermes-agent python3 tests/hermes-plugin/run_tests.py
```

When no Hermes is found, the modules **skip** rather than fail, and the summary reports the exact
skipped count:

```
  0 passed   110 skipped   0 failed   (110 checks)
```

Each module declares its check count as `CHECKS = <n>`. The runner compares the declared count
with the number of checks actually produced and reports a mismatch as a failure, so the totals
cannot drift silently.

## Modules

Each module runs in its own process. That is not tidiness: the suites patch live Hermes module
attributes and a shared provider registry, so running them in one interpreter makes them
contaminate each other. Process isolation was added after exactly that failure.

| Module | Checks | Covers |
|---|---:|---|
| `test_file_cli_path.py` | 51 | Patching and restoration of the file/CLI playback path; `/jin on`, `/jin off`, `/jin status`; settings resolution and coercion; name respelling; every fallback branch; a real local decode and send; no-replay after partial delivery; unload restoration; reload safety |
| `test_desktop_stream.py` | 50 | Arming the tap only on the speak-stream handshake; PCM to the device while the renderer receives equal-length silence; exact 512-byte blocks; realtime pacing; chunked-versus-one-shot resampling equivalence; unreachable-before-audio keeps the Mac path; mid-stream failure causes no replay; barge-in cuts instead of draining; `/jin off` restores the stock path; unload restoration; reload safety |
| `test_mdns_resilience.py` | 9 | An unresolvable hostname fails cleanly; a remembered address is used when the name fails; a pinned address skips resolution; a successful connect learns the address; the Mac fallback still applies with nothing cached |

The desktop module drives the **real Starlette WebSocket class methods** against a fake sink
rather than a mock of them, so the tap is exercised through the same code path the server uses.

## Test guarantees

Every module, by default:

- uses local TCP sockets on `127.0.0.1` as stand-ins for the device, and recording stubs for
  playback, so **no audio is ever produced**;
- never contacts the ESP32 or any network address outside localhost;
- replaces `edge_tts.Communicate` with a fake before any synthesis, so **no external TTS service
  is called**;
- requires no credentials, API keys, or Wi-Fi details;
- creates no files inside the repository. Scratch files go to the system temporary directory.

The one real external dependency is the local `ffmpeg` binary, used to decode a generated tone.
Nothing is downloaded.

## Not included, and why

These were used to support the reported results but are deliberately absent from the repository:

- **The pronunciation measurement.** Edge was asked to synthesize seven candidate spellings and a
  local Whisper model transcribed each one, to establish that Edge reads the literal spelling as
  "Jean" and that several respellings land on the intended sound. It is excluded because it calls
  an external TTS service and needs a speech-recognition model. The *substitution logic* is fully
  covered offline; the acoustic evidence is recorded in `docs/HERMES-INTEGRATION.md`.
- **Hardware-in-the-loop behaviour.** No test proves that the firmware's mouth animation tracks the
  audio, that its envelope thresholds behave as intended, or that the device accepts the stream
  over real Wi-Fi. Those were verified by ear and by photograph on the physical device.
- **The end-to-end desktop path.** The suites cover the interception point, but not a running
  Hermes app, the FastAPI app object, the Electron renderer, or the sentence pipeline that feeds
  the stream. Reproducing that needs a live Hermes session.
- **Firmware builds.** There is no compile check or firmware test here.
- **Real network conditions.** Wi-Fi dropouts, roaming, DHCP lease changes, and mDNS flakiness
  were observed live but are not reproducible in a test; `test_mdns_resilience.py` models the
  resolution failure without needing a network.
- **The live device send.** `jin_send.py --say` proves the device plays audio; it needs hardware
  and it makes sound, so it stays a manual check.
