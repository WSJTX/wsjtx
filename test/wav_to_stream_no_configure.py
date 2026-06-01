#!/usr/bin/env python3
"""
wav-to-stream validation harness — no-configure-frame variant.

Pipes a 12 kHz mono int16 WAV file through `jt9 --stream` using the
streaming protocol, but does NOT send a configure frame.
Decode parameters come from jt9's CLI args (-F/-f/-L/-H/-d) instead.

This exercises the CLI-arg threading path through init_default_params.
Earlier, streaming_io.f90 silently overrode the CLI
args with FT8-baseline hardcoded defaults — so a no-configure invocation
produced 0 decodes regardless of what the user passed on the CLI.

Usage:
    test/wav_to_stream_no_configure.py <jt9-binary> <wav-file>
        [--mode-flag MODE_FLAG] [--ntol NTOL] [--nrxfreq HZ]
        [--flow HZ] [--fhigh HZ] [--ndepth N]

NDJSON lines and stderr are printed to stdout (consumers MUST drain
stderr). Exit code is jt9's exit code.
"""
from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import wave
from pathlib import Path

FRAME_AUDIO = 0x01
FRAME_CONTROL = 0x02
CHUNK_SAMPLES = 12000


def wsjl_session_header(rate_khz: int = 12) -> bytes:
    return b"WSJL" + bytes([0, 1]) + struct.pack("<H", rate_khz)


def frame(type_byte: int, body: bytes) -> bytes:
    return bytes([type_byte]) + struct.pack("<I", len(body)) + body


def halt_json() -> bytes:
    return b'{"t":"halt"}'


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("jt9", type=Path)
    p.add_argument("wav", type=Path)
    p.add_argument("--mode-flag", default="-8",
                   help="jt9 mode flag (-8 FT8, -9 JT9, etc.)")
    p.add_argument("--ntol", type=int, default=1000)
    p.add_argument("--nrxfreq", type=int, default=1500)
    p.add_argument("--flow", type=int, default=200)
    p.add_argument("--fhigh", type=int, default=3000)
    p.add_argument("--ndepth", type=int, default=1)
    p.add_argument("--quiet", action="store_true",
                   help="Suppress non-JSON stderr lines.")
    args = p.parse_args()

    if not args.jt9.is_file():
        print(f"ERROR: jt9 binary not found: {args.jt9}", file=sys.stderr)
        return 2
    if not args.wav.is_file():
        print(f"ERROR: WAV file not found: {args.wav}", file=sys.stderr)
        return 2

    with wave.open(str(args.wav)) as w:
        if w.getnchannels() != 1 or w.getsampwidth() != 2 or w.getframerate() != 12000:
            print("ERROR: WAV must be 12 kHz mono 16-bit", file=sys.stderr)
            return 2
        audio = w.readframes(w.getnframes())

    cmd = [
        str(args.jt9), "--stream", args.mode_flag,
        "-F", str(args.ntol), "-f", str(args.nrxfreq),
        "-L", str(args.flow), "-H", str(args.fhigh),
        "-d", str(args.ndepth),
    ]

    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )

    try:
        proc.stdin.write(wsjl_session_header())
        # NOTE: NO configure frame here — that is the whole point of this
        # harness. CLI args from -F/-f/-L/-H/-d above must be honored by
        # init_default_params with no help from a runtime configure.
        bytes_per_chunk = CHUNK_SAMPLES * 2
        for off in range(0, len(audio), bytes_per_chunk):
            proc.stdin.write(frame(FRAME_AUDIO, audio[off : off + bytes_per_chunk]))
        proc.stdin.write(frame(FRAME_CONTROL, halt_json()))
        proc.stdin.flush()
        proc.stdin.close()
    except BrokenPipeError:
        pass

    out_bytes = proc.stdout.read()
    proc.wait(timeout=60)
    out = out_bytes.decode("utf-8", errors="replace")
    for line in out.splitlines():
        if args.quiet and not line.startswith("{"):
            continue
        print(line)
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
