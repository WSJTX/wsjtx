# 
# Digital Function Key File 
# 
# Edits may be necessary before using this file
#  In some contests - replaceF1 expression CQ "TEST" with c
#    For example: WPX IOTA BARTG SPRINT    
#  Replace F6 and F9-11 Spare,  with contest exchange expres
#    For example - Number? State? Zone?  
#    non-ESM - no other changes required 
#    ESM - RUN F2 - remove {ENTER} and first ! to avoid doub
# 
# S&P F1 calls CQ and automatically places the program in RU
# F2 F3 F4 F5 use "!" macro for his callsign
# F2 F7 {SENTRST} will default to 599 or allows manual entry
# 
################### 
#   RUN Messages 
################### 
F1 Run CQ,{TX}CQ {MYCALL} CQ{RX}
F2 Run Exch,{TX}! 599 {EXCH}{RX}
F3 Run TU,{TX}! TU CQ {MYCALL} CQ{RX}
F4 {MYCALL},{TX}{MYCALL}{RX}
F5 His Call,{TX}!{RX}
F6 NR?,{TX}! NR?{RX} 
F7 Spare,
F8 Agn?,{TX}! agn?{RX}
F9 Spare, 
F10 Spare, 
F11 NOW,{TX}!{LOGTHENPOP}NOW {F5}{F2}{RX}
F12 Wipe,{WIPE}
# 
################### 
#   S&P Messages 
################### 
# "&" doubled, displays one "&" in the button label
F1 S&&P CQ,{TX}CQ {MYCALL} CQ{RX}
F2 S&&P Exch,{TX}! 599 {EXCH}{RX}
F3 S&&P TU,{TX}! TU {MYCALL}{RX}
F4 S&&P Call Him,{TX}! {MYCALL}{RX}
F5 His Call,{TX}!{RX}
F6 {MYCALL},{TX}{MYCALL}{RX}
F7 My Exch,{TX}599 {EXCH}{RX}
F8 Agn?,{TX}agn?{RX}
F9 Spare, 
F10 Spare, 
F11 Spare, 
F12 Wipe,{WIPE}
