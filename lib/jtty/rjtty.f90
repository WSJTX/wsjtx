program rjtty

! Decode JTTY data in one or more WAV files.

   use wavhdr
!
! MAX_FRAMES = 16 in pack_jtty. Each frame is
! 53 symbols (13 sync + 40 codeword).
! Maximum length of a transmission is
! 16*53*(symbol_duration).
! With baud rate 31.25 s^-1, symbol duration = 32 ms,
! so maximum txt = 16*53*0.032 = 27.136 s.
!
   parameter (NMAX=20*12000)                 !Max length of data
   parameter (NSPB=2048)                     !Samples per buffer
   type(hdr) h
   character*80 fname
   character*80 umsg
   character*8 arg
   integer*2 iwave(NMAX)
   logical synced,eom,synced0
   data synced/.false./,eom/.false./

   nargs=iargc()
   if(nargs.lt.3) then
      print*,'Usage:    rjtty smin ifly fname [...]'
      print*,'Examples: rjtty   5    0  000000_000001.wav'
      print*,'          rjtty   5    1    *.wav'
      go to 999
   endif
   call getarg(1,arg)
   read(arg,*) smin
   call getarg(2,arg)
   read(arg,*) ifly
   call getarg(3,arg)
   read(arg,*) icoh

   f0=1500.0
   ftol=50.0

   do ifile=1,nargs-3
      call getarg(ifile+3,fname)
      open(10,file=fname,status='old',access='stream')
      read(10) h
      nwave=h%ndata/2
      read(10) iwave(1:nwave)
      close(10)
      iwave(nwave+1:) = 0
      iz=0
      synced=.false.
      synced0=.false.
      eom=.false.

      if(ifly.eq.0) then
! Decode using the full iwave(1:nwave)
         iz=nwave
         call jtty_decode(iwave,iz,f0,ftol,smin,synced,xdt,f1,snr,umsg)
         write(*,3001) ifile,xdt,f1,snr,trim(umsg)
3001     format(i3,f8.2,2f8.1,2x,a)
      else

   istart=1
   nframe=53*384+7680
   kchar=0

! Process data on the fly, one buffer at a time:
         do ibuf=1,16
            synced=.false.                      ! sync on evey call for now
            call jtty_decode(iwave(istart),nframe,f0,ftol,smin,synced,xdt,  &
                 f1,snr,umsg)
            if(synced) then
               n = len(trim(umsg))
               do i=1,n
                  if(umsg(i:i).eq.'~') umsg(i:i)=' '
               enddo
               kchar = kchar + n
               if(kchar.lt.80) then
                  write(*,'(a)',advance='no') umsg(1:n)
               else
                  write(*,'(a)') umsg(1:n)
                  write(*,*) 'debug ',umsg(1:n)
               endif
            endif
            istart=istart+53*384-1
            if(nwave-istart .lt. nframe/2) exit
         enddo
         write(*,*)
      endif
   enddo  !ifile

999 end program rjtty
