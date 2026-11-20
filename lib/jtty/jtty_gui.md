# User interface for JTTY mode

This brief document offers a straw-man plan for a
new, user-friendly operator interface for JTTY mode in *WSJT-X*. It does *not* describe the way the program operates now (version 3.3.0-beta5). Rather, it's a set of guidelines for how the program *might* operate with new, yet-to-be-written GUI code. Critiques,
suggestions for improvement, and complete alternative plans are requested and
strongly encouraged! Please submit any such contributions to this Discussion.

JTTY mode manages PTT, keyboard input, message transmission, and display of decoded messages through a
combination of GUI controls, function-key macros, and buffer
management code. Most operator activity can be managed without removing hands from the keyboard.

When screen focus is anywhere in the *WSJT-X* main window, keyboard
input is processed in a dedicated C++ function -- perhaps `jtty_key_struck()` in `mainwindow-jtty.cpp`, or a new function called from there.  Normal text
input (letters, digits, and punctuation) is sent to the **Tx message** entry field where it becomes a queued, editable message. JTTY-specific keyboard shortcuts are defined for a number of special keys or key combinations, and these may be acted on immediately when that key is struck.  

PTT is asserted or released by toggling the **Enable TX** button or an
equivalent function key, say `F9`, or by execution of macros `%[` or `%]` to assert or release PTT. A transmission starts immediately after PTT is asserted. Text present in the **Tx message** buffer is encoded sequentially, with a small look-ahead that enables recognition of shorthands defined in the JTTY grammar, and submitted frame-by-frame for transmission. Text already sent is highlighted with yellow background, also on a frame-by-frame basis. If the **Tx message** buffer is (or becomes) empty, diddle characters are sent. The end-of-message macro `%]` terminates a transmission and returns PTT to its Rx state. The `ESC` key immediately terminates a transmission (if one is in
progress) and erases the **Tx message** field. There is no limit to message length; messages requiring more than 16 frames are queued and sent sequentially, with no gaps.

Function keys `F1` - `F8` hold programmable macros, and default sets of macro definitions are provided for contesting and general chat-style QSOs. Additional macro sets can be defined by the user.

