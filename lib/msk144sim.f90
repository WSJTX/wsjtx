program msk144sim

  use wavhdr
  parameter (NMAX=15*12000)
  real pings(0:NMAX-1)
  real waveform(0:NMAX-1)
  character arg*8,msg*37,msgsent*37,fname*40
  real wave(0:NMAX-1)              !Simulated received waveform
  real*8 twopi,freq,phi,dphi0,dphi1,dphi
  type(hdr) h                          !Header for .wav file
  integer*2 iwave(0:NMAX-1)
  integer itone(144)                   !Message bits

  nargs=iargc()
  if(nargs.ne.5) then
     print*,'Usage:   msk144sim       message      freq width snr nfiles'
     print*,'Example: msk144sim "K1ABC W9XYZ EN37" 1500  0.12   2    1'
     print*,'         msk144sim "K1ABC W9XYZ EN37" 1500  2.5   15    1'
     go to 999
  endif
  call getarg(1,msg)
  call getarg(2,arg)
  read(arg,*) freq
  call getarg(3,arg)
  read(arg,*) width
  call getarg(4,arg)
  read(arg,*) snrdb
  call getarg(5,arg)
  read(arg,*) nfiles

!sig is the peak amplitude of the ping. 
  sig=sqrt(2.0)*10.0**(0.05*snrdb)
  h=default_header(12000,NMAX)
  i1=len(trim(msg))-5
  ichk=0
  itype=1
  call fmtmsg(msg,iz)
  call genmsk_128_90(msg,ichk,msgsent,itone,itype) 
  write(*,*) 'Requested message: ',msg
  write(*,*) 'Message sent     : ',msgsent
  write(*,*) 'Tones: '
  if(itone(41).ge.0) then
     write(*,'(1x,72i1)') itone(1:72)
     write(*,'(1x,72i1)') itone(73:144)
  else
     write(*,'(1x,40i1)') itone(1:40)
  endif

  twopi=8.d0*atan(1.d0)
  nsym=144
  nsps=6
  if( itone(41) .lt. 0 ) nsym=40
  baud=2000.d0
  dphi0=twopi*(freq-0.25d0*baud)/12000.d0
  dphi1=twopi*(freq+0.25d0*baud)/12000.d0
  phi=0.0
  k=0
  nreps=NMAX/(nsym*nsps)

  do jrep=1,nreps
    do i=1,nsym
      if( itone(i) .eq. 0 ) then
        dphi=dphi0
      else
        dphi=dphi1
      endif
      do j=1,nsps
        waveform(k)=cos(phi)
        k=k+1
        phi=mod(phi+dphi,twopi)
      enddo 
    enddo
  enddo 

  if(itype.lt.1 .or. itype.gt.7) then
     print*,'Illegal message'
     go to 999
  endif

  call makepings(pings,NMAX,width,sig)

!  call sgran()
  do ifile=1,nfiles                  !Loop over requested number of files
     write(fname,1002) ifile         !Output filename
1002 format('000000_',i6.6)
     open(10,file=fname(1:13)//'.wav',access='stream',status='unknown')

     wave=0.0
     iwave=0
     fac=sqrt(6000.0/2500.0)
     do i=0,NMAX-1
        xx=gran()
        wave(i)=pings(i)*waveform(i) + fac*xx
        iwave(i)=30.0*wave(i)
     enddo

     write(10) h,iwave               !Save the .wav file
     close(10)

  enddo

999 end program msk144sim
