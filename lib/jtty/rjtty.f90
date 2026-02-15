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

   parameter (NMAX=180*12000)                 !Max length of data
   type(hdr) h
   character*80 fname
   character*80 umsg
   character*8 arg
   integer*8 count0, count1, clkfreq
   integer*2 iwave(NMAX)
   logical synced,success,newsig
    
   f0=1500.0
   ftol=50.0
   nsps = 384

   nargs=iargc()
   if(nargs.lt.6) then
      print*,'Usage:    rjtty smin ndebug nsps  f0  ftol  fname [...]'
      print*,'Examples: rjtty   4    0    384  1500  50  000000_000001.wav'
      print*,'          rjtty   4    1    240  1500  50  *.wav'
      print*,'nsps choices are: 240, 320, 384, 480 samples/symbol'
      go to 999
   endif
   call getarg(1,arg)
   read(arg,*) smin
   call getarg(2,arg)
   read(arg,*) ndebug
   call getarg(3,arg)
   read(arg,*) nsps 
   if(nsps .ne. 240 .and. nsps .ne. 320 .and. nsps .ne. 384 .and. nsps .ne. 480) then
      print*,'nsps choices are: 240, 320, 384, 480 samples/symbol'
   endif
   call getarg(4,arg)
   read(arg,*) f0
   call getarg(5,arg)
   read(arg,*) ftol

   nframe = 53*nsps
   nchunk = nframe + nframe/4
   ndecodes = 0

   do ifile=1,nargs-5
      call getarg(ifile+5,fname)
      open(10,file=fname,status='old',access='stream')
      read(10) h
      nwave=min(h%ndata/2,NMAX)
      read(10) iwave(1:nwave)
      close(10)
      if(nwave.lt.NMAX) iwave(nwave+1:NMAX) = 0
      kchar=0
      istart=1
      nsync=0
      f1good = -99.
      xdtgood = -99.
      missed_syncs = 0
      newsig = .false.

! Process data on the fly, one buffer at a time:
      do while (istart+nchunk-1 .le. nwave)
         success=.false.
         call system_clock(count0,clkfreq)
         call jtty_mdecode(iwave(istart),nchunk,nsps,f0,ftol,smin,synced,xdt,  &
            f1,snr,umsg,success,nharderrors,nsync,dmin)
         call system_clock(count1,clkfreq)

         if(success) then
            missed_syncs=0
         else
            missed_syncs = missed_syncs + 1
            if(missed_syncs.ge.2) then
               f1good = -99.
               xdtgood = -99.
               missed_syncs = 0
               newsig = .false.
            endif
         endif

         if(success) then
            ndecodes = ndecodes + 1
            newsig = .false.
            if(abs(f1-f1good).gt.2.0 .or. abs(xdt-xdtgood).gt.0.004) then
               newsig = .true.
               if(ndecodes.gt.1) write(*,'(a)')         !Advance to next display line
            endif
            f1good = f1
            xdtgood = xdt

            if(ndebug.gt.0) then
               tdecode=float(count1-count0)/clkfreq
               write(71,3071) istart,xdt,f1,snr,synced,success,newsig,kchar,   &
                    missed_syncs,nsync,nharderrors,dmin,tdecode,trim(umsg)
3071           format(i8,f7.3,f7.1,f6.1,3L2,4i4,f7.1,f7.3,2x,a)
            endif

            if(umsg(1:4).eq.'599 ') umsg='~'//trim(umsg)
            n = len(trim(umsg))
            do i=1,n
               if(umsg(i:i).eq.'~') umsg(i:i)=' '
            enddo
            kchar = min(kchar + n, 80)
            write(*,'(a)',advance='no') umsg(1:n)
            if(n.ge.3) then
               if(umsg(n-2:n).eq.' CQ') write(*,'(a)',advance='no') ' '
            endif
         endif
         istart=istart+nframe/4
      enddo
      write(*,*) ''
   enddo  !ifile

999 end program rjtty
