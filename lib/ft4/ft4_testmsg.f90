  parameter (MAXTEST=75,NTEST=47)
  character*37 testmsg(MAXTEST)
  data testmsg(1:NTEST)/                   &
       "CQ K1ABC FN42",                    &
       "K1ABC W9XYZ EN37",                 &
       "W9XYZ K1ABC -11",                  &
       "K1ABC W9XYZ R-09",                 &
       "W9XYZ K1ABC RRR",                  &
       "K1ABC W9XYZ 73",                   &
       "K1ABC W9XYZ RR73",                 &
       "CQ KH1/KH7Z",                      &
       "CQ FD K1ABC FN42",                 &
       "K1ABC W9XYZ 6A WI",                &
       "W9XYZ K1ABC R 2B EMA",             &
       "CQ TEST K1ABC/R FN42",             &
       "K1ABC/R W9XYZ EN37",               &
       "W9XYZ K1ABC/R R FN42",             &
       "K1ABC/R W9XYZ RR73",               &
       "CQ TEST K1ABC FN42",               &
       "K1ABC W9XYZ 579 WI",               &
       "W9XYZ K1ABC R 589 MA",             &
       "K1ABC KA0DEF 559 MO",              &
       "TU; KA0DEF K1ABC R 569 MA",        &
       "KA1ABC G3AAA 529 0013",            &
       "TU; G3AAA K1ABC R 559 MA",         &
       "CQ G4ABC/P IO91",                  &
       "G4ABC/P PA9XYZ JO22",              &
       "PA9XYZ 590003 IO91NP",             &
       "G4ABC/P R 570007 JO22DB",          &
       "PA9XYZ G4ABC/P RR73",              &
       "CQ PJ4/K1ABC",                     &
       "PJ4/K1ABC <W9XYZ>",                &
       "W9XYZ <PJ4/K1ABC> -11",            &
       "<PJ4/K1ABC> W9XYZ R-09",           &
       "<W9XYZ> PJ4/K1ABC RRR",            &
       "PJ4/K1ABC <W9XYZ> 73",             &
       "CQ W9XYZ EN37",                    & 
       "<W9XYZ> YW18FIFA",                 &
       "<YW18FIFA> W9XYZ -11",             &
       "W9XYZ <YW18FIFA> R-09",            &
       "YW18FIFA <W9XYZ> RRR",             &
       "<W9XYZ> YW18FIFA 73",              &
       "TNX BOB 73 GL",                    &
       "CQ YW18FIFA",                      &
       "<YW18FIFA> KA1ABC",                &
       "KA1ABC <YW18FIFA> -11",            &
       "<YW18FIFA> KA1ABC R-17",           &
       "<KA1ABC> YW18FIFA RR73",           &
       "<YW18FIFA> KA1ABC 73",             &
       "123456789ABCDEF012"/
