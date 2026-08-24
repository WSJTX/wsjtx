program jt65

  ! Test the JT65 decoder for WSJT-X

  use options
  use timer_module, only: timer
  use timer_impl, only: init_timer
  use jt65_test
  use readwav

  character c,mode
  logical :: display_help=.false.,nrobust=.false.,single_decode=.false., ljt65apon=.false.
  type(wav_header) :: wav
  integer*2 id2(NZMAX)
  real*4 dd(NZMAX)
  character*80 infile
  character(len=500) optarg
  character*12 mycall,hiscall
  character*6 hisgrid
  integer :: files_read, samples_read

  type (option) :: long_options(12) = [ &
       option ('aggressive',.true.,'a','aggressiveness [0-10], default AGGR=0','AGGR'), &
       option ('depth',.true.,'d','depth=5 hinted decoding, default DEPTH=0','DEPTH'),  &
       option ('freq',.true.,'f','signal frequency, default FREQ=1270','FREQ'),         &
       option ('help',.false.,'h','Display this help message',''),                      &
       option ('mode',.true.,'m','Mode A, B, C. Default is A.','MODE'),                 &
       option ('ntrials',.true.,'n','number of trials, default TRIALS=10000','TRIALS'), &
       option ('robust-sync',.false.,'r','robust sync',''),                             &
       option ('my-call',.true.,'c','my callsign',''),                                  &
       option ('his-call',.true.,'x','his callsign',''),                                &
       option ('his-grid',.true.,'g','his grid locator',''),                            &
       option ('experience-decoding',.true.,'X'                                         &
               ,'experience decoding options (1..n), default FLAGS=0','FLAGS'),         &
       option ('single-signal-mode',.false.,'s','decode at signal frequency only','') ]

  naggressive=10
  nfqso=1500
  ntrials=100000
  nexp_decode=0
  ntol=20
  nsubmode=0
  nlow=200
  nhigh=4000
  n2pass=1
  ndepth=1
  nQSOProgress=6

  do
     call getopt('a:d:f:hm:n:rc:x:g:X:s',long_options,c,optarg,narglen,nstat,noffset,nremain,.true.)
     if( nstat .ne. 0 ) then
        exit
     end if
     select case (c)
     case ('a')
        read (optarg(:narglen), *) naggressive
     case ('d')
        read (optarg(:narglen), *) ndepth
     case ('f')
        read (optarg(:narglen), *) nfqso
     case ('h')
        display_help = .true.
     case ('m')
        read (optarg(:narglen), *) mode
        if( mode .eq. 'b' .or. mode .eq. 'B' ) then
          nsubmode=1
        endif
        if( mode .eq. 'c' .or. mode .eq. 'C' ) then
          nsubmode=2
        endif
     case ('n')
        read (optarg(:narglen), *) ntrials
     case ('r')
        nrobust=.true.
     case ('c')
        read (optarg(:narglen), *) mycall
     case ('x')
        read (optarg(:narglen), *) hiscall
     case ('g')
        read (optarg(:narglen), *) hisgrid
     case ('X')
        read (optarg(:narglen), *) nexp_decode
     case ('s')
        single_decode=.true.
        ntol=100
        nlow=nfqso-ntol
        nhigh=nfqso+ntol
        n2pass=1
     end select
  end do

  if(single_decode) nexp_decode=ior(nexp_decode,32)
  if(display_help .or. nstat.lt.0 .or. nremain.lt.1) then
     print *, ''
     print *, 'Usage: jt65 [OPTIONS] file1 [file2 ...]'
     print *, ''
     print *, '       JT65 decode pre-recorded .WAV file(s)'
     print *, ''
     print *, 'OPTIONS:'
     print *, ''
     do i = 1, size (long_options)
        call long_options(i) % print (6)
     end do
     go to 999
  endif

  call init_timer ('timer.out')
  call timer('jt65    ',0)

  ndecoded=0
  files_read=0
  do ifile=noffset+1,noffset+nremain
     nfa=nlow
     nfb=nhigh
     minsync=0
     call get_command_argument(ifile,optarg,narglen)
     infile=optarg(:narglen)
     call timer('read    ',0)
     call wav%read (infile, ios, optarg)
     if (ios /= 0) then
        call timer('read    ',1)
        write(*,*) 'Cannot read WAV file ',trim(infile),': ',trim(optarg)
        cycle
     end if
     if (wav%audio_format%sample_rate /= 12000) then
        close(unit=wav%lun)
        call timer('read    ',1)
        write(*,*) 'Cannot read WAV file ',trim(infile),': unsupported sample rate'
        cycle
     end if
     i1=index(infile,'.wav')
     if( i1 .eq. 0 ) i1=index(infile,'.WAV')
     read(infile(i1-4:i1-1),*,err=998) nutc
     npts=52*12000
     id2=0
     call wav%read_samples(id2(1:npts), samples_read, ios, optarg)
     close(unit=wav%lun)
     call timer('read    ',1)
     if (ios /= 0) then
        write(*,*) 'Cannot read WAV file ',trim(infile),': ',trim(optarg)
        cycle
     end if
     if (samples_read < npts) print*,'EOF on input file ',trim(infile)
     files_read=files_read+1
     dd(1:npts)=id2(1:npts)
     dd(npts+1:)=0.
     call test(dd,nutc,nfa,nfb,nfqso,ntol,nsubmode, &
          n2pass,nrobust,ntrials,naggressive,ndepth, &
          mycall,hiscall,hisgrid,nexp_decode,nQSOProgress,ljt65apon)
!     if(nft.gt.0) exit
  enddo

  call timer('jt65    ',1)
  call timer('jt65    ',101)
  if (files_read == 0) stop 2
  !  call four2a(a,-1,1,1,1)                  !Free the memory used for plans
  !  call filbig(a,-1,1,0.0,0,0,0,0,0)        ! (ditto)
  go to 999

998 print*,'Cannot read from file:'
  print*,infile

999 continue
end program jt65
