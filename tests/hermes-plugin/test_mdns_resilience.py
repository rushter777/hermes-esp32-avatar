"""Address resilience when the hostname fails to resolve.

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

CHECKS = 9

sys.path.insert(0, HERMES_SRC)
import tools.voice_mode as vm
import hermes_cli.voice as cli_voice
import tools.tts_tool as tts_tool

PLUGIN = REPO / "hermes-plugin" / "jin-esp32-bridge" / "__init__.py"

spec = importlib.util.spec_from_file_location("avatar_plugin", PLUGIN)
mod = importlib.util.module_from_spec(spec)
sys.modules["avatar_plugin"] = mod
spec.loader.exec_module(mod)


class C:
    def __init__(self):
        self.s = {}

    def get_config(self, k, d=None):
        return self.s.get(k, d)

    def set_config(self, k, v):
        self.s[k] = v

    def on_unload(self, cb):
        pass

    def register_command(self, n, h, **k):
        self.c = {n: h}


mod.register(C())

class Sink:
    def __init__(self):
        self.srv = socket.socket(); self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0)); self.port = self.srv.getsockname()[1]; self.srv.listen(4)
        self.ok = False
        self.header_received = threading.Event()
        threading.Thread(target=self._serve, daemon=True).start()

    def _serve(self):
        while True:                      # accept repeatedly: earlier checks consume connections too
            try:
                conn, _ = self.srv.accept()
            except OSError:
                return
            with conn:
                head = conn.recv(8)
                if head == b"JIN1" + (16000).to_bytes(4, "little"):
                    self.ok = True
                    self.header_received.set()


sink = Sink()
BOGUS = "jin-nonexistent-xyz.invalid"     # RFC 2606: guaranteed not to resolve

print("\n1. reproduce the live failure: a name that will not resolve")
mod._override.clear()
mod._override.update(host=BOGUS, port=sink.port, esp32_output=True)
mod._LAST_GOOD.clear()
sock, errors = mod._connect_device(BOGUS, sink.port)
check("unresolvable name fails cleanly (no hang, no exception)", sock is None, f"errors={len(errors)}")
check("error is reported for diagnosis", bool(errors) and "invalid" in errors[0])

print("\n2. the fix: a remembered address is used when the name fails")
mod._LAST_GOOD[BOGUS] = "127.0.0.1"
sock, errors = mod._connect_device(BOGUS, sink.port)
check("connects via the cached address despite the bad name", sock is not None, f"errors={errors}")
if sock:
    sock.close()

print("\n3. an explicitly pinned host_ip wins outright")
mod._LAST_GOOD.clear()
mod._override["host_ip"] = "127.0.0.1"
sock, errors = mod._connect_device(BOGUS, sink.port)
check("pinned host_ip connects with no name lookup", sock is not None, f"errors={errors}")
if sock:
    sock.close()

print("\n4. a fresh address is learned on success")
mod._override["host_ip"] = ""
mod._LAST_GOOD.clear()
sock, _e = mod._connect_device("127.0.0.1", sink.port)
if sock:
    sock.close()
check("successful connect records the address", mod._LAST_GOOD.get("127.0.0.1") == "127.0.0.1",
      f"{mod._LAST_GOOD}")

print("\n5. the real desktop path recovers from a bad name too")
mod._override.update(host=BOGUS, host_ip="", port=sink.port)
mod._LAST_GOOD[BOGUS] = "127.0.0.1"
fwd = mod._Forwarder(BOGUS, sink.port, 24000)
sink.ok = False
sink.header_received.clear()
check("_Forwarder.connect succeeds via the cached address", fwd.connect() is True)
fwd.finish()
check("device received the JIN1 header", sink.header_received.wait(timeout=1.0))

print("\n6. with nothing cached and a bad name, the Mac fallback still applies")
mod._LAST_GOOD.clear()
mod._override.update(host=BOGUS, port=sink.port)
fwd2 = mod._Forwarder(BOGUS, sink.port, 24000)
check("connect fails, marking the stream unavailable", fwd2.connect() is False)
check("unavailable == 'nothing was sent', so local playback is safe", fwd2.unavailable is True)
