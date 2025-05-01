program testEchoCall

  parameter (NSPS=4480,NH=NSPS/2,NZ=3*12000)
  integer ihdr(11)
  integer itone(6)
  integer*2 iwave(NZ)                    !Raw data, 12000 Hz sample rate
  complex c0(NZ)                         !Analytic data, 6000 Hz sample rate
  complex c1(0:NH-1)
  real s(0:NSPS-1),p(0:NSPS-1)
  character*120 fname
  character*37 c
  character*6 callsign
  equivalence (nDop0,iwave(1))
  equivalence (nDopAudio0,iwave(3))
  equivalence (nfrit0,iwave(5))
  equivalence (f10,iwave(7))
  equivalence (fspread0,iwave(9))
  data c/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'/

  narg=iargc()
  if(narg.lt.2) then
     print*,'Usage: testEchoCall callsign fname1 [fname2, ...]'
     go to 999
  endif
  call getarg(1,callsign)

  itone=0                                               !Default character is blank
  k=1
  do i=1,len(trim(callsign))
     m=ichar(callsign(i:i))
     if(m.ge.48 .and. m.le.57) itone(i)=m-47       !0-9
     if(m.ge.65 .and. m.le.90) itone(i)=m-54       !A-Z
     if(m.ge.97 .and. m.le.122) itone(i)=m-86      !a-z
  enddo

  df=6000.0/NH
  do ifile=2,narg
     call getarg(ifile,fname)
     open(10,file=trim(fname),access='stream',status='unknown')
     read(10) ihdr,iwave
     close(10)
     iwave(1:12)=0
     call ana64(iwave,NZ,c0)
     p=0.
     do j=1,6
        ib=j*NH
        ia=ib-NH+1
        c1=c0(ia:ib)
        call four2a(c1,NH,1,-1,1)           !Forward c2c
        do i=0,NH-1
           s(i)=real(c1(i))**2 + aimag(c1(i))**2
           write(12,3012) i*df,s(i)
        enddo
        n=nint(itone(j)*2*df/df)
        p=p+cshift(s,n)
     enddo
     do i=0,NSPS/2
        write(13,3012) i*df,p(i)
3012    format(f10.3,e12.3)
     enddo
  enddo

999 end program testEchoCall
