program rjtty

! Decode JTTY data in one or more WAV files.

   use wavhdr
   use jtty_mod
   use jtty_mdec

! MAX_FRAMES = 16 in pack_jtty. Each frame is
! 59 symbols (13 sync + 46 codeword).
! Maximum length of a transmission is
! 16*59*(symbol_duration).
! With baud rate 31.25 s^-1, symbol duration = 32 ms,
! so maximum txt = 16*59*0.032 = 30.208 s.

   parameter (NMAX=180*12000)                 !Max length of data
   type(hdr) h
   character*80 fname
   character*8 arg
   integer*2 iwave(NMAX)
   logical success,newsig
    
   f0=1500.0
   ftol=50.0
   nsps = 384
   nfa=200
   nfb=2800

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

   nframe = 59*nsps
   nchunk = nframe + nframe/4
   ndecodes = 0

   do ifile=1,nargs-5
      call getarg(ifile+5,fname)
      open(10,file=fname,status='old',access='stream')
      read(10) h
      nwave=min(h%ndata/2,NMAX)
      read(10) iwave(1:nwave)
      close(10)
      if(ndebug.gt.0) then
         n=len_trim(fname)
         write(*,'(a)') fname(n-16:n)
      endif
      if(nwave.lt.NMAX) iwave(nwave+1:NMAX) = 0
      ndecodes=0
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
         call jtty_mdecode_step(iwave,nwave,istart,1,nchunk,nsps,ndebug,nfa,nfb,f0,ftol,smin)
         istart=istart+nframe/4
      enddo
   enddo  !ifile

999 end program rjtty
