subroutine ft8_a8d(dd,mycall,dxcall,dxgrid,f1a,xdt,fbest,xsnr,plog,msgbest)

! List decoding for FT8, activated only at nfqso (Rx Freq) +/- 10 Hz and when
! DxCall and DxGrid are populated. Returns xdt, fbest, and msgbest.

  use packjt77
  use ft8_a7
  use timer_module, only: timer
  include 'ft8_params.f90'           !Set various constants
  parameter (NWAVE=NN*NSPS/NDOWN)    !Length of generated cwave = 2528
  parameter (NZZ=3200)               !Length of downsampled arrays
  parameter (NFFT=NZZ,NH=NFFT/2)     !FFT length
  parameter (NMSGS=206)              !Number of messages to be tried
  parameter (PLOG_MIN=-160.0)        !Minimum log probability
  character mycall*12,dxcall*12,dxgrid*6
  character*37 msg,msgsent,msgbest
  character*77 c77
  integer*1 msgbits(77)
  real xjunk(NZZ)
  real s(-NH:NH)                     !Power spectrum of cd0
!  real s00(-NH:NH)                   !Raw spectrum of downsampled data
  real s0(-NH:NH)                    !Best-fit re-centered spectrum for this msg
  real s1(-NH:NH)                    !Overall best-fit re-centered spectrum
  real s8(0:7)                       !Spectrum of a single symbol
  real dd(NMAX)                      !Floating point copy of iwave
  real a(5)
  complex cwave(0:NZZ-1)             !Complex waveform corresponding to msg
  complex cd(0:NZZ-1)                !Data from dd, now complex and sampled at 200 Hz
  complex cd0(0:NZZ-1)               !All tones in cd nominally shifted to 0 Hz
  complex csymb(0:31)
  integer itone(NN)
  integer itone_best(NN)
  integer ipk(1)
  logical newdat

  f1=f1a
  newdat=.true.
! Mix from f1 to baseband and downsample from dd into cd (complex, 200 Hz sampling)
  call ft8_downsample(dd,newdat,f1,cd)
  fac=1.e-6

  fsd=200.0                              !Downsampled rate (Hz)
  bt=2.0                                 !Gaussian smoothing parameter
  dt=1.0/fsd
  df=200.0/NFFT
  s=0.
  ss=0.
  sq=0.

! Find the message that fits the received waveform best
  sbest=0.
  tbest=0.
  fbest=0.
  msgbest=''
  itone_best=0
  do imsg=1,NMSGS
     call getmsg(imsg,mycall,dxcall,dxgrid,msg)

! Source-encode the message, get itone(), and generate complex FT8 waveform.
     i3=-1
     n3=-1
     call pack77(msg,i3,n3,c77)
     call genft8(msg,i3,n3,msgsent,msgbits,itone)
     call gen_ft8wave(itone,NN,32,bt,fsd,0.0,cwave,xjunk,1,NWAVE)
     cwave(NWAVE:)=0.

! For this message, find the best lag
     spk=0.
     fpk=0.
     tpk=0.
     lagpk=0
     lag1=-200
     lag2=200
     lagstep=4
     do iter=1,2
        if(iter.eq.2) then
           lag1=lagpk-8
           lag2=lagpk+8
           lagstep=1
        endif
        do lag=lag1,lag2,lagstep
           do i=0,NWAVE-1
              j=i+lag + 100
              cd0(i)=0.
              if(j.ge.0 .and. j.le.NWAVE-1) cd0(i)=cd(j)*conjg(cwave(i))
           enddo

           cd0(NWAVE:)=0.
           call four2a(cd0,NFFT,1,-1,1)               !Forward c2c FFT

           do i=0,NFFT-1
              j=i
              if(i.gt.NH) j=i-NFFT
              s(j)=fac*(real(cd0(i))**2 + aimag(cd0(i))**2)
           enddo

           call smo121(s,NFFT+1)

           smax=0.
           do j=-NH,NH
              smax=max(s(j),smax)
              if(s(j).gt.spk) then
                 spk=s(j)
                 fpk=j*df + f1
                 lagpk=lag
                 tpk=lag*dt
                 s0=s                       !Save best-fit spectrum for this msg
              endif
           enddo  ! j
        enddo  ! lag
     enddo  ! iter

     if(spk.gt.sbest) then
        sbest=spk
        fbest=fpk
        tbest=tpk
        msgbest=msg
        itone_best=itone
        s1=s0                                !Save overall best-fit spectrum
     endif
  enddo  ! imsg

  a=0
  a(1)=f1-fbest
!  if(abs(a(1)+5.0).gt.10.0) then             !Effective Ftol = 10 Hz
  if(abs(a(1)).gt.5.0) then                   !Effective Ftol = 5 Hz
     msgbest=''
     return
  endif
  call twkfreq1(cd,NZZ,200.0,a,cd)           !Re-center the spectrum
  
  xdt=tbest
  ave=(sum(s1(-200:-100)) + sum(s1(100:200)))/202.0
  s1=s1/ave - 1.0
  s1pk=maxval(s1(-32:32))
  sig=0.
  nsig=0
  do i=-32,32
     if(s1(i).lt.0.5*s1pk) cycle
     sig=sig+s1(i)
     nsig=nsig+1
  enddo
  sig=sig/nsig
  xsnr=db(sig)-35.0

  if(msgbest.ne.'') then
! Compute probability of successful decode.
     plog=0.0
     nhard=0
     nsum=0
     sum_sync=0.
     sum_sig=0.
     sum_big=0.
     do k=1,NN
        i0=32*(k-1) + nint((tbest+0.5)/0.005)
        csymb=0.
        do i=0,31
           if(i0+i.ge.0 .and. i0+i.le.NZZ-1) csymb(i)=cd(i0+i)
        enddo
        call four2a(csymb,32,1,-1,1)           !c2c
        do i=0,7
           s8(i)=real(csymb(i))**2 + aimag(csymb(i))**2
        enddo
        s8sum=sum(s8)
        if(s8sum.gt.0.0) then
           p=s8(itone_best(k))/s8sum
           plog=plog + log(p)
           nsum=nsum+1
        endif
        ipk=maxloc(s8)-1
        if(ipk(1).ne.itone_best(k)) nhard=nhard+1
        if(k.le.7 .or. (k.ge.37 .and. k.le.43) .or. k.ge.73) then
           sum_sync=sum_sync + s8(itone_best(k))
        else
           sum_sig=sum_sig + s8(itone_best(k))
        endif
        sum_big=sum_big + s8(ipk(1))
     enddo
     if(nsum.lt.NN) plog=plog + (NN-nsum)*log(0.125)
     sigobig=(sum_sync+sum_sig)/sum_big
!     write(60,3060) nsum,xdt,fbest,xsnr,plog,nhard,sigosync,sigobig,trim(msgbest)
!3060 format(i2,f8.3,3f8.1,i5,2f7.2,1x,a)
     if(nhard.gt.54 .or. plog.lt.-159.0 .or. sigobig.lt.0.71) msgbest=''
  endif
  
  return
end subroutine ft8_a8d
