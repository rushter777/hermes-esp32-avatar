"""jin-esp32-bridge — route Hermes TTS to the Jin ESP32 avatar.

Transport (unchanged from the build that passed on the device)
-------------------------------------------------------------
* 16 kHz, signed int16, little-endian, mono PCM.
* **512-byte blocks** = 256 samples = exactly **16 ms** per block.
* Every block paced against a monotonic deadline ``t0 + n * period`` (no drift).
* ``TCP_NODELAY`` set; 62.5 small writes/second is otherwise Nagle bait.
* **One ``JIN1`` header and one TCP connection per sentence file, closed at end of file.**
  The firmware uses connection closure to return the mouth to flat and reset its sample
  timeline, so no connection is ever held idle between sentences.
* Final partial block padded with silence so every block carries a full 16 ms.

Playback policy
---------------
``esp32_output`` on + ``suppress_local`` on (defaults): the Mac is muted and the send runs
*synchronously* inside ``play_audio_file``, wrapped in ``mark_audio_output_active(True/False)``.
Because the send is paced to realtime the activity window equals the true audio duration, so
``is_audio_output_active()`` stays honest for full-duplex barge-in.

Fallback: if the device cannot be reached **before any audio is sent**, the original Mac sink is
called (``mac_fallback``). If a send fails **after** audio started, there is deliberately no local
replay — the head already played on the device and the tail would double-speak.

Patch targets (three, and they are not interchangeable)
-------------------------------------------------------
``play_audio_file`` is imported lazily inside each function at most call sites, so the module
attribute swap covers them — but ``hermes_cli/voice.py:112`` binds it at **module level** for its
``speak_text`` path. And ``_generate_edge_tts`` is bound at import time in ``tools/tts_tool.py``
(line 56), which is where production actually calls it. Patching the defining module
(``tools.tts_tool_providers``) would silently do nothing.

``ctx.on_unload`` restores every attribute it swapped, but only while it still points at our
replacement, so a later patch by something else is never clobbered. Registering twice (a reload)
cleans up first, so patches cannot stack.
"""

from __future__ import annotations

import array
import asyncio
import contextlib
import logging
import math
import queue
import re
import shutil
import socket
import struct
import subprocess
import threading
import time
import weakref
from typing import Any, Dict, Optional

logger = logging.getLogger(__name__)

# ── transport constants (do not drift from the tested firmware contract) ────
RATE = 16000
BLOCK = 512
CONNECT_TIMEOUT = 2.0
MAGIC = b"JIN1"
HEADER = struct.Struct("<4sI")

# ── settings: plugins.entries.jin-esp32-bridge.settings.* ────────────────────
DEFAULTS = {
    "host": "jin.local",
    "host_ip": "",
    "port": 3333,
    "esp32_output": True,
    "suppress_local": True,
    "mac_fallback": True,
    "jin_spelling": "Jinn",
}
_BOOL_KEYS = {"esp32_output", "suppress_local", "mac_fallback"}

# host -> last address that actually accepted a connection. mDNS (jin.local) is flaky on macOS:
# a lookup can fail with "nodename nor servname provided" while the device is up and reachable
# milliseconds later. Caching the address we last reached makes the hot path both faster and far
# more reliable, and it self-heals: if the cached address stops working we fall back to the name.
_LAST_GOOD: dict = {}

# A capitalised, word-bounded "Jin" only. Leaves "jin.local", "JIN1",
# "jin-esp32-bridge" and any lowercase token untouched.
_NAME_RE = re.compile(r"\bJin\b")

_ctx = None
_MISSING = object()                               # sentinel: the registry key did not exist
_override: dict = {}                              # runtime wins over the config file
_stop = threading.Event()
_patched: list[tuple[object, str, object, object]] = []   # (target, attr, original, ours)
_FILE_TAP_OK = False          # tools.voice_mode / hermes_cli.voice playback patched
_DESKTOP_TAP_OK = False       # starlette WebSocket PCM egress tapped


# ── settings access ─────────────────────────────────────────────────────────
def _coerce(key: str, value):
    default = DEFAULTS[key]
    if key in _BOOL_KEYS:
        if isinstance(value, bool):
            return value
        if isinstance(value, str):
            return value.strip().lower() in {"1", "true", "yes", "on"}
        if isinstance(value, (int, float)):
            return bool(value)
        return default
    if key == "port":
        try:
            return int(value)
        except (TypeError, ValueError):
            return default
    return default if value is None else str(value)


def _setting(key: str):
    if key in _override:
        return _override[key]
    if _ctx is None:
        return DEFAULTS[key]
    try:
        raw = _ctx.get_config(key, DEFAULTS[key])
    except Exception as exc:  # config read must never break speech
        logger.debug("jin-esp32-bridge: get_config(%s) failed: %s", key, exc)
        return DEFAULTS[key]
    return DEFAULTS[key] if raw is None else _coerce(key, raw)


# ── transport ───────────────────────────────────────────────────────────────
def _blocks(path: str, block: int = BLOCK):
    """Decode to 16 kHz int16 LE mono and emit exact `block`-sized blocks, tail padded."""
    exe = shutil.which("ffmpeg")
    if not exe:  # pragma: no cover - environment guard
        logger.warning("jin-esp32-bridge: ffmpeg not on PATH; nothing sent")
        return
    proc = subprocess.Popen(
        [exe, "-v", "error", "-i", str(path),
         "-f", "s16le", "-acodec", "pcm_s16le", "-ac", "1", "-ar", str(RATE), "-"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    assert proc.stdout is not None
    buf = bytearray()
    try:
        while True:
            chunk = proc.stdout.read(65536)
            if not chunk:
                break
            buf += chunk
            while len(buf) >= block:
                yield bytes(buf[:block])
                del buf[:block]
        if buf:
            buf += b"\x00" * (block - len(buf))
            yield bytes(buf)
    finally:
        with contextlib.suppress(Exception):
            proc.stdout.close()
        with contextlib.suppress(Exception):
            proc.wait(timeout=10)


def _connect_device(host: str, port: int):
    """Connect to the device, surviving mDNS flakiness. Returns (socket_or_None, errors).

    Order: an explicitly pinned ``host_ip`` wins; otherwise try the last address that worked before
    falling back to the name. A successful connect (re)learns the address, so a DHCP change is
    picked up on the next attempt instead of pinning a stale one.
    """
    errors = []
    candidates = []
    pinned = (_setting("host_ip") or "").strip()
    if pinned:
        candidates.append(pinned)
    cached = _LAST_GOOD.get(host)
    if cached and cached not in candidates:
        candidates.append(cached)
    if host not in candidates:
        candidates.append(host)
    for target in candidates:
        try:
            sock = socket.create_connection((target, port), timeout=CONNECT_TIMEOUT)
        except OSError as exc:
            errors.append(f"{target}: {exc}")
            continue
        with contextlib.suppress(Exception):
            _LAST_GOOD[host] = sock.getpeername()[0]
        return sock, errors
    return None, errors


def _probe(host: str, port: int) -> bool:
    """Is anything listening? Cheap, read-only, sends nothing."""
    sock, _errors = _connect_device(host, port)
    if sock is None:
        return False
    with contextlib.suppress(Exception):
        sock.close()
    return True


def _send_file(path: str) -> str:
    """One connection, one header, one file, closed at end of file.

    Returns ``"sent"`` (whole file delivered), ``"aborted"`` (stopped or failed mid-file; some
    audio already reached the device) or ``"unavailable"`` (nothing was sent — safe to fall back).
    """
    if not shutil.which("ffmpeg"):
        return "unavailable"
    host, port = _setting("host"), int(_setting("port"))
    sock, errors = _connect_device(host, port)
    if sock is None:
        logger.warning("jin-esp32-bridge: %s:%d unreachable (%s)", host, port, "; ".join(errors))
        return "unavailable"

    period = BLOCK / (RATE * 2)
    gen = _blocks(path)
    try:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.sendall(HEADER.pack(MAGIC, RATE))
        # The device's sample timeline starts with the first audio block, so pacing starts here.
        t0 = time.monotonic()
        for i, blk in enumerate(gen, start=1):
            if _stop.is_set():
                logger.info("jin-esp32-bridge: stopped at block boundary %d", i)
                return "aborted"
            sock.sendall(blk)
            behind = (t0 + i * period) - time.monotonic()
            if behind > 0:
                time.sleep(behind)
        return "sent"
    except OSError as exc:
        logger.warning("jin-esp32-bridge: send failed mid-file (%s)", exc)
        return "aborted"
    finally:
        with contextlib.suppress(Exception):
            gen.close()
        with contextlib.suppress(Exception):
            sock.close()          # closure == end-of-utterance for the firmware


# ── playback wrappers ───────────────────────────────────────────────────────
def _make_play(local_play):
    """Build the ``play_audio_file`` replacement, bound to *local_play* (the real Mac sink)."""

    def play_audio_file(file_path, *args, **kwargs):
        if not _setting("esp32_output"):
            return local_play(file_path, *args, **kwargs)

        if not _setting("suppress_local"):
            # Debugging mode: clear first, so this send cannot inherit an earlier barge-in signal.
            _stop.clear()
            threading.Thread(target=_send_file, args=(str(file_path),), daemon=True).start()
            return local_play(file_path, *args, **kwargs)

        import tools.voice_mode as voice_mode

        _stop.clear()
        voice_mode.mark_audio_output_active(True)
        try:
            result = _send_file(str(file_path))
            if result == "unavailable":
                if _setting("mac_fallback"):
                    logger.warning("jin-esp32-bridge: device unavailable, playing on the Mac")
                    return local_play(file_path, *args, **kwargs)
                logger.warning("jin-esp32-bridge: device unavailable, mac_fallback off — nothing played")
                return False
            # "sent", or "aborted" (partial): never replay locally, that would double-speak.
            return True
        finally:
            voice_mode.mark_audio_output_active(False)

    return play_audio_file


def _make_stop(original_stop):
    def stop_playback(*args, **kwargs):
        # Cuts an in-flight send at the next 16 ms block boundary; still terminates afplay.
        _stop.set()
        return original_stop(*args, **kwargs)

    return stop_playback


def _apply_jin_spelling(text):
    """Respell the name on the TTS-bound string only. The visible text is never touched."""
    spelling = _setting("jin_spelling")
    if not spelling or not isinstance(text, str):
        return text
    return _NAME_RE.sub(spelling, text)


def _make_edge(original_generate):
    async def _generate_edge_tts(text, output_path, tts_config, *args, **kwargs):
        return await original_generate(_apply_jin_spelling(text), output_path, tts_config, *args, **kwargs)

    return _generate_edge_tts


# ── desktop read-replies: /api/audio/speak-stream ───────────────────────────
# The desktop speaks via the speak-stream WebSocket, which streams raw int16 PCM frames to the
# Electron renderer (Web Audio). That path never calls play_audio_file, so it needs its own tap.
#
# Two facts shape the design:
#
# 1. With ``tts.provider: edge`` the WebSocket is never actually used. ``_REGISTRY`` in
#    tools/tts_streaming.py holds elevenlabs/openai/gemini/xai only; ``edge`` has no chunked API,
#    so ``resolve_streaming_provider`` returns None, the endpoint sends ``{"type":"fallback"}``
#    and the client POSTs /api/audio/speak for base64 audio instead. Tapping the socket alone
#    would therefore be a no-op. So we ALSO register an edge streamer -- the extension point the
#    module documents ("Adding a streamer is @register("name") on a subclass") -- while jin is on.
#    /jin off removes it, restoring today's exact fallback path.
#
# 2. The tap sits on the PCM egress only, so every upstream behaviour is preserved untouched:
#    SentenceChunker boundaries, the idle-flush cadence, the stop/barge-in latch, provider
#    resolution, the WebSocketDisconnect/RuntimeError handling and the final close. We do not
#    re-implement any of it.
#
# Suppression sends *silence of equal length* rather than dropping frames. The client settles
# 'fallback' when a stream produced no audio, and the caller would then re-speak the same text
# through /api/audio/speak -- two voices, the exact thing we are preventing. Equal-length silence
# keeps the client's ``started`` state and its speaking indicator honest while staying inaudible.

_WS_FWD: "weakref.WeakKeyDictionary" = weakref.WeakKeyDictionary()
_WS_LOCK = threading.Lock()
_EDGE_STREAMER_BACKUP: list = []          # [(name, previous class or _MISSING)]


class _Resampler:
    """Stateful linear resampler, arbitrary rate -> RATE, continuous across frames.

    Carries the fractional phase and the previous input sample between calls, so feeding a
    signal in many small frames yields the same output as feeding it in one piece. Resampling
    each frame independently would restart the phase every frame: discontinuities plus drift.
    """

    def __init__(self, src_rate: int):
        self.src_rate = int(src_rate) or RATE
        self.step = self.src_rate / float(RATE)
        self.phase = 0.0
        self.prev = None

    def process(self, pcm: bytes) -> bytes:
        n_in = len(pcm) // 2
        if n_in <= 0:
            return b""
        src = array.array("h")
        src.frombytes(pcm[: n_in * 2])
        if self.prev is None:
            self.prev = float(src[0])

        def sample_at(i: int) -> float:
            if i < 0:
                return self.prev              # the last sample of the previous frame
            return float(src[i])

        out = array.array("h")
        pos = self.phase
        # Interpolate while both neighbours exist inside this frame (index n_in-1 is the last).
        while True:
            i = int(math.floor(pos))
            if i < -1 or i + 1 > n_in - 1:
                break
            frac = pos - i
            value = sample_at(i) * (1.0 - frac) + sample_at(i + 1) * frac
            if value > 32767.0:
                value = 32767.0
            elif value < -32768.0:
                value = -32768.0
            out.append(int(value))
            pos += self.step

        self.phase = pos - n_in              # rebase onto the next frame's timeline
        self.prev = float(src[n_in - 1])
        return out.tobytes()


class _Forwarder:
    """One utterance: PCM in, exact 512-byte blocks out, paced to realtime, one socket."""

    def __init__(self, host: str, port: int, src_rate: int):
        self.host, self.port = host, int(port)
        self.resampler = _Resampler(src_rate)
        self.q: "queue.Queue" = queue.Queue()
        self.stop = threading.Event()
        self.tail = bytearray()
        self.socket = None
        self.worker: Optional[threading.Thread] = None
        self.unavailable = False             # nothing was sent -> local fallback is still safe
        self.dead = False                    # failed mid-utterance -> never replay locally
        self.ended = False                   # endpoint sent {"type":"end"} -> normal completion
        self.blocks = 0
        self.payload = 0

    def connect(self) -> bool:
        """Connect and send the header. Called off the event loop, before any frame is withheld."""
        sock, errors = _connect_device(self.host, self.port)
        if sock is None:
            logger.warning("jin-esp32-bridge: desktop stream cannot reach %s:%s (%s)",
                           self.host, self.port, "; ".join(errors))
            self.unavailable = True
            return False
        try:
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            sock.sendall(HEADER.pack(MAGIC, RATE))
        except OSError as exc:
            logger.warning("jin-esp32-bridge: desktop stream header failed (%s)", exc)
            with contextlib.suppress(Exception):
                sock.close()
            self.unavailable = True
            return False
        self.socket = sock
        self.worker = threading.Thread(target=self._pace, name="jin-esp32-desktop", daemon=True)
        self.worker.start()
        return True

    def feed(self, pcm: bytes) -> None:
        if self.dead or self.stop.is_set():
            return
        buf = self.resampler.process(pcm)
        if buf:
            self.tail += buf
            while len(self.tail) >= BLOCK:
                block = bytes(self.tail[:BLOCK])
                del self.tail[:BLOCK]
                self.q.put(block)

    def _flush_tail(self) -> None:
        if self.tail:                        # pad ONLY the final partial block
            self.tail += b"\x00" * (BLOCK - len(self.tail))
            self.q.put(bytes(self.tail))
            self.tail = bytearray()

    def finish(self) -> None:
        """Whole utterance delivered: pad, drain, close so the mouth returns to rest."""
        if self.worker is None:
            return
        self._flush_tail()
        self.q.put(None)

    def abort(self) -> None:
        """Barge-in / disconnect: cut at the next block boundary and close immediately."""
        self.stop.set()
        self.q.put(None)

    def _pace(self) -> None:
        period = BLOCK / (RATE * 2)
        t0 = time.monotonic()
        n = 0
        try:
            while True:
                block = self.q.get()
                if block is None or self.stop.is_set():
                    break
                self.socket.sendall(block)
                n += 1
                self.blocks += 1
                self.payload += len(block)
                behind = (t0 + n * period) - time.monotonic()
                if behind > 0:
                    time.sleep(behind)
        except OSError as exc:
            # Failed after audio began: drop the rest and never replay it locally.
            self.dead = True
            logger.warning("jin-esp32-bridge: desktop stream failed mid-utterance (%s)", exc)
        finally:
            with contextlib.suppress(Exception):
                self.socket.close()


def _make_send_json(original):
    async def send_json(self, data, *args, **kwargs):
        try:
            if (isinstance(data, dict) and data.get("type") == "start"
                    and isinstance(data.get("sample_rate"), int) and data["sample_rate"] > 0
                    and _setting("esp32_output")):
                fwd = _Forwarder(_setting("host"), _setting("port"), data["sample_rate"])
                # Decide reachability BEFORE withholding anything: an unreachable device must
                # leave the normal Mac path intact.
                if await asyncio.to_thread(fwd.connect):
                    with _WS_LOCK:
                        _WS_FWD[self] = fwd
                    logger.info("jin-esp32-bridge: desktop stream -> %s:%d @ %d Hz (source %d Hz)",
                                _setting("host"), _setting("port"), RATE, data["sample_rate"])
            elif isinstance(data, dict) and data.get("type") == "end":
                # The endpoint sends {"type":"end"} only on normal completion. Its ABSENCE at
                # close time is how barge-in is distinguishable, and the difference matters:
                # normal => drain the queued remainder; barge-in => cut immediately.
                fwd = _WS_FWD.get(self)
                if fwd is not None:
                    fwd.ended = True
        except Exception as exc:
            logger.warning("jin-esp32-bridge: desktop stream setup failed (%s)", exc)
        return await original(self, data, *args, **kwargs)

    return send_json


def _make_send_bytes(original):
    async def send_bytes(self, data):
        fwd = _WS_FWD.get(self)
        if fwd is not None and not fwd.unavailable:
            if not fwd.dead:
                try:
                    fwd.feed(data)
                except Exception as exc:
                    fwd.dead = True
                    logger.warning("jin-esp32-bridge: desktop stream feed failed (%s)", exc)
            # Equal-length silence: inaudible, and it keeps the client's stream state honest so
            # it never falls back and re-speaks the same text.
            return await original(self, b"\x00" * len(data))
        return await original(self, data)

    return send_bytes


def _make_ws_close(original):
    async def close(self, *args, **kwargs):
        with _WS_LOCK:
            fwd = _WS_FWD.pop(self, None)
        if fwd is not None:
            # No {"type":"end"} on barge-in, so close is the universal finalizer and the presence
            # of `end` is what separates "finished speaking" from "the user cut me off".
            try:
                if fwd.dead or not fwd.ended:
                    fwd.abort()          # barge-in or client disconnect: cut now, close the socket
                else:
                    fwd.finish()         # completed utterance: drain the tail, then close
            except Exception as exc:
                logger.debug("jin-esp32-bridge: desktop stream finalize failed (%s)", exc)
        return await original(self, *args, **kwargs)

    return close


class JinEdgeStreamer:
    """Edge TTS as a real streaming provider: sentence -> int16 mono PCM at ``sample_rate``.

    Hermes ships no edge streamer because edge has no chunked PCM API; it returns MP3 chunks.
    We decode those incrementally through ffmpeg, which is also the resampler, so the PCM handed
    up is continuous. Registered only while /jin on is active, so /jin off restores the stock
    fallback path exactly.
    """

    sample_rate: int = 24000
    channels: int = 1
    sample_width: int = 2

    def __init__(self, tts_config: Dict[str, Any], section: Dict[str, Any]):
        self.tts_config = tts_config or {}
        self.section = section or {}

    @staticmethod
    def available() -> bool:
        try:
            import edge_tts  # noqa: F401
        except Exception:
            return False
        return bool(shutil.which("ffmpeg"))

    def _kwargs(self) -> Dict[str, Any]:
        try:
            from tools.tts_tool_providers import DEFAULT_EDGE_VOICE
            default_voice = DEFAULT_EDGE_VOICE
        except Exception:  # pragma: no cover - provider module renamed
            default_voice = "en-US-GuyNeural"
        voice = self.section.get("voice") or self.tts_config.get("voice") or default_voice
        kwargs: Dict[str, Any] = {"voice": voice}
        try:
            speed = float(self.section.get("speed", self.tts_config.get("speed", 1.0)))
        except (TypeError, ValueError):
            speed = 1.0
        if speed != 1.0:
            kwargs["rate"] = f"{round((speed - 1.0) * 100):+d}%"
        return kwargs

    def stream(self, text: str):
        import edge_tts

        speakable = _apply_jin_spelling(text)
        proc = subprocess.Popen(
            [shutil.which("ffmpeg"), "-v", "error", "-f", "mp3", "-i", "pipe:0",
             "-f", "s16le", "-acodec", "pcm_s16le", "-ac", "1",
             "-ar", str(self.sample_rate), "-"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        )

        def _feed():
            # A separate thread so ffmpeg's stdout is drained concurrently: writing the whole
            # synthesis into a pipe we are not reading yet would deadlock on the buffer.
            loop = asyncio.new_event_loop()
            try:
                async def _pump():
                    async for chunk in edge_tts.Communicate(speakable, **self._kwargs()).stream():
                        if chunk.get("type") == "audio" and chunk.get("data"):
                            proc.stdin.write(chunk["data"])
                loop.run_until_complete(_pump())
            except Exception as exc:
                logger.warning("jin-esp32-bridge: edge stream feed failed (%s)", exc)
            finally:
                with contextlib.suppress(Exception):
                    proc.stdin.close()
                loop.close()

        feeder = threading.Thread(target=_feed, name="jin-edge-feed", daemon=True)
        feeder.start()
        try:
            while True:
                data = proc.stdout.read(8192)
                if not data:
                    break
                yield data
        finally:
            with contextlib.suppress(Exception):
                feeder.join(timeout=5)
            with contextlib.suppress(Exception):
                proc.stdout.close()
            if proc.poll() is None:
                with contextlib.suppress(Exception):
                    proc.kill()


def _set_edge_streamer(active: bool) -> str:
    """Register/unregister the edge streamer in tools.tts_streaming._REGISTRY."""
    try:
        from tools.tts_streaming import _REGISTRY
    except Exception as exc:  # pragma: no cover - module renamed
        return f"unavailable ({exc})"
    if active:
        if _EDGE_STREAMER_BACKUP:
            return "already registered"
        _EDGE_STREAMER_BACKUP.append(("edge", _REGISTRY.get("edge", _MISSING)))
        _REGISTRY["edge"] = JinEdgeStreamer
        return "registered"
    if _EDGE_STREAMER_BACKUP:
        name, previous = _EDGE_STREAMER_BACKUP.pop()
        if previous is _MISSING:
            _REGISTRY.pop(name, None)
        else:
            _REGISTRY[name] = previous
        return "removed"
    return "not registered"


# ── /jin command ────────────────────────────────────────────────────────────
def _status() -> str:
    host, port = _setting("host"), int(_setting("port"))
    enabled = bool(_setting("esp32_output"))
    reachable = _probe(host, port) if enabled else None
    where = "" if reachable is None else ("reachable" if reachable else "NOT reachable")
    return "\n".join([
        f"jin-esp32-bridge: {'ON' if enabled else 'OFF'}",
        f"  device        {host}:{port}" + (f"  ({where})" if where else "  (not probed while off)"),
        f"  address       last reached {_LAST_GOOD.get(host) or '(none yet)'}"
        + (f"; pinned to {_setting('host_ip')}" if (_setting("host_ip") or "").strip() else ""),
        f"  transport     {RATE} Hz int16 mono, {BLOCK} B blocks (16 ms), "
        f"one connection + JIN1 header per sentence",
        f"  mac playback  {'suppressed' if _setting('suppress_local') else 'also plays'}",
        f"  fallback      {'on' if _setting('mac_fallback') else 'off'} "
        f"(Mac plays only if nothing was sent)",
        f"  'Jin' spoken  as {_setting('jin_spelling')!r}",
        # Both integration paths are reported separately: they fail independently.
        f"  paths         file/CLI playback {'patched' if _FILE_TAP_OK else 'NOT patched'}; "
        f"desktop read-replies {'patched' if _DESKTOP_TAP_OK else 'NOT patched'}",
        f"  edge streamer "
        + ("registered -> desktop streams to the device"
           if _EDGE_STREAMER_BACKUP else
           "not registered -> /jin off uses the stock fallback (POST /api/audio/speak)"),
    ])


def _cmd_jin(raw_args: str = "") -> str:
    args = (raw_args or "").strip().split()
    action = (args[0].lower() if args else "status")

    if action in ("on", "off"):
        want = action == "on"
        _override["esp32_output"] = want      # immediate effect, even if the write fails
        note = ""
        try:
            _ctx.set_config("esp32_output", want)
        except Exception as exc:
            note = f"\n  (could not persist to config.yaml: {exc})"
        # The edge streamer is the switch that decides WHICH desktop path is used: registered
        # means the desktop streams over the WebSocket (and we tap it); removed means the stock
        # fallback to POST /api/audio/speak, i.e. exactly the pre-plugin behaviour.
        streamer = _set_edge_streamer(want)
        return f"Jin ESP32 output {'ON' if want else 'OFF'} (edge streamer {streamer})." + note + "\n" + _status()

    if action in ("status", "state"):
        return _status()

    return "Usage: /jin on | /jin off | /jin status"


# ── patch bookkeeping ───────────────────────────────────────────────────────
def _install(target, attr: str, build):
    original = getattr(target, attr, None)
    if original is None:
        logger.warning("jin-esp32-bridge: %s has no %s; skipped",
                       getattr(target, "__name__", target), attr)
        return None
    ours = build(original)
    setattr(target, attr, ours)
    _patched.append((target, attr, original, ours))
    logger.debug("jin-esp32-bridge: patched %s.%s", getattr(target, "__name__", target), attr)
    return ours


def _cleanup() -> None:
    """Unload: stop any active send, then restore exactly what we replaced."""
    global _FILE_TAP_OK, _DESKTOP_TAP_OK
    _stop.set()
    # Drop the edge streamer first: the desktop immediately returns to the stock POST fallback.
    _set_edge_streamer(False)
    with _WS_LOCK:
        for fwd in list(_WS_FWD.values()):
            with contextlib.suppress(Exception):
                fwd.abort()
        _WS_FWD.clear()
    restored, skipped = [], []
    for target, attr, original, ours in reversed(_patched):
        name = f"{getattr(target, '__name__', '?')}.{attr}"
        if getattr(target, attr, None) is ours:
            setattr(target, attr, original)
            restored.append(name)
        else:                                    # replaced by something else; leave it alone
            skipped.append(name)
    _patched.clear()
    _override.clear()
    _FILE_TAP_OK = False
    _DESKTOP_TAP_OK = False
    logger.info("jin-esp32-bridge: unloaded; restored %s%s",
                ", ".join(restored) or "nothing",
                f"; left alone (changed elsewhere): {', '.join(skipped)}" if skipped else "")


def register(ctx) -> None:
    """Called by the Hermes plugin loader."""
    global _ctx, _FILE_TAP_OK, _DESKTOP_TAP_OK
    _ctx = ctx

    if _patched:                                  # a reload must not stack patches
        logger.info("jin-esp32-bridge: previous registration still active; cleaning up first")
        _cleanup()

    try:
        import tools.voice_mode as voice_mode
    except Exception as exc:  # pragma: no cover - environment guard
        logger.warning("jin-esp32-bridge: tools.voice_mode unavailable (%s); not installed", exc)
        return

    local_play = voice_mode.play_audio_file      # the real Mac sink, pre-patch
    # One wrapper object, installed at both bindings, because pre-patch those two names point at
    # the same function and keeping that invariant avoids two divergent replacement layers.
    play_wrapper = _make_play(local_play)
    targets = []

    if _install(voice_mode, "play_audio_file", lambda _o: play_wrapper):
        targets.append("tools.voice_mode.play_audio_file")
    if _install(voice_mode, "stop_playback", _make_stop):
        targets.append("tools.voice_mode.stop_playback")

    # hermes_cli/voice.py binds play_audio_file at MODULE level, so its speak_text path needs
    # its own swap; the lazy in-function imports elsewhere pick up the attribute above.
    try:
        import hermes_cli.voice as cli_voice
        if getattr(cli_voice, "play_audio_file", None) is local_play:
            if _install(cli_voice, "play_audio_file", lambda _o: play_wrapper):
                targets.append("hermes_cli.voice.play_audio_file")
        else:
            logger.debug("jin-esp32-bridge: hermes_cli.voice already bound elsewhere; skipped")
    except Exception as exc:
        logger.warning("jin-esp32-bridge: hermes_cli.voice not patched (%s)", exc)

    # tools/tts_tool.py binds _generate_edge_tts at import time (line 56) and calls that binding,
    # so the defining module's attribute is NOT where production reads from.
    try:
        import tools.tts_tool as tts_tool
        if _install(tts_tool, "_generate_edge_tts", _make_edge):
            targets.append("tools.tts_tool._generate_edge_tts")
    except Exception as exc:
        logger.warning("jin-esp32-bridge: edge pronunciation patch skipped (%s)", exc)

    _FILE_TAP_OK = any(t.endswith("play_audio_file") for t in targets)

    # Desktop read-replies. The speak-stream WebSocket hands PCM to the renderer over
    # ws.send_bytes and never touches play_audio_file, so it needs its own tap. We tap
    # starlette's WebSocket class rather than the endpoint function on purpose: FastAPI
    # copies the endpoint into the route at include time, so patching the module function
    # or the route object depends on load order, while the class method is what actually
    # executes no matter when the app was built. The tap is inert until a socket announces
    # the speak-stream protocol with its {"type":"start","sample_rate":N} handshake, which
    # nothing else in Hermes sends.
    try:
        from starlette.websockets import WebSocket as _StarletteWS

        if _install(_StarletteWS, "send_json", _make_send_json):
            targets.append("starlette.WebSocket.send_json")
        if _install(_StarletteWS, "send_bytes", _make_send_bytes):
            targets.append("starlette.WebSocket.send_bytes")
        if _install(_StarletteWS, "close", _make_ws_close):
            targets.append("starlette.WebSocket.close")
        _DESKTOP_TAP_OK = True
    except Exception as exc:
        logger.warning("jin-esp32-bridge: desktop speak-stream tap skipped (%s)", exc)

    # Only while jin is on: this is what makes the desktop use the streaming WebSocket instead of
    # falling back to POST /api/audio/speak, so /jin off keeps the stock path byte-for-byte.
    if _setting("esp32_output"):
        _set_edge_streamer(True)

    ctx.on_unload(_cleanup)

    try:
        ctx.register_command(
            "jin", _cmd_jin,
            description="Jin ESP32 audio output: on, off or status",
            args_hint="<on|off|status>", argument_mode="options",
        )
    except Exception as exc:
        logger.warning("jin-esp32-bridge: could not register /jin (%s)", exc)

    logger.info(
        "jin-esp32-bridge: installed; TTS -> %s:%d @ %d Hz, %d B blocks (16 ms), one connection "
        "per sentence; local playback %s; mac fallback %s; 'Jin' spoken as %r; patched %s",
        _setting("host"), _setting("port"), RATE, BLOCK,
        "suppressed" if _setting("suppress_local") else "kept",
        "on" if _setting("mac_fallback") else "off",
        _setting("jin_spelling"), ", ".join(targets) or "nothing",
    )
