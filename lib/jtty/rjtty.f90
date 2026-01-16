program rjtty

! Decode JTTY data in one or more WAV files.

   use wavhdr
   use jtty_mod

! MAX_FRAMES = 16 in pack_jtty. Each frame is
! 53 symbols (13 sync + 40 codeword).
! Maximum length of a transmission is
! 16*53*(symbol_duration).
! With baud rate 31.25 s^-1, symbol duration = 32 ms,
! so maximum txt = 16*53*0.032 = 27.136 s.

   parameter (NMAX=30*12000)                 !Max length of data
   parameter (NSPB=2048)                     !Samples per buffer
   type(hdr) h
   character*80 fname
   character*80 umsg
   character*8 arg
   integer*8 count0, count1, clkfreq
   integer*2 iwave(NMAX)
   logical synced,success
   data synced/.false./

   f0=1500.0
   ftol=50.0

   nargs=iargc()
   if(nargs.lt.5) then
      print*,'Usage:    rjtty smin ndebug  f0  ftol  fname [...]'
      print*,'Examples: rjtty   3    0    1500  50  000000_000001.wav'
      print*,'          rjtty   3    1    1500  50  *.wav'
      go to 999
   endif
   call getarg(1,arg)
   read(arg,*) smin
   call getarg(2,arg)
   read(arg,*) ndebug
   call getarg(3,arg)
   read(arg,*) f0
   call getarg(4,arg)
   read(arg,*) ftol


   do ifile=1,nargs-4
      call getarg(ifile+4,fname)
      open(10,file=fname,status='old',access='stream')
      read(10) h
      nwave=h%ndata/2
      read(10) iwave(1:min(nwave,360000))
      close(10)
      iwave(nwave+1:) = 0

      synced=.false.
      nchunk=53*384+53*384/4
      nframe=53*384
      kchar=0
      istart=1

! Process data on the fly, one buffer at a time:
      do while (istart+nchunk-1 .le. nwave)
         synced=.false.                      ! sync on evey call for now
         success=.false.
 
         call system_clock(count0,clkfreq)
         call jtty_decode(iwave(istart),nchunk,f0,ftol,smin,synced,xdt,  &
            f1,snr,umsg,success,nharderrors,nsync)
         call system_clock(count1,clkfreq)

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
            tdecode=float(count1-count0)/clkfreq
            if(ndebug.gt.0) write(71,3071) istart,xdt,f1,snr,synced,nsync,nharderrors,tdecode, &
               trim(umsg)
3071        format(i8,f7.3,f8.1,f6.1,L3,i5,i5,f7.3,2x,a)
         endif
         istart=istart+nframe/4
      enddo
      write(*,*) ''
   enddo  !ifile

999 end program rjtty
