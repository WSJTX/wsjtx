#!/usr/bin/env python3
"""
wav-to-stream validation harness.

Pipes a 12 kHz mono int16 WAV file through `jt9 --stream` using the
streaming protocol, then prints the resulting NDJSON decodes.
Used to verify that streaming-mode decode results match the WAV-mode
ground truth (cross-mode validation).

Usage:
    test/wav_to_stream_harness.py <jt9-binary> <wav-file> [--mode FT8]

Exit code is jt9's exit code. NDJSON lines are written to stdout, one
per `{"v":1,"t":"...",...}` event from jt9. Diagnostic stderr from jt9
is merged into stdout (consumers MUST drain stderr; this harness uses
redirect_error_stream=true).
"""
from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import wave
from pathlib import Path

# Frame-type bytes (matching streaming_io.f90).
FRAME_AUDIO = 0x01
FRAME_CONTROL = 0x02

# Audio chunk size in 12 kHz samples per frame. ~1 s per frame keeps the
# protocol's audio-doesn't-span-period gotcha trivially satisfied.
CHUNK_SAMPLES = 12000


def wsjt_session_header(rate_khz: int = 12) -> bytes:
    """8-byte WSJT session header: magic + fmt + channels + rate_kHz LE u16."""
    return b"WSJT" + bytes([0, 1]) + struct.pack("<H", rate_khz)


def frame(type_byte: int, body: bytes) -> bytes:
    """[type:1][len:4 LE][body:len]"""
    return bytes([type_byte]) + struct.pack("<I", len(body)) + body


def configure_json(
    mode: str = "FT8",
    depth: int = 3,
    ntol: int | None = 1000,
    nfa: int | None = 200,
    nfb: int | None = 3000,
    mycall: str = "KJ5HST",
    mygrid: str = "EM18",
    nutc: int | None = None,
    version: int | None = None,
    bad_depth: bool = False,
    omit_mygrid: bool = False,
    # Phase 4: operator identity + frequency-window keys. None =>
    # omit the key from the frame (so they default to no-op on the fixtures).
    his_call: str | None = None,
    his_grid: str | None = None,
    my_b_call: str | None = None,
    his_b_call: str | None = None,
    my_call_standard: bool | None = None,
    his_call_standard: bool | None = None,
    tx_audio_offset_hz: int | None = None,
    jt65_jt9_split_hz: int | None = None,
    max_drift_hz: int | None = None,
    bad_phase4: bool = False,
    # Phase 5: decoder-tuning basic fields. None => omit (no-op).
    dt_tolerance_seconds: float | None = None,
    eme_delay_seconds: float | None = None,
    kin_samples: int | None = None,
    nzhsym_per_period: int | None = None,
    npts_c0_array: int | None = None,
    min_width: int | None = None,
    min_sync: int | None = None,
    n_2pass: int | None = None,
    robust_mode: bool | None = None,
    nagain_flag: bool | None = None,
    tx_mode: str | None = None,
    clear_average: bool | None = None,
    bad_phase5: bool = False,
    # Phase 6: FT8/FT4 specifics cluster. None => omit (no-op).
    multithreaded_ft8: bool | None = None,
    ft8_cycles: int | None = None,
    ft8_rxf_sensitivity: int | None = None,
    ft8_threads: int | None = None,
    ft8_decoder_start: int | None = None,
    ft8_low_threshold: bool | None = None,
    ft8_subpass: bool | None = None,
    ft8_ap_on: bool | None = None,
    ap_cq_only: bool | None = None,
    ap_my_call: bool | None = None,
    jt65_ap_on: bool | None = None,
    ap_width_hz: int | None = None,
    hide_ft8_duplicates: bool | None = None,
    common_ft8b: bool | None = None,
    enable_dxc_search: bool | None = None,
    wide_dxc_search: bool | None = None,
    superfox_mode: bool | None = None,
    even_sequence: bool | None = None,
    hound_mode: bool | None = None,
    multi_instance: bool | None = None,
    skip_tx1: bool | None = None,
    nagain_filter: bool | None = None,
    stop_hint: bool | None = None,
    hint_mode: bool | None = None,
    bad_phase6: bool = False,
    # Phase 7: diagnostic / sequencing fields. None => omit (no-op).
    last_tx_seconds_ago: int | None = None,
    currently_txing: bool | None = None,
    mode_changed: bool | None = None,
    qso_progress_state: int | None = None,
    sec_band_changed: int | None = None,
    delay_units: int | None = None,
    bad_phase7: bool = False,
    # Phase 8: string & scaled / derived fields. None => omit.
    utc: str | None = None,
    date: str | None = None,
    n_trials: int | None = None,
    candthin_threshold: float | None = None,
    dt_center_seconds: float | None = None,
    # Phase 9: bit-packed fields. None => omit.
    # Sending ANY ndepth-unpacked key auto-drops the legacy `depth` key
    # (precedence — a frame carrying both is a configure_type_error conflict).
    depth_level: int | None = None,
    q65_maxiters_level: int | None = None,
    use_averaging: bool | None = None,
    deep_ap_search: bool | None = None,
    q65_auto_clear_average: bool | None = None,
    contest_type: str | None = None,
    single_decode: bool | None = None,
    vhf_features: bool | None = None,
    noise_blanker_level: int | None = None,
    bad_phase9: bool = False,
    phase9_conflict: bool = False,
) -> bytes:
    payload: dict = {
        "t": "configure",
        "mode": mode,
        "depth": depth,
        "mycall": mycall,
        "mygrid": mygrid,
    }
    if omit_mygrid:
        # Drop mygrid so a mycall value equal to the literal token "mygrid" has
        # no real mygrid key to anchor to — exercises the value-vs-key aliasing
        # regression guard.
        payload.pop("mygrid", None)
    if bad_depth:
        # Wrong JSON type for an int key: depth as a string. Exercises the
        # configure_type_error / atomic-decline path.
        payload["depth"] = "abc"
    if version is not None:
        payload["version"] = version
    if ntol is not None:
        payload["ntol"] = ntol
    if nfa is not None:
        payload["nfa"] = nfa
    if nfb is not None:
        payload["nfb"] = nfb
    if nutc is not None:
        payload["nutc"] = nutc
    # Phase 4 keys (omitted when None so they don't perturb the default frame).
    if his_call is not None:
        payload["his_call"] = his_call
    if his_grid is not None:
        payload["his_grid"] = his_grid
    if my_b_call is not None:
        payload["my_b_call"] = my_b_call
    if his_b_call is not None:
        payload["his_b_call"] = his_b_call
    if my_call_standard is not None:
        payload["my_call_standard"] = my_call_standard
    if his_call_standard is not None:
        payload["his_call_standard"] = his_call_standard
    if tx_audio_offset_hz is not None:
        payload["tx_audio_offset_hz"] = tx_audio_offset_hz
    if jt65_jt9_split_hz is not None:
        payload["jt65_jt9_split_hz"] = jt65_jt9_split_hz
    if max_drift_hz is not None:
        payload["max_drift_hz"] = max_drift_hz
    if bad_phase4:
        # Wrong JSON type for a Phase 4 BOOLEAN key: my_call_standard as a
        # number. Exercises the configure_type_error / atomic-decline path for
        # a Phase 4 key (mirrors --bad-type's depth-as-string for an int key).
        # Placed last so it overrides any my_call_standard set above.
        payload["my_call_standard"] = 5
    # Phase 5 keys (omitted when None so they don't perturb the default frame).
    if dt_tolerance_seconds is not None:
        payload["dt_tolerance_seconds"] = dt_tolerance_seconds
    if eme_delay_seconds is not None:
        payload["eme_delay_seconds"] = eme_delay_seconds
    if kin_samples is not None:
        payload["kin_samples"] = kin_samples
    if nzhsym_per_period is not None:
        payload["nzhsym_per_period"] = nzhsym_per_period
    if npts_c0_array is not None:
        payload["npts_c0_array"] = npts_c0_array
    if min_width is not None:
        payload["min_width"] = min_width
    if min_sync is not None:
        payload["min_sync"] = min_sync
    if n_2pass is not None:
        payload["n_2pass"] = n_2pass
    if robust_mode is not None:
        payload["robust_mode"] = robust_mode
    if nagain_flag is not None:
        payload["nagain_flag"] = nagain_flag
    if tx_mode is not None:
        payload["tx_mode"] = tx_mode
    if clear_average is not None:
        payload["clear_average"] = clear_average
    if bad_phase5:
        # Wrong JSON type for a Phase 5 REAL key: dt_tolerance_seconds as a
        # string. Exercises the configure_type_error / atomic-decline path for a
        # Phase 5 key. Placed last so it overrides any value set above.
        payload["dt_tolerance_seconds"] = "abc"
    # Phase 6 keys (omitted when None so they don't perturb the default frame).
    if multithreaded_ft8 is not None:
        payload["multithreaded_ft8"] = multithreaded_ft8
    if ft8_cycles is not None:
        payload["ft8_cycles"] = ft8_cycles
    if ft8_rxf_sensitivity is not None:
        payload["ft8_rxf_sensitivity"] = ft8_rxf_sensitivity
    if ft8_threads is not None:
        payload["ft8_threads"] = ft8_threads
    if ft8_decoder_start is not None:
        payload["ft8_decoder_start"] = ft8_decoder_start
    if ft8_low_threshold is not None:
        payload["ft8_low_threshold"] = ft8_low_threshold
    if ft8_subpass is not None:
        payload["ft8_subpass"] = ft8_subpass
    if ft8_ap_on is not None:
        payload["ft8_ap_on"] = ft8_ap_on
    if ap_cq_only is not None:
        payload["ap_cq_only"] = ap_cq_only
    if ap_my_call is not None:
        payload["ap_my_call"] = ap_my_call
    if jt65_ap_on is not None:
        payload["jt65_ap_on"] = jt65_ap_on
    if ap_width_hz is not None:
        payload["ap_width_hz"] = ap_width_hz
    if hide_ft8_duplicates is not None:
        payload["hide_ft8_duplicates"] = hide_ft8_duplicates
    if common_ft8b is not None:
        payload["common_ft8b"] = common_ft8b
    if enable_dxc_search is not None:
        payload["enable_dxc_search"] = enable_dxc_search
    if wide_dxc_search is not None:
        payload["wide_dxc_search"] = wide_dxc_search
    if superfox_mode is not None:
        payload["superfox_mode"] = superfox_mode
    if even_sequence is not None:
        payload["even_sequence"] = even_sequence
    if hound_mode is not None:
        payload["hound_mode"] = hound_mode
    if multi_instance is not None:
        payload["multi_instance"] = multi_instance
    if skip_tx1 is not None:
        payload["skip_tx1"] = skip_tx1
    if nagain_filter is not None:
        payload["nagain_filter"] = nagain_filter
    if stop_hint is not None:
        payload["stop_hint"] = stop_hint
    if hint_mode is not None:
        payload["hint_mode"] = hint_mode
    if bad_phase6:
        # Wrong JSON type for a Phase 6 BOOLEAN key: ft8_ap_on as a number.
        # Exercises the configure_type_error / atomic-decline path for a Phase 6
        # key. Placed last so it overrides any ft8_ap_on set above.
        payload["ft8_ap_on"] = 5
    # Phase 7 keys (omitted when None so they don't perturb the default frame).
    if last_tx_seconds_ago is not None:
        payload["last_tx_seconds_ago"] = last_tx_seconds_ago
    if currently_txing is not None:
        payload["currently_txing"] = currently_txing
    if mode_changed is not None:
        payload["mode_changed"] = mode_changed
    if qso_progress_state is not None:
        payload["qso_progress_state"] = qso_progress_state
    if sec_band_changed is not None:
        payload["sec_band_changed"] = sec_band_changed
    if delay_units is not None:
        payload["delay_units"] = delay_units
    if bad_phase7:
        # Wrong JSON type for a Phase 7 INT key: last_tx_seconds_ago as a string.
        # Exercises the configure_type_error / atomic-decline path for a Phase 7
        # key (expected:"int" got:"string"). Placed last so it overrides any
        # last_tx_seconds_ago set above.
        payload["last_tx_seconds_ago"] = "soon"
    # Phase 8 keys (omitted when None so they don't perturb the default frame).
    # utc/date are ISO strings; n_trials is a plain int the receiver encodes to
    # nranera; candthin_threshold/dt_center_seconds are floats scaled /100.
    if utc is not None:
        payload["utc"] = utc
    if date is not None:
        payload["date"] = date
    if n_trials is not None:
        payload["n_trials"] = n_trials
    if candthin_threshold is not None:
        payload["candthin_threshold"] = candthin_threshold
    if dt_center_seconds is not None:
        payload["dt_center_seconds"] = dt_center_seconds
    # Phase 9 keys (omitted when None so they don't perturb the default frame).
    # ndepth group: sending any of these drops the legacy `depth` key (the receiver
    # declines a frame carrying BOTH the full-int depth and an unpacked ndepth key —
    # conflicting encodings). This mirrors a real producer sending one encoding form.
    ndepth_keys = {
        "depth_level": depth_level,
        "q65_maxiters_level": q65_maxiters_level,
        "use_averaging": use_averaging,
        "deep_ap_search": deep_ap_search,
        "q65_auto_clear_average": q65_auto_clear_average,
    }
    if any(v is not None for v in ndepth_keys.values()):
        payload.pop("depth", None)
    for k, v in ndepth_keys.items():
        if v is not None:
            payload[k] = v
    # nexp_decode group (contest_type / single_decode / vhf_features / noise_blanker).
    if contest_type is not None:
        payload["contest_type"] = contest_type
    if single_decode is not None:
        payload["single_decode"] = single_decode
    if vhf_features is not None:
        payload["vhf_features"] = vhf_features
    if noise_blanker_level is not None:
        payload["noise_blanker_level"] = noise_blanker_level
    if bad_phase9:
        # Wrong JSON type for a Phase 9 INT key: depth_level as a string. Drop the
        # legacy depth so the only error is the depth_level type mismatch (exercises
        # the configure_type_error / atomic-decline path for a Phase 9 key).
        payload.pop("depth", None)
        payload["depth_level"] = "deep"
    if phase9_conflict:
        # Co-present legacy depth (full int) + unpacked depth_level -> the receiver
        # declines the frame (conflicting encodings). Force BOTH present
        # (the unpacked block above would otherwise have dropped depth).
        payload["depth"] = depth
        payload["depth_level"] = 1
    return json.dumps(payload, separators=(",", ":")).encode("utf-8")


def halt_json() -> bytes:
    return b'{"t":"halt"}'


# --- Stream-vs-WAV parity ---------------------------------------------------
#
# Identity is the decoded MESSAGE text. Empirically the snr/dt/freq fields
# drift between the WAV and stream decode paths for the SAME signal (snr
# ±2 dB, dt ±0.05 s, freq ±3 Hz on the FT8/JT9 fixtures) because the early
# nzhsym streaming passes compute slightly different fine estimates than the
# full-period WAV decode. So they are reported as diagnostics, never asserted;
# the message text is the only exactly-stable field and is the parity identity.

# WSJT-X decode-line sync indicators (column 5), stripped before the message.
_SYNC_MARKERS = {"~", "@", "&", "+", "*", "#", "$", "`"}


def _norm_msg(s: str) -> str:
    """Collapse runs of whitespace to single spaces and strip ends, so a
    right-padded WAV text message and a trimmed NDJSON message compare equal."""
    return " ".join(s.split())


def parse_wav_golden(path: Path) -> list[dict]:
    """Parse a WSJT-X jt9 text decode file (test/fixtures/*.expected.txt).

    Line format:  HHMMSS  SNR  DT  FREQ <sync>  MESSAGE
    The <DecodeFinished ...> trailer and blank lines are skipped. Returns a
    list of {"snr": int, "dt": float, "freq": int, "message": str}.
    """
    out: list[dict] = []
    for raw in path.read_text().splitlines():
        if not raw.strip():
            continue
        if raw.lstrip().startswith("<"):  # <DecodeFinished ...> trailer
            continue
        tok = raw.split()
        if len(tok) < 5:
            continue
        try:
            snr = int(tok[1])
            dt = float(tok[2])
            freq = int(tok[3])
        except ValueError:
            # Not a decode row (e.g. an unexpected header) — skip defensively.
            continue
        rest = tok[4:]
        if rest and rest[0] in _SYNC_MARKERS:
            rest = rest[1:]
        out.append({
            "snr": snr,
            "dt": dt,
            "freq": freq,
            "message": _norm_msg(" ".join(rest)),
        })
    return out


def parse_stream_decodes(lines: list[str]) -> list[dict]:
    """Pull the decode payload (snr/dt/freq/message) from each
    {"t":"decode",...} NDJSON line, ignoring all other events and any
    non-JSON diagnostic lines merged from stderr."""
    out: list[dict] = []
    for line in lines:
        s = line.strip()
        if not s.startswith("{"):
            continue
        try:
            obj = json.loads(s)
        except json.JSONDecodeError:
            continue
        if obj.get("t") != "decode":
            continue
        out.append({
            "snr": obj.get("snr"),
            "dt": obj.get("dt"),
            "freq": obj.get("freq"),
            # `or ""` (not a get-default): an explicit "message":null sets the
            # key, so get("message","") would return None and crash _norm_msg.
            "message": _norm_msg(obj.get("message") or ""),
        })
    return out


def run_parity(golden_path: Path, stream_lines: list[str], jt9_rc: int,
               label: str) -> int:
    """Assert WAV-golden signals ⊆ stream decodes (by message). Print a
    per-signal diagnostic table; return 0 if all present, 1 if any dropped."""
    golden = parse_wav_golden(golden_path)
    stream = parse_stream_decodes(stream_lines)

    if not golden:
        # A golden that parses to zero signals cannot anchor a containment
        # check — "WAV signals ⊆ stream" is vacuously true. Fail loudly so a
        # truncated / corrupt / empty baseline is caught, not silently passed.
        print(f"stream-vs-WAV parity: {label}")
        print(f"  golden: {golden_path}  (0 WAV-confirmed signals)")
        print(f"  stream: {len(stream)} decodes  (jt9 rc={jt9_rc})")
        print("  PARITY FAIL: golden has 0 signals — cannot verify containment "
              "(empty / truncated / corrupt baseline).")
        return 1

    by_msg: dict[str, list[dict]] = {}
    for d in stream:
        by_msg.setdefault(d["message"], []).append(d)

    print(f"stream-vs-WAV parity: {label}")
    print(f"  golden: {golden_path}  ({len(golden)} WAV-confirmed signals)")
    print(f"  stream: {len(stream)} decodes  (jt9 rc={jt9_rc})")
    print("  identity = message text (containment); dFreq/dSNR/dDT are drift "
          "diagnostics, not asserted")
    print(f"  {'status':<8}{'message':<24}{'dFreq':>7}{'dSNR':>6}{'dDT':>7}")

    missing: list[dict] = []
    for g in golden:
        cands = by_msg.get(g["message"])
        if not cands:
            missing.append(g)
            print(f"  {'MISSING':<8}{g['message']:<24}{'-':>7}{'-':>6}{'-':>7}")
            continue
        # If a message somehow appears more than once, diagnose against the
        # nearest-frequency stream decode.
        best = min(cands, key=lambda d: abs((d["freq"] or 0) - g["freq"]))
        d_freq = (best["freq"] or 0) - g["freq"]
        d_snr = (best["snr"] if best["snr"] is not None else 0) - g["snr"]
        d_dt = (best["dt"] if best["dt"] is not None else 0.0) - g["dt"]
        # int() casts guard the :d format codes against a spec-drift float
        # freq/snr in the NDJSON (jt9 emits both as integers today).
        print(f"  {'OK':<8}{g['message']:<24}"
              f"{int(d_freq):>7d}{int(d_snr):>6d}{d_dt:>+7.2f}")

    present = len(golden) - len(missing)
    print(f"  result: {present}/{len(golden)} WAV signals present; "
          f"{len(stream)} stream decodes "
          f"({len(stream) - present} extra — streaming may legitimately find more)")
    if missing:
        print(f"  PARITY FAIL: {len(missing)} WAV-confirmed signal(s) dropped "
              f"by the stream:")
        for g in missing:
            print(f"    - {g['message']!r} (WAV freq {g['freq']} Hz, snr {g['snr']})")
        return 1
    print("  PARITY OK: every WAV-confirmed signal is present in the stream.")
    return 0


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("jt9", type=Path)
    p.add_argument("wav", type=Path)
    p.add_argument("--mode", default="FT8")
    p.add_argument("--mycall", default="KJ5HST",
                   help="Callsign for the main configure frame. Set to a key "
                        "token (e.g. 'mygrid') with --omit-mygrid to exercise "
                        "the value-vs-key aliasing guard.")
    p.add_argument("--omit-mygrid", action="store_true",
                   help="Omit the mygrid key from the main configure frame.")
    p.add_argument("--ntol", type=int, default=1000)
    p.add_argument("--nfa", type=int, default=200)
    p.add_argument("--nfb", type=int, default=3000)
    p.add_argument("--nutc", type=int, default=None,
                   help="UTC HHMMSS for the configure frame.")
    p.add_argument("--schema-version", type=int, default=None,
                   help="Body schema version on the configure frame. "
                        "A version != 1 must be declined with unknown_schema_version.")
    p.add_argument("--bad-control", action="store_true",
                   help="Also send a malformed control frame (missing the required "
                        "\"t\" field) to exercise the configure_parse_error path.")
    p.add_argument("--bad-type", action="store_true",
                   help="Make the MAIN configure frame carry a wrong-typed key "
                        "(depth as a JSON string) to exercise the "
                        "configure_type_error / atomic-decline path. "
                        "Combine with --mode to verify a co-present good key "
                        "(e.g. mode) is NOT applied when the frame is declined.")
    p.add_argument("--bad-type-preamble", action="store_true",
                   help="Send a wrong-typed configure frame BEFORE the valid main "
                        "configure to verify recovery: the bad frame is "
                        "declined with configure_type_error and the following valid "
                        "frame applies normally.")
    # --- Phase 4: operator identity + frequency-window keys -------------------
    p.add_argument("--his-call", dest="his_call", default=None,
                   help="Phase 4: his_call (-> hiscall, char12).")
    p.add_argument("--his-grid", dest="his_grid", default=None,
                   help="Phase 4: his_grid (-> hisgrid, char6).")
    p.add_argument("--my-b-call", dest="my_b_call", default=None,
                   help="Phase 4: my_b_call (-> mybcall, char12).")
    p.add_argument("--his-b-call", dest="his_b_call", default=None,
                   help="Phase 4: his_b_call (-> hisbcall, char12).")
    p.add_argument("--my-call-standard", dest="my_call_standard",
                   action="store_true", default=None,
                   help="Phase 4: my_call_standard=true (-> lmycallstd).")
    p.add_argument("--his-call-standard", dest="his_call_standard",
                   action="store_true", default=None,
                   help="Phase 4: his_call_standard=true (-> lhiscallstd).")
    p.add_argument("--tx-audio-offset-hz", dest="tx_audio_offset_hz", type=int,
                   default=None,
                   help="Phase 4: tx_audio_offset_hz (-> nftx; decouples tx "
                        "from rxfreq, which sets both nfqso+nftx).")
    p.add_argument("--jt65-jt9-split-hz", dest="jt65_jt9_split_hz", type=int,
                   default=None, help="Phase 4: jt65_jt9_split_hz (-> nfsplit).")
    p.add_argument("--max-drift-hz", dest="max_drift_hz", type=int, default=None,
                   help="Phase 4: max_drift_hz (-> max_drift, Q65 drift search).")
    p.add_argument("--bad-phase4", action="store_true",
                   help="Make the MAIN configure carry a wrong-typed Phase 4 key "
                        "(my_call_standard as a number) to exercise "
                        "configure_type_error / atomic-decline for a Phase 4 key.")
    # Phase 5: decoder-tuning basic fields. None => omit the key.
    p.add_argument("--dt-tolerance-seconds", dest="dt_tolerance_seconds",
                   type=float, default=None,
                   help="Phase 5: dt tolerance seconds (-> real(c_float) dttol).")
    p.add_argument("--eme-delay-seconds", dest="eme_delay_seconds",
                   type=float, default=None,
                   help="Phase 5: EME delay seconds (-> real(c_float) emedelay; "
                        "D1 — overwritten by per-mode policy on a mode/tr frame).")
    p.add_argument("--kin-samples", dest="kin_samples", type=int, default=None,
                   help="Phase 5: kin sample count (-> kin; recomputed per period).")
    p.add_argument("--nzhsym-per-period", dest="nzhsym_per_period", type=int,
                   default=None,
                   help="Phase 5: nzhsym per period (-> nzhsym; recomputed per period).")
    p.add_argument("--npts-c0-array", dest="npts_c0_array", type=int, default=None,
                   help="Phase 5: npts for the c0() array (-> npts8; JT9 decode).")
    p.add_argument("--min-width", dest="min_width", type=int, default=None,
                   help="Phase 5: min width (-> minw).")
    p.add_argument("--min-sync", dest="min_sync", type=int, default=None,
                   help="Phase 5: min sync (-> minsync).")
    p.add_argument("--n-2pass", dest="n_2pass", type=int, default=None,
                   help="Phase 5: 2-pass count (-> n2pass).")
    p.add_argument("--robust-mode", dest="robust_mode", action="store_true",
                   default=None, help="Phase 5: robust mode flag (-> nrobust).")
    p.add_argument("--nagain-flag", dest="nagain_flag", action="store_true",
                   default=None, help="Phase 5: decode-again flag (-> nagain).")
    p.add_argument("--tx-mode", dest="tx_mode", default=None,
                   help="Phase 5: tx mode enum JT9/JT65 or int 9/65 (-> ntxmode).")
    p.add_argument("--clear-average", dest="clear_average", action="store_true",
                   default=None, help="Phase 5: clear-average flag (-> nclearave).")
    p.add_argument("--bad-phase5", action="store_true",
                   help="Make the MAIN configure carry a wrong-typed Phase 5 key "
                        "(dt_tolerance_seconds as a string) to exercise "
                        "configure_type_error / atomic-decline for a Phase 5 key.")
    # Phase 6: FT8/FT4 specifics cluster. None => omit the key. The
    # bool flags are store_true/default=None (presence => true); --no-* variants
    # send an explicit false where a gate needs it (ft8_ap_on defaults true).
    p.add_argument("--multithreaded-ft8", dest="multithreaded_ft8",
                   action="store_true", default=None,
                   help="Phase 6: multithreaded_ft8=true (-> lmultift8; "
                        "receiver-ignored on the single-pass stream path).")
    p.add_argument("--ft8-cycles", dest="ft8_cycles", type=int, default=None,
                   help="Phase 6: ft8_cycles (-> nft8cycles).")
    p.add_argument("--ft8-rxf-sensitivity", dest="ft8_rxf_sensitivity", type=int,
                   default=None, help="Phase 6: ft8_rxf_sensitivity (-> nft8rxfsens).")
    p.add_argument("--ft8-threads", dest="ft8_threads", type=int, default=None,
                   help="Phase 6: ft8_threads (-> nmt).")
    p.add_argument("--ft8-decoder-start", dest="ft8_decoder_start", type=int,
                   default=None, help="Phase 6: ft8_decoder_start (-> ndecoderstart).")
    p.add_argument("--ft8-low-threshold", dest="ft8_low_threshold",
                   action="store_true", default=None,
                   help="Phase 6: ft8_low_threshold=true (-> lft8lowth).")
    p.add_argument("--ft8-subpass", dest="ft8_subpass", action="store_true",
                   default=None, help="Phase 6: ft8_subpass=true (-> lft8subpass).")
    p.add_argument("--ft8-ap-on", dest="ft8_ap_on", action="store_true",
                   default=None, help="Phase 6: ft8_ap_on=true (-> lft8apon, AP on).")
    p.add_argument("--no-ft8-ap-on", dest="ft8_ap_on", action="store_false",
                   help="Phase 6: ft8_ap_on=false (-> lft8apon; AP off — observable, "
                        "reduces FT8 decodes vs the default-true).")
    p.add_argument("--ap-cq-only", dest="ap_cq_only", action="store_true",
                   default=None, help="Phase 6: ap_cq_only=true (-> lapcqonly).")
    p.add_argument("--ap-my-call", dest="ap_my_call", action="store_true",
                   default=None, help="Phase 6: ap_my_call=true (-> lapmyc).")
    p.add_argument("--jt65-ap-on", dest="jt65_ap_on", action="store_true",
                   default=None, help="Phase 6: jt65_ap_on=true (-> ljt65apon).")
    p.add_argument("--ap-width-hz", dest="ap_width_hz", type=int, default=None,
                   help="Phase 6: ap_width_hz (-> napwid, RAW; decoder halves it).")
    p.add_argument("--hide-ft8-duplicates", dest="hide_ft8_duplicates",
                   action="store_true", default=None,
                   help="Phase 6: hide_ft8_duplicates=true (-> lhideft8dupes).")
    p.add_argument("--common-ft8b", dest="common_ft8b", action="store_true",
                   default=None, help="Phase 6: common_ft8b=true (-> lcommonft8b).")
    p.add_argument("--enable-dxc-search", dest="enable_dxc_search",
                   action="store_true", default=None,
                   help="Phase 6: enable_dxc_search=true (-> lenabledxcsearch).")
    p.add_argument("--wide-dxc-search", dest="wide_dxc_search",
                   action="store_true", default=None,
                   help="Phase 6: wide_dxc_search=true (-> lwidedxcsearch).")
    p.add_argument("--superfox-mode", dest="superfox_mode", action="store_true",
                   default=None, help="Phase 6: superfox_mode=true (-> b_superfox).")
    p.add_argument("--even-sequence", dest="even_sequence", action="store_true",
                   default=None, help="Phase 6: even_sequence=true (-> b_even_seq).")
    p.add_argument("--hound-mode", dest="hound_mode", action="store_true",
                   default=None, help="Phase 6: hound_mode=true (-> lhound).")
    p.add_argument("--multi-instance", dest="multi_instance", action="store_true",
                   default=None, help="Phase 6: multi_instance=true (-> lmultinst).")
    p.add_argument("--skip-tx1", dest="skip_tx1", action="store_true",
                   default=None, help="Phase 6: skip_tx1=true (-> lskiptx1).")
    p.add_argument("--nagain-filter", dest="nagain_filter", action="store_true",
                   default=None, help="Phase 6: nagain_filter=true (-> nagainfil).")
    p.add_argument("--stop-hint", dest="stop_hint", action="store_true",
                   default=None, help="Phase 6: stop_hint=true (-> nstophint).")
    p.add_argument("--hint-mode", dest="hint_mode", action="store_true",
                   default=None, help="Phase 6: hint_mode=true (-> nhint).")
    p.add_argument("--bad-phase6", action="store_true",
                   help="Make the MAIN configure carry a wrong-typed Phase 6 key "
                        "(ft8_ap_on as a number) to exercise "
                        "configure_type_error / atomic-decline for a Phase 6 key.")
    # Phase 7: diagnostic / sequencing fields. None => omit the key.
    # All 6 are inert on the FT8/JT9 stream fixtures (multithread-block-only /
    # AP-only) — see lib/streaming_control.f90 Phase 7 comment.
    p.add_argument("--last-tx-seconds-ago", dest="last_tx_seconds_ago",
                   type=int, default=None,
                   help="Phase 7: last_tx_seconds_ago (-> nlasttx).")
    p.add_argument("--currently-txing", dest="currently_txing",
                   action="store_true", default=None,
                   help="Phase 7: currently_txing=true (-> ltxing).")
    p.add_argument("--mode-changed", dest="mode_changed",
                   action="store_true", default=None,
                   help="Phase 7: mode_changed=true (-> lmodechanged).")
    p.add_argument("--qso-progress-state", dest="qso_progress_state",
                   type=int, default=None,
                   help="Phase 7: qso_progress_state (-> nQSOProgress; steers AP "
                        "decode passes, inert without AP-only decodes). Apply "
                        "GUARDS the valid GUI range [0,5]; an out-of-range value is "
                        "ignored (it would crash the FT8 decoder's AP-array index).")
    p.add_argument("--sec-band-changed", dest="sec_band_changed",
                   type=int, default=None,
                   help="Phase 7: sec_band_changed (-> nsecbandchanged; D3 inert, "
                        "multithread-block-only).")
    p.add_argument("--delay-units", dest="delay_units",
                   type=int, default=None,
                   help="Phase 7: delay_units (-> ndelay; D3 inert, "
                        "multithread-block-only).")
    p.add_argument("--bad-phase7", action="store_true",
                   help="Make the MAIN configure carry a wrong-typed Phase 7 key "
                        "(last_tx_seconds_ago as a string) to exercise "
                        "configure_type_error / atomic-decline for a Phase 7 key.")
    # Phase 8: string & scaled / derived fields. None => omit.
    p.add_argument("--utc", dest="utc", default=None,
                   help="Phase 8: utc ISO \"HH:MM:SS\" (-> nutc, full HHMMSS; "
                        "decode-observable in the time field; WINS over --nutc).")
    p.add_argument("--date", dest="date", default=None,
                   help="Phase 8: date ISO \"YYYY-MM-DD\" (-> yymmdd; superfox-only, "
                        "inert on FT8/JT9). year>=2100 -> configure_type_error.")
    p.add_argument("--n-trials", dest="n_trials", type=int, default=None,
                   help="Phase 8: n_trials (-> encoded nranera; JT65-only, inert on "
                        "FT8/JT9). Must be 10^N or 3*10^N else configure_type_error.")
    p.add_argument("--candthin-threshold", dest="candthin_threshold",
                   type=float, default=None,
                   help="Phase 8: candthin_threshold (-> ncandthin = round(x*100)).")
    p.add_argument("--dt-center-seconds", dest="dt_center_seconds",
                   type=float, default=None,
                   help="Phase 8: dt_center_seconds (-> ndtcenter = round(x*100)).")
    # Phase 9: bit-packed fields. ndepth-group flags
    # auto-drop the legacy --depth (precedence; co-present is a conflict error).
    p.add_argument("--depth-level", dest="depth_level", type=int, default=None,
                   help="Phase 9: depth_level (ndepth bits 0-2). The one Phase-9 key "
                        "decode-observable on FT8/JT9 (depth_level=1 -> ndepth=1 "
                        "changes the FT8 pass structure). Drops the legacy depth key.")
    p.add_argument("--q65-maxiters-level", dest="q65_maxiters_level", type=int,
                   default=None,
                   help="Phase 9: q65_maxiters_level (ndepth bits 0-1; Q65-only). "
                        "Must agree with depth_level's low 2 bits if both are set.")
    p.add_argument("--use-averaging", dest="use_averaging", action="store_true",
                   default=None,
                   help="Phase 9: use_averaging (ndepth bit 4; Q65/JT65/JT4, inert FT8/JT9).")
    p.add_argument("--deep-ap-search", dest="deep_ap_search", action="store_true",
                   default=None,
                   help="Phase 9: deep_ap_search (ndepth bit 5; JT4/JT65, inert FT8/JT9).")
    p.add_argument("--q65-auto-clear-average", dest="q65_auto_clear_average",
                   action="store_true", default=None,
                   help="Phase 9: q65_auto_clear_average (ndepth bit 7; Q65, inert FT8/JT9).")
    p.add_argument("--contest-type", dest="contest_type", default=None,
                   help="Phase 9: contest_type enum string -> ncontest (nexp_decode "
                        "bits 0-2; SpecOp 5/8/9 collapse to 1). Unknown -> silent skip.")
    p.add_argument("--single-decode", dest="single_decode", action="store_true",
                   default=None,
                   help="Phase 9: single_decode (nexp_decode bit 5; FST4/JT65, not "
                        "the single-pass FT8 decoder).")
    p.add_argument("--vhf-features", dest="vhf_features", action="store_true",
                   default=None,
                   help="Phase 9: vhf_features (nexp_decode bit 6; Q65/JT65, inert FT8/JT9).")
    p.add_argument("--noise-blanker-level", dest="noise_blanker_level", type=int,
                   default=None,
                   help="Phase 9: noise_blanker_level (nexp_decode upper byte, +3 "
                        "offset; FST4-only, inert FT8/JT9).")
    p.add_argument("--bad-phase9", action="store_true",
                   help="Make the MAIN configure carry a wrong-typed Phase 9 key "
                        "(depth_level as a string) to exercise the "
                        "configure_type_error / atomic-decline for a Phase 9 key.")
    p.add_argument("--phase9-conflict", action="store_true",
                   help="Make the MAIN configure carry BOTH the legacy depth and the "
                        "unpacked depth_level (conflict) to exercise the "
                        "configure_type_error / atomic-decline on conflicting encodings.")
    p.add_argument("--prepend-mode", default=None,
                   help="Send a configure with this mode FIRST (no audio), then "
                        "the main configure + audio. Used to test mode-change "
                        "semantics.")
    p.add_argument("--prepend-with-explicit-narrowing", action="store_true",
                   help="When --prepend-mode is set, the prepended configure "
                        "also sets ntol/nfa/nfb to narrow values. Tests that "
                        "the subsequent main configure restores defaults.")
    p.add_argument(
        "--quiet",
        action="store_true",
        help="Suppress non-JSON stderr lines (e.g., TRACE, header-ok).",
    )
    p.add_argument(
        "--parity",
        type=Path,
        default=None,
        help="Stream-vs-WAV decode-parity mode. Instead of "
             "printing NDJSON, decode the WAV via --stream, parse the WSJT-X "
             "text golden at this path, and assert CONTAINMENT: every "
             "WAV-confirmed signal (by message text) is present in the stream. "
             "Exit 0 if all present, 1 if any is dropped. snr/dt/freq are shown "
             "as drift diagnostics only (they legitimately differ between the "
             "WAV and stream decode paths); message text is the identity.",
    )
    args = p.parse_args()

    if not args.jt9.is_file():
        print(f"ERROR: jt9 binary not found: {args.jt9}", file=sys.stderr)
        return 2
    if not args.wav.is_file():
        print(f"ERROR: WAV file not found: {args.wav}", file=sys.stderr)
        return 2
    if args.parity is not None and not args.parity.is_file():
        print(f"ERROR: parity golden not found: {args.parity}", file=sys.stderr)
        return 2

    with wave.open(str(args.wav)) as w:
        if w.getnchannels() != 1:
            print(
                f"ERROR: WAV must be mono (got {w.getnchannels()} channels)",
                file=sys.stderr,
            )
            return 2
        if w.getsampwidth() != 2:
            print(
                f"ERROR: WAV must be 16-bit (got {w.getsampwidth()} byte samples)",
                file=sys.stderr,
            )
            return 2
        if w.getframerate() != 12000:
            print(
                f"ERROR: WAV must be 12 kHz (got {w.getframerate()} Hz)",
                file=sys.stderr,
            )
            return 2
        audio = w.readframes(w.getnframes())

    proc = subprocess.Popen(
        [str(args.jt9), "--stream"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,  # mandatory: consumers must drain stderr
    )

    try:
        proc.stdin.write(wsjt_session_header())

        # Malformed control frame (missing "t") -> configure_parse_error.
        # Sent before the normal configure; the stream must continue and decode.
        if args.bad_control:
            proc.stdin.write(frame(FRAME_CONTROL, b'{"mode":"FT8"}'))

        # Wrong-typed preamble frame -> configure_type_error, declined.
        # Sent before the valid main configure; the stream must recover and the
        # following valid frame must apply normally.
        if args.bad_type_preamble:
            proc.stdin.write(frame(FRAME_CONTROL, configure_json(
                mode=args.mode, bad_depth=True,
            )))

        # Optional mode-change preamble (tests mode-change semantics).
        if args.prepend_mode:
            if args.prepend_with_explicit_narrowing:
                proc.stdin.write(frame(FRAME_CONTROL, configure_json(
                    mode=args.prepend_mode, ntol=10, nfa=1400, nfb=1600,
                )))
            else:
                proc.stdin.write(frame(FRAME_CONTROL, configure_json(
                    mode=args.prepend_mode,
                    ntol=None, nfa=None, nfb=None,   # rely on per-mode defaults
                )))

        # Main configure. When testing mode-change semantics, the bug being
        # gated is "after a prior mode's state, the main configure with no
        # ntol/nfa/nfb should reset to per-mode/baseline defaults." So pass
        # None (omit fields) when prepending, to exercise the restore path.
        # Phase 4 keys ride on the MAIN configure only (not the preambles).
        phase4 = dict(
            his_call=args.his_call, his_grid=args.his_grid,
            my_b_call=args.my_b_call, his_b_call=args.his_b_call,
            my_call_standard=args.my_call_standard,
            his_call_standard=args.his_call_standard,
            tx_audio_offset_hz=args.tx_audio_offset_hz,
            jt65_jt9_split_hz=args.jt65_jt9_split_hz,
            max_drift_hz=args.max_drift_hz,
            bad_phase4=args.bad_phase4,
        )
        # Phase 5 keys also ride on the MAIN configure only.
        phase5 = dict(
            dt_tolerance_seconds=args.dt_tolerance_seconds,
            eme_delay_seconds=args.eme_delay_seconds,
            kin_samples=args.kin_samples,
            nzhsym_per_period=args.nzhsym_per_period,
            npts_c0_array=args.npts_c0_array,
            min_width=args.min_width,
            min_sync=args.min_sync,
            n_2pass=args.n_2pass,
            robust_mode=args.robust_mode,
            nagain_flag=args.nagain_flag,
            tx_mode=args.tx_mode,
            clear_average=args.clear_average,
            bad_phase5=args.bad_phase5,
        )
        # Phase 6 keys also ride on the MAIN configure only.
        phase6 = dict(
            multithreaded_ft8=args.multithreaded_ft8,
            ft8_cycles=args.ft8_cycles,
            ft8_rxf_sensitivity=args.ft8_rxf_sensitivity,
            ft8_threads=args.ft8_threads,
            ft8_decoder_start=args.ft8_decoder_start,
            ft8_low_threshold=args.ft8_low_threshold,
            ft8_subpass=args.ft8_subpass,
            ft8_ap_on=args.ft8_ap_on,
            ap_cq_only=args.ap_cq_only,
            ap_my_call=args.ap_my_call,
            jt65_ap_on=args.jt65_ap_on,
            ap_width_hz=args.ap_width_hz,
            hide_ft8_duplicates=args.hide_ft8_duplicates,
            common_ft8b=args.common_ft8b,
            enable_dxc_search=args.enable_dxc_search,
            wide_dxc_search=args.wide_dxc_search,
            superfox_mode=args.superfox_mode,
            even_sequence=args.even_sequence,
            hound_mode=args.hound_mode,
            multi_instance=args.multi_instance,
            skip_tx1=args.skip_tx1,
            nagain_filter=args.nagain_filter,
            stop_hint=args.stop_hint,
            hint_mode=args.hint_mode,
            bad_phase6=args.bad_phase6,
        )
        # Phase 7 keys also ride on the MAIN configure only.
        phase7 = dict(
            last_tx_seconds_ago=args.last_tx_seconds_ago,
            currently_txing=args.currently_txing,
            mode_changed=args.mode_changed,
            qso_progress_state=args.qso_progress_state,
            sec_band_changed=args.sec_band_changed,
            delay_units=args.delay_units,
            bad_phase7=args.bad_phase7,
        )
        # Phase 8 keys also ride on the MAIN configure only.
        phase8 = dict(
            utc=args.utc,
            date=args.date,
            n_trials=args.n_trials,
            candthin_threshold=args.candthin_threshold,
            dt_center_seconds=args.dt_center_seconds,
        )
        # Phase 9 keys also ride on the MAIN configure only.
        phase9 = dict(
            depth_level=args.depth_level,
            q65_maxiters_level=args.q65_maxiters_level,
            use_averaging=args.use_averaging,
            deep_ap_search=args.deep_ap_search,
            q65_auto_clear_average=args.q65_auto_clear_average,
            contest_type=args.contest_type,
            single_decode=args.single_decode,
            vhf_features=args.vhf_features,
            noise_blanker_level=args.noise_blanker_level,
            bad_phase9=args.bad_phase9,
            phase9_conflict=args.phase9_conflict,
        )
        if args.prepend_mode:
            proc.stdin.write(frame(FRAME_CONTROL, configure_json(
                mode=args.mode, ntol=None, nfa=None, nfb=None,
                nutc=args.nutc, version=args.schema_version,
                bad_depth=args.bad_type, mycall=args.mycall,
                omit_mygrid=args.omit_mygrid,
                **phase4, **phase5, **phase6, **phase7, **phase8, **phase9,
            )))
        else:
            proc.stdin.write(frame(FRAME_CONTROL, configure_json(
                mode=args.mode, ntol=args.ntol, nfa=args.nfa, nfb=args.nfb,
                nutc=args.nutc, version=args.schema_version,
                bad_depth=args.bad_type, mycall=args.mycall,
                omit_mygrid=args.omit_mygrid,
                **phase4, **phase5, **phase6, **phase7, **phase8, **phase9,
            )))

        # Audio frames: CHUNK_SAMPLES * 2 bytes per chunk.
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
    out_lines = out.splitlines()

    if args.parity is not None:
        return run_parity(
            args.parity, out_lines, proc.returncode,
            f"{args.mode} {args.wav.name}",
        )

    for line in out_lines:
        if args.quiet and not line.startswith("{"):
            continue
        print(line)
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
