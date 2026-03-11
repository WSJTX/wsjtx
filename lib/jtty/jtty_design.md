# JTTY

JTTY is a possible new WSJT-X mode designed for  fast RTTY-like contest exchanges and other keyboard-to-keyboard  communication on the amateur radio bands. Its general goal is an operational feel similar to standard RTTY, but with much better weak-signal performance and much smaller error rate. JTTY transmissions typically last a few seconds and can start at any time. Any arbitrary message may be sent using upper-case letters, digits, spaces, and punctuation. Source encoding is especially well optimized for the short, fixed-format messages frequently used in RTTY-style radio contesting.

A JTTY transmission is composed of one or more frames, each carrying a 32-bit information payload. Two payload bits select one of four possible message types; the remainder convey details of user information for that type. The most basic type conveys a sequence of any five characters from the supported 64-character ASCII subset. Other message types (and sub-types) can carry a standard ham callsign, contest exchange informatio, and flags to convey common message words like `CQ`, `TU`, `599`, `AGN?`, and `TU NOW`. 
To ensure the integrity of decoded messages, a 10-bit CRC is appended to each 32-bit payload. The result is then encoded using a strong binary block code with parameters (n,k) = (80,42). This code is a shortened form of the (128,90) low-density parity check code used for MSK144 and described by Franke and Taylor in the July/August 2017 issue of QEX.

JTTY signals are modulated with Gaussian-smoothed 4-tone frequency-shift keying (4-GFSK). Each frame has a 13-symbol synchronization sequence followed by 40 symbols to carry the payload and error-control bits. At the currently selected symbol rate, 12000/384 = 31.25 baud, a JTTY frame is transmitted in (13+40)/31.25 = 1.696 s. The nominal signal bandwidth is 125 Hz, and 99% of transmitted power falls within bandwidth xxx Hz.

Source encoding is optimized for message formats typically used in RTTY-style contesting. Repetition of important message fragments such as callsigns and contest serial numbers is generally not needed, since JTTY's strong FEC already guarantees reliable copy. Most contest-style transmissions require only one or two frames and thus have duration 1.7 or 3.4 s. Received message frames are displayed to the operator sequentially, on the fly, rather than at the end of a longer transmission, as in other WSJT-X modes. There is no need for RTTY formatting necessities such as CR/LF at the start of a transmission, LETTERS/NUMBERS shift characters, an extra space at the end of transmission, etc. Achievable QSO rates for JTTY in contesting circumstances should be significantly higher than for standard 45-baud RTTY. JTTY will be reliable at signal levels well below those needed for RTTY, and there will be far less on-screen
garbage.

An example showing the messages exchanged for two successive
contest-style QSOs is shown below, along with the number of frames required for each message. In this sequence KA1ABC calls CQ, receives replies from WB9XYZ and JA6DEF, and works both callers in sequence, including a requested repeat from JA6DEF.

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

These examples of two typical contest QSOs use a total of 9 transmission intervals and 12 JTTY frames. The average transmission length is thus about 1.7*12/9 = 2.3 s; with another 0.7 s for T/R switching, hardware and software latencies, and operator reaction times we get something like 3 s per T/R interval and perhaps 13 to 15 s per QSO. If sustained, such rates could yield more than 200 QSOs/hour.

Source-encoding of JTTY messages involves packing and unpacking procedures similar to those used in other WSJT-X modes. Here are a few examples of the way contest-style messages are packed into the smallest possible number of frames, together with the software parameters i2.n2 for their first one or two frames:

| i2.n2 | i2.n2 | NC  | NF  | Message           |
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
