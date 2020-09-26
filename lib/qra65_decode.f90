module qra65_decode

   type :: qra65_decoder
      procedure(qra65_decode_callback), pointer :: callback
   contains
      procedure :: decode
   end type qra65_decoder

   abstract interface
      subroutine qra65_decode_callback (this,nutc,sync,nsnr,dt,freq,    &
         decoded,nap,qual,ntrperiod,fmid,w50)
         import qra65_decoder
         implicit none
         class(qra65_decoder), intent(inout) :: this
         integer, intent(in) :: nutc
         real, intent(in) :: sync
         integer, intent(in) :: nsnr
         real, intent(in) :: dt
         real, intent(in) :: freq
         character(len=37), intent(in) :: decoded
         integer, intent(in) :: nap
         real, intent(in) :: qual
         integer, intent(in) :: ntrperiod
         real, intent(in) :: fmid
         real, intent(in) :: w50
      end subroutine qra65_decode_callback
   end interface

contains

  subroutine decode(this,callback,iwave,nutc,ntrperiod,nsubmode,nfqso,   &
       ntol,ndepth,mycall,hiscall,hisgrid)

    use timer_module, only: timer
    use packjt
    use, intrinsic :: iso_c_binding
    parameter (NMAX=300*12000)             !### Needs to be 300*12000 ###
    class(qra65_decoder), intent(inout) :: this
    procedure(qra65_decode_callback) :: callback
    character(len=12) :: mycall, hiscall
    character(len=6) :: hisgrid
    character*37 decoded
    integer*2 iwave(NMAX)                 !Raw data
    integer dat4(12)
    logical lapdx,ltext
    complex, allocatable :: c0(:)         !Analytic signal, 6000 S/s
    real, allocatable, save :: s3(:,:)    !Symbol spectra
    real, allocatable, save :: s3a(:,:)   !Symbol spectra for avg messages
    real a(5)
    data nc1z/-1/,nc2z/-1/,ng2z/-1/,maxaptypez/-1/,nsubmodez/-1/
    save nc1z,nc2z,ng2z,maxaptypez,nsave,nsubmodez

    mode65=2**nsubmode
    nfft1=ntrperiod*12000
    nfft2=ntrperiod*6000
    allocate (c0(0:nfft1-1))

    if(nsubmode.ne.nsubmodez) then
       if(allocated(s3)) deallocate(s3)
       if(allocated(s3a)) deallocate(s3a)
       allocate(s3(-64:64*mode65+63,63))
       allocate(s3a(-64:64*mode65+63,63))
    endif
    
    if(ntrperiod.eq.15) then
       nsps=1800
    else if(ntrperiod.eq.30) then
       nsps=3600
    else if(ntrperiod.eq.60) then
       nsps=7680
    else if(ntrperiod.eq.120) then
       nsps=16000
    else if(ntrperiod.eq.300) then
       nsps=41472
    else
      stop 'Invalid TR period'
    endif
    baud=12000.0/nsps
    df1=12000.0/nfft1

!    do i=1,12000*ntrperiod
!       write(61,3061) i/12000.0,iwave(i)/32767.0
!3061   format(2f12.6)
!    enddo

    this%callback => callback
    
    if(nutc.eq.-999) print*,lapdx,nfa,nfb,nfqso  !Silence warning

! Prime the QRA decoder for possible use of AP
    call packcall(mycall(1:6),nc1,ltext)
    call packcall(hiscall(1:6),nc2,ltext)
    call packgrid(hisgrid(1:4),ng2,ltext)
    b90=20.0                 !8 to 25 is OK; not very critical
    nFadingModel=1

! AP control could be done differently, but this works well:
    maxaptype=0
    if(ndepth.eq.2) maxaptype=3
    if(ndepth.eq.3) maxaptype=5

    if(nc1.ne.nc1z .or. nc2.ne.nc2z .or. ng2.ne.ng2z .or.            &
         maxaptype.ne.maxaptypez) then
       do naptype=0,maxaptype
          if(naptype.eq.2 .and. maxaptype.eq.4) cycle
          call qra64_dec(s3,nc1,nc2,ng2,naptype,1,nSubmode,b90,      &
               nFadingModel,dat4,snr2,irc)
       enddo
       nc1z=nc1
       nc2z=nc2
       ng2z=ng2
       maxaptypez=maxaptype
       s3a=0.
       nsave=0
    endif
    naptype=maxaptype

    call timer('sync_q65',0)
    call sync_qra65(iwave,ntrperiod*12000,mode65,nsps,nfqso,ntol,xdt,f0,snr1)
    call timer('sync_q65',1)

! Downsample to give complex data at 6000 S/s
    call timer('down_q65',0)
    fac=2.0/nfft1
    c0=fac*iwave(1:nfft1)
    call four2a(c0,nfft1,1,-1,1)           !Forward c2c FFT
    c0(nfft2/2+1:nfft2)=0.                 !Zero the top half
    c0(0)=0.5*c0(0)
    call four2a(c0,nfft2,1,1,1)            !Inverse c2c FFT
    a=0.
    a(1)=-(f0 + mode65*baud)             !Data tones start mode65 bins higher
    call twkfreq(c0,c0,ntrperiod*6000,6000.0,a)
    call timer('down_q65',1)

    jpk=(xdt+0.5)*6000 - 384                       !### Empirical ###
    if(ntrperiod.ge.60) jpk=(xdt+1.0)*6000 - 384   !### TBD ??? ###
    if(jpk.lt.0) jpk=0
    xdt=jpk/6000.0 - 0.5
    LL=64*(mode65+2)
    NN=63
    call timer('spec_q65',0)
    call spec_qra65(c0(jpk:),nsps/2,s3,LL,NN)  !Compute synced symbol spectra
    call timer('spec_q65',1)

    do j=1,63                              !Normalize to symbol baseline
       call pctile(s3(:,j),LL,40,base)
       s3(:,j)=s3(:,j)/base
    enddo

    LL2=64*(mode65+1)-1
    s3max=20.0
    do j=1,63                              !Apply AGC to suppress pings
     xx=maxval(s3(-64:LL2,j))
     if(xx.gt.s3max) s3(-64:LL2,j)=s3(-64:LL2,j)*s3max/xx
    enddo

! Call Nico's QRA64 decoder
    call timer('qra64_de',0)
    call qra64_dec(s3,nc1,nc2,ng2,naptype,0,nSubmode,b90,      &
         nFadingModel,dat4,snr2,irc)
    call timer('qra64_de',1)

    if(irc.lt.0) then
! No luck so far. Try for an average decode.
       call timer('qra64_av',0)
       s3a=s3a+s3
       nsave=nsave+1
       if(nsave.ge.2) then
          call qra64_dec(s3a,nc1,nc2,ng2,naptype,0,nSubmode,b90,      &
               nFadingModel,dat4,snr2,irc)
          if(irc.ge.0) irc=100*nsave + irc
       endif
       call timer('qra64_av',1)
    endif
    snr2=snr2 + db(6912.0/nsps)
    if(irc.gt.0) call badmsg(irc,dat4,nc1,nc2,ng2)

    decoded='                                     '
    if(irc.ge.0) then
       nsave=0
       s3a=0.
       call unpackmsg(dat4,decoded)               !Unpack the user message
       call fmtmsg(decoded,iz)
       if(index(decoded,"000AAA ").ge.1) then
! Suppress a certain type of garbage decode.
          decoded='                      '
          irc=-1
       endif
       nsnr=nint(snr2)
       call this%callback(nutc,sync,nsnr,xdt,f0,decoded,              &
            irc,qual,ntrperiod,fmid,w50)
    else
       snr2=0.
       nsnr=-25 
!### TEMPORARY? ###       
       call this%callback(nutc,sync,nsnr,xdt,f0,decoded,              &
            irc,qual,ntrperiod,fmid,w50)
!###
    endif

!    write(61,3061) nutc,irc,xdt,f0,snr1,snr2,trim(decoded)
!3061 format(i6.6,i4,4f10.2,2x,a)

    return
  end subroutine decode

end module qra65_decode
