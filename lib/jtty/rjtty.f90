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

   parameter (NSPS=384)
   parameter (NMAX=30*12000)                 !Max length of data
   parameter (NFRAME=53*NSPS)
   parameter (NCHUNK=NFRAME + NFRAME/4)
   type(hdr) h
   character*80 fname
   character*80 umsg
   character*8 arg
   integer*8 count0, count1, clkfreq
   integer*2 iwave(NMAX)
   logical synced,success

   f0=1500.0
   ftol=50.0

   nargs=iargc()
   if(nargs.lt.5) then
      print*,'Usage:    rjtty smin ndebug  f0  ftol  fname [...]'
      print*,'Examples: rjtty   2    0    1500  50  000000_000001.wav'
      print*,'          rjtty   2    1    1500  50  *.wav'
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
      nwave=min(h%ndata/2,NMAX)
      read(10) iwave(1:nwave)
      close(10)
      if(nwave.lt.NMAX) iwave(nwave+1:NMAX) = 0
      kchar=0
      istart=1
      synced=.false. 
      nsync=0
! Process data on the fly, one buffer at a time:
      do while (istart+NCHUNK-1 .le. nwave)
         success=.false.
!synced=.false.               !uncomment this to disable use of prior sync
         call system_clock(count0,clkfreq)
         call jtty_decode(iwave(istart),NCHUNK,nsps,f0,ftol,smin,synced,xdt,  &
            f1,snr,umsg,success,nharderrors,nsync,dmin)
         call system_clock(count1,clkfreq)
         if(success) then
            if(umsg(1:4).eq.'599 ') umsg='~'//trim(umsg)
            n = len(trim(umsg))
            do i=1,n
               if(umsg(i:i).eq.'~') umsg(i:i)=' '
            enddo
            kchar = kchar + n
            if(kchar.lt.80) then
               write(*,'(a)',advance='no') umsg(1:n)
               if(umsg(n-2:n).eq.' CQ') write(*,'(a)',advance='no') ' '
            else
               write(*,'(a)') umsg(1:n)
               write(*,*) 'debug ',umsg(1:n)
            endif
            tdecode=float(count1-count0)/clkfreq
            if(ndebug.gt.0) then
               write(71,3071) istart,xdt,f1,snr,synced,success,nsync,  &
                    nharderrors,dmin,tdecode,trim(umsg)
3071           format(i8,f7.3,f8.1,f6.1,2L3,i5,i5,f9.1,f7.3,2x,a)
            endif
            istart=istart+NFRAME
            if(nsync.lt.12) synced=.false.
         else
            istart=istart+NFRAME/4
            synced=.false.
         endif
      enddo
      write(*,*) ''
   enddo  !ifile

999 end program rjtty
