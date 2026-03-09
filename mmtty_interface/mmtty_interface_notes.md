# MMTTY Interface Notes

## Purpose
The purpose of this work is to be the bridge between the WSJT-X application suite and N1MM Logger+, by **emulating** the MMTTY interface which uses Windows messages. This allows WSJT-X to handle the RTTY encoding/decoding while presenting itself to N1MM in the same way the original MMTTY engine would.

## Resources
- **MMTTY source code**: [https://github.com/n5ac/mmtty](https://github.com/n5ac/mmtty)
- **MMTTY Remote Control Specification**: [eremote.txt](https://raw.githubusercontent.com/n5ac/mmtty/master/eremote.txt) (Written by Makoto Mori JE3HHT, translated by JA7UDE Nobuyuki Oba)
- internal documentation/headers: `MMTTYIF.hpp` and `MMTTY_Messages.hpp`.
- using mmtty_interface/mmtty_interface.exe to watch the windows messages being sent and received between N1MM Logger+

## How It Works

### Architecture and Invocation
When N1MM Logger+ attempts to use MMTTY, instead of launching the real MMTTY RTTY engine, it launches wsjtx_jtty.exe with the command line arguments -r and -h<hwnd> where hwnd is a hexadecimal value of the N1MM Logger+ window handle. `mmtty_interface.exe`. 
The wsjtx_jtty.exe executable is a wrapper designed to receive the command-line arguments N1MM expects and then starts wsjt-x with the 'jtty' configuration profile, passing the command line arguments to wsjt-x.
While originally written as a batch file, N1MM Logger+ sets the current directory to it's own, making the batch file unable to find the wsjt-x executable. 


### Message Passing
The interface relies heavily on the legacy Windows Messaging system (`PostMessageA` / `RegisterWindowMessage`). 
Both WSJT-X (via the `MMTTYIF` class) and N1MM Logger+ register a custom system-wide message string: `"MMTTY"`.

1. **Initialization**: When N1MM invokes `mmtty_interface.exe`, it passes `-h<N1MM_hwnd>`. N1MM expects to hear back from MMTTY on this handle.
2. **Handle Exchange**: The `MMTTYIF` class inside WSJT-X broadcasts or directly posts `TXM_THREAD`, `TXM_HANDLE`, and `TXM_START` to alert N1MM that the "engine" is alive and running, providing its own window handle.
3. Once N1MM receives `TXM_HANDLE`, it acknowledges via `RXM_HANDLE`. At this point, the two applications pass messages directly without broadcasting.

### Data Flow (Transmission)
When N1MM Logger+ wants to transmit a macro or a typed character, it sends `RXM_PTT` and `RXM_CHAR` messages to WSJT-X. WSJT-X sends sequences of characters, while MMTTY is designed for character-at-a-time transmission. It is observed that using keyboard macros in N1MM Logger+ sends messages as quickly as possible, with no delay between characters. JTTY sends more than one character per transmission, so the characters are buffered in the MMTTYIF class to handle the delay between character queueing and actual transmission.  Using the control-K command in N1MM Logger+ with typed messages does not work as well as of yet.

MMTTYIF echos the sequence of sent characters back to N1MM Logger+ via `TXM_CHAR` messages; they are sent as one group. Future work could include the timed sending of these characters to N1MM Logger+ to simulate RTTY, however this would not represent the reality of over-the-air transmission.

- **PTT Handling**: `RXM_PTT` triggers the `app_ptt_on` and `app_ptt_off` signals within WSJT-X to engage the transceiver and the JTTY modulator.
- **Character Reception**: `RXM_CHAR` messages carry the ASCII characters N1MM wants to transmit. These are buffered inside `MMTTYIF`. When the TX buffer timer expires, the characters are sent to the JTTY routines in WSJT-X to be modulated to audio and transmitted over the air.

Conversely, when WSJT-X completes transmitting a char or receives data off the air, it calls `echo_tx_message_to_n1mm` or its receiving equivalent.
1. `echo_tx_message_to_n1mm` iterates over the string and calls `app_rx_char`.
2. `app_rx_char` posts a `TXM_CHAR` window message to N1MM with the `lParam` set to the ASCII character value.
3. N1MM displays these characters in its RX/TX windows as if they came from MMTTY.

### PTT and Timers
- **TX Buffer Timer**: There is a configurable TX buffer delay (`m_txDelayMs`, defaulting to 40ms) designed to allow FIFO stuffing of `RXM_CHAR` commands from N1MM. This timer ensures characters from N1MM are buffered gracefully before triggering the PTT sequence in WSJT-X.
- **Inactivity Timeout**: The interface includes a 7-second auto-termination `m_inactivityTimer`, activated on startup. If N1MM Logger+ isn't responsive or closed without sending an `RXM_EXIT` sequence, WSJT-X handles the IPC teardown cleanly.

## Future Work
- Refine the PTT turnaround times to minimize latency between the TX command and MMTTY's actual carrier generation.
- Add additional queueing in MMTTYIF to handle characters received from N1MM logger while the previous messages are being sent.
- Expand support for shared memory (`COMARRAY`) to parse FFT / spectrum data from MMTTY dynamically into the WSJT-X waterfall, if desired, rather than just handling text/PTT control.
- Improve cross-platform abstractions to ensure that if `mmtty_interface` components are built on Linux/macOS, they stub out cleanly (as MMTTY IPC heavily relies on `HWND` and `PostMessage` win32 structures).
- N1MM Logger+ currently handles the radio interface via CAT, but does not provide the DX Suite Commander rig interface while running in RTTY mode. A workaround for this if N1MM Logger+ does not change is to read the UDP Messages broadcast from N1MM which do contain the current operating frequency.
- Changing the wsjt-x jtty window frequencies should report these back to N1MM Logger+ for more accurate frequency logging.

## Setup
- Make sure N1MM Logger+ can control the radio, including PTT control, CAT, and frequency.
- Make a new configuration in WSJT-X, called "jtty". If there will be two VFOs in use, make JTTY2 for the second VFO.
- JTTY and JTTY2 configurations in wsjtx should have rig set for None (since N1MM logger will control the rig).
- in N1MM Logger+, in the digital modes configuration tab, make sure MMTTY DI-1 and DI-2 are set for AFSK, and that the MMTTY path is set to the location of the wsjtx_jtty.exe file (e.g. `C:\Program Files\wsjtx\bin\wsjtx_jtty.exe`)

- When N1MM Logger+ is put into RTTY mode, it should show the Digital Interface window. If it doesn't, choose the "Digital Interface" selection under the N1MM Logger+ Window menu. After the digital interface window appears, it may take a few seconds for wsjt-x to appear. On my machine, it's about 7-8 seconds. 
- WSJT-X should be showing 'JTTY' as the mode and configuration in the footer of the WSJT-X window.
- In RTTY mode, pressing the macro key buttons in N1MM Logger+ should cause the appropriate text to be send to wsjt-x. This text should show in the digital interface window, AND in the TX window of WSJT-X.
- Note that there are certain jtty message sequences that will send faster than others, because of the jtty message format. For example, a CQ message SHOULD BE something like CQ N9ADG instead of the RTTY-inspired CQ TEST N9ADG N9ADG CQ
