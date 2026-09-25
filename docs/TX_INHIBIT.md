# TX Inhibit and KEY agent

Design authority for this repository: how **wsjtx-inhibit** implements
**TX Inhibit**, and how a **KEY agent** drives it.

**Operators (install / KEY test):** [INSTALL.md](../INSTALL.md)  
**Code map:** §7

---

## Glossary

| Term | Meaning |
|------|---------|
| **WSJT-X station** | One digi position: this app + PC + network + radio + antenna (CAT/PTT). Multi-op has several. May be an unattended go-box, remote from the priority SSB/CW station and the KEY agent. |
| **KEY agent** | Role: any process that sees the priority KEY and sends hold / keepalive / **release hold**. Not a binary name. |
| **`inhibit-agent`** | Standalone KEY agent in this tree. Operator supplies the serial port and dest `host:port`. [INHIBIT_AGENT.md](INHIBIT_AGENT.md). Makes TX Inhibit usable on a dual-radio seat with only WSJT-X. |
| **TX Inhibit** | Product / feature name (Settings, badge, this build). |
| **want_tx** | Software wants to transmit (FT8 sequence, “Enable Tx”, audio path). |
| **hold** | One or more **leases** are live: KEY agent(s) have told this WSJT-X station to inhibit transmit. |
| **lease** | Per-**Controller ID** expiring hold row. Hold = logical **OR** of live leases. |
| **Controller ID** | Stable machine-readable lease owner (wire field). A sender may only refresh/release its own row. |
| **assert PTT** / **release PTT** | Physical PTT line active / inactive (RTS/DTR as configured). Same pattern for KEY: **assert KEY** / **release KEY**. |
| **release hold** | Explicit end of **this controller’s** lease: type-18 packet with TTL ms `0`. Normal end after agent hang. |
| **hang** / anti-chatter (KEY agent only) | After last KEY open, how long break-in CW keeps renewing hold before **release hold**. **1.5 × word gap** at measured WPM (≥ 10 WPM). Non-break-in CW / SSB: hang **0** (release on KEY open). Not in the WSJT-X station; no wire field. |
| **hold timeout** / lease TTL | **Safety** timer per lease: how long the station keeps that controller’s row without a refresh. Wire TTL ms (~500–600 ms); keepalives ~200 ms. Not hang; not CW speed. |
| **deadman** | Agent stops without finishing hang and **release hold** (crash, kill, path loss). That controller’s lease then expires; hold ends when no leases remain. |
| **TxInhibit module** | Code under `TxInhibit/` plus the pin filter in `HamlibTransceiver`. |

**Core equation**

```text
assert PTT  ⇔  want_tx  and  not hold
hold        ⇔  any live per-controller lease
```

The KEY agent **tells WSJT-X stations to inhibit transmit**; while a hold is active, the
WSJT-X station will not **assert PTT** even if `want_tx` is true.

**Two different timers — different purpose, different time scale**

| Timer | Where | Purpose | Typical scale |
|-------|--------|---------|----------------|
| **Hang** (anti-chatter) | KEY agent (KEYing monitor) | Break-in CW: keep hold across gaps; EOT after KEY open **&gt; 1.5× word gap**. Non-break-in / SSB: hang 0, EOT on KEY open. | **1.5 × 7 dits** at measured WPM (see §3.4). |
| **Lease TTL** | WSJT-X station only | Survive **loss of hold messages** for that controller. | **TTL ≈ 500–600 ms**; renew **~200 ms**. Independent of WPM. |

These are not interchangeable. Hang is CW/operator policy. Lease TTL is
UDP reliability / fail-open safety.

**History vs current algorithm.** Early designs used one global hold (any
release cleared everyone). The **current** algorithm keeps **per-controller
leases** OR’d, and **explicitly ends** a controller’s lease with TTL `0`
when agent hang finishes. Station lease TTL is only the safety path
(including deadman).

**Not the same as radio “TX Inhibit” menus.** Many rigs latch inhibit until
PTT drops. Sequencing continues (unlike **Halt Tx**).

Wire format: `NetworkMessage::TxInhibit` type **18** (§4). No hang field.

---

## 1. Roles

| Role | Where | Job |
|------|--------|-----|
| **WSJT-X station** | App + PC + network + radio + antenna (may be remote go-box) | When TX Inhibit is enabled and PTT is RTS/DTR: apply the equation (when may this station **assert PTT**). |
| **KEY agent** | Process that sees the priority radio’s KEY | While KEY is asserted (and during agent **hang** after **release KEY**), sends hold keepalives; when hang finishes, **releases hold** (`ttl_ms: 0`). |

Any program that speaks §4 is a valid KEY agent. This tree ships a standalone
agent so a dual-radio seat works with only WSJT-X:

| Program | Tree | Setup |
|---------|------|--------|
| **`inhibit-agent`** / **`inhibit-agent-gui`** | this tree | Operator supplies USB-serial CTS and dest `host:port`. [INHIBIT_AGENT.md](INHIBIT_AGENT.md). |
| **`send_inhibit_hold.py`** | this tree | Scripted bench hold / release. |

```text
  Priority radio
       │ KEY sense
       ▼
  ┌───────────┐     “inhibit transmit” / keepalive / release hold (UDP)
  │ KEY agent │ ────────────────────────────────────────────────────┐
  └───────────┘                                                     │
                                                                    ▼
  want_tx ──► │  TxInhibit filter  │ ── RTS/DTR ──► radio PTT
              │  assert PTT ⇔        │
              │    want_tx and not hold │
              └────────────────────┘
```

The hold request is **UDP only**. Local CTS KEY on the WSJT-X station PTT dongle is
**out of this build** (§5).

---

## 2. WSJT-X station behaviour (this program)

A **WSJT-X station** is one digi position: this program, PC, network, radio, and antenna
(CAT/PTT path). It may be unattended (e.g. a go-box) and remote from the priority
SSB/CW station and the KEY agent host.

- **Opt-in.** Settings → Radio → **Enable TX Inhibit**. Default **off**
  (stock WSJT-X PTT). Requires **PTT method** = RTS or DTR.
- **Sequencing and audio stay the same.** TX Inhibit only decides whether the
  WSJT-X station may **assert PTT** when the KEY agent has said inhibit transmit.
- **Equation:** `assert PTT ⇔ want_tx and not hold`.
- **hold** is active while an unexpired **hold timeout** is set (KEY agent or
  test tool).
- Inhibit listen port: **always ephemeral** (OS-assigned IPv4). Announced in
  status-bar tooltip and in **InhibitStatus** (type 17) on the UDP Server
  stream. Controllers learn `host:port` from type 17 — there is no fixed
  well-known port.
- **UDP inhibit endpoint** = that `host:<udp-port>` for type-18 holds.
  **PTT serial device** = Settings → Radio → Port for RTS/DTR.
- **Arming.** After Enable and a successful bind, the Settings checkbox label
  shows **TX Inhibit enabled**. If bind cannot publish a non-zero port,
  **TX Inhibit has failed to arm**: the checkbox shows
  **TX Inhibit failed (no port)**, a status message appears, and live type 17
  is not sent with port 0. Digi CAT/PTT continue; KEY holds are not received.
- Status bar (red): **`INHIBIT`**. Tooltip may show holder / UDP inhibit endpoint.

### Setup summary

1. **PTT method** = **RTS** or **DTR**.
2. **PTT port** = real serial device (`COMx` / `/dev/ttyUSBx`), same COM as
   CAT if shared, or a separate PTT adapter. List value **CAT** is OmniRig-style
   proxy only — Hamlib still needs a real device name for RTS/DTR.  
   If the device is missing from the list, type the full path. Set the port
   **before** Enable.
3. Check **Enable TX Inhibit**. Available only when RTS/DTR is selected
   **and** PTT port is set. Clear the port and Enable clears too.
   After the rig opens, confirm the label becomes **TX Inhibit enabled**
   (not **TX Inhibit failed (no port)**).
4. Wire RTS/DTR → radio PTT/SEND (or USB SEND / PC KEYING).
5. **WARNING — turn radio VOX off.** TX Inhibit only gates the RTS/DTR PTT line.
   If VOX is on, audio can still key the radio while the badge says **INHIBIT**.
6. Point the KEY agent at this WSJT-X station **UDP inhibit endpoint**
   (tooltip / InhibitStatus type 17). Port must be non-zero.

### Shared USB CAT + RTS/DTR

Many contest radios expose **one USB** with CAT on a virtual COM and
**RTS/DTR** mappable to PTT. Shared COM is normal when Handshake is **None**
and the radio maps the line to SEND/PTT (not flow control).

**Implementation:** Hamlib owns the serial port (one fd for shared CAT+PTT).
The TxInhibit module does **not** open a second serial handle. It only
filters whether `do_ptt` may **assert PTT**.

Also common: CAT-only PTT (not filtered by TX Inhibit), or a second keyline
path when several apps share the station.

#### Brands (USB RTS/DTR → radio PTT?)

| Brand | USB RTS/DTR can key TX? | Notes |
|-------|-------------------------|--------|
| **Icom** (7300, 7610, 705, …) | **Yes** | **USB SEND** = RTS or DTR (PTT). |
| **Elecraft** (K3S, K4, …) | **Yes** | USB RTS/DTR → PTT / KEY / FSK. |
| **Yaesu** (FTDX10, FT-710, …) | **Yes (usual)** | **PC KEYING** = RTS/DTR; CAT RTS handshake **OFF**. |
| **Kenwood** (TS-590SG, …) | **Usually no on built-in USB** | Hardware PTT often ACC; external USB-serial common. |
| **FlexRadio** | **Not classic USB SEND** | Virtual COM / SmartSDR models differ. |

#### Common setup problems

| Symptom | Cause | Try |
|---------|--------|-----|
| Flaky CAT / stuck TX | RTS used as handshake | Handshake **None**; radio SEND not flow control |
| Keys on port open | Polarity / forced DTR-RTS | Check logger forced lines; try other modem line |
| Keys with no WSJT-X running | USB-serial default RTS (open/close or ModemManager) | `inhibit-agent` now forces RTS+DTR idle; Linux udev: `tools/inhibit-agent/99-keyline-not-modem.rules` |
| “Worked yesterday” | Another app owns COM | One owner; restart after other apps close |
| Test PTT OK, no RF | Often audio/mode | DATA mode, levels, power meter |
| CW key instead of PTT | Icom USB Keying vs USB SEND | PTT → **USB SEND** |
| Port-open TX blip | Driver toggles DTR/RTS | Known with some USB-serial chips |
| Multi-app contest mess | Two apps key same lines | Logger digi / port-handoff rules |
| “TX Inhibit never stops PTT” | CAT-only PTT, or feature off | **Enable TX Inhibit** + RTS/DTR |
| Checkbox **TX Inhibit failed (no port)** | Bind / port discovery failed | Fix PTT path; reopen rig; check status message |

#### WSJT-X station options

| Option | Notes |
|--------|--------|
| Same USB COM for CAT + RTS/DTR | Supported when menus match checklist |
| Separate USB-serial for PTT | Fine |
| CAT PTT only | Not filtered — use RTS/DTR for TX Inhibit |
| Kenwood / Flex | Plan external keyline or Flex’s model |

**Shared-cable checklist**

1. Handshake **None**.  
2. Radio: line = USB SEND / PC KEYING / PTT, not flow control.  
3. **PTT method** = RTS or DTR.  
4. **PTT port** = same real COM as CAT (not list value “CAT”).  
5. **Enable TX Inhibit** checked (only after port is set).  
6. One program owns the modem lines.  
7. Confirm with **Test PTT** and RF/ALC, not only “CAT green.”

---

## 3. KEY agent

The KEY agent reads the priority **KEY** line and runs **two parallel state
machines** that meet only at **end of transmission**.

| State machine | On KEY assert (start) | Ongoing | End of transmission |
|---------------|----------------------|---------|---------------------|
| **Hold sender** | Send **hold** immediately with `ttl_ms` = **hold_timeout_ms** (~500–600 ms safety window) | **Keepalives** ~every 200 ms while hold is active | On signal from KEYing monitor: send **release hold** (`ttl_ms: 0`) |
| **KEYing monitor** | Start KEY timing assessment | Classify break-in CW vs continuous KEY (non-break-in CW / SSB); measure CW speed if break-in | Decide **when** the KEY sequence is over; signal Hold sender to release |

Hold sender does **not** guess CW. KEYing monitor does **not** send UDP.
Only Hold sender emits packets; only KEYing monitor decides release timing.

### 3.1 Two timers (again): hang vs hold timeout

| | **Hang (anti-chatter)** | **Hold timeout (safety)** |
|--|-------------------------|---------------------------|
| Owner | KEY agent (KEYing monitor) | WSJT-X station (from each hold packet) |
| Purpose | CW **break-in**: keep hold across element/letter gaps so co-band PTT does not chatter | Survive **lost hold UDP**; fail-open if agent dies |
| Sized by | **1.5 × word gap** at measured CW speed (≥ 10 WPM) | Keepalive period + margin (**~500–600 ms**); renew **~200 ms** |
| On wire | No | Yes: `ttl_ms` = `hold_timeout_ms` |
| End of hold | Monitor says EOT → Hold sender sends `ttl_ms: 0` | No packet before timeout |

Do **not** size hang from hold timeout, or hold timeout from WPM.

### 3.2 KEY classes and when transmission ends

| KEY class | What KEY looks like | Hang after KEY open | End of transmission (EOT) |
|-----------|---------------------|---------------------|---------------------------|
| **Break-in CW** | KEY opens between dits/dahs (element / letter gaps visible) | **1.5 × word gap** at measured WPM (WPM from 10 upward) | KEY stays **open** longer than hang (i.e. longer than 1.5× word gap) |
| **Non-break-in CW** | KEY continuous for whole send (no gaps on KEY sense) | **0** | **Immediately** on **release KEY** |
| **SSB** (and similar) | KEY continuous for whole send | **0** | **Immediately** on **release KEY** |

KEYing monitor classifies the stream, then uses that class for EOT. On EOT it
signals Hold sender → **release hold** (`ttl_ms: 0`). That packet may fire
**before** the first keepalive would have been scheduled (short continuous KEY).

### 3.3 Morse timing baseline (Paris)

| Quantity | Units (dits) | Time at W WPM |
|----------|--------------|---------------|
| Dit | 1 | \(T_\mathrm{dit} = 1200 / W\) ms |
| Dah | 3 | \(3\,T_\mathrm{dit}\) |
| Element gap (dit–dah inside a letter) | 1 | \(T_\mathrm{dit}\) |
| Letter gap | 3 | \(3\,T_\mathrm{dit}\) |
| **Word gap** | **7** | \(7\,T_\mathrm{dit}\) |
| **Hang (break-in)** | **1.5 × word gap = 10.5** | \(1.5 \times 7\,T_\mathrm{dit} = 10.5\,T_\mathrm{dit}\) |

### 3.4 Timelines by WPM — break-in vs non-break-in

#### Morse unit times and hang (break-in)

| WPM | Dit (ms) | Element gap (ms) | Letter gap (ms) | Word gap (ms) | Hang = 1.5× word gap (ms) |
|-----|----------|------------------|-----------------|---------------|---------------------------|
| 10 | 120 | 120 | 360 | 840 | **1260** |
| 20 | 60 | 60 | 180 | 420 | **630** |
| 30 | 40 | 40 | 120 | 280 | **420** |
| 40 | 30 | 30 | 90 | 210 | **315** |

Compare to **hold timeout ~500–600 ms** and **keepalive ~200 ms** (fixed safety
path, same at all WPM): hang at 10–20 WPM is **longer** than one hold timeout;
at 30–40 WPM hang is **shorter**. That is fine — keepalives re-arm hold timeout
during hang; hang is not the safety timer.

#### Break-in CW — example KEY open/close pattern

```text
  KEY:  █ █   █ █ █     █ …     (dits/dahs; opens between elements)
  hold: |---- active, keepalives every ~200 ms ----|
  on open longer than hang (1.5× word gap) → release hold (ttl_ms: 0)
```

#### Non-break-in CW / SSB — continuous KEY

```text
  KEY:  ████████████████████████    (no opens until the operator is done)
  hold: |-- active --|
  on first KEY open → hang = 0 → release hold (ttl_ms: 0) immediately
```

#### Hold active duration (from first KEY assert to release hold)

**Break-in CW** (hang after last element):

| Content (approx. Morse units) | 10 WPM | 20 WPM | 30 WPM | 40 WPM | Then hang (1.5× word) |
|-------------------------------|--------|--------|--------|--------|------------------------|
| Min assess **A** / **N** (5 u) | 0.60 s | 0.30 s | 0.20 s | 0.15 s | + hang table above |
| **QRZ** (37 u) | 4.44 s | 2.22 s | 1.48 s | 1.11 s | + hang |
| **THANK YOU** (85 u) | 10.2 s | 5.10 s | 3.40 s | 2.55 s | + hang |

Hold stays up for **message time + hang**, then **`ttl_ms: 0`**.

**Non-break-in CW / SSB** (same RF duration if continuous KEY for whole text):

| Content | KEY-down duration (same unit totals if continuous) | Hang | Release |
|---------|-----------------------------------------------------|------|---------|
| As short as **QRZ** (~1.1–4.4 s by WPM if it were Morse-length) | One continuous KEY of that length (or whatever the operator holds) | **0** | On KEY open: **`ttl_ms: 0` immediately** |
| **thank you** (~2.6–10 s Morse-equivalent if continuous) | Continuous KEY | **0** | Immediate **`ttl_ms: 0`** |

### 3.5 Assessing KEY type and CW speed

KEYing monitor runs **in parallel** with Hold sender (hold is already on).

**Break-in CW assessment** needs visible structure:

| Need to observe | Why |
|-----------------|-----|
| At least one **dit** | Short mark |
| At least one **dah** | Long mark (~3× dit) |
| At least one **element gap** (KEY open ~1 dit while still in character) | Proves break-in (gaps exist) |

Minimum Morse pattern with dit + gap + dah: **A** (`·-`) or **N** (`-·`) = **5** dit units.

| WPM | Max time to first assess-ready (5 units) |
|-----|------------------------------------------|
| 10 | **600 ms** |
| 20 | **300 ms** |
| 30 | **200 ms** |
| 40 | **150 ms** |

That is well under a full **QRZ** or **thank you**, and under the first
keepalive interval in the slowest case only at 10 WPM (600 ms vs 200 ms
keepalive — assessment can finish after a few keepalives; hold was already
sent at t = 0).

**Non-break-in CW / SSB:** KEY has **no gaps** until the end. Monitor never
sees element gaps → classify continuous → hang = 0 → EOT on first KEY open.

#### Distinguishability (QRZ … thank you)

| Observation during a short call | Break-in CW | Non-break-in CW / SSB |
|--------------------------------|-------------|------------------------|
| KEY opens of ~1–3 dits **during** the send | Yes (many) | **No** |
| KEY open ≥ hang (1.5× word gap) **before** EOT | No (that *is* EOT) | First open **is** EOT |
| Continuous KEY for entire “QRZ” / “thank you” | No | Yes |

Even **QRZ** at 40 WPM (~1.1 s of Morse structure) contains many element and
letter gaps — clearly break-in. A continuous KEY of similar length with **zero**
internal opens is clearly non-break-in / SSB. No reliance on hold timeout for
this classification.

### 3.6 Hold sender ↔ KEYing monitor (including race rules)

```text
  KEY assert
      │
      ├─► Hold sender:  send hold (ttl_ms = hold_timeout_ms) immediately
      │                 schedule keepalives ~200 ms while hold_active
      │
      └─► KEYing monitor: sample KEY edges; classify; measure WPM if break-in
                          decide EOT (hang rules above)
                                │
                                ▼
                          signal Hold sender: END_HOLD
                                │
                                ▼
                          Hold sender: stop keepalives, then send ttl_ms: 0
```

**Release may precede the first keepalive.** Example: non-break-in KEY down
50 ms then up → hold at t = 0, release hold at t ≈ 50 ms; no keepalive sent.

**No race between keepalive and `ttl_ms: 0`:**

1. **Single sender path** — one goroutine/thread/queue owns all UDP hold
   packets (keepalive and release).
2. On **END_HOLD**: set `hold_active = false`, **cancel** pending keepalive
   timer/work, **then** enqueue **release hold** (`ttl_ms: 0`).
3. Keepalive loop must check `hold_active` **before** each send; after
   END_HOLD, no further `ttl_ms > 0` packets.
4. Optional belt-and-suspenders: ignore keepalives after a local release
   generation counter increments.

Never “fire keepalive and release concurrently” on two threads without
ordering. A late keepalive after `ttl_ms: 0` would re-arm the WSJT-X station
hold timeout and look like a stuck hold.

### 3.7 Agent inputs

| Input | Notes |
|-------|--------|
| USB-serial **CTS** (or other pin) | KEY → interface → CTS. This is what **`inhibit-agent`** reads. |
| GPIO / other | Agent-specific |
| Manual / test | `tools/send_inhibit_hold.py --interactive` |

Debounce and **hang** (anti-chatter) live in the agent. The WSJT-X station applies only
**hold timeout** (safety) so a **deadman** cannot leave a hold stuck forever.

### 3.8 Targets

```text
host:port   e.g.  192.168.1.40:51432   (port from InhibitStatus type 17)
```

Unicast UDP is enough for small multi-op. Each WSJT-X station binds its own
ephemeral inhibit port and parses independently. Controllers build the short
destination list (typically ≤3 digi seats on the same band) from type 17.
The gate does not know about bands or fleets.

Multiple agents → same WSJT-X station: **one lease per Controller ID**, combined with
logical **OR**. A release clears **only that controller’s** lease.

---

## 4. UDP protocol (WSJT-X station ↔ agent)

**Transport:** UDP, `NetworkMessage` / `QDataStream` (schema 3), max **512** bytes.  
**Port:** **ephemeral** (WSJT-X station binds; agent sends to the port from type 17).  
**Type:** `NetworkMessage::TxInhibit` = **18** (inbound). Outbound announce/telemetry is
`InhibitStatus` = **17** on the normal UDP Server path (unicast or multicast).

**InhibitStatus cadence:** type **17** is sent when hold/badge/counters change,
when the inhibit listen port binds or clears (Enable TX Inhibit apply / rig
close), and **periodically every `NetworkMessage::pulse` seconds (15 s)** while
TX Inhibit stays enabled. That pulse helps late joiners learn the
**UDP inhibit endpoint**. With an always-ephemeral listen port, type 17 is how
controllers learn `inhibit_port`. Holds still go **unicast** to that
`host:port` — type 17 is discovery and triage, not the hold path.

**Port 0 rules:** a live type 17 never carries port 0 as a KEY-agent target.
Port 0 is only the disable/clear announce (feature off or rig closed).
After `QUdpSocket::bind(0)`, if Qt `localPort()` is still 0, the gate reads
the OS port with `getsockname` once. If the port is still 0, arming fails
(`lineError`, no `portBound(0)`). Windows links `ws2_32` for that path.

Common header (all NetworkMessage types): magic `0xadbccbda`, schema, type, Id (utf8).

| Field | Type | Required | Meaning |
|-------|------|----------|---------|
| Id (target) | utf8 | yes (may be empty) | Target WSJT-X instance key. **Empty** = any instance at this UDP address/port. **Non-empty** must match this instance’s Id or the datagram is ignored. |
| Controller ID | utf8 | yes, **non-empty** | Lease key. Sender may only refresh/release this row. |
| TTL ms | quint32 | yes | Lease lifetime from receipt, or **0** = release **this** controller. Non-zero: 100…30000. Not agent hang. |
| Station | utf8 | recommended | Badge (`held by …`); human-readable. |

### Hold (and keepalive — same shape)

Nonzero TTL upserts that Controller ID’s lease to `now + TTL`. Keepalives are the
same message; they refresh the lease while KEY is asserted or agent hang is running.

### Release hold (normal end)

TTL **0** deletes **only that** Controller ID’s lease. Sent when agent hang finishes
after **release KEY** (or on operator quit). **This is how a healthy agent ends its
lease.** Do not rely on station timeout as the normal path.

### Hold / deadman

If no refresh arrives before a lease’s TTL, that row expires. Hold ends when **no**
leases remain. That is the deadman path for agent crash / path loss.

### Invalid packets

Bad magic/schema, wrong type, empty Controller ID, bad TTL, short/oversize, stream
error: **ignored** (counted as invalid). Legacy JSON is not accepted.

### 4.1 Trust model — read this before exposing the port

**There is no authentication.** The station binds `0.0.0.0:<ephemeral>` and accepts a
hold from any host that can reach that port. That is deliberate for a small trusted
multi-op LAN, where the alternative (keys, pairing, config) buys nothing against the
actual threat.

**What an attacker can do:** stop you transmitting. Sustained suppression needs
sustained packets — a single lease lasts at most its TTL (30 s ceiling, ~600 ms in
practice), so the effect decays as soon as the packets stop.

**What an attacker cannot do: make you transmit.** The protocol has no packet that
causes emission. Every message either starts, refreshes, or ends a *suppression*. The
worst outcome is a station that will not key — inconvenient, and safe in the direction
that matters for an unattended transmitter.

**Operator guidance:**

| Situation | Do |
|---|---|
| Agent on the same PC | Nothing. Loopback is not reachable from outside. |
| Agent on your LAN | Allow inbound UDP to the announced inhibit ports on the **private** profile only (ports change per launch). |
| Station reachable from the internet | **Do not forward** the inhibit listen ports. |
| Shared or untrusted network | Treat "someone can stop my TX" as a real possibility. |

**Id filtering:** non-empty type-18 Id must match this instance. Targeting is still by
unicast destination list managed outside the gate (from type 17, or local config).

---

## 5. Local CTS KEY — not in this build

**Decision:** no CTS in the WSJT-X station binary. **UDP hold only** + RTS/DTR PTT.

**Why:** floating/driven CTS caused intermittent false hold. Worse than
requiring a KEY agent (`inhibit-agent`) on UDP.

Until CTS is opt-in and safe: KEY agent → UDP.

---

## 6. Testing locally

Enable TX Inhibit, RTS/DTR on a real serial port, read the bound port from the
status-bar tooltip (or InhibitStatus type 17), then send the same UDP a KEY
agent would to that **host:port**. Expect red **INHIBIT**; WSJT-X station
does not **assert PTT** while hold is active.

### KEY agent (`inhibit-agent` / `inhibit-agent-gui`)

Standalone KEY agent so TX Inhibit works on a dual-radio seat with only
WSJT-X. USB-serial CTS in, dest `host:port` out (port from type 17 / tooltip).
Design: [INHIBIT_AGENT.md](INHIBIT_AGENT.md).

| Binary | Tree | Notes |
|--------|------|--------|
| **`inhibit-agent`** | this repo | Standalone CLI. CTS KEY `--port` + dest `--addr host:port`. |
| **`inhibit-agent-gui`** | this repo | CTS KEY; dest `host:port` in the window. |

```text
inhibit-agent --port /dev/ttyUSB0 --addr 127.0.0.1:51432
inhibit-agent COM7 192.168.1.40:51432
inhibit-agent-gui
```

Hang policy (default): break-in **1.5× word gap** from measured dit;
continuous KEY (long mark ≥500 ms) **hang = 0**.

### Python: `tools/send_inhibit_hold.py`

Scripted hold / release with no KEY dongle:

```bash
python3 tools/send_inhibit_hold.py --interactive
python3 tools/send_inhibit_hold.py --ttl-ms 3000 --station TEST
python3 tools/send_inhibit_hold.py --ttl-ms 0
```

---

## 7. Code map

| Piece | Location |
|-------|----------|
| Parse, hold timeout, badge, counters (pure) | `TxInhibit/TxInhibitLogic.hpp` |
| UDP listen, hold timeout, want_tx mix | `TxInhibit/TxInhibitGate.{hpp,cpp}` (no serial) |
| Pin filter: `do_ptt` → want_tx → assert PTT / release PTT | `Transceiver/HamlibTransceiver.cpp` |
| Settings **Enable TX Inhibit** (checkbox labels + arm fail) + signals | `Configuration.{hpp,cpp,ui}` |
| Status badge + `InhibitStatus` | `widgets/mainwindow.cpp` |
| MessageClient type 17 | `Network/NetworkMessage.hpp`, `MessageClient` |
| Standalone KEY agent | **`inhibit-agent`** / **`inhibit-agent-gui`** (`tools/inhibit-agent/`); `send_inhibit_hold.py` |

**Maintainer notes (algorithm)**

- Hamlib alone owns serial/CAT and may **assert PTT** / **release PTT** (RTS/DTR).
  TxInhibit never opens a second serial handle.
- With TX Inhibit active, software PTT state follows **want_tx**, not the pin
  reading (poll must not treat “**release PTT** while hold” as “not want_tx”).
- Normal hold end is **release hold** (`ttl_ms: 0`) after agent hang.
  Fail-open: a **deadman** still ends via the WSJT-X station **hold timeout**.
- Agent **hang** is not implemented in the WSJT-X station; WSJT-X station only has **hold timeout**.
- UDP bind / port discovery failure **fails Inhibit arming**. Digi CAT/PTT
  continue. Settings label **TX Inhibit failed (no port)**; status message
  from `lineError`. Controllers that reject port 0 never get a usable target
  from a live type 17.
- Bind recovery: `localPort()` after `bind(0)`; if 0, one `getsockname`; if
  still 0, fail arm (no delayed retry). Never emit `portBound(0)`.
- Test hook: `WSJTX_TX_INHIBIT_FORCE_BIND_FAIL=1` forces arming failure.
- Identifier names in code may still say gate/hold/block/intent; align in a
  later code pass. This document is the language target.
