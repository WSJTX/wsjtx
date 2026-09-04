# JTTY Design

JTTY is a new digital mode designed for fast RTTY-like contest exchanges and other keyboard-to-keyboard communication on the amateur radio bands. It has an operational feel similar to standard RTTY, but far better weak-signal performance and a lower error rate. JTTY transmissions can start at any time and typically last a few seconds. Any arbitrary message can be sent using letters, digits, spaces, and punctuation. Source encoding is especially well optimized for the short, fixed-format messages generally used in RTTY-style radio contesting.

## Frame and waveform

A transmission contains one or more 1.888-second frames. Each frame carries a 32-bit source-grammar word followed by two universal control bits: a reserved bit that is always zero and an end-of-message (EOM) bit set only on the final source atom. A 12-bit CRC extends this 34-bit payload to 46 bits. A tail-biting, rate-1/2 convolutional code with constraint length K=10 produces 92 coded bits.

The waveform is Gaussian-smoothed four-tone frequency-shift keying (4-GFSK), with two coded bits per tone symbol. A frame contains a 13-symbol synchronization sequence followed by 46 coded symbols. At 12000/384 = 31.25 baud, its duration is (13+46)/31.25 = 1.888 seconds. The occupied bandwidth (99% of transmitted power) is 127 Hz.

The receiver verifies FEC, CRC, the universal reserved-zero bit, and the complete source grammar. A structurally invalid source word is discarded before display, EOM handling, slot creation, or signal subtraction.

## Source contracts

JTTY separates general operator text from typed native actions. Exact registered control phrases are the narrow case whose type is self-identifying.

Ordinary keyboard text, externally queued strings, and untagged N1MM/MMTTY text use the literal source interface. Text is folded to uppercase, whitespace is normalized, and unsupported characters become `#`. If the complete normalized message exactly matches one of the registered control phrases, it uses the corresponding CONTROL atom; all other literal input uses five-character TEXT5 frames. A call-looking string or a string beginning with `599` is still literal. The encoder does not infer contest semantics from its spelling.

The eight shipped JTTY function-key templates use a NativeMacro contract. When a default template is selected, it is compiled to typed call and exchange atoms before placeholder expansion. Native atoms provide compact transmission and unambiguous fields for future logger integration. A customized template that does not match a native form falls back unchanged to literal TEXT5. A recognized native template with invalid runtime data is rejected rather than silently transmitted with different semantics.

This boundary preserves operator intent: type literal text when typography matters, and select a native function-key action when the semantic exchange is intended.

## Source grammar

Bits 31-32 select one of four message types. The complete normative definition, including field ranges, enum assignments, validity rules, and golden vectors, is in [jtty_source_encoding.txt](jtty_source_encoding.txt).

| `i2.n2` | Contents | Canonical rendering |
| --- | --- | --- |
| `0.0` | CQ call action | `CQ <call> CQ` |
| `0.1` | CALL action | `<call>` |
| `0.2` | TU/CQ call action | `TU <call> CQ` |
| `0.3` | CALL/TU action | `<call> TU` |
| `1.0` | CALL/AGN call action | `<call> AGN?` |
| `1.1` | TU NOW/CALL action | `TU NOW <call>` |
| `1.2`, `1.3` | Reserved | Invalid in version 1 |
| `2` | STRUCT30 | Typed exchange, pair, time, control, or GRID4 atom |
| `3` | TEXT5 | Five six-bit JTTY characters |

STRUCT30 places a 27-bit family body before a three-bit family selector:

| Family | Name | Fields, in transmitted bit order |
| --- | --- | --- |
| `000` | EXCH_NUM | `role1 + number_kind4 + value17 + zero5` |
| `001` | EXCH_LOC | `role1 + location_kind4 + length1 + base36_token16 + zero5` |
| `010` | EXCH_PAIR | `pair_schema3 + pair_data23 + zero1` |
| `011` | EXCH_NUM_TIME | `role1 + serial14 + minute_of_day11 + zero1` |
| `100` | MISC | `subtype4 + subtype_data23` |
| `101` | Profiled/dense extension | Reserved |
| `110` | Versioned extension | Reserved |
| `111` | Guard space | Invalid |

The assigned fields cover serials, zones, ages, power, checks, four-digit first-license years, generic numbers, two- or three-character locations, zone/location pairs, Field Day class/section pairs, serial/time pairs, GRID4, and 18 common control phrases. Native values render canonically: serials use at least three digits, zones and checks at least two, UTC time exactly four, and license years exactly four. A full exchange adds `599`; a field-only atom omits it. Decimal width, separator style, `5NN` spelling, and visible repetition do not consume wire bits.

The former `i2=2` meaning, literal `599 ` followed by five characters, has been intentionally replaced by STRUCT30. There is no discriminator: old receivers display new STRUCT30 bits as `599` text, and some old type-2 frames are valid new STRUCT30 words with different meanings. JTTY is unreleased, so there is no legacy decoder mode. Literal `599 ...` text remains available through TEXT5.

## Native function keys

Current native templates and control phrases match after case folding and
whitespace normalization, before placeholders are expanded. The three
superseded serial templates are accepted only in their exact saved-default
form so edited variants remain literal.

| Key | Template | Native meaning |
| --- | --- | --- |
| F1 | `CQ %M CQ` | CQ with the configured call |
| F2 | `%H %E` | His call, then configured full exchange |
| F3 | `%H TU CQ %M CQ` | His call/TU, then CQ with the configured call |
| F4 | `%M` | Configured call |
| F5 | `%H` | His call |
| F6 | `TU NOW %Q %E` | Queued call, then configured full exchange |
| F7 | `%H AGN?` | Request repeat from his call |
| F8 | `%E` | Configured full exchange |

Keyboard shortcuts and the clickable F1-F8 buttons select the same actions. Native atom sequences use an implicit single-space separator and set EOM only on the last atom. Consecutive queued native messages do not need an intervening TEXT5 space frame.

`%E` parses the selected contest profile: the default is a decimal serial, FIELD_DAY is exactly `<count><class> <section>`, and RTTY is a decimal serial or canonical two- or three-character state/province. `%G` is a field-only GRID4; the exact `599 %G` template selects its full-exchange role. The exact old F2, F6, and F8 defaults using `599 %N` remain recognized during migration and are replaced only when unchanged in saved settings. Edited variants remain literal.

The practical GUI and N1MM transmit subset covers Call8, serial or RTTY state/province exchanges, Field Day class/section, GRID4, and registered control phrases. The remaining normative STRUCT30 types are decoded, validated, and rendered canonically but are not inferred from text or exposed as general-purpose macros yet.

## Tagged N1MM actions

N1MM may explicitly request native encoding by starting TXTEXT with `[[JTTY:<ACTION>]]`. The assigned actions are `CQ`, `CALL_EXCH`, `CALL_TU_CQ`, `MYCALL`, `HISCALL`, `TU_NOW_EXCH`, `CALL_MY`, `CALL_TU_MY`, `EXCH`, `GRID`, and `CONTROL`. N1MM expands `{MYCALL}`, `!` or `{CALL}`, and `{EXCH}` before WSJT-X validates the action payload under the active profile.

Untagged N1MM text remains literal. A malformed tag, unknown action, invalid call, invalid exchange or grid, unregistered control phrase, or profile mismatch is rejected rather than transmitted as literal bracket text. The exact grammar and action payloads are in [jtty_source_encoding.txt](jtty_source_encoding.txt), with operating examples in [jtty_n1mm_integration.md](jtty_n1mm_integration.md).

## Contest exchange example

The native defaults keep the usual run sequence compact while retaining explicit meanings:

| Run station | S+P stations | Frames |
| --- | --- | ---: |
| `CQ KA1ABC CQ` | | 1 |
| | `WB9XYZ` | 1 |
| | `JA6DEF` | 1 |
| `WB9XYZ 599 101` | | 2 |
| | `599 057` | 1 |
| `TU NOW JA6DEF 599 102` | | 2 |
| | *(no decode)* | 1 |
| `JA6DEF AGN?` | | 1 |
| | `599 292` | 1 |
| `TU KA1ABC CQ` | | 1 |

Typed exchanges such as `599 05 NWT`, `599 156 1749`, and `1D EMA` fit one STRUCT30 frame. Literal input with the same visible characters uses TEXT5 and may take more frames. Strong FEC makes repeated visible fields unnecessary; when RF redundancy is desired, repeating the protected atom provides another independent synchronization, FEC, and CRC opportunity.
