# JTTY GUI: proposed implementation plan

A companion to `jtty_gui.md`, offered for discussion. It proposes how the guidelines in that document could be implemented, and adds a few suggestions aimed at making JTTY comfortable for operators who already know RTTY, for contesting and for rag-chewing.

## Where things stand (`feat/jtty-new-GUI`)

Phases 1–4 (including 4b, Call next and TU NOW) are implemented and tested;
phase 5 is not started. The
transmit machinery the guidelines describe already existed — what phase 1
replaced was the control layer on top of it.

- **One PTT state.** `F9` turns PTT on and off. On, the Tx box streams out frame by frame and diddle fills any gap; off finishes what is queued, then drops PTT. `Esc` aborts immediately and clears the box. Ctrl+K, Alt+J, Alt+K, Ctrl+H and Ctrl+Shift+K survive as aliases for one beta. The **Enable Tx** button is deliberately not involved — it stays hidden in JTTY, so none of the auto-sequencing behind it is touched.
- **Commits are frame-driven.** Text leaves the box when the audio backlog falls below one frame, not in 5-word batches. The compaction holdback is 0; phase 3a found macro text doesn't need one either (see phase 3).
- **F-keys go through the Tx box (phase 3a).** An F-key expands its macro, appends it to the box, and obeys `%[` / `%]`, so macro text gets the same highlighting as typed text and `Esc` stops both.
- **Macro sets (phase 3b).** Run, S&P, Chat and user sets, 16 macros each (F1–F8, Shift+F1–F8), shown as a 4×4 grid of labelled buttons and edited in a dialog.
- **Keyboard capture (phase 2).** `Esc`, F1–F10 work regardless of focus, in the main window and the Wide Graph; typing anywhere except an editable field goes to the end of the Tx box.
- **Already done and worth keeping:** unlimited message length, gapless chaining of queued messages, yellow sent / gray queued coloring, locking of committed text against edits, and the holdback that keeps the compact encoder from splitting a shorthand.
- **Known gaps:** the Tx echo is segment-granular while transmission is frame-granular (see phase 5), and PTT state shows only in a minimal Rx/Tx/Diddle label.

## Proposed plan

### Phase 1: One PTT model (the core change) — implemented

Shipped as proposed except: **Enable Tx** is not a PTT control (it stays hidden
in JTTY, so `F9` is the only toggle until `%[` / `%]` arrive in phase 3), and
the old shortcuts were kept as aliases rather than retired. Diddle also had to
shrink to one frame and be queued half a frame ahead; see the commit for why.

- **One state replaces three.** "PTT on" means the existing armed live session. **Enable Tx**, `F9` and `%[` all turn it on. With PTT on, the Tx box is streamed out frame by frame, and diddle fills any time it runs empty.
- `%]`, `F9`-while-on, and **EnableTX**-while-on stop a transmission gracefully. Whatever is already in the box
  finishes sending, then PTT drops. `Esc` stops immediately and clears the box. (N1MM and fldigi work the same way: soft stop to receive, hard abort on `Esc`.)
- **Typing with PTT off is type-ahead.** Text waits in the box, still editable, until PTT goes on. Rag-chewers will want this for composing a reply while the other station is still sending.
- **Commit text as each frame is needed, not in 5-word batches.** Today text is committed every 5 words or after a 1.5 s pause. Instead, take text from the box only when the audio queue drops below about one frame (~1.9 s) of audio. Text stays editable as long as possible and latency is lowest.
- **Retire the old shortcuts** (Ctrl+K, Alt+J, Alt+K, Ctrl+H), perhaps keeping them as quiet aliases for one beta.

### Phase 2: Keyboard routing — implemented, awaiting hands-on test

Decisions taken: `F10` jumps to **DX Call** (since phase 4, to the **Call** field that mirrors it) (selecting its contents, so typing
replaces them); the Wide Graph counts as part of the main window; Backspace
away from any field deletes the last *unsent* character of the Tx box, while
arrows, Page Up/Down, Home/End and Tab stay with the focused widget; `Enter`
is unchanged until phase 4. Global keys act only when unmodified, so
Shift+F1, Shift+F6, Ctrl+F11 and similar keep their existing meanings, and
auto-repeat never re-fires F1–F10 or `Esc`. Keys in child dialogs (Log QSO,
message boxes) are never routed. Typing from elsewhere is appended to the Tx
box without moving focus there.

- **Move JTTY key routing** into its own small, unit-testable class (e.g. `widgets/JttyKeyRouter.{hpp,cpp}`), following the `DecodeOutputPlan` precedent.
- **Fields that need typing keep their keystrokes.** "Typing anywhere goes to the Tx box" must exempt **DX Call**, **DX Grid**, the serial-number box, spin boxes and the macro editor. Proposed rule: if focus is in an editable field, that field keeps the key; otherwise printable keys go to the end of the Tx box.
- **Global keys work regardless of focus:** `F1` – `F8`, `F9`, `Esc`, and whatever key (perhaps `Tab` or `F10`?) is used to initiate entering a to-be-worked **Dx Call** (Phase 4).
- Existing bindings to respect: `F11`/`F12` for Rx frequency step — useful for tuning.
- **Shift+F11 / Shift+F12** step the Tx frequency down / up by 150 Hz in
  JTTY (60 Hz in other modes), from any focus and from the Wide Graph.

### Phase 3: Macros

Split into 3a (F-keys through the Tx box) and 3b (macro sets and editor),
both implemented and tested.

**Compactness check (done).** All 15 native forms, in each of the None, RTTY
Roundup and Field Day exchange profiles, take exactly as many frames when
their expanded text is encoded in one piece as they do as native atoms. Tones
are identical in 33 of the 45 cases; the other 12 (`%E` and `599 %N` forms)
send the exchange as plain text rather than a structured atom, at the same
frame count. Committed one word at a time, though, a macro costs up to 2.5×
the frames (`%H TU CQ %M CQ`: 2 whole, 5 split), so a macro must be committed
in one piece. Appending the whole expanded macro, with a trailing space, in a
single step guarantees that; no holdback is needed.

- *To revisit:* decoding is not affected by sending exchanges as plain text
  rather than structured atoms, so we accept it for now. We might reconsider
  later — David (`dchristle`) to comment.

**3a — F-keys through the Tx box (implemented)**
- **`%[` and `%]`** turn PTT on and off, through the same `setJttyPttEnabled()`
  `F9` uses. They apply to the whole macro wherever they appear: the text is
  appended first, then `%[` keys up, then `%]` stops gracefully (PTT off
  already sends everything in the box before dropping). A macro with neither
  just queues as type-ahead. A typical run-mode exchange is `%[%H %E%]`.
- **An F-key expands its macro when pressed** and appends the result plus a
  trailing space to the Tx box, so the operator sees exactly what will go out
  and can still edit it until it is sent.
- **A macro using `%H` or `%Q` is refused while DX Call is empty**, with a
  red status-bar warning (`showStatusWarning()`), rather than sending an
  exchange without a call (the
  native path used to refuse these).
- The separate native-atom F-key path is gone from the GUI. The N1MM/MMTTY
  route keeps its own native compilation and is unaffected.
- Contest-set defaults became the Run set (superseded by 3b's macro sets).
- The serial-number bump still keys on a sent message starting with `TU `;
  phase 4's `%L` replaces that heuristic.
- Tests: `test_jtty_messages` checks `%[` / `%]` parsing;
  `test_jtty_macro_sets` checks that every Run macro costs no more frames as
  text than as native atoms.

**3b — Macro sets and editor (done)**
- **Model:** `widgets/JttyMacroSets` (`Jtty::MacroLibrary`) holds the sets,
  saved under `JttyMacroSets` in the settings. Built-in sets can be edited and
  restored to defaults but not renamed or deleted, and are always present;
  user sets can be copied from any set, renamed and deleted. The old
  `JTTY_msgN`, `JTTY_FT8_msgN`, `JTTY_Run_msgN` and `JTTY_MessageStyle` keys
  are removed on save.
- **Main panel:** the eight line edits became Shift-bank buttons, giving a
  4×4 grid (F1–F4, Shift+F1–F4, F5–F8, Shift+F5–F8). Each button shows its
  key and label, its tooltip shows the macro, and empty keys are disabled.
  The combo selects the set; **Edit macros...** opens the editor.
- **Editor** (`widgets/JttyMacroEditor`): set chooser with New / Rename /
  Delete / Restore defaults, a 16-row Key / Label / Macro table, and a
  token reference line. It edits a copy; Cancel discards everything, OK
  adopts it with the set last shown as current.
- The key router's focus check now treats an editable combo whose line edit
  is read-only (like the macro-set combo) as not taking typing.
- **Macro sets:** Run, S&P and Chat, plus user-defined sets, chosen from a
  combo box that replaces Contest/FT8. The FT8-style placeholders are
  dropped; anyone who wants them can make a user set. Existing saved macros
  are simply overwritten — there have been no JTTY contests yet.
- **Shift+F1–F8 as a second bank**, 16 macros per set. In JTTY this overrides
  Shift+F1 (copyright notice) and Shift+F6 (decode remaining files); WAV
  testing in JTTY uses Ctrl+X and Alt+F6 instead anyway.
- **A macro editor dialog with user-editable button labels**, replacing the
  eight always-visible line edits and freeing space on the main panel.
- Proposed defaults, to be tried:
  - **Run:** F1 `%[CQ %M CQ%]`, F2 `%[%H %E%]`, F3 `%[%H TU CQ %M CQ%]`,
    F4 `%[%M%]`, F5 `%[%H%]`, F6 `%[TU NOW %Q %E%]`, F7 `%[%H AGN?%]`,
    F8 `%[%E%]`.
  - **S&P:** F1 `%[%M%]`, F2 `%[%H %E%]`, F3 `%[TU %M%]`, F4 `%[%M%]`,
    F5 `%[%H%]`, F7 `%[AGN?%]`, F8 `%[%E%]`.
  - **Chat:** F1 `%[CQ CQ CQ DE %M %M K%]`, F2 `%[%H DE %M ` (opens an over,
    PTT stays on for typing), F3 `BTU %H DE %M K%]`, F4 `%H DE %M SK%]`.

### Phase 4: Contest workflow — implemented

**As implemented:**
- **Enter Sends Message.** A checkbox on the JTTY panel (on by default,
  saved) turns it on. Each macro set says what Enter sends (editor:
  "Enter sends"):
  - Run: no DX call, F1 (CQ); then F2 (call + exchange); then F3 (TU, log).
  - S&P: F4 (my call); then F2 (exchange); then F3 (TU, log).
  - Send message (Chat): Enter in the Tx box, or anywhere typing goes to
    it, acts like the Send message button: with PTT off it sends the box,
    with PTT on it ends the over. Enter in DX Call keeps its usual meaning.

  The step follows whichever macro key was last sent, however it was sent
  (Enter, F-key or button), and resets when DX Call changes. After logging,
  Run's Enter goes back to CQ; S&P's does nothing until a new DX Call. Enter
  works from the Tx box, DX Call, or anywhere that isn't a text field; Enter
  in DX Call also moves focus back to the Tx box.
- **`%L`** logs the QSO when the key is pressed, as the Log QSO button does
  (so the "prompt to log" and "auto log" settings apply), records the
  exchange sent, and bumps the serial number. It replaces the old rule that
  logged whenever a transmission began with "TU ". Run F3 and S&P F3 now end
  in `%L`. With DX Call empty it is refused, like `%H`.
- **Call, Sent and Rcvd fields** sit under the QSO Frequency pane, in
  the row with **Lower case**, shown only when the macro set's Enter role is
  Run or S&P. Call mirrors DX Call (either can be edited), and `F10` jumps
  to it (to DX Call when the fields are hidden). Sent is filled in as `%E`
  expands (no 599: it carries no information), and refreshes when the
  serial number, call or settings change; it can be edited. Rcvd (received) holds
  the exchange received and clears when the call changes. `%L` logs Sent
  and Rcvd as shown, with 599 added in front for the log except for
  Field Day. Enter in any of these fields sends, like Enter in
  DX Call.
- **Click a callsign:** double-click sets the call (and its SNR), as before.
  Shift+double-click adds the clicked word to Rcvd.
- **Call next and TU NOW (phase 4b).** Ctrl+double-click a callsign to put
  it in **Call next** (`jttyNextCallEntry`), which `%Q` now sends (it used
  to repeat the DX call). A macro with both `%Q` and `%L`, such as Run F6
  `%[TU NOW %Q %E%L%]`, logs the current QSO, sends the exchange with the
  *next* serial number, moves Call next into Call and clears it, and leaves
  Enter ready to send TU. `%Q` with Call next empty is refused.
- **Include Time is gone:** decode lines always start with the UTC time.
- **N1MM / MMTTY:** untouched. That path never uses the macro sets, and key
  routing is limited to the main window and the Wide Graph.

**As proposed:**

- **Click a callsign in a receive pane to make it the DX Call (`%H`).** Shift-click could grab the received exchange.
- **Enter sends the next logical message** (like N1MM's "Enter Sends Message"):
  - Run, no call entered: CQ.
  - Run, call entered: exchange.
  - Run, exchange received: TU and log.
  - S&P: my call, then the exchange.
- **New logging macro`%L`** logs the QSO and bumps the serial number, enabling `TU %M%L%]`.
- **Keep the N1MM / MMTTY interface path working** — many contesters will drive JTTY from N1MM, and the new key handling must not disturb it.

### Phase 5: Display

**Echo (implemented, awaiting hands-on test).** Transmitted text is echoed
a frame at a time, as each frame finishes sending, into one growing line
per over (PTT on to PTT off) in the QSO Frequency pane; diddle is left
out. `Esc` leaves exactly the frames that went out. The whole over goes to
ALL.TXT as one line when PTT drops; the N1MM echo is still per segment.
Code: `Jtty::sentChars()` and `Jtty::TransmitEcho` in
`widgets/JttyTransmitEcho.hpp` (tests in `test_jtty_transmit_queue`),
used by `updateJttyTransmitDisplay()` / `endJttyTxEcho()`.

**PTT indicator (implemented, awaiting hands-on test).** The label above
Send message is now large (14 pt bold) and colour-coded: **Rx** green,
**Tx** red, **Diddle** orange, and **Tx → Rx** pink while queued text
finishes after PTT off or during a one-shot send
(`updateJttyPttStateLabel()`).

**Recall (implemented, awaiting hands-on test).** Ctrl+Up / Ctrl+Down, from
the Tx box or anywhere typing goes to it, step through the last 20 overs
like a shell history, replacing the unsent part of the Tx box; stepping
down past the newest restores the draft. Any edit ends the recall.
Code: `Jtty::TransmitHistory` in `widgets/JttyTransmitHistory.hpp`,
`recallJttyTransmission()`; overs are added by `endJttyTxEcho()`.

**Tx box states:** already correct (sent highlighted, committed gray and
locked, editable plain), stepping a frame at a time like the echo.
**Sent text clears at PTT drop (implemented, awaiting hands-on test).**
When an over finishes and PTT drops, its text leaves the Tx box (it is in
the QSO pane and in recall); unsent type-ahead stays. Esc still clears all.
Code: `clearJttySentText()`, called from `finishJttyDrain()`.

**Font size (implemented, awaiting hands-on test).** The base is Settings →
General → Decoded Text Font (both decode panes and the Tx box). In JTTY,
Ctrl+= (or Ctrl++) and Ctrl+- step it a point at a time and Ctrl+0
restores it; the offset is saved as `JTTY_FontZoom` and other modes keep
the base size. Code: `zoomJttyFont()`, `jttyDisplayFont()`; key route
`ZoomFont`.

- A large Tx / Rx / Diddle indicator.
- Clear text states in the Tx box: sent (yellow), committed (gray, locked), editable (plain).
- Transmitted text echoed into the QSO Frequency pane in its own color, as RTTY programs do.
  - Phase 1's frame-driven commits make this more visible: each commit is its own message, so a slowly typed over is echoed as one line per ~1.9 s frame rather than one line per over. Only the local echo fragments — the frames chain gaplessly and `jtty_mdecode.f90` stitches them, so the receiving station sees continuous text. Accumulating the echo and emitting it when PTT drops would fix it.
  - The echo is also still segment-granular (80 characters) while transmission is frame-granular (~1.9 s). Phase 1 moved it from segment start to segment completion, so aborted text is no longer echoed or written to ALL.TXT as sent; the remaining gap is the reverse, that frames sent before an `Esc` go unrecorded locally even though the far end decodes them. Echoing per frame via `segment.frameCharStarts` (already captured for the highlight split) closes both, and could reuse the growing-message display the receive side uses.
- Rag-chew comforts: adjustable font size for Rx and Tx panes; a recallable history of recent transmissions (e.g. Ctrl+Up).

### Testing

- Unit tests under `tests/unit/` for the key router and for `%[`, `%]`, `%L` expansion.
- The existing `JttyTxLoopbackTestController` for end-to-end checks of PTT on, diddle, `%]` and `Esc`.
- **Important:** Hands-on testing of a built executable before any PR, since much of this is about how the interface feels.

### Suggested order

Phases 1–3 together on `feat/jtty-new-GUI`; Phase 4 separately afterwards; Phase 5 items wherever they fit.

## Open questions

1. ~~**Esc:** clear the whole box, or only the unsent part?~~ Resolved: clears the whole box.
2. ~~**Enter:** drive the "Enter Sends Message" sequence, or leave that for a later version?~~ Resolved: implemented in phase 4, with a checkbox to turn it off.
3. ~~**Old shortcuts:** remove them now, or keep as aliases for one beta?~~ Resolved: kept as aliases for one beta.

## Appendix: code map for implementers

- `widgets/mainwindow_jtty.cpp` — JTTY GUI logic:
  - Keys: `jtty_key_struck()` asks `Jtty::routeKey()` what to do with each
    key press and acts on it (`appendJttyTxBoxText()`,
    `backspaceJttyTxBox()`, `focusJttyDxCall()`, `setJttyPttEnabled()`,
    `sendJttyFunctionKey()`, `clearJttyLiveEntry()`). Enter goes to
    `jttyEnterSendsMessage()` (step in `m_jttyEnterStep`, rules in
    `Jtty::enterSendsKey()` / `stepAfterKey()`).
    `installJttyWideGraphKeyFilter()` routes the Wide Graph's keys the same
    way.
  - PTT (phase 1): `setJttyPttEnabled()` is the single on/off entry point;
    `maybeCommitJttyLiveEntry()` commits text when the backlog
    (`jttyQueuedSamples()`) drops below a frame; `maybeTopUpJttyFiller()` /
    `jttyPendingIsOnlyFiller()` keep diddle half a frame ahead;
    `idleFlushJttyLiveEntry()` releases a trailing unterminated word after a
    pause; `updateJttyPttStateLabel()` shows Rx/Tx/Diddle.
  - Phase-1 aliases: `commitJttyLiveEntry()` (Ctrl+K),
    `startJttyLiveStream()` (Alt+J), `insertJttyChatSignoff()` (Alt+K),
    `insertJttyChatOpener()` (Ctrl+H), `clearJttyLiveEntry()` (Ctrl+Shift+K,
    and `Esc`).
  - Tx box display: `applyJttyLiveEntryFormatting()`,
    `guardJttyLiveEntryLock()`, `updateJttyLiveEntryFrameLabel()`;
    `m_jttyLiveEntryCommitted` is the length of the locked prefix.
  - Transmit queue: `commitJttyLiveEntryPlan()`, `execute_jtty_tx()`,
    `feedJttyTransmitQueue()`, `onJttyBackendProgress()`,
    `finishJttyDrain()`, `abort_jtty_tx()`.
  - Macros: `sendJttyFunctionKey()` (parse `%[`/`%]`/`%L` with
    `Jtty::parseMacroControl()`, expand, append to the box, toggle PTT;
    `%L` calls `logJttyQso()`),
    `jtty_msg_expand()`,
    `on_comboBoxJttyStyle_currentIndexChanged()` (select a set),
    `refreshJttyMacroControls()` (combo items, button labels and tooltips);
    `on_pbEditJttyMacros_clicked()` in `mainwindow_slots.cpp` runs the
    editor.
- `widgets/JttyMacroSets.{hpp,cpp}` — `Jtty::MacroLibrary`, default sets,
  settings load/save; tests in `tests/unit/jtty/test_jtty_macro_sets.cpp`.
- `widgets/JttyMacroEditor.{hpp,cpp}` — the macro editor dialog.
- `widgets/JttyKeyRouter.{hpp,cpp}` — pure key-routing rules
  (`classifyKeyFocus()`, `routeKey()`); unit tests in
  `tests/unit/jtty/test_jtty_key_router.cpp`.
- `widgets/mainwindow.cpp` — `eventFilter`'s `QEvent::KeyPress` case calls
  `jtty_key_struck()` first (the filter is installed on every main-window
  child), then handles the Tx_Message alias shortcuts; `keyPressEvent` also
  calls it as a fallback; `on_actionJTTY_triggered()` sets up the mode;
  `WSPR_config()` selects the JTTY controls page (`controls_stack_widget`
  index 1).
- `widgets/mainwindow.ui` — JTTY page `page_4`: `pbF1`–`pbF8`, `pbSF1`–`pbSF8`,
  `comboBoxJttyStyle`, `pbEditJttyMacros`, `cbJttyEnterSends`, `jttyNextCallEntry`, `sbSerialNumber_2`, `pbSendMessage`,
  `labelJttyFrameCount`, `Tx_Message`. Under the decode panes (JTTY only):
  `cbLowerCase` and `jttyQsoFieldsWidget` (`jttyCallEntry`, `jttySentEntry`, `jttyReceivedEntry`).
- `widgets/JttyMessages.hpp` — filler text (`jttyFillerText`), `parseMacroControl()`, macro
  templates (`nativeMacroTemplate`), macro context.
- `widgets/JttyLiveEntry.hpp` — `planIncrementalCommit()`,
  `maxCompactAtomWords` (3).
- `lib/jtty/JttyTransmit.{hpp,cpp}` — encoder: `NativeForm` list,
  `expandLiteralMacro`, `compileNativeMacro`, `encodeTransmitText`; text
  compaction lookahead is `try_compact()` in `lib/jtty/jtty_mod.f90`.
- One frame = 59 symbols (59×1536 samples at 48 kHz Tx, 59×384 at 12 kHz Rx),
  about 1.9 s.
