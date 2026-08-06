# JTTY

JTTY is a possible new WSJT-X mode designed for  fast "RTTY-like" contest exchanges and other keyboard-to-keyboard  communication on the amateur radio bands. Its general goal is an operational feel similar to standard RTTY, but with much better weak-signal performance and much smaller error rate. 

JTTY transmissions can start at any time and typically last a few seconds. Any arbitrary message can be sent using upper-case letters, digits, spaces, and punctuation. Source encoding is especially well optimized for the short, fixed-format messages frequently used in RTTY-style radio contesting.

A JTTY transmission is composed of one or more frames, each carrying a 32-bit message-grammar payload. Two payload bits select one of four possible message types; the remainder convey details of user information for that type. The most basic type conveys any sequence of five characters from the supported 64-character ASCII subset. Other message types (and sub-types) can carry a standard amateur callsign, contest exchange information, and flags to convey common message fragments like `CQ`, `TU`, `599`, `AGN?`, and `TU NOW`.

Two further control bits are appended to the 32 message-grammar bits before FEC coding, making a 34-bit payload: a reserved bit that is always transmitted as 0 (used at the receiver to roughly halve the undetected-error rate, since a wrong candidate decode must now also hit this bit by chance rather than passing on CRC alone), and an end-of-message (EOM) flag that marks a frame as the last one of a multi-frame message.

To ensure the integrity of decoded messages, a 12-bit CRC is appended to the 34-bit payload, giving a 46-bit block. This block is encoded using a tail-biting rate-1/2 convolutional code (TBCC), giving a (92,46) code -- 92 coded bits for 46 information bits (34 payload + 12 CRC). The default constraint length is K=12 (encoder memory nu=11), using the maximum-free-distance, noncatastrophic generator polynomial pair for that memory order; shorter and longer constraint lengths (K=10, 11, 13) are also supported. Decoding uses a wrap-around Viterbi algorithm (WAVA) list decoder, which exploits the tail-biting structure to decode the whole circular frame without needing known start/end states, followed by CRC (and reserved-bit) verification of each candidate on the list.

JTTY signals are modulated with Gaussian-smoothed 4-tone frequency-shift keying (4-GFSK), two coded bits per tone symbol. Each frame has a 13-symbol synchronization sequence followed by 46 symbols that carry the 92 coded bits. At the currently selected symbol rate, 12000/384 = 31.25 baud, a JTTY frame is transmitted in (13+46)/31.25 = 1.888 s. The signal bandwidth is 125 Hz at the half-power points and
xxx Hz at -60 dBc.

Source encoding is optimized for the message formats typically used in RTTY-style contesting. Repetition of important message fragments such as callsigns and contest serial numbers is generally not needed, since JTTY's strong FEC already guarantees reliable copy. Most contest-style transmissions require only one or two frames, and thus have durations either 1.9 or 3.8 s. Received message frames are displayed to the operator sequentially, on the fly, rather than at the end of a longer transmission as in other WSJT-X modes. There is no need for RTTY formatting necessities such as CR/LF at the start of a transmission, an extra space at the end of transmission, or LETTERS/NUMBERS shift characters. Achievable QSO rates for JTTY in contesting circumstances should be significantly higher than for standard 45-baud RTTY. JTTY will be reliable at signal levels well below those needed for RTTY, and there will be far less on-screen garbage.

An example showing the messages exchanged for two successive contest-style QSOs is shown below, along with the number of frames required for each message. In this sequence KA1ABC calls CQ, receives replies from WB9XYZ and JA6DEF, and works both callers in sequence, including a requested repeat from JA6DEF.

<div style="page-break-after: always;"></div>

| Run station             | S+P stations | Frames |
| ---                     | ---          | :---   |
| `CQ KA1ABC CQ`          |              | 1      |
|                         | `WB9XYZ`     | 1      |
|                         | `JA6DEF`     | 1      |
| `WB9XYZ 599 101`        |              | 2      |
|                         | `599 057`    | 1      |
| `TU NOW JA6DEF 599 102` |              | 2      |
|                         | (no decode)  | 1      |
| `JA6DEF AGN?`           |              | 1      |
|                         | `599 292`    | 1      |
| `TU KA1ABC CQ`          |              | 1      |

These examples of typical contest QSOs use a total of 9 transmission intervals and 11 JTTY frames. The average transmission length is thus about 1.888*11/9 = 2.3 s; with a generous 1 s for T/R switching, hardware and software latencies, and operator reaction times, we get something like 3.3 s per T/R interval and 15 s per QSO. If sustained, such rates could still yield over 200 QSOs/hour.

Source-encoding of JTTY messages involves packing and unpacking algorithms similar to those used in other WSJT-X modes. The table on the next page illustrate the packing of contest-style messages into the smallest possible number of frames. Software parameters i2 and n2 (the message type and sub-type) are shown for their first one or two frames. NC is the number of characters in the message, and NF the number of frames.

<div style="page-break-after: always;"></div>

| Frame 1, i2.n2 | Frame 2, i2.n2 | NC  | NF  | Message           |
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
| 3     | 3     | 45  | 11  | `THE QUICK BROWN FOX JUMPED OVER THE LAZY DOG.` |
