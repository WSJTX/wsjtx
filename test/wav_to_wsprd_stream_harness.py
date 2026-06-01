#!/usr/bin/env python3
"""
wav-to-wsprd-stream validation harness.

Pipes a 12 kHz mono int16 WAV file through `wsprd --stream` using the
streaming protocol, then prints the resulting NDJSON
decodes. Used to verify that streaming-mode decode results match the
file-mode ground truth.

Usage:
    test/wav_to_wsprd_stream_harness.py <wsprd-binary> <wav-file>
        [--dialfreq MHZ] [--wspr-type 2|15]
        [--mycall CALL] [--mygrid GRID]

Filename convention: YYMMDD_HHMM.wav (matches existing wsprd file-mode
behavior). Date/time get extracted from the filename and sent in the
configure frame.

NDJSON lines are written to stdout. stderr is merged into stdout
(consumers MUST drain stderr).
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import subprocess
import sys
import wave
from pathlib import Path

FRAME_AUDIO = 0x01
FRAME_CONTROL = 0x02

# Audio chunk size in 12 kHz samples per frame. ~1 s per frame.
CHUNK_SAMPLES = 12000


def wsjl_session_header(rate_khz: int = 12) -> bytes:
    return b"WSJL" + bytes([0, 1]) + struct.pack("<H", rate_khz)


def frame(type_byte: int, body: bytes) -> bytes:
    return bytes([type_byte]) + struct.pack("<I", len(body)) + body


def configure_json(
    date: str, uttime: str, dialfreq: float, wspr_type: int,
    mycall: str = "KJ5HST", mygrid: str = "EM18",
) -> bytes:
    payload = {
        "t": "configure",
        "date": date,
        "time": uttime,
        "dialfreq": dialfreq,
        "wspr_type": wspr_type,
        "mycall": mycall,
        "mygrid": mygrid,
    }
    return json.dumps(payload, separators=(",", ":")).encode("utf-8")


def halt_json() -> bytes:
    return b'{"t":"halt"}'


def parse_filename_timestamp(wav_path: Path) -> tuple[str, str]:
    """Extract YYMMDD and HHMM from a YYMMDD_HHMM.wav filename."""
    m = re.match(r"^(\d{6})_(\d{4})\.wav$", wav_path.name)
    if not m:
        raise ValueError(f"WAV filename must match YYMMDD_HHMM.wav: {wav_path.name}")
    return m.group(1), m.group(2)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("wsprd", type=Path)
    p.add_argument("wav", type=Path)
    p.add_argument("--dialfreq", type=float, default=14.0956)
    p.add_argument("--wspr-type", type=int, default=2, choices=[2, 15])
    p.add_argument("--mycall", default="KJ5HST")
    p.add_argument("--mygrid", default="EM18")
    p.add_argument(
        "--quiet", action="store_true",
        help="Suppress non-JSON stderr lines (e.g., header-ok).",
    )
    args = p.parse_args()

    if not args.wsprd.is_file():
        print(f"ERROR: wsprd binary not found: {args.wsprd}", file=sys.stderr)
        return 2
    if not args.wav.is_file():
        print(f"ERROR: WAV file not found: {args.wav}", file=sys.stderr)
        return 2

    date, uttime = parse_filename_timestamp(args.wav)

    with wave.open(str(args.wav)) as w:
        if w.getnchannels() != 1:
            print(f"ERROR: WAV must be mono (got {w.getnchannels()} channels)",
                  file=sys.stderr)
            return 2
        if w.getsampwidth() != 2:
            print(f"ERROR: WAV must be 16-bit (got {w.getsampwidth()} byte samples)",
                  file=sys.stderr)
            return 2
        if w.getframerate() != 12000:
            print(f"ERROR: WAV must be 12 kHz (got {w.getframerate()} Hz)",
                  file=sys.stderr)
            return 2
        audio = w.readframes(w.getnframes())

    proc = subprocess.Popen(
        [str(args.wsprd), "-0"],   # -0 == streaming mode (wsprd uses getopt single-char only)
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )

    try:
        proc.stdin.write(wsjl_session_header())
        proc.stdin.write(frame(
            FRAME_CONTROL,
            configure_json(date, uttime, args.dialfreq, args.wspr_type,
                           args.mycall, args.mygrid),
        ))
        bytes_per_chunk = CHUNK_SAMPLES * 2
        for off in range(0, len(audio), bytes_per_chunk):
            proc.stdin.write(frame(FRAME_AUDIO, audio[off : off + bytes_per_chunk]))
        proc.stdin.write(frame(FRAME_CONTROL, halt_json()))
        proc.stdin.flush()
        proc.stdin.close()
    except BrokenPipeError:
        pass

    out_bytes = proc.stdout.read()
    proc.wait(timeout=120)
    out = out_bytes.decode("utf-8", errors="replace")
    for line in out.splitlines():
        if args.quiet and not line.startswith("{"):
            continue
        print(line)
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
