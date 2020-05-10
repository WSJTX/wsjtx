  parameter (MAXTEST=75,NTEST=47)
  character*37 testmsg(MAXTEST)
  data testmsg(1:NTEST)/                           &
      "TNX BOB 73 GL",                             &     ! 0.0
      "PA9XYZ 590003 IO91NP",                      &     ! 0.2
      "G4ABC/P R 570007 JO22DB",                   &     ! 0.2
      "K1ABC W9XYZ 6A WI",                         &     ! 0.3
      "W9XYZ K1ABC R 17B EMA",                     &     ! 0.3
      "123456789ABCDEF012",                        &     ! 0.5
      "CQ K1ABC FN42",                             &     ! 1.
      "K1ABC W9XYZ EN37",                          &     ! 1.
      "W9XYZ K1ABC -11",                           &     ! 1.
      "K1ABC W9XYZ R-09",                          &     ! 1.
      "W9XYZ K1ABC RRR",                           &     ! 1.
      "K1ABC W9XYZ 73",                            &     ! 1.
      "K1ABC W9XYZ RR73",                          &     ! 1.
      "CQ FD K1ABC FN42",                          &     ! 1.
      "CQ TEST K1ABC/R FN42",                      &     ! 1.
      "K1ABC/R W9XYZ EN37",                        &     ! 1.
      "W9XYZ K1ABC/R R FN42",                      &     ! 1.
      "K1ABC/R W9XYZ RR73",                        &     ! 1.
      "CQ TEST K1ABC FN42",                        &     ! 1.
      "W9XYZ <PJ4/K1ABC> -11",                     &     ! 1.
      "<PJ4/K1ABC> W9XYZ R-09",                    &     ! 1.
      "CQ W9XYZ EN37",                             &     ! 1.
      "<YW18FIFA> W9XYZ -11",                      &     ! 1.
      "W9XYZ <YW18FIFA> R-09",                     &     ! 1.
      "<YW18FIFA> KA1ABC",                         &     ! 1.
      "KA1ABC <YW18FIFA> -11",                     &     ! 1.
      "<YW18FIFA> KA1ABC R-17",                    &     ! 1.
      "<YW18FIFA> KA1ABC 73",                      &     ! 1.
      "CQ G4ABC/P IO91",                           &     ! 2.
      "G4ABC/P PA9XYZ JO22",                       &     ! 2.
      "PA9XYZ G4ABC/P RR73",                       &     ! 2.
      "K1ABC W9XYZ 579 WI",                        &     ! 3.
      "W9XYZ K1ABC R 589 MA",                      &     ! 3.
      "K1ABC KA0DEF 559 MO",                       &     ! 3.
      "TU; KA0DEF K1ABC R 569 MA",                 &     ! 3.
      "KA1ABC G3AAA 529 0013",                     &     ! 3.
      "TU; G3AAA K1ABC R 559 MA",                  &     ! 3.
      "CQ KH1/KH7Z",                               &     ! 4.
      "CQ PJ4/K1ABC",                              &     ! 4.
      "PJ4/K1ABC <W9XYZ>",                         &     ! 4.
      "<W9XYZ> PJ4/K1ABC RRR",                     &     ! 4.
      "PJ4/K1ABC <W9XYZ> 73",                      &     ! 4.
      "<W9XYZ> YW18FIFA",                          &     ! 4.
      "YW18FIFA <W9XYZ> RRR",                      &     ! 4.
      "<W9XYZ> YW18FIFA 73",                       &     ! 4.
      "CQ YW18FIFA",                               &     ! 4.
      "<KA1ABC> YW18FIFA RR73"/
