# JTTY and N1MM Logger+ Integration

## Summary

N1MM Logger+ can send either literal JTTY text or an explicit native action. Ordinary TXTEXT uses the literal source interface: an exact whole-message registered control phrase uses its CONTROL atom, while other text uses TEXT5. Other native encoding is opt-in through a leading `[[JTTY:<ACTION>]]` marker in the transmitted text. The marker is consumed by WSJT-X and is never put on the air.

The bundled `JTTY Messages.mc` uses tagged actions for its common Run and S&P messages. This provides compact Call8 and STRUCT30 transmission without asking WSJT-X to infer meaning from visible logger text.

## Setup

- Configure N1MM Logger+ with WSJT-X as for FT8. A starting point is [Operating WW Digi with N1MM and WSJT-X](https://www.rttycontesting.com/tutorials/n1mm/operating-ww-digi-with-n1mm/).
- Verify radio control, PTT, audio, and the `forEW1` configuration in FT8 before selecting JTTY.
- Enter `JTTY` in the N1MM callsign field and press Enter to start WSJT-X in JTTY mode.
- Confirm that the WSJT-X footer shows JTTY and the window title contains `forEW1`.
- Use radio test mode, zero output power, or a dummy load for initial checks.
- Save the existing Digital Function Key file before loading the supplied `JTTY Messages.mc` example.

End-to-end validation with N1MM on Windows, including two-radio focus, call selection, queue movement, and Run/S&P state transitions, remains required before release.

## Literal TXTEXT

N1MM expands logger macros before passing TXTEXT to WSJT-X. An untagged result uses the literal source interface:

- lowercase letters become uppercase;
- leading, trailing, and repeated spaces are removed;
- unsupported characters become `#`;
- an exact whole-message registered control phrase uses one CONTROL frame;
- other input is divided into five-character TEXT5 frames after normalization.

Apart from exact registered control phrases, no visible pattern selects native encoding. `CQ N9ADG CQ`, `N9ADG`, and `599 123` remain literal unless TXTEXT begins with an assigned JTTY action marker. This prevents contest inference or semantic rewriting of customized messages.

The former `i2=2` shortcut for literal `599 ` plus five characters has been replaced by STRUCT30. There is no compatibility discriminator: an old receiver displays new STRUCT30 bits as `599` text, and some old type-2 frames are valid new STRUCT30 words with a different meaning. JTTY is unreleased, so no legacy decoder mode is retained.

## Tagged TXTEXT grammar

Place the marker so it is the leading transmitted text. Non-transmitting N1MM
commands may execute between `{TX}` and the marker:

```text
{TX}[[JTTY:<ACTION>]]<payload>{RX}
```

N1MM expands `{MYCALL}`, `!` or `{CALL}`, and `{EXCH}`. WSJT-X removes the marker, normalizes whitespace and case in the expanded payload, and validates it under the selected action and active JTTY profile. Tag and action matching is case-insensitive, but the tag must start at the first non-space transmitted character.

| Action | Expanded payload | Native sequence |
| --- | --- | --- |
| `CQ` | `<mycall>` | CQ with my call |
| `CALL_EXCH` | `<hiscall> <exchange>` | His call, configured full exchange |
| `CALL_TU_CQ` | `<hiscall> <mycall>` | His call/TU, CQ with my call |
| `MYCALL` | `<mycall>` | My call |
| `HISCALL` | `<hiscall>` | His call |
| `TU_NOW_EXCH` | `<hiscall> <exchange>` | TU NOW/his call, configured full exchange |
| `CALL_MY` | `<hiscall> <mycall>` | His call, my call |
| `CALL_TU_MY` | `<hiscall> <mycall>` | His call/TU, my call |
| `EXCH` | `<exchange>` | Configured full exchange |
| `GRID` | `<grid4>` | Field-only GRID4 |
| `CONTROL` | `<registered phrase>` | Registered control phrase |

Payloads contain dynamic values, not the phrases implied by an action. For
example, `CQ` receives only the callsign, `CALL_TU_CQ` receives two callsigns,
and `TU_NOW_EXCH` receives a callsign and exchange. The action supplies `CQ`,
`TU`, `TU NOW`, and canonical full-exchange report text where applicable. Do
not add a hardcoded `599` to an exchange payload.

Profile exchange payloads are:

- Default: one decimal serial in the 17-bit serial domain.
- `FIELD_DAY`: exactly `<count><class> <section>`, with count 1-32, class A-F, and a registered ARRL/RAC section.
- `RTTY`: either one decimal serial or one canonical two- or three-character state/province token. A three-character token may not have a leading zero because its two-character spelling would be canonical.

For tagged N1MM input, `DX` is an expanded two-letter location token. The
special `DX` and `#` meanings that select the live serial apply only to the
local WSJT-X RTTY `%E` configuration.

`GRID` accepts exactly one valid four-character Maidenhead locator. `CONTROL` accepts exactly one of the 18 registered phrases, including spaces and punctuation where shown in `jtty_source_encoding.txt`.

An unknown or malformed leading JTTY marker, unsupported action, missing field, invalid call, invalid profile exchange, invalid grid, or unregistered control phrase is rejected. It never falls back to literal transmission. Untagged messages, including customized N1MM macros, use the literal source interface; only an exact registered control phrase is contracted to CONTROL. A tag-shaped substring later in the text is just literal text.

An all-rejected transaction receives `OUTPUTCOMPLETE` at `XMIT OFF`. When a transaction also contains accepted or pending audio, completion waits until that audio has drained. `ABORT` clears the transaction without reporting a successful output completion.

## Bundled action mappings

The supplied macro file deliberately omits hardcoded `599`. Full-role native exchanges render the report canonically when the selected profile calls for it; Field Day class/section does not.

| Context | Key | Tagged meaning |
| --- | --- | --- |
| Run | F1 | `CQ` |
| Run | F2 | `CALL_EXCH` |
| Run | F3 | `CALL_TU_CQ` |
| Run | F4 | `MYCALL` |
| Run | F5 | `HISCALL` |
| Run | F6 | `CONTROL NR?` |
| Run | F8 | `CONTROL AGN?` |
| S&P | F1 | `CQ` |
| S&P | F2 | `CALL_EXCH` |
| S&P | F3 | `CALL_TU_MY` |
| S&P | F4 | `CALL_MY` |
| S&P | F5 | `HISCALL` |
| S&P | F6 | `MYCALL` |
| S&P | F7 | `EXCH` |
| S&P | F8 | `CONTROL AGN?` |

Run F11 is intentionally left spare. N1MM call stacking can transmit a call
correction before `{LOGTHENPOP}` and change the station referenced by subsequent
callsign and exchange macros. That sequence cannot satisfy the leading-tag
contract reliably without validated N1MM TXTEXT ordering. `TU_NOW_EXCH` remains
available for an explicit leading tag whose already-expanded payload names the
intended station and exchange.

## Native WSJT-X function keys

The WSJT-X JTTY editor offers the same native subset without an N1MM tag. The shipped defaults are:

| Key | Template |
| --- | --- |
| F1 | `CQ %M CQ` |
| F2 | `%H %E` |
| F3 | `%H TU CQ %M CQ` |
| F4 | `%M` |
| F5 | `%H` |
| F6 | `TU NOW %Q %E` |
| F7 | `%H AGN?` |
| F8 | `%E` |

`%E` is the configured profile exchange. `%G` is a field-only GRID4, while the exact template `599 %G` selects full-role GRID4. Current templates and control phrases match before expansion after case and whitespace normalization.

The exact former F2, F6, and F8 defaults (`%H 599 %N`, `TU NOW %Q 599 %N`, and `599 %N`) remain recognized during migration and are replaced by `%E` only when unchanged in saved settings. An edited old default is a customized literal template. Likewise, an unrecognized current template falls back to literal TEXT5 after normal expansion. A recognized native template with invalid runtime data is rejected rather than changing semantics through fallback.

Clickable F1-F8 buttons and keyboard shortcuts select the same actions. Native sequences set EOM only on their final atom and do not add a TEXT5 spacing frame between queued messages.

## Current scope

The practical tagged and GUI transmit subset includes Call8, serial and RTTY state/province exchanges, Field Day class/section, GRID4, and registered controls. The full wire codec also validates and canonically renders other STRUCT30 number/location kinds, ZONE_LOC3, and EXCH_NUM_TIME. Those forms are receive-only in the current operator-facing integration unless an explicit native codec caller constructs them; they are never inferred from literal text.

The complete wire definition is in [`jtty_source_encoding.txt`](jtty_source_encoding.txt).
