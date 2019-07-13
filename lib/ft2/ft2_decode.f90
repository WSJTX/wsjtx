subroutine ft2_decode(cdatetime0,nfqso,iwave,ndecodes,mycall,hiscall,nrx,line)

  use crc
  use packjt77
  include 'ft2_params.f90'
  character message*37,c77*77
  character*61 line
  character*37 decodes(100)
  character*120 data_dir
  character*17 cdatetime0,cdatetime
  character*6 mycall,hiscall,hhmmss
  complex c2(0:NMAX/16-1)                  !Complex waveform
  complex cb(0:NMAX/16-1)
  complex cd(0:144*10-1)                  !Complex waveform
  complex c1(0:9),c0(0:9)
  complex ccor(0:1,144)
  complex csum,cterm,cc0,cc1,csync1
  real*8 fMHz

  real a(5)
  real rxdata(128),llr(128)               !Soft symbols
  real llr2(128)
  real sbits(144),sbits1(144),sbits3(144)
  real ps(0:8191),psbest(0:8191)
  real candidate(3,100)
  real savg(NH1)
  integer*2 iwave(NMAX)                 !Generated full-length waveform  
  integer*1 message77(77),apmask(128),cw(128)
  integer*1 hbits(144),hbits1(144),hbits3(144)
  integer*1 s16(16)
  logical unpk77_success
  data s16/0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0/

  hhmmss=cdatetime0(8:13)
  fs=12000.0/NDOWN                       !Sample rate
  dt=1/fs                                !Sample interval after downsample (s)
  tt=NSPS*dt                             !Duration of "itone" symbols (s)
  baud=1.0/tt                            !Keying rate for "itone" symbols (baud)
  txt=NZ*dt                              !Transmission length (s)
  twopi=8.0*atan(1.0)
  h=0.8                                  !h=0.8 seems to be optimum for AWGN sensitivity (not for fading)

  dphi=twopi/2*baud*h*dt*16  ! dt*16 is samp interval after downsample
  dphi0=-1*dphi
  dphi1=+1*dphi
  phi0=0.0
  phi1=0.0
  do i=0,9
    c1(i)=cmplx(cos(phi1),sin(phi1))
    c0(i)=cmplx(cos(phi0),sin(phi0))
    phi1=mod(phi1+dphi1,twopi)
    phi0=mod(phi0+dphi0,twopi)
  enddo
  the=twopi*h/2.0
  cc1=cmplx(cos(the),-sin(the))
  cc0=cmplx(cos(the),sin(the))
  
  data_dir="."
  fMHz=7.074
  ncoh=1  
  candidate=0.0
  ncand=0
  fa=375.0
  fb=3000.0
  syncmin=0.2
  maxcand=100
  nfqso=-1
  call getcandidates2a(iwave,fa,fb,maxcand,savg,candidate,ncand)
  ndecodes=0
  do icand=1,ncand
     f0=candidate(1,icand)
     if( f0.le.375.0 .or. f0.ge.(5000.0-375.0) ) cycle 
     call ft2_downsample(iwave,f0,c2) ! downsample from 160s/Symbol to 10s/Symbol
! 750 samples/second here
     ibest=-1
     sybest=-99.
     dfbest=-1.
!###     do if=-15,+15
     do if=-30,30
        df=if
        a=0.
        a(1)=-df
        call twkfreq1(c2,NMAX/16,fs,a,cb)
        do is=0,374                           !DT search range is 0 - 0.5 s
           csync1=0.
           cterm=1
           do ib=1,16
              i1=(ib-1)*10+is
              i2=i1+136*10
              if(s16(ib).eq.1) then
                 csync1=csync1+sum(cb(i1:i1+9)*conjg(c1(0:9)))*cterm
                 cterm=cterm*cc1
              else
                 csync1=csync1+sum(cb(i1:i1+9)*conjg(c0(0:9)))*cterm
                 cterm=cterm*cc0
              endif
           enddo
           if(abs(csync1).gt.sybest) then
              ibest=is
              sybest=abs(csync1)
              dfbest=df
           endif
        enddo
     enddo

     a=0.
     a(1)=-dfbest
     call twkfreq1(c2,NMAX/16,fs,a,cb)
     ib=ibest
     cd=cb(ib:ib+144*10-1) 
     s2=sum(real(cd*conjg(cd)))/(10*144)
     cd=cd/sqrt(s2)
     do nseq=1,5
        if( nseq.eq.1 ) then  ! noncoherent single-symbol detection
           sbits1=0.0
           do ibit=1,144
              ib=(ibit-1)*10
              ccor(1,ibit)=sum(cd(ib:ib+9)*conjg(c1(0:9)))        
              ccor(0,ibit)=sum(cd(ib:ib+9)*conjg(c0(0:9)))   
              sbits1(ibit)=abs(ccor(1,ibit))-abs(ccor(0,ibit))
              hbits1(ibit)=0
              if(sbits1(ibit).gt.0) hbits1(ibit)=1
           enddo
           sbits=sbits1
           hbits=hbits1
           sbits3=sbits1
           hbits3=hbits1
        elseif( nseq.ge.2 ) then
           nbit=2*nseq-1
           numseq=2**(nbit)
           ps=0
           do ibit=nbit/2+1,144-nbit/2
              ps=0.0
              pmax=0.0
              do iseq=0,numseq-1
                 csum=0.0
                 cterm=1.0
                 k=1
                 do i=nbit-1,0,-1
                    ibb=iand(iseq/(2**i),1) 
                    csum=csum+ccor(ibb,ibit-(nbit/2+1)+k)*cterm
                    if(ibb.eq.0) cterm=cterm*cc0
                    if(ibb.eq.1) cterm=cterm*cc1
                    k=k+1
                 enddo
                 ps(iseq)=abs(csum) 
                 if( ps(iseq) .gt. pmax ) then
                    pmax=ps(iseq)
                    ibflag=1
                 endif
              enddo
              if( ibflag .eq. 1 ) then
                 psbest=ps
                 ibflag=0
              endif
              call getbitmetric(2**(nbit/2),psbest,numseq,sbits3(ibit))
              hbits3(ibit)=0
              if(sbits3(ibit).gt.0) hbits3(ibit)=1
           enddo
           sbits=sbits3
           hbits=hbits3
        endif
        nsync_qual=count(hbits(1:16).eq.s16)
        if(nsync_qual.lt.10) exit 
        rxdata=sbits(17:144)
        rxav=sum(rxdata(1:128))/128.0
        rx2av=sum(rxdata(1:128)*rxdata(1:128))/128.0
        rxsig=sqrt(rx2av-rxav*rxav)
        rxdata=rxdata/rxsig
        sigma=0.80
        llr(1:128)=2*rxdata/(sigma*sigma)
        apmask=0
        max_iterations=40
        do ibias=0,0
           llr2=llr
           if(ibias.eq.1) llr2=llr+0.4
           if(ibias.eq.2) llr2=llr-0.4
           call bpdecode128_90(llr2,apmask,max_iterations,message77,cw,nharderror,niterations)
           if(nharderror.ge.0) exit 
        enddo
        nhardmin=-1
        if(sum(message77).eq.0) cycle
        if( nharderror.ge.0 ) then
           write(c77,'(77i1)') message77(1:77)
           call unpack77(c77,nrx,message,unpk77_success)
           idupe=0
           do i=1,ndecodes
              if(decodes(i).eq.message) idupe=1 
           enddo
           if(idupe.eq.1) exit
           ndecodes=ndecodes+1 
           decodes(ndecodes)=message
           xsnr=db(sybest*sybest) - 115.0   !### Rough estimate of S/N ###
           nsnr=nint(xsnr)
           freq=f0+dfbest
           write(line,1000) hhmmss,nsnr,ibest/750.0,nint(freq),message
1000       format(a6,i4,f5.2,i5,' + ',1x,a37)
           open(24,file='all_ft2.txt',status='unknown',position='append')
           write(24,1002) cdatetime0,nsnr,ibest/750.0,nint(freq),message,    &
                nseq,nharderror,nhardmin
           if(hhmmss.eq.'      ') write(*,1002) cdatetime0,nsnr,             &
                ibest/750.0,nint(freq),message,nseq,nharderror,nhardmin
1002       format(a17,i4,f6.2,i5,' Rx  ',a37,3i5)
           close(24)

!### Temporary: assume most recent decoded message conveys "hiscall".
           i0=index(message,' ')
           if(i0.ge.3 .and. i0.le.7) then
              hiscall=message(i0+1:i0+6)
              i1=index(hiscall,' ')
              if(i1.gt.0) hiscall=hiscall(1:i1)
           endif
           nrx=-1
           if(index(message,'CQ ').eq.1) nrx=1
           if((index(message,trim(mycall)//' ').eq.1) .and.                 &
                (index(message,' '//trim(hiscall)//' ').ge.4)) then
              if(index(message,' 559 ').gt.8) nrx=2
              if(index(message,' R 559 ').gt.8) nrx=3
              if(index(message,' RR73 ').gt.8) nrx=4
           endif
!###
           exit
        endif
     enddo ! nseq
  enddo !candidate list

  return
end subroutine ft2_decode

subroutine getbitmetric(ib,ps,ns,xmet)
  real ps(0:ns-1)
  xm1=0
  xm0=0
  do i=0,ns-1
    if( iand(i/ib,1) .eq. 1 .and. ps(i) .gt. xm1 ) xm1=ps(i)
    if( iand(i/ib,1) .eq. 0 .and. ps(i) .gt. xm0 ) xm0=ps(i)
  enddo
  xmet=xm1-xm0
  return
end subroutine getbitmetric

subroutine downsample2(ci,f0,co)
  parameter(NI=144*160,NH=NI/2,NO=NI/16)  ! downsample from 200 samples per symbol to 10
  complex ci(0:NI-1),ct(0:NI-1) 
  complex co(0:NO-1)
  fs=12000.0
  df=fs/NI
  ct=ci
  call four2a(ct,NI,1,-1,1)             !c2c FFT to freq domain
  i0=nint(f0/df)
  ct=cshift(ct,i0)
  co=0.0
  co(0)=ct(0)
  b=8.0
  do i=1,NO/2
     arg=(i*df/b)**2
     filt=exp(-arg)
     co(i)=ct(i)*filt
     co(NO-i)=ct(NI-i)*filt
  enddo
  co=co/NO
  call four2a(co,NO,1,1,1)            !c2c FFT back to time domain
  return
end subroutine downsample2

subroutine ft2_downsample(iwave,f0,c)

! Input: i*2 data in iwave() at sample rate 12000 Hz
! Output: Complex data in c(), sampled at 1200 Hz

  include 'ft2_params.f90'
  parameter (NFFT2=NMAX/16)
  integer*2 iwave(NMAX)
  complex c(0:NMAX/16-1)
  complex c1(0:NFFT2-1)
  complex cx(0:NMAX/2)
  real x(NMAX)
  equivalence (x,cx)

  BW=4.0*75
  df=12000.0/NMAX
  x=iwave
  call four2a(x,NMAX,1,-1,0)             !r2c FFT to freq domain
  ibw=nint(BW/df)
  i0=nint(f0/df)
  c1=0.
  c1(0)=cx(i0)
  do i=1,NFFT2/2
     arg=(i-1)*df/bw
     win=exp(-arg*arg)
     c1(i)=cx(i0+i)*win
     c1(NFFT2-i)=cx(i0-i)*win
  enddo
  c1=c1/NFFT2
  call four2a(c1,NFFT2,1,1,1)            !c2c FFT back to time domain
  c=c1(0:NMAX/16-1)
  return
end subroutine ft2_downsample
