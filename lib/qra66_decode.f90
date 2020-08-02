module qra66_decode

   type :: qra66_decoder
      procedure(qra66_decode_callback), pointer :: callback
   contains
      procedure :: decode
   end type qra66_decoder

   abstract interface
      subroutine qra66_decode_callback (this,nutc,sync,nsnr,dt,freq,    &
         decoded,nap,qual,ntrperiod,fmid,w50)
         import qra66_decoder
         implicit none
         class(qra66_decoder), intent(inout) :: this
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
      end subroutine qra66_decode_callback
   end interface

contains

  subroutine decode(this,callback,iwave,nutc,nfa,nfb,nfqso,ndepth,lapdx,   &
       mycall,hiscall,hisgrid)

    use timer_module, only: timer
    use packjt
    use, intrinsic :: iso_c_binding
    parameter (NFFT1=15*12000,NFFT2=15*6000)
    class(qra66_decoder), intent(inout) :: this
    procedure(qra66_decode_callback) :: callback
    character(len=12) :: mycall, hiscall
    character(len=6) :: hisgrid
    character*37 decoded
    integer*2 iwave(NFFT1)                 !Raw data
    integer ipk(1)
    integer dat4(12)
    logical lapdx,ltext
    complex c0(0:NFFT1-1)                  !Spectrum, then analytic signal
    real s(900)
    real s3a(-64:127,63)
    real s3(-64:127,63)
    real a(5)
    data nc1z/-1/,nc2z/-1/,ng2z/-1/,maxaptypez/-1/
    save nc1z,nc2z,ng2z,maxaptypez

    this%callback => callback
    nsps=1920
    baud=12000.0/nsps
    df1=12000.0/NFFT1
    
    if(nutc.eq.-999) print*,lapdx,nfa,nfb,nfqso  !Silence warning

! Prime the QRA decoder for possible use of AP
    call packcall(mycall(1:6),nc1,ltext)
    call packcall(hiscall(1:6),nc2,ltext)
    call packgrid(hisgrid(1:4),ng2,ltext)
    nSubmode=0
    b90=20
    nFadingModel=1

! These probably need work:
    maxaptype=0
    if(ndepth.eq.2) maxaptype=3
    if(ndepth.eq.3) maxaptype=5

! Prime the decoder for possible AP decoding
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
    endif
    naptype=maxaptype

! Compute the full-length spectrum
    fac=2.0/NFFT1
    c0=fac*iwave
    call four2a(c0,NFFT1,1,-1,1)           !Forward c2c FFT

    nadd=101
    nh=nadd/2
    df2=nh*df1
    iz=3000.0/df2
    do i=1,iz                              !Compute smoothed spectrum
       s(i)=0.
       j0=nh*i
       do j=j0-nh,j0+nh
          s(i)=s(i) + real(c0(j))**2 + aimag(c0(j))**2
       enddo
    enddo
    call smo121(s,iz)
    ia=1450/df2
    ib=1550/df2
    s(:ia)=0.
    s(ib:)=0.
    ipk=maxloc(s)
    f0=df2*ipk(1) - 0.5*baud               !Candidate sync frequency

!    do i=1,iz
!       write(51,3051) i*df2,s(i)
!3051   format(f12.6,e12.3)
!    enddo

    c0(NFFT2/2+1:NFFT2)=0.                 !Zero the top half
    c0(0)=0.5*c0(0)
    call four2a(c0,nfft2,1,1,1)            !Inverse c2c FFT
    call sync66(c0,f0,jpk,sync)            !c0 is analytic signal at 6000 S/s
    xdt=jpk/6000.0 - 0.5

!    write(*,3003) jpk,f0,sync
!3003 format('A',i6,f8.2,f12.1)

    a=0.
    a(1)=-(f0 + 1.5*baud)
    call twkfreq(c0,c0,85*NSPS,6000.0,a)    
    call spec66(c0(jpk:jpk+85*NSPS-1),s3)
    s3a=s3/maxval(s3)
!    do j=1,63
!       ipk=maxloc(s3a(-64:127,j))
!       write(54,3054) j,ipk(1)-65
!3054   format(2i8)
!       do i=-64,127
!          write(53,3053) i,2*s3a(i,j)+j-1
!3053      format(i8,f12.6)
!       enddo
!    enddo

    call pctile(s3a,192*63,40,base)
    s3a=s3a/base
!    print*,'A',maxval(s3a),ndepth,maxaptype,naptype
    s3lim=10.
    where(s3a(-64:127,1:63)>s3lim) s3a(-64:127,1:63)=s3lim
    
    call qra64_dec(s3a,nc1,nc2,ng2,naptype,0,nSubmode,b90,      &
         nFadingModel,dat4,snr2,irc)
    if(irc.gt.0) call badmsg(irc,dat4,nc1,nc2,ng2)

    decoded='                                     '
    if(irc.ge.0) then
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
       nsnr=nint(db(sync))
!### TEMPORARY? ###       
       call this%callback(nutc,sync,nsnr,xdt,f0,decoded,              &
            irc,qual,ntrperiod,fmid,w50)
!###
    endif
!    write(*,3001) snr2,xdt,f0,decoded(1:22)
!3001 format('B',f5.1,f6.2,f7.1,2x,a22)

    return
  end subroutine decode

end module qra66_decode
