program jt65sim

! Generate simulated JT65 data for testing WSJT-X

  use wavhdr
  use packjt
  use options
  parameter (NMAX=54*12000)              ! = 648,000 @12kHz
  parameter (NFFT=10*65536,NH=NFFT/2)
  type(hdr) h                            !Header for .wav file
  integer*2 iwave(NMAX)                  !Generated waveform
  integer*4 itone(126)                   !Channel symbols (values 0-65)
  integer dgen(12)                       !Twelve 6-bit data symbols
  integer sent(63)                       !RS(63,12) codeword
  real*4 xnoise(NMAX)                    !Generated random noise
  real*4 dat(NMAX)                       !Generated real data
  complex cdat(NMAX)                     !Generated complex waveform
  complex cspread(0:NFFT-1)              !Complex amplitude for Rayleigh fading
  complex z
  real*8 f0,dt,twopi,phi,dphi,baud,fsample,freq,sps
  character msg*22,fname*11,csubmode*1,c,optarg*500,numbuf*32
!  character call1*5,call2*5
  logical :: display_help=.false.,seed_prngs=.true.
  type (option) :: long_options(13) = [ &
    option ('help',.false.,'h','Display this help message',''),                                &
    option ('sub-mode',.true.,'m','sub mode, default MODE=A','MODE'),                          &
    option ('num-sigs',.true.,'n','number of signals per file, default SIGNALS=10','SIGNALS'), &
    option ('f0',.true.,'F','base frequency offset, default F0=1500.0','F0'), &
    option ('doppler-spread',.true.,'d','Doppler spread, default SPREAD=0.0','SPREAD'),        &
    option ('drift per min',.true.,'D','Frequency drift (Hz/min), default DRIFT=0.0','DRIFT'),        &
    option ('time-offset',.true.,'t','Time delta, default SECONDS=0.0','SECONDS'),             &
    option ('num-files',.true.,'f','Number of files to generate, default FILES=1','FILES'),    &
    option ('no-prng-seed',.false.,'p','Do not seed PRNGs (use for reproducible tests)',''),   &
    option ('strength',.true.,'s','S/N in dB (2500Hz reference b/w), default SNR=0','SNR'),    &
    option ('11025',.false.,'S','Generate at 11025Hz sample rate, default 12000Hz',''),        &
    option ('gain-offset',.true.,'G','Gain offset in dB, default GAIN=0dB','GAIN'),            &
    option ('message',.true.,'M','Message text','Message') ]

  integer nprc(126)                                      !Sync pattern
  data nprc/1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0,  &
            0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1,  &
            0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1,  &
            0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1,  &
            1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1,  &
            0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1,  &
            1,1,1,1,1,1/

! Default parameters:
  csubmode='A'
  mode65=1
  nsigs=10
  bf0=1500.
  fspread=0.
  drift=0.
  xdt=0.
  snrdb=0.
  nfiles=1
  nsample_rate=12000
  gain_offset=0.
  msg="K1ABC W9XYZ EN37"

  do
     call getopt('hm:n:F:d:D:t:f:ps:SG:M:',long_options,c,optarg,narglen,nstat,noffset,nremain,.true.)
     if( nstat .ne. 0 ) then
        exit
     end if
     select case (c)
     case ('h')
        display_help = .true.
     case ('m')
        read (optarg(:narglen), *) csubmode
        if(csubmode.eq.'A') mode65=1
        if(csubmode.eq.'B') mode65=2
        if(csubmode.eq.'C') mode65=4
     case ('n')
        read (optarg(:narglen), *,err=10) nsigs
     case ('F')
        read (optarg(:narglen), *,err=10) bf0
     case ('d')
        read (optarg(:narglen), *,err=10) fspread
     case ('D')
        read (optarg(:narglen), *,err=10) drift 
     case ('t')
        read (optarg(:narglen), *) numbuf
        if (numbuf(1:1) == '\') then !'\'
           read (numbuf(2:), *,err=10) xdt
        else
           read (numbuf, *,err=10) xdt
        end if
     case ('f')
        read (optarg(:narglen), *,err=10) nfiles
     case ('p')
        seed_prngs=.false.
     case ('s')
        read (optarg(:narglen), *) numbuf
        if (numbuf(1:1) == '\') then !'\'
           read (numbuf(2:), *,err=10) snrdb
        else
           read (numbuf, *,err=10) snrdb
        end if
     case ('S')
        nsample_rate=11025
     case ('G')
        read (optarg(:narglen), *) numbuf
        if (numbuf(1:1) == '\') then !'\'
           read (numbuf(2:), *, err=10) gain_offset
        else
           read (numbuf, *, err=10) gain_offset
        end if
     case ('M')
        read (optarg(:narglen), '(A)',err=10) msg
     end select
     cycle
10   display_help=.true.
     print *, 'Optional argument format error for option -', c
  end do

  if(display_help .or. nstat.lt.0 .or. nremain.ge.1) then
     print *, ''
     print *, 'Usage: jt65sim [OPTIONS]'
     print *, ''
     print *, '       Generate one or more simulated JT65 signals in .WAV file(s)'
     print *, ''
     print *, 'Example: jt65sim -m B -n 10 -d 0.2 -s \\-24.5 -t 0.0 -f 4'
     print *, ''
     print *, 'OPTIONS: NB Use \ (\\ on *nix shells) to escape -ve arguments'
     print *, ''
     do i = 1, size (long_options)
       call long_options(i) % print (6)
     end do
     go to 999
  endif

  if (seed_prngs) then
     call init_random_seed()    ! seed Fortran RANDOM_NUMBER generator
     call sgran()               ! see C rand generator (used in gran)
  end if

  rms=100. * 10. ** (gain_offset / 20.)

  fsample=nsample_rate               !Sample rate (Hz)
  dt=1.d0/fsample                    !Sample interval (s)
  twopi=8.d0*atan(1.d0)
  npts=54*nsample_rate               !Total samples in .wav file
  baud=11025.d0/4096.d0              !Keying rate
  sps=real(nsample_rate)/baud        !Samples per symbol, at fsample=NSAMPLE_RATE Hz
  nsym=126                           !Number of channel symbols
  h=default_header(nsample_rate,npts)
  dfsig=2000.0/nsigs                 !Freq spacing between sigs in file (Hz)

  do ifile=1,nfiles                  !Loop over requested number of files
     write(fname,1002) ifile         !Output filename
1002 format('000000_',i4.4)
     open(10,file=fname//'.wav',access='stream',status='unknown')

     xnoise=0.
     cdat=0.
     if(snrdb.lt.90) then
        do i=1,npts
           xnoise(i)=gran()          !Generate gaussian noise
        enddo
     endif

     do isig=1,nsigs                        !Generate requested number of sigs
        if(mod(nsigs,2).eq.0) f0=bf0 + dfsig*(isig-0.5-nsigs/2)
        if(mod(nsigs,2).eq.1) f0=bf0 + dfsig*(isig-(nsigs+1)/2)
        xsnr=snrdb
        if(snrdb.eq.0.0) xsnr=-19 - isig
        if(csubmode.eq.'B' .and. snrdb.eq.0.0) xsnr=-21 - isig
        if(csubmode.eq.'C' .and. snrdb.eq.0.0) xsnr=-21 - isig

        call packmsg(msg,dgen,itype)        !Pack message into 12 six-bit bytes
        call rs_encode(dgen,sent)           !Encode using RS(63,12)
        call interleave63(sent,1)           !Interleave channel symbols
        call graycode65(sent,63,1)          !Apply Gray code

        nprc_test=0
        i1=len(trim(msg))
        if(i1.gt.10) then
           if(msg(i1-3:i1).eq.' OOO') nprc_test=1
        endif
        k=0
        do j=1,nsym                         !Insert sync and data into itone()
           if(nprc(j).eq.nprc_test) then
              k=k+1
              itone(j)=sent(k)+2
           else
              itone(j)=0
           endif
        enddo

        if(len(trim(msg)).eq.2.or.len(trim(msg)).eq.3) then
          nshorthand=0
          if(msg(1:2).eq.'RO') nshorthand=2
          if(msg(1:3).eq.'RRR') nshorthand=3
          if(msg(1:2).eq.'73') nshorthand=4
          if(nshorthand.gt.0) then
            ntoggle=0
            do i=1,nsym,4
              itone(i)=ntoggle*10*nshorthand
              if(i+1.le.126) itone(i+1)=ntoggle*10*nshorthand
              if(i+2.le.126) itone(i+2)=ntoggle*10*nshorthand
              if(i+3.le.126) itone(i+3)=ntoggle*10*nshorthand
              ntoggle=mod(ntoggle+1,2)
            enddo
          endif
        endif

        bandwidth_ratio=2500.0/(fsample/2.0)
        sig=sqrt(2*bandwidth_ratio)*10.0**(0.05*xsnr)
        if(xsnr.gt.90.0) sig=1.0
        write(*,1020) ifile,isig,f0,csubmode,xsnr,xdt,fspread,msg
1020    format(i4,i4,f10.3,2x,a1,2x,f5.1,f6.2,f5.1,1x,a22)

        phi=0.d0
        dphi=0.d0
        k=nsample_rate + xdt*nsample_rate     !Start audio at t = xdt + 1.0 s
        isym0=-99
        do i=1,npts                         !Add this signal into cdat()
           isym=floor(i/sps)+1
           if(isym.gt.nsym) exit
           freq=f0 + (drift/60.0)*(i-npts/2)*dt + itone(isym)*baud*mode65
           dphi=twopi*freq*dt
           phi=phi + dphi
           if(phi.gt.twopi) phi=phi-twopi
           xphi=phi
           z=cmplx(cos(xphi),sin(xphi))
           k=k+1
           if(k.ge.1) cdat(k)=cdat(k) + sig*z
        enddo
     enddo

     if(fspread.ne.0) then                  !Apply specified Doppler spread
        df=real(nsample_rate)/nfft
        twopi=8*atan(1.0)
        cspread(0)=1.0
        cspread(NH)=0.

! The following options were added 3/15/2016 to make the half-power tone 
! widths equal to the requested Doppler spread.  (Previously we effectively 
! used b=1.0 and Gaussian shape, which made the tones 1.665 times wider.)
!        b=2.0*sqrt(log(2.0))                     !Gaussian (before 3/15/2016)
!        b=2.0                                    !Lorenzian 3/15 - 3/27
        b=6.0                                     !Lorenzian 3/28 onward

        do i=1,NH
           f=i*df
           x=b*f/fspread
           z=0.
           a=0.
           if(x.lt.3.0) then                          !Cutoff beyond x=3
!              a=sqrt(exp(-x*x))                      !Gaussian
              a=sqrt(1.111/(1.0+x*x)-0.1)             !Lorentzian
              call random_number(r1)
              phi1=twopi*r1
              z=a*cmplx(cos(phi1),sin(phi1))
           endif
           cspread(i)=z
           z=0.
           if(x.lt.50.0) then
              call random_number(r2)
              phi2=twopi*r2
              z=a*cmplx(cos(phi2),sin(phi2))
           endif
           cspread(NFFT-i)=z
        enddo

        do i=0,NFFT-1
           f=i*df
           if(i.gt.NH) f=(i-nfft)*df
           s=real(cspread(i))**2 + aimag(cspread(i))**2
!          write(13,3000) i,f,s,cspread(i)
!3000      format(i5,f10.3,3f12.6)
        enddo
!        s=real(cspread(0))**2 + aimag(cspread(0))**2
!        write(13,3000) 1024,0.0,s,cspread(0)

        call four2a(cspread,NFFT,1,1,1)             !Transform to time domain

        sum=0.
        do i=0,NFFT-1
           p=real(cspread(i))**2 + aimag(cspread(i))**2
           sum=sum+p
        enddo
        avep=sum/NFFT
        fac=sqrt(1.0/avep)
        cspread=fac*cspread                   !Normalize to constant avg power
        cdat(1:npts)=cspread(1:npts)*cdat(1:npts) !Apply Rayleigh fading

!        do i=0,NFFT-1
!           p=real(cspread(i))**2 + aimag(cspread(i))**2
!           write(14,3010) i,p,cspread(i)
!3010       format(i8,3f12.6)
!        enddo

     endif

     dat(1:npts)=aimag(cdat(1:npts)) + xnoise(1:npts)              !Add the generated noise
     if(snrdb.lt.90.0) then
       dat(1:npts)=rms*dat(1:npts)
     else
       datpk=maxval(abs(dat(1:npts)))
       fac=32766.9/datpk
       dat(1:npts)=fac*dat(1:npts)
     endif
     if(any(abs(dat(1:npts)).gt.32767.0)) print*,"Warning - data will be clipped."
     iwave(1:npts)=nint(dat(1:npts))
     write(10) h,iwave(1:npts)                !Save the .wav file
     close(10)
  enddo

999 end program jt65sim
