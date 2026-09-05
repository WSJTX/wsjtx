# JTTY Design

JTTY is a new digital mode designed for  fast RTTY-like contest exchanges and other keyboard-to-keyboard  communication on the amateur radio bands. It has an operational feel similar to standard RTTY, but far better weak-signal performance and lower error rate. JTTY transmissions can start at any time and typically last a few seconds. Any arbitrary message can be sent using letters, digits, spaces, and punctuation. Source encoding is especially well optimized for the short, fixed-format messages generally used in RTTY-style radio contesting.

A JTTY transmission is composed of one or more frames, each carrying a 34-bit payload. The well defined message grammar uses 30 bits for a frame's message content and two bits to specify one of four possible message types. The most basic type, i2=3, conveys any sequence of five characters from the supported 64-character ASCII subset; other message types and sub-types can encode a standard amateur callsign, contest exchange information, and flags to convey common contest-message fragments like `CQ`, `TU`, `599`, `AGN?`, and `TU NOW`. Two further control bits convey an end-of-message (EOM) indicator marking a frame as the last one of a multi-frame message, and a reserved bit presently transmitted as 0. To ensure the integrity of decoded messages, a 12-bit CRC is appended to the 34-bit payload, giving a 46-bit block. This block is encoded using a tail-biting convolutional code (TBCC) with rate 1/2 and constraint length K=10, making a (92,46) block code. Decoding uses the wrap-around Viterbi algorithm (WAVA) followed by CRC verification.

JTTY signals are modulated with Gaussian-smoothed 4-tone frequency-shift keying (4-GFSK), two coded bits per tone symbol. Each frame has a 13-symbol synchronization sequence followed by 46 symbols that carry the 92 coded bits. At the currently selected symbol rate, 12000/384 = 31.25 baud, a JTTY frame is transmitted in (13+46)/31.25 = 1.888 s. The occupied bandwidth (99% of transmitted power) is 127 Hz.

By design, source encoding is optimized for the message formats typically used in RTTY-style contesting. Repetition of important message fragments such as callsigns and contest serial numbers will generally not be needed, since JTTY's strong FEC already guarantees reliable copy. Most contest-style transmissions require only one or two frames, and thus have durations of about 1.9 or 3.8 s. Decoded message frames are displayed to the operator sequentially, on the fly, rather than at the end of a longer transmission as in other WSJT-X modes. There is no need for typical RTTY formatting necessities such as CR/LF at the start of a transmission, an extra space at the end of transmission, or LETTERS/NUMBERS shift characters. Achievable QSO rates for JTTY in contesting circumstances should be significantly higher than for standard 45-baud RTTY. JTTY will be reliable at signal levels much weaker than those needed for RTTY, and operators will see far less on-screen garbage.

Example messages exchanged for two successive contest-style QSOs are shown below, along with the number of frames required for each message. In this sequence KA1ABC calls CQ, receives replies from WB9XYZ and JA6DEF, and works both callers in sequence, including a requested repeat from JA6DEF.

<div style="page-break-after: always;"></div>

| Run station             | Search and Pounce station(s) | Frames |
| ----------------------- | ---------------------------- | :----- |
| `CQ KA1ABC CQ`          |                              | 1      |
|                         | `WB9XYZ`                     | 1      |
|                         | `JA6DEF`                     | 1      |
| `WB9XYZ 599 101`        |                              | 2      |
|                         | `599 057`                    | 1      |
| `TU NOW JA6DEF 599 102` |                              | 2      |
|                         | (no decode)                  | 1      |
| `JA6DEF AGN?`           |                              | 1      |
|                         | `599 292`                    | 1      |
| `TU KA1ABC CQ`          |                              | 1      |

These examples of typical contest QSOs use a total of 9 transmission intervals and 11 JTTY frames. The average transmission length is thus about 1.888*11/9 = 2.3 s. With a generous allowance of 1 s for T/R switching, hardware and software latencies, and operator reaction times, we get something like 3.3 s per T/R interval and 15 s per QSO. If sustained, such rates would yield well over 200 QSOs/hour.

Source-encoding of JTTY messages involves packing and unpacking algorithms similar to those used in other WSJT-X modes. The following table illustrates how contest-style messages are packed into the smallest possible number of frames. Software parameters i2 and n2 (the message type and sub-type) are shown for the first one or two frames of each message. 

<div style="page-break-after: always;"></div>

| Frame 1 i2.n2 | Frame 2 i2.n2 | Characters | Frames | Message           |
| ---   | ---   | ---:| ---:| ---               |
| 0.0   |       | 12  |  1  | `CQ KA1ABC CQ`    |
| 0.1   |       |  6  |  1  | `WB9XYZ`          |
| 0.1   | 2     | 15  |  2  | `WB9XYZ 599 0123` |
| 0.2   |       | 12  |  1  | `TU KA1ABC CQ`    |
| 0.3   |       |  9  |  1  | `WB9XYZ TU`       |
| 0.3   | 0.0   | 22  |  2  | `WB9XYZ TU CQ KA1ABC CQ` |
| 1.0   |       | 11  |  1  |  `WB9XYZ AGN?`           |
| 1.1   | 2     | 21  |  2  | `TU NOW JA6DEF 599 123`  |
| 1.2   |       |     |     | (not yet assigned)       |
| 1.3   |       |     |     | (not yet assigned)       |
| 2     |       |  8  |  1  | `599 1234`               |
| 2     |       |  6  |  1  | `599 MA`                 |
| 2     |       |  8  |  1  | `599 FN42`               |
| 3     | 3     | 10  |  2  | `VP2/KA1ABC`             |
| 3     |       |  9  |  2  | `CQ KA1ABC`       |
| 3     | 3     | 15  |  3  | `CQ VP2/KA1ABC CQ`       |
| 3     | 3     | 18  |  4  | `PJ4/KA1ABC 599 124`     |
| 3     | 3     | 45  | 9 | `THE QUICK BROWN FOX JUMPED OVER THE LAZY DOG.` |
