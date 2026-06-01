#!/usr/bin/env python3
"""
External-producer streaming CONTRACT harness.

Emits the EXACT byte stream a minimal external streaming producer puts on the
wire and asserts `jt9 --stream` accepts it and answers in the dialect such a
producer parses. This pins the JOINT wire contract end-to-end for the
MINIMAL versionless 6-key configure frame: the broader streaming fixtures
send a richer frame (ntol/nfa/version/...), NOT the minimal frame a simple
external producer actually emits — so this test proves the minimal producer
format is accepted.

What the minimal external-producer format means here:
  * 8-byte WSJL header: 'WSJL' + fmt=0(int16) + ch=1(mono) + rate=12kHz LE.
  * configure frame = the MINIMAL versionless 6-key frame, key order fixed:
      {"t":"configure","mode":"<M>","depth":<d>,"trperiod":<p>,
       "mycall":"<c>","mygrid":"<g>"}
      NO "version", NO "ntol"/"nfa"/"nfb"/"rxfreq"/"nutc"/"dialfreq".
  * audio frames: int16 LE PCM @ 12 kHz, chunked ~100 ms (well under a
      period).
  * halt frame: {"t":"halt"}.

Asserted contract (both-sides-healthy) — jt9 must:
  1. emit a `ready` line,
  2. NOT emit any `error` line (the minimal frame is accepted, not declined),
  3. emit >= <floor> `decode` line(s), each carrying EVERY field an external
     consumer reads: message(non-empty), mode, snr, dt, freq, time.

If either end changes its producer/consumer format, update this file in
lockstep — it is one end of a wire contract.

Usage:
    test/radcon_contract_harness.py <jt9-binary> <wav-file> [--mode FT8]
                                    [--floor 1] [--depth 3] [--quiet]
Exit 0 on PASS, 1 on FAIL.
"""
import argparse
import json
import struct
import subprocess
import sys
import wave

FRAME_AUDIO = 0x01
FRAME_CONTROL = 0x02

# ~100 ms at 12 kHz, matching a typical external producer's audio-thread cadence.
CHUNK_SAMPLES = 1200

# trperiod a minimal producer sends = round(period seconds) for each mode
# (FT8 15.0, FT4 7.5->8, JT9/JT65/Q65 60, MSK144 15).
RADCON_TRPERIOD = {
    "FT8": 15, "FT4": 8, "JT9": 60, "JT65": 60, "Q65": 60, "MSK144": 15,
}
# Modes an external consumer recognises.
RADCON_MODES = {"FT8", "FT4", "JT9", "JT65", "Q65", "MSK144"}
# Fields an external consumer reads off a decode line.
RADCON_DECODE_FIELDS = ("message", "mode", "snr", "dt", "freq", "time")


def wsjl_session_header() -> bytes:
    # 'WSJL' + fmt=0(int16 PCM) + ch=1(mono) + rate_kHz=12 LE u16.
    return b"WSJL" + bytes([0, 1]) + struct.pack("<H", 12)


def framed(type_byte: int, body: bytes) -> bytes:
    # [type:1][len:4 LE][body:len]
    return bytes([type_byte]) + struct.pack("<I", len(body)) + body


def radcon_configure_json(mode: str, depth: int, mycall: str, mygrid: str) -> bytes:
    # Byte-for-byte the minimal external-producer configure frame — a hand
    # -built string (NOT json.dumps) so the key ORDER and the absence of
    # version/ntol exactly match what such a producer puts on the wire.
    trperiod = RADCON_TRPERIOD.get(mode, 15)
    s = ('{"t":"configure","mode":"%s","depth":%d,"trperiod":%d'
         % (mode, depth, trperiod))
    if mycall:
        s += ',"mycall":"%s"' % mycall
    if mygrid:
        s += ',"mygrid":"%s"' % mygrid
    s += "}"
    return s.encode("utf-8")


def radcon_halt_json() -> bytes:
    return b'{"t":"halt"}'


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("jt9")
    p.add_argument("wav")
    p.add_argument("--mode", default="FT8")
    p.add_argument("--depth", type=int, default=3)
    p.add_argument("--floor", type=int, default=1)
    p.add_argument("--mycall", default="KJ5HST")
    p.add_argument("--mygrid", default="EM18")
    p.add_argument("--quiet", action="store_true")
    args = p.parse_args()

    def say(*a):
        if not args.quiet:
            print(*a)

    with wave.open(args.wav) as w:
        if w.getnchannels() != 1:
            print("ERROR: WAV must be mono", file=sys.stderr)
            return 2
        if w.getsampwidth() != 2:
            print("ERROR: WAV must be 16-bit", file=sys.stderr)
            return 2
        if w.getframerate() != 12000:
            print("ERROR: WAV must be 12 kHz (post-resample wire rate)", file=sys.stderr)
            return 2
        audio = w.readframes(w.getnframes())

    proc = subprocess.Popen(
        [args.jt9, "--stream"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    )
    try:
        proc.stdin.write(wsjl_session_header())
        proc.stdin.write(framed(FRAME_CONTROL,
                                radcon_configure_json(args.mode, args.depth,
                                                      args.mycall, args.mygrid)))
        step = CHUNK_SAMPLES * 2  # bytes
        for off in range(0, len(audio), step):
            proc.stdin.write(framed(FRAME_AUDIO, audio[off:off + step]))
        proc.stdin.write(framed(FRAME_CONTROL, radcon_halt_json()))
        proc.stdin.flush()
        proc.stdin.close()
        out = proc.stdout.read().decode("utf-8", "replace")
        proc.wait(timeout=120)
    finally:
        if proc.poll() is None:
            proc.kill()

    ready = False
    errors = []
    decodes = []
    for line in out.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue
        t = obj.get("t")
        if t == "ready":
            ready = True
        elif t == "error":
            errors.append(obj)
        elif t == "decode":
            decodes.append(obj)

    # --- contract assertions (both-sides-healthy) ---
    ok = True
    if not ready:
        say("  FAIL: no `ready` line — jt9 did not accept the WSJL header")
        ok = False
    if errors:
        say("  FAIL: jt9 emitted %d error line(s) — minimal frame was DECLINED:"
            % len(errors))
        for e in errors:
            say("        %s" % json.dumps(e))
        ok = False
    if len(decodes) < args.floor:
        say("  FAIL: %d decode(s) < floor %d — minimal frame produced no decode"
            % (len(decodes), args.floor))
        ok = False
    # Every decode must carry the fields an external consumer reads.
    for d in decodes:
        missing = [f for f in RADCON_DECODE_FIELDS if f not in d]
        if missing:
            say("  FAIL: decode missing consumer field(s) %s: %s"
                % (missing, json.dumps(d)))
            ok = False
            break
        if not str(d.get("message", "")).strip():
            say("  FAIL: decode has empty message (a consumer drops it): %s"
                % json.dumps(d))
            ok = False
            break
        if d.get("mode") not in RADCON_MODES:
            say("  FAIL: decode mode %r not in the external consumer's mode set"
                % d.get("mode"))
            ok = False
            break

    if ok:
        say("  PASS: %s — ready ok, 0 errors, %d decode(s) >= %d, all consumer "
            "fields present" % (args.mode, len(decodes), args.floor))
        sample = decodes[0]
        say("        e.g. %s" % json.dumps(
            {k: sample.get(k) for k in RADCON_DECODE_FIELDS}))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
