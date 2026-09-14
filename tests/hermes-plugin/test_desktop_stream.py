"""Desktop read-replies speak-stream path.

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

CHECKS = 50

sys.path.insert(0, HERMES_SRC)
import tools.voice_mode as vm
import hermes_cli.voice as cli_voice
import tools.tts_tool as tts_tool
from starlette.websockets import WebSocket   # noqa: E402


PLUGIN = REPO / "hermes-plugin" / "jin-esp32-bridge" / "__init__.py"

REAL = {n: getattr(WebSocket, n) for n in ("send_json", "send_bytes", "close")}
REAL_PLAY, REAL_STOP, REAL_CLI, REAL_EDGE = (
    vm.play_audio_file, vm.stop_playback, cli_voice.play_audio_file, tts_tool._generate_edge_tts)
PLAYED = []


def local_stub(file_path, *a, **kw):
    PLAYED.append(str(file_path))
    return True


vm.play_audio_file = local_stub
cli_voice.play_audio_file = local_stub

spec = importlib.util.spec_from_file_location("jin_esp32_bridge", PLUGIN)
mod = importlib.util.module_from_spec(spec)
sys.modules["jin_esp32_bridge"] = mod
spec.loader.exec_module(mod)


class FakeCtx:
    def __init__(self, settings=None):
        self.settings, self.unload, self.commands = dict(settings or {}), [], {}

    def get_config(self, k, d=None):
        return self.settings.get(k, d)

    def set_config(self, k, v):
        self.settings[k] = v

    def on_unload(self, cb):
        self.unload.append(cb)

    def register_command(self, n, h, **k):
        self.commands[n] = h


class Sink:
    """Throwaway TCP sink standing in for the ESP32."""

    def __init__(self, reset_after=None):
        self.reset_after = reset_after
        self.header = b""
        self.blocks, self.payload, self.sizes = 0, 0, set()
        self.closed = threading.Event()
        self.srv = socket.socket()
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.port = self.srv.getsockname()[1]
        self.srv.listen(4)
        threading.Thread(target=self._serve, daemon=True).start()

    def _serve(self):
        conn, _ = self.srv.accept()
        read = 0
        with conn:
            self.header = conn.recv(8)
            while True:
                b = conn.recv(512)
                if not b:
                    break
                read += len(b)
                self.blocks += 1
                self.payload += len(b)
                self.sizes.add(len(b))
                if self.reset_after is not None and read >= self.reset_after:
                    conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                    struct.pack("ii", 1, 0))   # RST on close
                    break
        self.closed.set()


def make_ws(client_frames):
    """A real Starlette WebSocket whose ASGI send records instead of transmitting."""
    scope = {"type": "websocket", "path": "/api/audio/speak-stream", "headers": [],
             "query_string": b"", "scheme": "ws", "subprotocols": [],
             "client": ("127.0.0.1", 1), "server": ("127.0.0.1", 8000)}

    async def _send(msg):
        client_frames.append(msg)

    conn = {"sent": False}

    async def _receive():
        # Starlette's accept() waits for the ASGI websocket.connect message before it will
        # send the handshake; after that the endpoint only receives on barge-in, which this
        # harness does not exercise, so block.
        if not conn["sent"]:
            conn["sent"] = True
            return {"type": "websocket.connect"}
        await asyncio.sleep(3600)

    return WebSocket(scope, receive=_receive, send=_send)


def pcm(n_samples, rate=24000):
    """A deterministic ramp, so resampling errors are detectable."""
    return b"".join(struct.pack("<h", (i * 61) % 3000 - 1500) for i in range(n_samples))


print("\n1. registration")
ctx = FakeCtx()
mod.register(ctx)
check("WebSocket.send_json patched", WebSocket.send_json is not REAL["send_json"])
check("WebSocket.send_bytes patched", WebSocket.send_bytes is not REAL["send_bytes"])
check("WebSocket.close patched", WebSocket.close is not REAL["close"])
check("desktop tap reported", mod._DESKTOP_TAP_OK is True)
check("file tap reported", mod._FILE_TAP_OK is True)
check("edge streamer registered", mod._EDGE_STREAMER_BACKUP != [])
from tools.tts_streaming import _REGISTRY, resolve_streaming_provider   # noqa: E402
check("_REGISTRY['edge'] is the plugin streamer", _REGISTRY.get("edge") is mod.JinEdgeStreamer)
check("resolve_streaming_provider now resolves edge",
      resolve_streaming_provider({"provider": "edge"}) is not None)
status = ctx.commands["jin"]("status")
check("status reports file/CLI path", "file/CLI playback patched" in status)
check("status reports desktop path", "desktop read-replies patched" in status)
check("status reports edge streamer", "edge streamer registered" in status)

print("\n2. stateful resampler: chunked feed must equal one-shot feed")
src = pcm(2400)
one = mod._Resampler(24000)
whole = one.process(src)
chunked = mod._Resampler(24000)
parts = [chunked.process(src[i:i + 100]) for i in range(0, len(src), 100)]  # 100 B = 50 samples
joined = b"".join(parts)
check("chunked == one-shot (no discontinuity, no drift)", joined == whole,
      f"{len(joined)}B vs {len(whole)}B")
check("output length is 2/3 of input", len(whole) == 3200, f"{len(whole)}B")

print("\n3. desktop streaming goes to the device and silences the renderer")
sink = Sink()
mod._override.clear()
mod._override.update(esp32_output=True, host="127.0.0.1", port=sink.port, suppress_local=True)
frames = []
ws = make_ws(frames)


async def _stream():
    await ws.accept()
    await ws.send_json({"type": "start", "sample_rate": 24000, "channels": 1})
    for i in range(0, len(src), 400):
        await ws.send_bytes(src[i:i + 400])
    await ws.send_json({"type": "end"})
    await ws.close()


t0 = time.monotonic()
asyncio.run(_stream())
sink.closed.wait(5)
elapsed = time.monotonic() - t0
magic, rate = struct.unpack("<4sI", sink.header)
binary = [f for f in frames if f.get("type") == "websocket.send" and "bytes" in f]
check("device header is JIN1 @ 16000", magic == b"JIN1" and rate == 16000, f"{magic!r} {rate}")
check("every device block is exactly 512 B", sink.sizes == {512}, f"{sink.sizes}")
check("device payload matches the resampled 0.1 s + final padded block",
      sink.payload == 3584 and sink.blocks == 7, f"{sink.payload}B / {sink.blocks} blocks")
real_bytes = sum(len(src[i:i + 400]) for i in range(0, len(src), 400))
check("renderer got equal-length frames (protocol intact)", len(binary) == 12, f"{len(binary)}")
nonzero = [(len(f["bytes"]), sum(1 for b in f["bytes"] if b)) for f in binary]
check("renderer frames are silence, not audio",
      all(nz == 0 for _l, nz in nonzero), f"(len,nonzero)={nonzero[:3]}")
check("renderer silence length matches the real frames it replaced",
      sum(len(f["bytes"]) for f in binary) == real_bytes, f"{sum(len(f['bytes']) for f in binary)}B")
json_types = [json.loads(f["text"]).get("type") for f in frames if "text" in f]
check("start/end JSON still reach the renderer", json_types == ["start", "end"], f"{json_types}")
check("paced to realtime (~7 x 16 ms)", 0.06 < elapsed < 0.45, f"{elapsed:.3f}s")
check("forwarder released after close", len(mod._WS_FWD) == 0)

print("\n4. unreachable before any audio -> the Mac path is retained")
mod._override.update(host="127.0.0.1", port=39997)
frames2 = []
ws2 = make_ws(frames2)


async def _unreachable():
    await ws2.accept()
    await ws2.send_json({"type": "start", "sample_rate": 24000, "channels": 1})
    await ws2.send_bytes(src)
    await ws2.close()


asyncio.run(_unreachable())
binary2 = [f["bytes"] for f in frames2 if "bytes" in f]
check("no forwarder was created", len(mod._WS_FWD) == 0)
check("renderer received the REAL audio (Mac fallback intact)",
      binary2 == [src], f"{[len(b) for b in binary2]}")

print("\n5. failure after audio began -> no replay of the utterance")
sink3 = Sink(reset_after=2048)
mod._override.update(host="127.0.0.1", port=sink3.port)
frames3 = []
ws3 = make_ws(frames3)
big = pcm(48000)          # 1.0 s of source, enough to outlive the reset


async def _fail_mid():
    await ws3.accept()
    await ws3.send_json({"type": "start", "sample_rate": 24000, "channels": 1})
    for i in range(0, len(big), 4096):
        await ws3.send_bytes(big[i:i + 4096])
    await ws3.send_json({"type": "end"})
    await ws3.close()


asyncio.run(_fail_mid())
time.sleep(0.3)
binary3 = [f["bytes"] for f in frames3 if "bytes" in f]
check("renderer still got only silence (no restart on the Mac)",
      binary3 and all(set(b) == {0} for b in binary3))
check("device received a partial utterance then stopped", sink3.payload > 0, f"{sink3.payload}B")

print("\n6. cancellation / barge-in")
sink4 = Sink()
mod._override.update(host="127.0.0.1", port=sink4.port)
frames4 = []
ws4 = make_ws(frames4)


async def _barge():
    await ws4.accept()
    await ws4.send_json({"type": "start", "sample_rate": 24000, "channels": 1})
    await ws4.send_bytes(pcm(24000))
    # barge-in: the endpoint never sends {"type":"end"} on stop, it just closes
    await ws4.close()


asyncio.run(_barge())
sink4.closed.wait(3)
check("close finalises the stream so the mouth returns to rest", sink4.closed.is_set())
check("socket closed after cancellation", len(mod._WS_FWD) == 0)
check("barge-in cut the queued audio instead of draining it", sink4.blocks < 10,
      f"sent {sink4.blocks} of 63 possible blocks")
check("only whole blocks ever reached the device", sink4.sizes <= {512}, f"{sink4.sizes}")

print("\n7. /jin off restores the stock desktop path")
off = ctx.commands["jin"]("off")
check("off reports the streamer removed", "removed" in off, off.splitlines()[0])
check("_REGISTRY no longer holds edge", "edge" not in _REGISTRY)
check("resolve_streaming_provider returns None again (stock fallback)",
      resolve_streaming_provider({"provider": "edge"}) is None)
frames5 = []
ws5 = make_ws(frames5)


async def _off():
    await ws5.accept()
    await ws5.send_json({"type": "start", "sample_rate": 24000, "channels": 1})
    await ws5.send_bytes(src)
    await ws5.close()


asyncio.run(_off())
binary5 = [f["bytes"] for f in frames5 if "bytes" in f]
check("with /jin off the renderer gets the real audio untouched", binary5 == [src])
check("no forwarder created while off", len(mod._WS_FWD) == 0)
check("status now reports the stock path", "not registered" in ctx.commands["jin"]("status"))

ctx.commands["jin"]("on")
check("back on: edge streamer re-registered", _REGISTRY.get("edge") is mod.JinEdgeStreamer)

print("\n8. the edge streamer actually produces PCM (no playback anywhere)")
try:
    streamer = mod.JinEdgeStreamer({"provider": "edge"}, {})
    chunks = [c for c in streamer.stream("Jin here. Testing one two.")]
    total = sum(len(c) for c in chunks)
    check("stream yielded int16 PCM frames", total > 2000 and total % 2 == 0 and len(chunks) > 1,
          f"{total}B in {len(chunks)} chunks")
    check("declares 24 kHz mono int16", (streamer.sample_rate, streamer.channels, streamer.sample_width)
          == (24000, 1, 2))
except Exception as exc:
    check("stream yielded int16 PCM frames", False, f"raised {exc!r}")

print("\n9. unload restoration")
mod._stop.clear()
ctx.unload[0]()
check("WebSocket.send_json restored", WebSocket.send_json is REAL["send_json"])
check("WebSocket.send_bytes restored", WebSocket.send_bytes is REAL["send_bytes"])
check("WebSocket.close restored", WebSocket.close is REAL["close"])
check("edge streamer unregistered on unload", "edge" not in _REGISTRY)
check("all forwarders aborted", len(mod._WS_FWD) == 0)
check("no patch records left", mod._patched == [])
check("tap flags cleared", mod._DESKTOP_TAP_OK is False and mod._FILE_TAP_OK is False)
check("playback restored to our stub", vm.play_audio_file is local_stub)

print("\n10. reload safety with the desktop tap included")
mod.register(ctx)
first = len(mod._patched)
mod.register(ctx)
check("re-register does not stack patches", len(mod._patched) == first,
      f"{first} -> {len(mod._patched)}")
check("7 patch targets tracked", first == 7, f"{first}")
mod._cleanup()
check("clean after cleanup", mod._patched == [] and "edge" not in _REGISTRY)

vm.play_audio_file, vm.stop_playback = REAL_PLAY, REAL_STOP
cli_voice.play_audio_file = REAL_CLI
