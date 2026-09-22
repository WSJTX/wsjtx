# JTTY Design

JTTY is a new digital mode designed for fast RTTY-like contest exchanges and other keyboard-to-keyboard communication on the amateur radio bands. It has an operational feel similar to standard RTTY, but far better weak-signal performance and a lower error rate. JTTY transmissions can start at any time and typically last a few seconds. Any arbitrary message can be sent using letters, digits, spaces, and punctuation. Source encoding is especially well optimized for the short, fixed-format messages generally used in RTTY-style radio contesting.

## Frame and waveform

A transmission contains one or more 1.888-second frames. Each frame carries a 32-bit source-grammar word followed by two universal control bits: a reserved bit that is always zero and an end-of-message (EOM) bit set only on the final source atom. A 12-bit CRC extends this 34-bit payload to 46 bits. A tail-biting, rate-1/2 convolutional code with constraint length K=10 produces 92 coded bits.

The waveform is Gaussian-smoothed four-tone frequency-shift keying (4-GFSK), with two coded bits per tone symbol. A frame contains a 13-symbol synchronization sequence followed by 46 coded symbols. At 12000/384 = 31.25 baud, its duration is (13+46)/31.25 = 1.888 seconds. The occupied bandwidth (99% of transmitted power) is 127 Hz.

The receiver verifies FEC, CRC, the universal reserved-zero bit, and the complete source grammar. A structurally invalid source word is discarded before display, EOM handling, slot creation, or signal subtraction.

## Source contracts

JTTY separates automatic text packing from typed native actions.

Ordinary keyboard text, `sjtty` input, externally queued strings, and untagged N1MM/MMTTY text use the literal source interface. Text is folded to uppercase, spaces are normalized, and unsupported characters become `#`. The explicit RTTY Roundup profile also canonicalizes report-prefixed serials as described below. The encoder chooses the minimum-frame combination of recognized compact atoms and five-character TEXT5 frames that preserves the resulting normalized text exactly. "Literal" identifies the text interface, not a requirement to use TEXT5 or bypass profile normalization.

The eight shipped JTTY function-key templates use a NativeMacro contract. When a default template is selected, it is compiled to typed call and exchange atoms before placeholder expansion. Native atoms provide compact transmission and unambiguous fields for future logger integration. A customized template that does not match a native form falls back to automatic text packing after normal placeholder expansion. A recognized native template with invalid runtime data is rejected rather than silently transmitted with different semantics.

Earlier JTTY packing minimized frames using compact calls, a generic `599 ` plus five-character format, and TEXT5. STRUCT30 replaced that generic exchange format with richer typed atoms. Restricting ordinary input to TEXT5 avoided guessing whether `05` meant a serial, zone, or check, but also lost unambiguous callsign compaction. Automatic packing retains the minimum-frame algorithm, with context-free recognition and an explicit exchange profile that can normalize serials and enable additional typed candidates.

## Automatic text packing

At complete token boundaries, the packer considers the six callsign forms below, registered control phrases (including within longer messages), and these STRUCT30 forms:

| Text form | Automatic atom |
| --- | --- |
| Canonical unsigned decimal, optionally preceded by `599` | GENERIC_NUMERIC, 0-131071, with no leading zeros except `0` |
| `599 <location>` | GENERIC_QTH, exactly two or three base-36 characters including at least one letter |
| Valid four-character Maidenhead locator, optionally preceded by `599` | GRID4 |
| `<count><class> <section>` | CLASS_SECTION, count 1-32, class A-F, registered ARRL/RAC section |

Every candidate must pass the existing codec validation and render exactly as its span in the normalized text. Calls must round-trip through the standard callsign codec; QTH tokens must obey the wire format's canonical length rules.

The GUI captures an exchange profile from the existing special operating activity when each message is submitted: Unknown, Field Day, or RTTY Roundup. No activity means Unknown even though native macros default to serial exchanges. All profiles retain the context-free candidates above. Only RTTY Roundup adds candidates: full `599 <number>` as SERIAL and full `599 <location>` as STATE_PROVINCE, using the native exchange rules and requiring a location token to contain a letter. Bare numbers are never inferred as serials. Field Day adds no candidates because class/section syntax is already self-identifying. No new configuration or wire format is needed.

Zones, checks, ages, power, license years, other specific location categories, zone/location pairs, and serial/time pairs are not inferred. Under RTTY Roundup, equally compact serial or state/province candidates take precedence over their generic equivalents through the subtype ordering below.

A dynamic program over character offsets selects the fewest frames under this recognition policy, rather than greedily taking the first compact form. TEXT5 consumes exactly five characters except at the end; an interior short fragment cannot be padded to reach a compact candidate because that would change the text. Structured atoms supply an implicit single space when followed by another token. Equal-cost alternatives prefer a structured atom, then the longest consumed span, then atom kind/subtype/role order.

Before packing, the explicit RTTY Roundup profile canonicalizes a decimal token following a complete `599` token as a serial: `599 05` becomes `599 005`, and `599 0123` becomes `599 123`. Both use one SERIAL frame. Eligible tokens contain one to six digits with value 0-131071; unsupported tokens retain their spelling and use the normal fallback. Bare digits and all text under Unknown or Field Day retain their spelling after ordinary normalization. The canonical message is returned to the GUI for display and logging. If serial normalization expands the message beyond 80 characters, submission is rejected rather than truncating it.

The result need not match the theoretical minimum with perfect knowledge of exchange semantics. Under Unknown or Field Day, `599 001` and `599 05` still take two frames. An explicit codec caller can encode `599 05` as a CQ zone in one frame, but the current GUI macros do not expose that numeric kind; RTTY Roundup explicitly selects serial meaning instead.

`sjtty` defaults to Unknown. An optional leading `--exchange-profile=unknown|field-day|rtty-roundup` selects the profile for either its one-message packing invocation or its eight-argument waveform invocation. Invalid profile values are errors. For example, `sjtty --exchange-profile=rtty-roundup "599 001"` selects the one-frame serial interpretation.

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

The former `i2=2` meaning, literal `599 ` followed by five characters, has been intentionally replaced by STRUCT30. There is no discriminator: old receivers display new STRUCT30 bits as `599` text, and some old type-2 frames are valid new STRUCT30 words with different meanings. JTTY is unreleased, so there is no legacy decoder mode. Automatic packing uses only the current grammar; unrecognized `599 ...` text remains available through TEXT5.

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

The practical native GUI and N1MM transmit subset covers Call8, serial or RTTY state/province exchanges, Field Day class/section, GRID4, and registered control phrases. Automatic text packing additionally uses GENERIC_NUMERIC and GENERIC_QTH without assigning contest-specific meanings. Other normative STRUCT30 types are decoded, validated, and rendered canonically but are not inferred from text or exposed as general-purpose macros yet.

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

Typed exchanges such as `599 05 NWT`, `599 156 1749`, and `1D EMA` fit one STRUCT30 frame. Automatic text packing recognizes `1D EMA`, but does not infer the zone/location or serial/time meanings of the other two examples. Strong FEC makes repeated visible fields unnecessary; when RF redundancy is desired, repeating the protected atom provides another independent synchronization, FEC, and CRC opportunity.

These frame counts apply to ordinary text, without a native template or contest profile:

| Input | Frames | Reason |
| --- | ---: | --- |
| `CQ K1ABC CQ` or `CQ KA1ABC CQ` | 1 | CQ call atom |
| `WB9XYZ` | 1 | Call atom |
| `WB9XYZ TU CQ KA1ABC CQ` | 2 | Two call atoms |
| `WB9XYZ 599 123` | 2 | Call and generic numeric exchange |
| `599 123` | 1 | Generic numeric exchange |
| `599 MA` | 1 | Generic QTH exchange |
| `599 FN42` | 1 | Full-role GRID4 |
| `1D EMA` | 1 | Class/section pair |
| `599 001` or `599 05` | 2 | No numeric kind is inferred to preserve leading zeros |
| `599 BRUCE` | 2 | No current generic five-character exchange atom |

With the RTTY Roundup profile, `599 001` takes one frame and `K1ABC 599 001` takes two. `599 05` becomes `599 005` in one frame. The other examples retain their frame counts, although `599 123` and `599 MA` use typed SERIAL and STATE_PROVINCE atoms instead of generic atoms.
