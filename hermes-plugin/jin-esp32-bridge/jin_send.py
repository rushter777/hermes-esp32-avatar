#!/usr/bin/env python3
"""jin_send.py — stream audio to the Jin ESP32 avatar endpoint.

Wire contract (matches the firmware build physically tested on the device):

    b"JIN1"                       4 bytes    magic
    <uint32 little-endian>        4 bytes    sample rate in Hz (offset 4)
    int16 little-endian mono PCM, sent as fixed 512-byte blocks

    512 bytes = 256 mono int16 samples = exactly 16 ms at 16000 Hz
    (62.5 blocks per second)

    ONE TCP connection and ONE JIN1 header PER SOURCE FILE, closed when the file ends.
    The firmware uses connection closure to return the mouth to its flat resting state and
    reset its sample timeline, so a connection is never left idle between files.

Usage
-----
    jin_send.py FILE [FILE ...]     one connection per file, in order
    jin_send.py --say "text"        synthesize with Hermes' own TTS voice, then send it
    jin_send.py --tone              send a 3-second 440 Hz test tone
    jin_send.py --probe             connect, header, 0.5 s of silence, close

Options
-------
    --host HOST      default jin.local
    --port PORT      default 3333
    --rate HZ        default 16000. MUST match the device's calibrated rate.
    --block N        default 512 bytes. Do not change without re-testing the firmware.
    --no-throttle    push blocks as fast as the socket accepts them (breaks mouth sync)
    --quiet          suppress per-file progress output

--say needs Hermes' virtualenv (edge_tts + the tools package live there):
    cd ~/.hermes/hermes-agent && venv/bin/python ~/Downloads/jin-esp32-bridge/jin_send.py --say "hello"

Exit codes: 0 ok, 1 usage/IO error, 2 the device could not be reached.
"""

from __future__ import annotations

import argparse
import contextlib
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

MAGIC = b"JIN1"
HEADER = struct.Struct("<4sI")
HERMES_SRC = Path.home() / ".hermes" / "hermes-agent"

BLOCK = 512                    # bytes per block: 256 int16 mono samples = 16 ms at 16 kHz
CONNECT_TIMEOUT = 5.0
BYTES_PER_SEC = lambda rate: rate * 2


def _ffmpeg() -> str:
    exe = shutil.which("ffmpeg")
    if not exe:
        sys.exit("ffmpeg not found on PATH (brew install ffmpeg).")
    return exe


def pcm_chunks(path: os.PathLike | str, rate: int, chunk: int = 65536):
    """Decode any ffmpeg-readable file to int16 LE mono PCM at `rate`, yielding raw chunks."""
    proc = subprocess.Popen(
        [_ffmpeg(), "-v", "error", "-i", str(path),
         "-f", "s16le", "-acodec", "pcm_s16le", "-ac", "1", "-ar", str(rate), "-"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    assert proc.stdout is not None
    try:
        while True:
            buf = proc.stdout.read(chunk)
            if not buf:
                break
            yield buf
    finally:
        with contextlib.suppress(Exception):
            proc.stdout.close()
        with contextlib.suppress(Exception):
            proc.wait(timeout=10)


def blocks(chunks, block: int = BLOCK):
    """Re-cut a byte stream into exact `block`-sized blocks; the final partial block is padded
    with silence so every block carries the same 16 ms of audio the firmware clocks against."""
    buf = bytearray()
    for c in chunks:
        buf += c
        while len(buf) >= block:
            yield bytes(buf[:block])
            del buf[:block]
    if buf:
        buf += b"\x00" * (block - len(buf))
        yield bytes(buf)


def silence(seconds: float, rate: int):
    yield from blocks(iter([b"\x00" * int(seconds * BYTES_PER_SEC(rate))]))


def test_tone(seconds: float, rate: int, freq: float = 440.0):
    """Sine tone without numpy."""
    import math
    total = int(seconds * rate)
    buf = bytearray()
    step = 2 * math.pi * freq / rate
    for i in range(total):
        buf += struct.pack("<h", int(12000 * math.sin(step * i)))
    if len(buf) % 2:
        buf += b"\x00"
    yield from blocks(iter([bytes(buf)]))


def send_source(host: str, port: int, rate: int, label: str, block_iter, *,
                throttle: bool, quiet: bool, block: int = BLOCK) -> int:
    """One connection, one header, one source, then close. Returns bytes sent.

    Block n is scheduled against the monotonic deadline t0 + n*period, so pacing cannot drift
    and cannot accumulate error across a long file.
    """
    period = block / BYTES_PER_SEC(rate)
    try:
        sock = socket.create_connection((host, port), timeout=CONNECT_TIMEOUT)
    except OSError as exc:
        print(f"jin_send: cannot reach {host}:{port} — {exc}", file=sys.stderr)
        sys.exit(2)

    sent = 0
    with sock:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.sendall(HEADER.pack(MAGIC, rate))
        # The device's sample timeline starts with the first audio block, so the pacing clock
        # starts here rather than before connect().
        t0 = time.monotonic()
        for i, blk in enumerate(block_iter, start=1):
            sock.sendall(blk)
            sent += len(blk)
            if throttle:
                behind = (t0 + i * period) - time.monotonic()
                if behind > 0:
                    time.sleep(behind)
    if not quiet:
        print(f"  {label}: {sent / BYTES_PER_SEC(rate):6.3f} s  ({sent // block} blocks of {block} B)")
    return sent


def synth_with_hermes(text: str, out_path: Path) -> Path:
    """Synthesize `text` with Hermes' configured TTS provider and voice (provider unchanged)."""
    if str(HERMES_SRC) not in sys.path:
        sys.path.insert(0, str(HERMES_SRC))
    try:
        from tools.tts_tool import text_to_speech_tool
    except Exception as exc:  # pragma: no cover
        sys.exit(
            f"jin_send: cannot import Hermes' TTS tool ({exc}).\n"
            f"  Run me with the Hermes venv python instead:\n"
            f"    cd {HERMES_SRC} && venv/bin/python {Path(__file__).resolve()} --say \"...\""
        )
    text_to_speech_tool(text=text, output_path=str(out_path))
    if not out_path.exists() or out_path.stat().st_size == 0:
        sys.exit("jin_send: TTS produced no audio.")
    return out_path


def main() -> int:
    ap = argparse.ArgumentParser(description="Stream audio to the Jin ESP32 avatar.")
    ap.add_argument("files", nargs="*", help="audio files to send, one connection each")
    ap.add_argument("--say", metavar="TEXT", help="synthesize TEXT with Hermes' TTS, then send it")
    ap.add_argument("--tone", action="store_true", help="send a 3 s 440 Hz test tone")
    ap.add_argument("--probe", action="store_true", help="connect, header, 0.5 s silence, close")
    ap.add_argument("--host", default="jin.local")
    ap.add_argument("--port", type=int, default=3333)
    ap.add_argument("--rate", type=int, default=16000)
    ap.add_argument("--block", type=int, default=BLOCK)
    ap.add_argument("--no-throttle", action="store_true", help="do not pace to realtime")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if not shutil.which("ffmpeg"):
        sys.exit("ffmpeg not found on PATH (brew install ffmpeg).")
    if args.block <= 0 or args.block % 2:
        ap.error("--block must be a positive even number of bytes")

    sources: list[tuple[str, object]] = []
    tmp: tempfile.TemporaryDirectory | None = None

    if args.probe:
        sources = [("silence 0.5 s", silence(0.5, args.rate))]
    elif args.tone:
        sources = [("440 Hz tone 3 s", test_tone(3.0, args.rate))]
    elif args.say:
        tmp = tempfile.TemporaryDirectory(prefix="jin-say-")
        out = Path(tmp.name) / "say.mp3"
        synth_with_hermes(args.say, out)
        sources = [(f'say "{args.say[:40]}"', blocks(pcm_chunks(out, args.rate), args.block))]
    elif args.files:
        for f in args.files:
            p = Path(f).expanduser()
            if not p.is_file():
                sys.exit(f"jin_send: no such file: {p}")
            sources.append((p.name, blocks(pcm_chunks(p, args.rate), args.block)))
    else:
        ap.error("nothing to send: pass FILE(s), --say TEXT, --tone, or --probe")

    try:
        total = 0
        for label, block_iter in sources:
            total += send_source(args.host, args.port, args.rate, label, block_iter,
                                 throttle=not args.no_throttle, quiet=args.quiet, block=args.block)
        if not args.quiet:
            print(f"jin_send: done, {total / BYTES_PER_SEC(args.rate):.2f} s total "
                  f"to {args.host}:{args.port} @ {args.rate} Hz, "
                  f"{len(sources)} connection(s)")
    finally:
        if tmp is not None:
            tmp.cleanup()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
