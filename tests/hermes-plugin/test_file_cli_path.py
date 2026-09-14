"""File and CLI playback path.

Repo-contained port of the verification suite used for this build.
Harness globals (`check`, `REPO`, `HERMES_SRC`, `load_plugin`, `SkipSuite`) are injected by
run_tests.py before this file executes. Local fake sockets and playback stubs only: no device
contact, no audible output, no external TTS calls, no credentials.
"""
import asyncio
import importlib.util
import json
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

CHECKS = 51

sys.path.insert(0, HERMES_SRC)
import tools.voice_mode as vm
import hermes_cli.voice as cli_voice
import tools.tts_tool as tts_tool
import edge_tts                     # noqa: E402  (Communicate is replaced with a fake)


PLUGIN = REPO / "hermes-plugin" / "jin-esp32-bridge" / "__init__.py"

# ── recording stub for local playback: this is what guarantees silence ───────
PLAYED = []
REAL_PLAY = vm.play_audio_file
REAL_STOP = vm.stop_playback
REAL_CLI_PLAY = cli_voice.play_audio_file
REAL_EDGE = tts_tool._generate_edge_tts


def local_stub(file_path, *a, **kw):
    PLAYED.append(str(file_path))
    return True


vm.play_audio_file = local_stub
cli_voice.play_audio_file = local_stub   # production has these two pointing at the SAME object

# ── load the plugin the way a hyphenated plugin dir must be loaded ──────────
spec = importlib.util.spec_from_file_location("jin_esp32_bridge", PLUGIN)
mod = importlib.util.module_from_spec(spec)
sys.modules["jin_esp32_bridge"] = mod
spec.loader.exec_module(mod)


class FakeCtx:
    def __init__(self, settings=None):
        self.settings = dict(settings or {})
        self.unload, self.commands = [], {}

    def get_config(self, key, default=None):
        return self.settings.get(key, default)

    def set_config(self, key, value):
        self.settings[key] = value

    def on_unload(self, cb):
        self.unload.append(cb)

    def register_command(self, name, handler, description="", args_hint="", argument_mode=None):
        self.commands[name] = handler


print("\n1. registration + patch targets")
check("cli_voice and voice_mode share one play_audio_file object (pre-patch)",
      cli_voice.play_audio_file is vm.play_audio_file)
ctx = FakeCtx()
mod.register(ctx)
check("tools.voice_mode.play_audio_file patched", vm.play_audio_file is not local_stub)
check("tools.voice_mode.stop_playback patched", vm.stop_playback is not REAL_STOP)
check("hermes_cli.voice.play_audio_file patched", cli_voice.play_audio_file is not REAL_CLI_PLAY)
check("tools.tts_tool._generate_edge_tts patched", tts_tool._generate_edge_tts is not REAL_EDGE)
check("7 patch records tracked (4 file/CLI + 3 WebSocket)", len(mod._patched) == 7, f"len={len(mod._patched)}")
check("/jin command registered", "jin" in ctx.commands)
check("on_unload registered", len(ctx.unload) == 1)

print("\n2. 'Jin' respelling (TTS-bound text only)")
cases = [
    ("Jin here.", "Jinn here."),
    ("Jin's voice", "Jinn's voice"),
    ("jin.local stays", "jin.local stays"),
    ("JIN1 stays", "JIN1 stays"),
    ("jin-esp32-bridge stays", "jin-esp32-bridge stays"),
    ("I asked Jin and Jin answered Jin.", "I asked Jinn and Jinn answered Jinn."),
]
for src, want in cases:
    got = mod._apply_jin_spelling(src)
    check(f"{src!r} -> {got!r}", got == want, "" if got == want else f"expected {want!r}")

print("\n3. the respelling actually reaches the TTS engine")
import edge_tts                                                   # noqa: E402
SEEN = {}


class _FakeCommunicate:
    def __init__(self, text, **kw):
        SEEN["text"] = text
        self._text = text

    async def save(self, path):
        Path(path).write_bytes(b"\x00" * 64)


edge_tts.Communicate = _FakeCommunicate
out = Path("/tmp/jin_edge_probe.mp3")
asyncio.run(tts_tool._generate_edge_tts("Jin here, said Jin.", str(out), {"edge": {"voice": "en-US-GuyNeural"}}))
check("engine received the respelling", SEEN.get("text") == "Jinn here, said Jinn.", f"got {SEEN.get('text')!r}")

print("\n4. /jin command")
status = ctx.commands["jin"]("status")
check("status mentions ON", "ON" in status)
check("status shows device", "jin.local:3333" in status)
check("status shows transport", "512 B blocks (16 ms)" in status)
check("usage on bad arg", ctx.commands["jin"]("bogus").startswith("Usage:"))
off = ctx.commands["jin"]("off")
check("off reports OFF", "jin-esp32-bridge: OFF" in off, off.splitlines()[0])
check("off persisted to settings", ctx.settings.get("esp32_output") is False)
check("off takes effect immediately", mod._setting("esp32_output") is False)
ctx.commands["jin"]("on")
check("on takes effect immediately", mod._setting("esp32_output") is True)

print("\n5. settings resolution from ctx")
ctx2 = FakeCtx({"host": "example.invalid", "port": "4444", "suppress_local": False,
                "mac_fallback": "no", "jin_spelling": ""})
mod._ctx = ctx2
mod._override.clear()
check("host from settings", mod._setting("host") == "example.invalid")
check("port coerced str->int", mod._setting("port") == 4444 and isinstance(mod._setting("port"), int))
check("bool coerced from bool", mod._setting("suppress_local") is False)
check("bool coerced from str 'no'", mod._setting("mac_fallback") is False)
check("empty spelling disables substitution", mod._apply_jin_spelling("Jin") == "Jin")
mod._ctx = ctx
mod._override.clear()

print("\n6. fallback rules (all local playback intercepted)")
DEAD_PORT = 39998  # nothing listening


def reset(**over):
    mod._override.clear()
    mod._override.update(over)
    PLAYED.clear()


# 6a. device output off -> Mac, no send
reset(esp32_output=False, host="127.0.0.1", port=DEAD_PORT)
vm.play_audio_file("/tmp/does-not-need-to-exist-1")
check("output OFF plays locally", PLAYED == ["/tmp/does-not-need-to-exist-1"])

# 6b. on, unreachable before any audio, fallback on -> Mac
reset(esp32_output=True, suppress_local=True, mac_fallback=True, host="127.0.0.1", port=DEAD_PORT)
vm.play_audio_file("/tmp/x-2")
check("unreachable + fallback on plays locally", PLAYED == ["/tmp/x-2"])

# 6c. on, unreachable, fallback OFF -> nothing at all
reset(esp32_output=True, suppress_local=True, mac_fallback=False, host="127.0.0.1", port=DEAD_PORT)
res = vm.play_audio_file("/tmp/x-3")
check("unreachable + fallback off plays nothing", PLAYED == [] and res is False)

# 6d. report exactly what _send_file returned for an unreachable host
reset(esp32_output=True, host="127.0.0.1", port=DEAD_PORT)
check("_send_file returns 'unavailable' when nothing was sent", mod._send_file("/tmp/x") == "unavailable")

print("\n7. live local sink: real ffmpeg decode, real send, real pacing (no device involved)")
SRC = "/tmp/jin_test_tone.wav"
subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=0.5",
                "-ar", "24000", "-ac", "1", "-c:a", "pcm_s16le", SRC], check=True)


class Sink:
    def __init__(self):
        self.bytes = 0
        self.blocks = 0
        self.header = b""
        self.sizes = set()
        self.srv = socket.socket()
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.port = self.srv.getsockname()[1]
        self.srv.listen(4)
        self.done = threading.Event()
        threading.Thread(target=self._serve, daemon=True).start()

    def _serve(self):
        conn, _ = self.srv.accept()
        with conn:
            self.header = conn.recv(8)
            while True:
                b = conn.recv(512)
                if not b:
                    break
                self.bytes += len(b)
                self.blocks += 1
                self.sizes.add(len(b))
        self.done.set()


sink = Sink()
reset(esp32_output=True, suppress_local=True, mac_fallback=False, host="127.0.0.1", port=sink.port)
t0 = time.monotonic()
res = vm.play_audio_file(SRC)
elapsed = time.monotonic() - t0
sink.done.wait(5)
import struct as _struct
magic, rate = _struct.unpack("<4sI", sink.header)
check("send reported success", res is True)
check("header magic + rate correct", magic == b"JIN1" and rate == 16000, f"{magic!r} {rate}")
check("all wire blocks are 512 B", sink.sizes == {512}, f"{sink.sizes}")
check("audio arrived, tail-padded to a whole number of blocks",
      sink.bytes == 16384 and sink.blocks == 32,
      f"{sink.bytes} bytes = {sink.blocks} blocks (16000 B of audio padded to 32x512)")
check("paced to realtime (~0.5 s)", 0.40 < elapsed < 0.75, f"{elapsed:.3f}s")
check("Mac stayed silent (suppressed)", PLAYED == [])

print("\n8. mid-send failure must NOT replay locally")
def boom_blocks(path, block=mod.BLOCK):
    yield b"\x00" * block
    raise OSError("simulated link drop after audio started")


sink2 = Sink()
sink2.srv.settimeout(5)
_real_blocks = mod._blocks
mod._blocks = boom_blocks
reset(esp32_output=True, suppress_local=True, mac_fallback=True, host="127.0.0.1", port=sink2.port)
res2 = vm.play_audio_file("/tmp/irrelevant")
mod._blocks = _real_blocks
check("wrapper returns success (not the unavailable path)", res2 is True)
check("no local replay after partial send", PLAYED == [], f"played={PLAYED}")
check("_blocks restored", callable(mod._blocks))

print("\n9. unload restoration")

mod._stop.clear()
ctx.unload[0]()
check("vm.play_audio_file restored to our stub", vm.play_audio_file is local_stub)
check("vm.stop_playback restored", vm.stop_playback is REAL_STOP)
check("hermes_cli.voice.play_audio_file restored", cli_voice.play_audio_file is local_stub)
check("tts_tool._generate_edge_tts restored", tts_tool._generate_edge_tts is REAL_EDGE)
check("no patch records left", mod._patched == [])
check("active transmission signalled to stop", mod._stop.is_set())

print("\n10. conditional restore: never clobber a foreign patch")
mod.register(ctx)
SENTINEL = lambda *a, **k: "sentinel"                                  # noqa: E731
cli_voice.play_audio_file = SENTINEL
mod._cleanup()
check("sentinel preserved when not ours", cli_voice.play_audio_file is SENTINEL)
check("our other patches still restored", vm.play_audio_file is local_stub)
cli_voice.play_audio_file = local_stub

print("\n11. reload safety: registering twice must not stack patches")
mod.register(ctx)
first = len(mod._patched)
mod.register(ctx)
second = len(mod._patched)
check("patch count does not grow on re-register", second == first == 7, f"{first} -> {second}")
check("still exactly one replacement layer: both cli and vm point at our wrapper",
      vm.play_audio_file is not local_stub and cli_voice.play_audio_file is vm.play_audio_file,
      f"vm patched={vm.play_audio_file is not local_stub}, shared={cli_voice.play_audio_file is vm.play_audio_file}")
mod._cleanup()

# ── restore the real sinks, the test is over ────────────────────────────────
vm.play_audio_file = REAL_PLAY
vm.stop_playback = REAL_STOP
