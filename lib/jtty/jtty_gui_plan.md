# JTTY GUI: proposed implementation plan

A companion to `jtty_gui.md`, offered for discussion. It proposes how the
guidelines in that document could be implemented, and adds a few suggestions
aimed at making JTTY comfortable for operators who already know RTTY, for
contesting and for rag-chewing.

## Where things stand (3.3.0-beta5 / `develop`)

Most of the transmit machinery the guidelines describe already exists. What
needs replacing is the control layer on top of it.

- **Three ways to transmit.** Ctrl+K sends the Tx box once. Alt+J starts a
  live session that sends as you type and fills pauses with `<<<<< <<<<<`
  (effectively the diddle). Alt+K sends a sign-off and ends the session. An
  RTTY operator has to learn a model their other programs don't use.
- **F-keys skip the Tx box.** F1–F8 encode the macro directly and queue it,
  so macro text never appears in the box and never gets the yellow "sent"
  highlighting.
- **Keyboard capture is partial.** Only Esc and F1–F8 work from anywhere in
  the main window. Typing reaches the Tx box only when it already has focus.
- **Already done and worth keeping:** unlimited message length, gapless
  chaining of queued messages, yellow sent / gray queued colouring, locking of
  committed text against edits, and a 3-word holdback so the compact encoder
  never splits a shorthand.

## Proposed plan

### Phase 1: One PTT model (the core change)

- **One state replaces three.** "PTT on" means today's armed live session.
  **Enable Tx**, F9 and `%[` all turn it on. With PTT on, the Tx box is
  streamed out frame by frame, and diddle fills any time it runs empty.
- **`%]` and F9-while-on stop gracefully.** Whatever is already in the box
  finishes sending, then PTT drops. **Esc** stops immediately and clears the
  box. (fldigi and N1MM work the same way: soft stop to receive, hard abort on
  Esc.)
- **Typing with PTT off is type-ahead.** Text waits in the box, still
  editable, until PTT goes on — useful for composing a reply while the other
  station is still sending.
- **Commit text as each frame is needed, not in 5-word batches.** Today text
  is committed every 5 words or after a 1.5 s pause. Instead, take text from
  the box only when the audio queue drops below about one frame (~1.9 s) of
  audio. Text stays editable as long as possible and latency is lowest.
- **Retire the old shortcuts** (Ctrl+K, Alt+J, Alt+K, Ctrl+H), perhaps keeping
  them as quiet aliases for one beta.

### Phase 2: Keyboard routing

- Move JTTY key routing into its own small, unit-testable class (e.g.
  `widgets/JttyKeyRouter.{hpp,cpp}`), following the `DecodeOutputPlan`
  precedent.
- **Fields that need typing keep their keystrokes.** "Typing anywhere goes to
  the Tx box" must exempt DX Call, DX Grid, the serial-number box, spin boxes
  and the macro editor. Proposed rule: if focus is in an editable field, that
  field keeps the key; otherwise printable keys go to the end of the Tx box.
- **Global keys work regardless of focus:** F1–F8, F9, Esc, and whatever key
  enters a typed call (Phase 4).
- Existing bindings to respect: F11/F12 (Rx frequency step — useful for
  tuning), F10 (Fast Graph). F9 appears to be free.

### Phase 3: Macros

- **Add `%[` and `%]`** to the macro language. A typical run-mode exchange
  becomes `%[%H 599 %N%]`.
- **F-keys insert their text into the Tx box** rather than queuing it
  separately, so macro and typed text get the same highlighting and both can
  be stopped with Esc.
  - *To verify first:* F-keys now use dedicated compact native encodings
    (`NativeForm::CallExchange` etc.). Confirm that the typed-text encoder,
    with its 3-word lookahead, produces equally compact frames for the same
    text. If not, macro text in the box must carry a marker so it keeps its
    compact encoding.
- **Macro sets:** Run, S&P and Chat by default, plus user-defined sets,
  replacing the present Contest/FT8 combo box. Shift+F1–F8 could give a second
  bank, as in MMTTY.
- **A macro editor dialog with user-editable button labels**, replacing the
  eight always-visible line edits and freeing space on the main panel.

### Phase 4: Contest workflow (additional suggestions)

- **Click a callsign in a receive pane to make it the DX Call (`%H`).**
  Shift-click could grab the received exchange.
- **Enter sends the next logical message** (like N1MM's "Enter Sends
  Message"):
  - Run, no call entered: CQ.
  - Run, call entered: exchange.
  - Run, exchange received: TU and log.
  - S&P: my call, then the exchange.
- **A logging macro** (e.g. `%L`) that logs the QSO and bumps the serial
  number, enabling `TU %M%L%]`.
- **Keep the N1MM/MMTTY interface path working** — many contesters will drive
  JTTY from N1MM, and the new key handling must not disturb it.

### Phase 5: Display

- A large TX / RX / DIDDLE indicator.
- Clear text states in the Tx box: sent (yellow), committed (gray, locked),
  editable (plain).
- Transmitted text echoed into the QSO Frequency pane in its own colour.
- Rag-chew comforts: adjustable font size for receive and Tx panes; a
  recallable history of recent transmissions (e.g. Ctrl+Up).

### Testing

- Unit tests under `tests/unit/` for the key router and for `%[`, `%]`, `%L`
  expansion.
- The existing `JttyTxLoopbackTestController` for end-to-end checks of PTT
  on, diddle, `%]` and Esc.
- Hands-on testing of a built executable before any PR, since much of this is
  about how the interface feels.

### Suggested order

Phases 1–3 together on `feat/jtty-new-GUI`; Phase 4 separately afterwards;
Phase 5 items wherever they fit.

## Open questions

1. **F9 while transmitting:** stop gracefully after the box empties
   (recommended), or immediately?
2. **Esc:** clear the whole box, or only the unsent part (keeping a visible
   record of what went out)?
3. **Enter:** drive the "Enter Sends Message" sequence, or leave that for a
   later version?
4. **Old shortcuts:** remove now, or keep as aliases for one beta?

## Appendix: code map for implementers

- `widgets/mainwindow_jtty.cpp` — JTTY GUI logic:
  - `jtty_key_struck()` (Esc, F1–F8), `sendJttyFunctionKey()` (native macro
    compile and direct enqueue), `jtty_msg_expand()`.
  - Live entry: `commitJttyLiveEntry()` (Ctrl+K), `startJttyLiveStream()`
    (Alt+J), `insertJttyChatSignoff()` (Alt+K), `insertJttyChatOpener()`
    (Ctrl+H), `clearJttyLiveEntry()` (Ctrl+Shift+K),
    `autoAdvanceJttyLiveEntry()` / `idleFlushJttyLiveEntry()` (5-word batch /
    1.5 s idle commits), `commitJttyLiveEntryPlan()`.
  - Display: `applyJttyLiveEntryFormatting()`, `guardJttyLiveEntryLock()`,
    `updateJttyLiveEntryFrameLabel()`.
  - Transmit queue: `execute_jtty_tx()`, `feedJttyTransmitQueue()`,
    `onJttyBackendProgress()`, `finishJttyDrain()` (keeps an armed session
    filler-padded), `abort_jtty_tx()`.
  - `on_comboBoxJttyStyle_currentIndexChanged()` — Contest/FT8 macro set
    swap via QSettings.
- `widgets/mainwindow.cpp` — `keyPressEvent` calls `jtty_key_struck()`; the
  Tx_Message shortcuts (Ctrl+K, Alt+J, Alt+K, Ctrl+H, undo/redo block) live in
  `eventFilter`'s `QEvent::KeyPress` case; `on_actionJTTY_triggered()` sets up
  the mode; `WSPR_config()` selects JTTY controls page
  (`controls_stack_widget` index 1).
- `widgets/mainwindow.ui` — JTTY page `page_4`: `msg1`–`msg8`, `pbF1`–`pbF8`,
  `comboBoxJttyStyle`, `sbSerialNumber_2`, `pbSendMessage`,
  `labelJttyFrameCount`, `Tx_Message`.
- `widgets/JttyMessages.hpp` — filler text (`jttyFillerText`), macro
  templates (`nativeMacroTemplate`, `ft8StyleMacroTemplate`), macro context.
- `widgets/JttyLiveEntry.hpp` — `planIncrementalCommit()`,
  `maxCompactAtomWords` (3).
- `lib/jtty/JttyTransmit.{hpp,cpp}` — encoder: `NativeForm` list,
  `expandLiteralMacro`, `compileNativeMacro`, `encodeTransmitText`; text
  compaction lookahead is `try_compact()` in `lib/jtty/jtty_mod.f90`.
- One frame = 59 symbols (59×1536 samples at 48 kHz Tx, 59×384 at 12 kHz Rx),
  about 1.9 s.
