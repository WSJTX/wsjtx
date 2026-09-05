# JTTY transmit manual checks

These checks cover MainWindow, PTT, and backend behavior that is outside the
FIFO unit-test boundary.

- Soundcard: send one JTTY message and verify the full tail plays before PTT drops.
- Soundcard: queue two or three messages rapidly and verify playback is contiguous with PTT held.
- Soundcard: press Stop during playback and verify queued audio is abandoned before PTT drops.
- Soundcard: press Esc before PTT comes up and during playback and verify clean aborts.
- Soundcard: send again after a natural drain and verify a fresh transmit session starts.
- TCI, where available: repeat the same checks and watch for backend enqueue failure warnings.
- Native macros: activate each default JTTY F1-F8 template by keyboard and by
  clicking its button; verify both paths show the same canonical message and
  enqueue the same number of frames.
- Native macros: enter a valid call and serial, send a call-plus-exchange
  template, and verify EOM appears only on the final atom.
- Native macros: make a recognized template's runtime call or serial invalid
  and verify transmission is rejected rather than sent as literal text.
- Literal fallback: customize a function-key template so it no longer matches
  a native form and verify its expanded text uses only TEXT5 frames.
- External text: send call-looking and `599`-prefixed strings from N1MM/MMTTY
  and verify they remain literal TEXT5 messages. Send a complete registered
  control phrase and verify it uses one CONTROL frame.
- Queueing: queue two native messages and verify no visible or transmitted
  TEXT5 spacing frame is inserted between them.
- Profile exchange: exercise `%E` with default serial, FIELD_DAY class/section,
  and both RTTY serial and state/province payloads; verify canonical rendering
  and rejection of a payload from the wrong profile.
- Grid macros: verify `%G` renders field-only GRID4 and exact `599 %G` renders
  full-role GRID4; reject malformed locators.
- Settings migration: load unchanged old F2, F6, and F8 defaults using `%N`
  and verify migration to `%E`; verify any edited variant remains literal.
- Windows/N1MM: load `JTTY Messages.mc`, exercise every tagged Run and S&P
  action, and verify the `[[JTTY:ACTION]]` marker is consumed rather than
  transmitted. Reject malformed tags, invalid profile payloads, and unknown
  actions; verify untagged TXTEXT remains literal. Verify an all-rejected
  transaction completes at OFF, and a rejected action followed by a valid
  action still starts and completes only after the accepted audio drains.
- Windows/N1MM: in a two-radio configuration, verify entry-window focus, `!`
  and `{CALL}` expansion, queued-call selection, `{LOGTHENPOP}`, and Run/S&P
  state changes. Before adding a Run F11 call-stacking macro, capture its
  TXTEXT ordering and verify that the leading tag, callsign, and exchange all
  refer to the intended queued station.
