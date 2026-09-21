# 
# Digital Function Key File Example for JTTY use
# 
# See the discussion in the accompanying jtty_n1mm_integration.md file
# The leading [[JTTY:ACTION]] marker opts into native Call8/STRUCT30 encoding.
# Remove the marker to send the expanded N1MM text literally with TEXT5.
#
# S&P F1 calls CQ and automatically places the program in RUN Mode
# F2 F3 F4 F5 use "!" macro for his callsign
# Exchange actions use the active JTTY profile; do not hardcode 599.
# Run F11 remains spare until N1MM call-stack TXTEXT ordering is validated.
# 
################### 
#   RUN Messages 
################### 
F1 Run CQ,{TX}[[JTTY:CQ]]{MYCALL}{RX}
F2 Run Exch,{TX}[[JTTY:CALL_EXCH]]! {EXCH}{RX}
F3 Run TU,{TX}[[JTTY:CALL_TU_CQ]]! {MYCALL}{RX}
F4 {MYCALL},{TX}[[JTTY:MYCALL]]{MYCALL}{RX}
F5 His Call,{TX}[[JTTY:HISCALL]]!{RX}
F6 NR?,{TX}[[JTTY:CONTROL]]NR?{RX}
F7 Spare,
F8 Agn?,{TX}[[JTTY:CONTROL]]AGN?{RX}
F9 Spare, 
F10 Spare, 
F11 Spare,
F12 Wipe,{WIPE}
# 
################### 
#   S&P Messages 
################### 
# "&" doubled, displays one "&" in the button label
F1 S&&P CQ,{TX}[[JTTY:CQ]]{MYCALL}{RX}
F2 S&&P Exch,{TX}[[JTTY:CALL_EXCH]]! {EXCH}{RX}
F3 S&&P TU,{TX}[[JTTY:CALL_TU_MY]]! {MYCALL}{RX}
F4 S&&P Call Him,{TX}[[JTTY:CALL_MY]]! {MYCALL}{RX}
F5 His Call,{TX}[[JTTY:HISCALL]]!{RX}
F6 {MYCALL},{TX}[[JTTY:MYCALL]]{MYCALL}{RX}
F7 My Exch,{TX}[[JTTY:EXCH]]{EXCH}{RX}
F8 Agn?,{TX}[[JTTY:CONTROL]]AGN?{RX}
F9 Spare, 
F10 Spare, 
F11 Spare, 
F12 Wipe,{WIPE}
