module jt4
  parameter (MAXAVE=64)
  integer iutc(MAXAVE)
  integer nfsave(MAXAVE)
  integer listutc(10)
  real    ppsave(207,7,MAXAVE)           !Accumulated data for message averaging
  real    rsymbol(207,7)                 
  real    dtsave(MAXAVE)
  real    syncsave(MAXAVE)
  real    flipsave(MAXAVE)
  real    zz(1260,65,7)

  integer nsave,nlist,ich1,ich2
  integer nch(7)
  integer npr(207)
  data rsymbol/1449*0.0/
  data nch/1,2,4,9,18,36,72/
  data npr/                                                         &
       0,0,0,0,1,1,0,0,0,1,1,0,1,1,0,0,1,0,1,0,0,0,0,0,0,0,1,1,0,0, &
       0,0,0,0,0,0,0,0,0,0,1,0,1,1,0,1,1,0,1,0,1,1,1,1,1,0,1,0,0,0, &
       1,0,0,1,0,0,1,1,1,1,1,0,0,0,1,0,1,0,0,0,1,1,1,1,0,1,1,0,0,1, &
       0,0,0,1,1,0,1,0,1,0,1,0,1,0,1,1,1,1,1,0,1,0,1,0,1,1,0,1,0,1, &
       0,1,1,1,0,0,1,0,1,1,0,1,1,1,1,0,0,0,0,1,1,0,1,1,0,0,0,1,1,1, &
       0,1,1,1,0,1,1,1,0,0,1,0,0,0,1,1,0,1,1,0,0,1,0,0,0,1,1,1,1,1, &
       1,0,0,1,1,0,0,0,0,1,1,0,0,0,1,0,1,1,0,1,1,1,1,0,1,0,1/

  save ppsave,zz
  
end module jt4
