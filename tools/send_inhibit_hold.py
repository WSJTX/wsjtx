#!/usr/bin/env python3
"""KEY-agent stand-in: send TX Inhibit hold/release UDP datagrams to a gate.

Protocol and KEY-agent design: docs/TX_INHIBIT.md

Grave/backtick ` is KEY *level* (not Space). Windows: VK_OEM_3; Linux: KEY_GRAVE
(/dev/input; may need group input).

  python3 tools/send_inhibit_hold.py --interactive
  python3 tools/send_inhibit_hold.py --ttl-ms 3000 --station TEST
  python3 tools/send_inhibit_hold.py --ttl-ms 0

Default UDP port is 22372.
"""
from __future__ import annotations

import argparse
import json
import os
import select
import socket
import sys
import time

DEFAULT_PORT = 22372
DEFAULT_TTL_MS = 600  # hold_timeout_ms (safety), not hang
KEEPALIVE_S = 0.2
# Hang = 1.5 × word gap = 10.5 × dit (docs/TX_INHIBIT.md §3.4); WPM ~10..40
HANG_MIN_S = 0.315
HANG_MAX_S = 1.260
HANG_DIT_MULT = 10.5
CONTINUOUS_MARK_S = 0.5  # non-break-in / SSB → hang 0


def encode(station: str, band: str, seq: int, ttl_ms: int) -> bytes:
    return json.dumps(
        {
            "tx_inhibit": 1,
            "ttl_ms": int(ttl_ms),
            "station": station,
            "band": band,
            "seq": int(seq),
        },
        separators=(",", ":"),
    ).encode()


def send_to(sock: socket.socket, host: str, port: int, payload: bytes) -> None:
    sock.sendto(payload, (host, port))


def one_shot(args: argparse.Namespace) -> None:
    msg = encode(args.station, args.band, args.seq, args.ttl_ms)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    send_to(sock, args.host, args.port, msg)
    print(f"sent {msg!r} -> {args.host}:{args.port}")


class HangPolicy:
    def __init__(self) -> None:
        self.dit_s = 0.0

    def hang_s_for_closure(self, closure_s: float) -> float:
        """Break-in: 1.5×word gap from dit estimate. Continuous KEY: 0."""
        if closure_s <= 0:
            return 0.0
        if closure_s >= CONTINUOUS_MARK_S:
            return 0.0  # non-break-in CW / SSB
        if self.dit_s <= 0:
            self.dit_s = closure_s
        else:
            self.dit_s = 0.35 * closure_s + 0.65 * self.dit_s
        hang = HANG_DIT_MULT * self.dit_s
        return max(HANG_MIN_S, min(HANG_MAX_S, hang))


def _space_level_win() -> bool | None:
    try:
        import ctypes
    except ImportError:
        return None
    VK_OEM_3 = 0xC0  # US keyboard `~ (grave)
    return bool(ctypes.windll.user32.GetAsyncKeyState(VK_OEM_3) & 0x8000)


def _quit_win() -> bool:
    try:
        import ctypes
    except ImportError:
        return False
    user32 = ctypes.windll.user32
    return bool(user32.GetAsyncKeyState(ord("Q")) & 0x8000) or bool(
        user32.GetAsyncKeyState(0x1B) & 0x8000
    )


_evdev_fd = None


def _open_evdev() -> bool:
    global _evdev_fd
    if _evdev_fd is not None:
        return _evdev_fd >= 0
    _evdev_fd = -1
    import glob
    import struct

    candidates = sorted(glob.glob("/dev/input/by-path/*-event-kbd")) + sorted(
        glob.glob("/dev/input/event*")
    )
    # EVIOCGKEY size: KEY_MAX is typically 767 → ~96 bytes; use 256
    EVIOCGKEY = 0x80004518 | (256 << 16)  # rough; use fcntl with correct size below
    for path in candidates:
        try:
            fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
        except OSError:
            continue
        # EVIOCGBIT(0) check skipped for brevity; try EVIOCGKEY
        buf = bytearray(128)
        try:
            import fcntl
            # EVIOCGKEY(len) = _IOC(_IOC_READ, 'E', 0x18, len)
            req = 0x80004518 | (len(buf) << 16)
            fcntl.ioctl(fd, req, buf)
        except OSError:
            os.close(fd)
            continue
        _evdev_fd = fd
        return True
    return False


def _space_level_linux() -> bool | None:
    if not _open_evdev():
        return None
    import fcntl

    # Drain
    try:
        while os.read(_evdev_fd, 64):
            pass
    except BlockingIOError:
        pass
    except OSError:
        return None
    buf = bytearray(128)
    req = 0x80004518 | (len(buf) << 16)
    try:
        fcntl.ioctl(_evdev_fd, req, buf)
    except OSError:
        return None
    # KEY_GRAVE = 41 (backtick / left quote)
    bit = 41
    return bool(buf[bit // 8] & (1 << (bit % 8)))


def space_down() -> bool | None:
    """True/False if level available; None if no platform reader."""
    if sys.platform == "win32":
        return _space_level_win()
    if sys.platform.startswith("linux"):
        return _space_level_linux()
    return None


def interactive(args: argparse.Namespace) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    seq = args.seq
    hang = HangPolicy()
    key_down = False
    band_held = False
    key_down_at = 0.0
    hang_until = -1.0
    fixed_hang = args.fixed_hang_ms
    use_fixed = fixed_hang is not None

    def emit(ttl_ms: int) -> None:
        nonlocal seq
        payload = encode(args.station, args.band, seq, ttl_ms)
        send_to(sock, args.host, args.port, payload)
        seq += 1
        action = "HOLD" if ttl_ms else "RELEASE"
        print(
            f"{time.strftime('%H:%M:%S')}  {action}  ttl_ms={ttl_ms}  "
            f"-> {args.host}:{args.port}",
            flush=True,
        )

    level = space_down()
    if level is None:
        print(
            "No KEY (grave/`) level reader on this platform (need Windows or Linux "
            "/dev/input). Use the built inhibit-test binary, or fix input access.",
            file=sys.stderr,
        )
        sys.exit(1)

    print(
        f"Interactive KEY (level) → {args.host}:{args.port}\n"
        f"  grave/`  = KEY level (press = hold, release = hang then free)\n"
        f"  (not Space — typing won't false-trigger)\n"
        f"  Ctrl-C    = quit\n"
        f"  station={args.station!r}  ttl_ms={args.ttl_ms}  "
        f"hang={'fixed '+str(fixed_hang)+'ms' if use_fixed else 'adaptive'}\n",
        flush=True,
    )

    try:
        while True:
            now = time.monotonic()
            space = bool(space_down())

            if space and not key_down:
                key_down = True
                key_down_at = now
                hang_until = -1.0
                if not band_held:
                    band_held = True
                    emit(args.ttl_ms)
                print(f"{now:10.3f}  KEY DOWN", flush=True)

            if not space and key_down:
                key_down = False
                closure = now - key_down_at
                if use_fixed:
                    hang_s = max(0.0, (fixed_hang or 0) / 1000.0)
                else:
                    hang_s = hang.hang_s_for_closure(closure)
                hang_until = now + hang_s
                dit = f"  dit≈{hang.dit_s*1000:.0f} ms" if hang.dit_s > 0 else ""
                print(
                    f"{now:10.3f}  KEY UP    closure={closure*1000:.0f} ms  "
                    f"hang={hang_s*1000:.0f} ms{dit}",
                    flush=True,
                )

            if not key_down and hang_until >= 0 and now >= hang_until:
                hang_until = -1.0
                if band_held:
                    emit(0)
                    band_held = False
                    print(f"{now:10.3f}  RELEASE (hang done)", flush=True)

            if band_held and (now - getattr(interactive, "_last_ka", 0)) >= KEEPALIVE_S:
                emit(args.ttl_ms)
                interactive._last_ka = now  # type: ignore[attr-defined]

            if band_held and not hasattr(interactive, "_last_ka"):
                interactive._last_ka = now  # type: ignore[attr-defined]

            time.sleep(0.01)
    except KeyboardInterrupt:
        if band_held:
            emit(0)
        print("\nquit (Ctrl-C)")


def main() -> None:
    p = argparse.ArgumentParser(
        description="Send TX Inhibit hold/release UDP datagrams to wsjtx-inhibit"
    )
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
    p.add_argument(
        "--ttl-ms",
        type=int,
        default=DEFAULT_TTL_MS,
        help="Hold TTL ms; 0 = release (one-shot). Interactive: keepalive TTL.",
    )
    p.add_argument("--station", default="TEST-SSB")
    p.add_argument("--band", default="144")
    p.add_argument("--seq", type=int, default=1)
    p.add_argument(
        "-i",
        "--interactive",
        action="store_true",
        help="Grave/` KEY level + hang (docs §3)",
    )
    p.add_argument(
        "--fixed-hang-ms",
        type=int,
        default=None,
        help="Interactive: fixed hang after KEY up instead of adaptive",
    )
    args = p.parse_args()
    if args.interactive:
        interactive(args)
    else:
        one_shot(args)


if __name__ == "__main__":
    main()
