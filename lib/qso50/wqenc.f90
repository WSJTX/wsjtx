subroutine wqenc(msg,ntype,data0)

!  Parse and encode a WSPR message.

  use packjt
  parameter (MASK15=32767)
  character*22 msg
  character*12 call1,call2
  character*4 grid
  character*9 name
  character ccur*4,cxp*2
  logical lbad1,lbad2
  integer*1 data0(11)
  integer nu(0:9)
  data nu/0,-1,1,0,-1,2,1,0,-1,1/

  read(msg,1001,end=1,err=1) ng,n1
1001 format(z4,z7)
  ntype=62
  n2=128*ng + (ntype+64)
  call pack50(n1,n2,data0)             !Pack 8 bits per byte, add tail
  go to 900

1  if(msg(1:6).eq.'73 DE ') go to 80
  if(index(msg,' W ').gt.0 .and. index(msg,' DBD ').gt.0) go to 90
  if(msg(1:4).eq.'QRZ ') go to 100
  if(msg(1:8).eq.'PSE QSY ') go to 110
  if(msg(1:3).eq.'WX ') go to 120

! Standard WSPR message (types 0 3 7 10 13 17 ... 60)
  i1=index(msg,' ')
  if(i1.lt.4 .or. i1.gt.7) go to 10
  call1=msg(:i1-1)
  grid=msg(i1+1:i1+4)
  call packcall(call1,n1,lbad1)
  call packgrid(grid,ng,lbad2)
  if(lbad1 .or. lbad2) go to 10
  ndbm=0
  read(msg(i1+5:),*,err=10,end=800) ndbm
  if(ndbm.lt.0 .or. ndbm.gt.60) go to 800
  ndbm=ndbm+nu(mod(ndbm,10))
  n2=128*ng + (ndbm+64)
  call pack50(n1,n2,data0)
  ntype=ndbm
  go to 900

! "BestDX" automated WSPR reply (type 1)
10 if(i1.ne.5 .or. msg(5:8).ne.' DE ') go to 20
  grid=msg(1:4)
  call packgrid(grid,ng,lbad2)
  if(lbad2) go to 800
  call1=msg(9:)
  call packcall(call1,n1,lbad1)
  if(lbad1) go to 800
  ntype=1
  n2=128*ng + (ntype+64)
  call pack50(n1,n2,data0)             !Pack 8 bits per byte, add tail
  go to 900

! CQ (msg #1; types 2, 4, 5)
20  if(msg(1:3).ne.'CQ ') go to 30
  if(index(msg,'/').le.0) then
     i2=index(msg(4:),' ')
     call1=msg(4:i2+3)
     grid=msg(i2+4:)
     call packcall(call1,n1,lbad1)
     if(lbad1) go to 30
     call packgrid(grid,ng,lbad2)
     if(lbad2) go to 30
     ntype=2
     n2=128*ng + (ntype+64)
     call pack50(n1,n2,data0)
  else
     ntype=4                                     ! or 5
     call1=msg(4:)
     call packpfx(call1,n1,ng,nadd)
     ntype=ntype+nadd
     n2=128*ng + ntype + 64
     call pack50(n1,n2,data0)
  endif
  go to 900

! Reply to CQ (msg #2; types 6,8,9,11)
30 if(msg(1:1).ne.'<' .and. msg(1:3).ne.'DE ') go to 40
  if(index(msg,' RRR ').gt.0) go to 50
  if(msg(1:1).eq.'<') then
     ntype=6
     i1=index(msg,'>')
     call1=msg(2:i1-1)
     read(msg(i1+1:),*,err=31,end=31) k,muf,ccur,cxp
     go to 130
31   call2=msg(i1+2:)
     call hash(call1,i1-2,ih)
     call packcall(call2,n1,lbad1)
     n2=128*ih + (ntype+64)
     call pack50(n1,n2,data0)
  else
     i1=index(msg(4:),' ')
     call1=msg(4:i1+2)
     if(index(msg,'/').le.0) then
        ntype=8
        ih=0
        call packcall(call1,n1,lbad1)
        grid=msg(i1+4:i1+7)
        call packgrid(grid,ng,lbad2)
        n2=128*ng + (ntype+64)
        call pack50(n1,n2,data0)
     else
        ntype=9                                   ! or 11
        call1=msg(4:)
        call packpfx(call1,n1,ng,nadd)
        ntype=ntype + 2*nadd
        n2=128*ng + ntype + 64
        call pack50(n1,n2,data0)
     endif
  endif
  go to 900

! Call(s) + report (msg #3; types -1 to -27)
! Call(s) + R + report (msg #4; types -28 to -54)
40 if(index(msg,' RRR').gt.0) go to 50
  i1=index(msg,'<')
  if(i1.gt.0 .and. (i1.lt.5 .or. i1.gt.8)) go to 50
  i2=index(msg,'/')
  if(i2.gt.0 .and.i2.le.4) then
     ntype=-10                                   ! -10 to -27
     i0=index(msg,' ')
     call1=msg(:i0-1)
     call packpfx(call1,n1,ng,nadd)
     ntype=ntype - 9*nadd
     i2=index(msg,' ')
     i3=index(msg,' R ')
     if(i3.gt.0) i2=i2+2                            !-28 to -36
     read(msg(i2+2:i2+2),*,end=800,err=800) nrpt
     ntype=ntype - (nrpt-1)
     if(i3.gt.0) ntype=ntype-27
     n2=128*ng + ntype + 64
     call pack50(n1,n2,data0)
     go to 900
  else if(i1.eq.0) then
     go to 50
  endif
  call1=msg(:i1-2)                               !-1 to -9
  i2=index(msg,'>')
  call2=msg(i1+1:i2-1)
  call hash(call2,i2-i1-1,ih)
  i3=index(msg,' R ')
  if(i3.gt.0) i2=i2+2                            !-28 to -36
  read(msg(i2+3:i2+3),*,end=42,err=42) nrpt
  go to 43
42 nrpt=1
43 ntype=-nrpt
  if(i3.gt.0) ntype=-(nrpt+27)
  call packcall(call1,n1,lbad1)
  n2=128*ih + (ntype+64)
  call pack50(n1,n2,data0)
  go to 900

50 i0=index(msg,'<')
  if(i0.le.0 .and. msg(1:3).ne.'DE ') go to 60
  i3=index(msg,' RRR')
  if(i3.le.0) go to 60
! Call or calls and RRR (msg#5; type2 12,14,15,16)
  i0=index(msg,'<')
  if(i0.eq.1) then
     if(index(msg,'/').le.0) then
        ntype=14
        i1=index(msg,'>')
        call1=msg(2:i1-1)
        call2=msg(i1+2:)
        i2=index(call2,' ')
        call2=call2(:i2-1)
        call packcall(call2,n1,lbad1)
        call hash(call1,i1-2,ih)
        n2=128*ih + (ntype+64)
        call pack50(n1,n2,data0)
     else
        stop '0002'
     endif
  else if(i0.ge.5 .and. i0.le.8) then
     if(index(msg,'/').le.0) then
        ntype=12
        i1=index(msg,'>')
        call1=msg(:i0-2)
        call2=msg(i0+1:i1-1)
        call packcall(call1,n1,lbad1)
        call hash(call2,i1-i0-1,ih)
        n2=128*ih + (ntype+64)
        call pack50(n1,n2,data0)
     else
        stop '0002'
     endif
  else
     i1=index(msg(4:),' ')
     call1=msg(4:i1+2)
     if(index(msg,'/').le.0) then
        ntype=9
        grid=msg(i1+4:i1+7)
     else
        ntype=15                                   ! or 16
        call1=msg(4:)
        i0=index(call1,' ')
        call1=call1(:i0-1)
        call packpfx(call1,n1,ng,nadd)
        ntype=ntype+nadd
        n2=128*ng + ntype + 64
        call pack50(n1,n2,data0)
     endif
  endif
  go to 900

! TNX <name> 73 GL (msg #6; type 18 ...)
60 if(msg(1:4).ne.'TNX ') go to 70
  ntype=18
  n1=0
  i2=index(msg(5:),' ')
  name=msg(5:i2+4)
  call packname(name,i2-1,n1,ng)
  n2=128*ng + (ntype+64)
  call pack50(n1,n2,data0)
  go to 900

! TNX name 73 GL (msg #6; type -56 ...)
70 if(msg(1:3).ne.'OP ') go to 80
  ntype=-56
  n1=0
  i2=index(msg(4:),' ')
  name=msg(4:i2+3)
  call packname(name,i2-1,n1,ng)
  n2=128*ng + (ntype+64)
  call pack50(n1,n2,data0)
  go to 900

! 73 DE call grid (msg #6; type 19)
80 if(msg(1:6).ne.'73 DE ') go to 90
  ntype=19
  i1=index(msg(7:),' ')
  call1=msg(7:)
  if(index(call1,'/').le.0) then
     i1=index(call1,' ')
     grid=call1(i1+1:)
     call1=call1(:i1-1)
     call packcall(call1,n1,lbad1)
     call packgrid(grid,ng,lbad2)
     if(lbad1 .or. lbad2) go to 800
     n2=128*ng + (ntype+64)
     call pack50(n1,n2,data0)
     go to 900
  else
     ntype=21                                   ! or 22
     call packpfx(call1,n1,ng,nadd)
     ntype=ntype + nadd
     n2=128*ng + ntype + 64
     call pack50(n1,n2,data0)
     go to 900
  endif

! [pwr] W [gain] DBD [73 GL] (msg #6; types 24, 25)
90  if(index(msg,' W ').le.0) go to 140
  ntype=25
  if(index(msg,' DBD 73 GL').gt.0) ntype=24
  i1=index(msg,' ')
  read(msg(:i1-1),*,end=800,err=800) watts
  if(watts.ge.1.0) nwatts=watts
  if(watts.lt.1.0) nwatts=3000 + nint(1000.*watts)
  if(index(msg,'DIPOLE').gt.0) then
     ndbd=30000
  else if(index(msg,'VERTICAL').gt.0) then
     ndbd=30001
  else
     i2=index(msg(i1+3:),' ')
     read(msg(i1+3:i1+i2+1),*,end=800,err=800) ndbd
  endif
  n1=nwatts
  ng=ndbd + 32
  n2=128*ng + (ntype+64)
  call pack50(n1,n2,data0)
  go to 900

! QRZ call (msg #3; type 26)
100 call1=msg(5:)
  call packcall(call1,n1,lbad1)
  if(lbad1) go to 800
  ntype=26
  n2=ntype+64
  call pack50(n1,n2,data0)
  go to 900

! PSE QSY [nnn] KHZ (msg #6; type 28)
110 ntype=28
  read(msg(9:),*,end=800,err=800) n1
  n2=ntype+64
  call pack50(n1,n2,data0)
  go to 900

! WX wx temp C|F wind (msg #6; type 29)
120 ntype=29
  if(index(msg,' CLEAR ').gt.0) then
     i1=10
     n1=10000
  else if(index(msg,' CLOUDY ').gt.0) then
     i1=11
     n1=20000
  else if(index(msg,' RAIN ').gt.0) then
     i1=9
     n1=30000
  else if(index(msg,' SNOW ').gt.0) then
     i1=9
     n1=40000
  endif
  read(msg(i1:),*,err=800,end=800) ntemp
  ntemp=ntemp+100
  i1=index(msg,' C ')
  if(i1.gt.0) ntemp=ntemp+1000
  n1=n1+ntemp
  if(index(msg,' CALM').gt.0) ng=1
  if(index(msg,' BREEZES').gt.0) ng=2
  if(index(msg,' WINDY').gt.0) ng=3
  if(index(msg,' DRY').gt.0) ng=4
  if(index(msg,' HUMID').gt.0) ng=5

  n2=128*ng + (ntype+64)
  call pack50(n1,n2,data0)

  go to 900

! Solar/geomagnetic/ionospheric data
130 ntype=63
  call packprop(k,muf,ccur,cxp,n1)
  call hash(call1,i1-2,ih)
  n2=128*ih + ntype + 64 
  call pack50(n1,n2,data0)
  go to 900

140 continue

! Plain text
800 ntype=-57
  call packtext2(msg(:8),n1,ng)
  n2=128*ng + ntype + 64
  call pack50(n1,n2,data0)
  go to 900

900 continue
  return
end subroutine wqenc
