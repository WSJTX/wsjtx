program sjtty_qrm

  ! Simulate received data for JTTY, a mode operationally similar to RTTY
  ! but providing much better performance and reliability.

  ! Messages are source-encoded into 34-bit blocks. A 12-bit CRC is
  ! added to create a 46 bit payload, which is then FEC-encoded using
  ! a tail-biting rate-1/2 convolutional code (K=10) to create 92-bit
  ! (46-symbol) codewords.

  ! Modulation is 4FSK at 12000/NSPS = 31.25 baud. Each transmitted frame
  ! consists of 13 sync symbols followed by 46 codeword symbols.

  ! This version generates multiple signals spread across nfa to nfb Hz.
  ! One signal falls at f0 = 1500 Hz and has the message "599 123" (or, if
  ! an 8th command-line argument is given, that text instead -- a longer,
  ! multi-frame message there is useful for exercising continuation-frame
  ! logic, e.g. the sticky-sync retry, that a single-frame message never
  ! reaches). Others are spread in frequency and DT, and contain just a
  ! callsign.

  use wavhdr
  use jtty_mod
  use jtty_fec

  parameter (NMAX=131072)           !Max size of .wav file
  parameter (MAX_TONES=59*16)       !Max number of channel symbols
  parameter (MAX_SIGS=20)           !Max number of signals
  character*12 arg                  !Command line argument
  character*2 arg4                  !The 4th command-line argument
  character*80 umsg                 !User-formatted message
  character*80 msg_override         !Optional 8th-arg override for f0=1500 signal
  character*17 fname                !Output file name,
  character*10 flags                !Single-character shorthand flags
  character*34 c32(16)
  character*6 xcall(25)
  complex cwave(0:NMAX-1)           !Complex generated waveform (12000 Hz)
  complex c0(0:NMAX-1)              !With propagation degradation
  complex cdat(0:NMAX-1)
  real wave (NMAX)                  !Real generated waveform (12000 Hz)
  real xnoise(NMAX)
  type(hdr) h                       !Header for .wav file
  integer itone(MAX_TONES)          !Array of tone frequencies for this message
  integer*2 iwave(NMAX)             !Data written to the *.wav file
  integer payload(PAYLOAD_BITS)
  integer tone_symbols(TOTAL_K)
  data flags/'!@#$%^&*()'/
  data idum/-1/

  ! Some arbitrary callsigns:
  data xcall/'AC9QI ','CF7GEM','EA1AAE','GI4FUE','IU8CNE','K2AK  ',   &
             'K5CIA ','K7CAH ','KA9KQH','KE0N  ','KM2J  ','KP4SX ',   &
             'LV7QFH','N2PPI ','N6GP  ','N8HMG ','NV8B  ','PA2GP ',   &
             'PY7BC ','W0OGH ','W4EIS ','W6RYO ','W8OI  ','WA2HIP',   &
             'WB2KSP'/
  
  nargs=iargc()
  if(nargs.ne.7 .and. nargs.ne.8) then
     print*,'Usage:   sjtty_qrm nsps prop nfa  nfb nsigs nfiles snr [message]'
     print*,'Example: sjtty_qrm  384  MM 1200 1800   5     10   -5'
     print*,'Example: sjtty_qrm  384  MM 1200 1800   5     10   -5 ',   &
          '"LET''S ASK BRIAN WHAT HE THINKS"'
     print*,'NSPS must be 240, 320, 384, or 480'
     print*,'ITU propagation models: AW LQ LM LD MQ MM MD HQ HM HD'
     print*,'Main signal at f0 = 1500 Hz and specified SNR, message'
     print*,'"599 123" unless the optional 8th argument overrides it --'
     print*,'quote it if it contains spaces. A message long enough to'
     print*,'need more than one frame exercises continuation-frame'
     print*,'decoder logic that "599 123" alone never reaches; frames'
     print*,'beyond what the fixed-size output buffer can hold at this'
     print*,'signal''s randomly-drawn DT are silently dropped (matches a'
     print*,'transmission truncated by a WAV-file boundary), with a'
     print*,'warning printed when that happens.'
     print*,'Off-freq signals at random freqs in range nfa to nfb Hz.'
     print*,'Off-freq SNRs randomized by +/- 5 dB around specified value'
     print*,'DT values random in range 0.1 to 1.0 s.'
     go to 999
  endif

  call getarg(1,arg)
  read(arg,*) nsps
  call getarg(2,arg)
  arg4=arg(1:2)
  if(arg(1:2).eq.'LQ') then              !ITU params for Low Latitude Quiet
     fspread=0.5
     delay=0.5
  else if(arg(1:2).eq.'LM') then         !Low Latitude Moderate
     fspread=1.5
     delay=2.0
  else if(arg(1:2).eq.'LD') then         !Low Latitude Disturbed
     fspread=10.0
     delay=6.0
  else if(arg(1:2).eq.'MQ') then         !Mid Latitude ... etc.
     fspread=0.1
     delay=0.5
  else if(arg(1:2).eq.'MM') then
     fspread=0.5
     delay=1.0
  else if(arg(1:2).eq.'MD') then
     fspread=1.0
     delay=2.0
  else if(arg(1:2).eq.'HQ') then
     fspread=0.5
     delay=1.0
  else if(arg(1:2).eq.'HM') then
     fspread=10.0
     delay=3.0
  else if(arg(1:2).eq.'HD') then
     fspread=30.0
     delay=7.0
  else if(arg(1:2).eq.'AW') then
     fspread=0.0
     delay=0.0
  endif
  call getarg(3,arg)
  read(arg,*) nfa
  call getarg(4,arg)
  read(arg,*) nfb
  call getarg(5,arg)
  read(arg,*) nsigs
  call getarg(6,arg)
  read(arg,*) nfiles
  call getarg(7,arg)
  read(arg,*) snrdb
  if(nfa.lt.0 .or. nfb.lt.0) then
     print*,'Frequency range endpoints must be nonnegative.'
     stop 1
  endif
  msg_override=' '
  if(nargs.eq.8) call getarg(8,msg_override)

  fsample=12000.0
  dt=1.0/fsample
  twopi=8.0*atan(1.0)
  bandwidth_ratio=2500.0/(fsample/2.0)
  if(snrdb.gt.90.0) sig=1.0
  bt=2.0                           !Default bt=2 (smaller ==> more smoothing)
  baud=fsample/nsps                !Symbol rate
  bw=4.0*baud                      !Signal bandwidth
  hmod=1.0                         !Modulation index

  npts=131072
  do ifile=1,nfiles
     write(fname,1002) ifile         !Output filename
1002 format('000000_',i6.6,'.wav')
     open(10,file=fname,access='stream',status='replace')
     write(*,1004) ifile,fname
1004 format(i3,2x,a17)
     xnoise=0.
     if(snrdb.lt.90) then
        do i=1,NMAX
           xnoise(i)=gran()          !Generate gaussian noise, rms=1.0
        enddo
     endif
     cdat=0.
     i0=25.0*ran1(idum)

     do isig=1,nsigs
        f0=nfa + (nfb-nfa)*ran1(idum)
        xdt=0.1 + 3.9*ran1(idum)
        snr=snrdb + 10.0*(ran1(idum)-0.5)
        i0=i0+1
        if(i0.gt.25) i0=1
        umsg=xcall(i0)
        if(isig.eq.1) then
           f0=1500.0
           snr=snrdb
           umsg='599 123'
           if(len_trim(msg_override).gt.0) umsg=msg_override
        endif
        sig=sqrt(2*bandwidth_ratio) * 10.0**(0.05*snr)
        write(*,1006) isig,f0,xdt,snr,sig,trim(umsg)
1006    format(i4,f8.1,f8.3,f7.1,f10.3,2x,a)

        i1=nint(xdt/dt)
        call pack_jtty(umsg,c32,nframes)
        ! itone/cwave/c0 are all fixed at NMAX -- gen_jttywave trusts its
        ! caller's nwave completely, with no bounds checking of its own,
        ! so a long enough message combined with this signal's own
        ! (randomly drawn) start offset i1 could otherwise write past the
        ! end of those arrays. Clamp to however many frames actually fit
        ! before building itone, rather than risk that: this can only
        ! happen with a message overriding the default "599 123" (that
        ! one frame always fits), so it's silent for existing callers.
        nframes_fit=max(0,(NMAX-i1)/(nsps*59))
        if(nframes.gt.nframes_fit) then
           write(*,1007) nframes,nframes_fit
1007       format('  WARNING: message needs ',i2,' frames but only ',   &
                i2,' fit in the output buffer at this DT -- truncating')
           nframes=nframes_fit
        endif
        if(nframes.lt.1) then
           print*,'  WARNING: no room for this signal at this DT -- skipping'
           cycle
        endif
        nsym=0
        do i=1,nframes
           read(c32(i),'(34i1)') payload
           call tbcc_encode(payload,tone_symbols,JTTY_TBCC_PROFILE_1167_1545_80F)
           ib=(i-1)*59+1   ! 59 tones per frame
           itone(ib:ib+12)=is13
           itone(ib+13:ib+58)=tone_symbols
           nsym=nsym+59
        enddo
        nwave=nsps*nsym
        icmplx=1
        call gen_jttywave(itone,nsym,nsps,bt,fsample,f0,cwave,wave,icmplx,nwave)
        c0=0.
        c0(i1:nwave+i1-1)=cwave(0:nwave-1)!
        if(fspread.ne.0.0 .or. delay.ne.0.0) then
           ! Apply channel propagation
           call watterson(c0,npts,nwave,fsample,delay,fspread)
        endif
        cdat=cdat + sig*c0
     enddo  ! isig

     c0=cdat
     wave=0.
     wave=aimag(c0)    !Signal with SNR and prop degradation
     iz=10*12000
     if(snrdb.lt.90) wave = wave + xnoise

     gain=100.0
     if(snrdb.lt.90.0) then
       wave(1:iz)=gain*wave(1:iz)
     else
       datpk=maxval(abs(wave(1:iz)))
       fac=32766.9/datpk
       wave(1:iz)=fac*wave(1:iz)
     endif

     iwave(1:iz)=nint(wave(1:iz))
     h=default_header(12000,iz)
     write(10) h,iwave(1:iz)                !Save to *.wav file
     close(10)
  enddo

999 end program sjtty_qrm
