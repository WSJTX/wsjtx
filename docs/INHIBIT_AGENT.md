# inhibit-agent — standalone KEY agent

**Audience:** operators who use TX Inhibit on a dual-radio seat **without WIMS**.  
**Scope:** this program only. Gate internals live in [TX_INHIBIT.md](TX_INHIBIT.md).

**KEY agent** is the role. This program is the standalone KEY agent: the
operator supplies the serial port and dest `host:port`. A WIMS
deployment can instead use **`wims-key-agent`** (destinations from WIMS
discovery). Same UDP protocol either way.

Without a KEY agent the gate only accepts hold datagrams — it cannot
see the SSB/CW KEY. Shipping this program is what makes TX Inhibit
usable on a station that is not a WIMS station.

---

## 0. Inhibit generators (same wire protocol)

| Program | Role |
|---------|------|
| **inhibit-agent** | **This program.** USB-serial **CTS** → hold. Operator setup. |
| **wims-key-agent** | WIMS KEY agent (WIMS tree, not this program). Same protocol. |
| **`tools/send_inhibit_hold.py`** | Lab / bench: scripted hold / release, no KEY dongle. |

All speak the same `tx_inhibit` datagrams.

**Name:** `inhibit-agent` (CLI) and `inhibit-agent-gui` (GUI). Linux and Windows.

---

## 1. Purpose

Give **SSB/CW KEY priority over WSJT-X transmit** on a dual-radio station:

1. Sense the SSB/CW **KEY** line on USB-serial **CTS**.
2. While KEY is asserted, send **TX Inhibit hold** datagrams to the gate.
3. On KEY release: **hang only for break-in CW**, then **release** (`ttl_ms: 0`).
   Continuous KEY (SSB / PTT / non-break-in) releases **immediately**.
   **No software PTT debounce** — the sense path is for **manual switches that
   already debounce**.

---

## 2. Two forms

| Form | Binary | Who | Required inputs |
|------|--------|-----|-----------------|
| **CLI** | `inhibit-agent` | Scripts, SSH, startup files | USB-serial **CTS** port and dest `host:port` |
| **GUI** | `inhibit-agent-gui` | Operators | Dest **host:port** in the window (default `127.0.0.1:22372`). CTS port auto-picked or `--port`. |

---

## 3. CLI (scripting)

```text
inhibit-agent --port /dev/ttyUSB0 --addr 127.0.0.1:22372
inhibit-agent --port COM7 --addr 192.168.1.40:22372
inhibit-agent COM7 192.168.1.40:22372
inhibit-agent --port COM7 --addr 127.0.0.1:22372 --invert
inhibit-agent --list-ports
```

| Flag | Default | Notes |
|------|---------|--------|
| `--port` / first positional | **required** | COM / tty device (CTS = KEY) |
| `--addr` / second positional | **required** | `host:port` of the WSJT-X gate |
| `--invert` | false | CTS polarity |
| `--ttl-ms` | `600` | Wire hold timeout (not hang) |

Stdout is timestamped `STATE` / `HOLD` / `RELEASE` / `SENSE FAULT` lines. Ctrl-C
sends release then exits (fail-safe: if the process dies, gate deadman also
clears).

---

## 4. GUI (operators)

Double-click `inhibit-agent-gui` (Windows) or run it from the install `bin/`
(Linux). It:

1. Prefers a serial device whose USB strings look like **Keyline** / **WA1HCO**.
2. Else uses the only non-builtin USB-serial port.
3. **Gate** field is dest `host:port`, default **`127.0.0.1:22372`**. Apply to change.
4. Shows **OPEN** / **INHIBITING** / **HANG** / **SENSE FAULT** in a large badge.

Usual same-PC seat: leave Gate at localhost; only the CTS port is auto-picked.

---

## 5. Hang

Same KEYing monitor as the design in [TX_INHIBIT.md](TX_INHIBIT.md) §3:

- Break-in CW: hang = **1.5 × word gap** (10.5 × dit), clamp 315–1260 ms.
- Continuous KEY / SSB: hang = **0**.

Hang exists only so WSJT-X PTT does not follow CW dits.

---

## 6. Wire identity

| Field | Value |
|-------|--------|
| `station` | `inhibit-agent` |
| `band` | `local` |

Not used for routing. Diagnostics / badge only.

---

## 7. Fail-safe

If the agent stops or the dongle disappears, keepalives stop. The gate deadman
(~600 ms) opens. Prefer a brief unprotected window over a stuck inhibit.

Wrong or missing COM is **SENSE FAULT**, never silent protection.
