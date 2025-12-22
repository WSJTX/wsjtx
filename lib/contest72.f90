program contest72

  use packjt
  integer dat(12)
  logical text,bcontest,ok
  character*22 msg,msg0,msg1
  character*72 ct1,ct2
  character*12 callsign1,callsign2
  character*1 c0
  character*42 c
  character*6 mygrid
  data c/'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?'/
  data bcontest/.true./
  data mygrid/"EM48  "/

! itype Message Type
!--------------------
!   1   Standardd message
!   2   Type 1 prefix
!   3   Type 1 suffix
!   4   Type 2 prefix
!   5   Type 2 suffix
!   6   Free text
!  -1   Does not decode correctly
  
  nargs=iargc()
  if(nargs.eq.0) open(10,file='contest_msgs.txt',status='old')

  nn=0
  do imsg=1,9999
     if(nargs.eq.1) then
        if(imsg.gt.1) exit
        call getarg(1,msg0)
     else
        read(10,1001,end=999) msg0
1001    format(a22)
     endif
     msg=msg0
     call packmsg(msg,dat,itype,bcontest)
     call unpackmsg(dat,msg1,bcontest,mygrid)
     ok=msg1.eq.msg0
     if(msg0.eq.'                      ') then
        write(*,1002)
     else
        if(jt_c2(1:1).eq.'W') msg0='  '//msg0(1:20)
        nn=nn+1
        write(*,1002) nn,msg0,ok,jt_itype,jt_nc1,jt_nc2,jt_ng,jt_k1,jt_k2
1002    format(i1,'. ',a22,L2,i2,2i10,i6,2i8)
        if(index(msg1,' 73 ').gt.4) nn=0
     endif
     if(.not.ok) print*,msg0,msg1
     if(itype.lt.0 .or. itype.eq.6) cycle
     
     if(msg(1:3).eq.'CQ ') then
        m=2
        write(ct1,1010) dat
1010    format(12b6.6)
!        write(*,1014) ct1
!1014    format(a72)
        cycle
     endif

     i1=index(msg,'<')
     if(i1.eq.1) then
        m=0
        cycle
     endif
     
     if(i.ge.5) then
        m=3
        cycle
     endif

     if(msg(1:6).eq.'73 CQ ') then
        m=4
        cycle
     endif
     
     call packmsg(msg,dat,itype,.false.)
     write(ct1,1010) dat
     call packtext(msg,nc1,nc2,ng,.false.,'')
!     write(ct2,1012) nc1,nc2,ng+32768
!1012 format(2b28.28,b16.16)
!     write(*,1014) ct1
!     write(*,1014) ct2
!     write(*,1014)
  enddo

999 end program contest72
