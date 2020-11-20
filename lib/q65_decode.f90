module q65_decode

   type :: q65_decoder
      procedure(q65_decode_callback), pointer :: callback
   contains
      procedure :: decode
   end type q65_decoder

   abstract interface
      subroutine q65_decode_callback (this,nutc,sync,nsnr,dt,freq,    &
         decoded,nap,qual,ntrperiod,fmid,w50)
         import q65_decoder
         implicit none
         class(q65_decoder), intent(inout) :: this
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
      end subroutine q65_decode_callback
   end interface

contains

  subroutine decode(this,callback,iwave,nutc,ntrperiod,nsubmode,nfqso,   &
       ntol,ndepth,mycall,hiscall,hisgrid,nQSOprogress,ncontest,lapcqonly)

! Decodes Q65 signals
! Input:  iwave            Raw data, i*2
!         nutc             UTC for time-tagging the decode
!         ntrperiod        T/R sequence length (s)
!         nsubmode         Tone-spacing indicator, 0-4 for A-E
!         nfqso            Target signal frequency (Hz)
!         ntol             Search range around nfqso (Hz)
!         ndepth           Optional decoding level
! Output: sent to the callback routine for display to user

    use timer_module, only: timer
    use packjt77
    use, intrinsic :: iso_c_binding
    parameter (NMAX=300*12000)            !Max TRperiod is 300 s
    class(q65_decoder), intent(inout) :: this
    procedure(q65_decode_callback) :: callback
    character(len=12) :: mycall, hiscall  !Used for AP decoding
    character(len=6) :: hisgrid
    character*37 decoded                  !Decoded message
    character*77 c77
    character*78 c78
    integer*2 iwave(NMAX)                 !Raw data
    real, allocatable :: dd(:)            !Raw data
    integer dat4(13)                      !Decoded message as 12 6-bit integers
    integer apsym0(58),aph10(10)
    integer apmask1(78),apsymbols1(78)
    integer apmask(13),apsymbols(13)
    logical lapcqonly,unpk77_success
    complex, allocatable :: c00(:)        !Analytic signal, 6000 Sa/s
    complex, allocatable :: c0(:)         !Analytic signal, 6000 Sa/s

    mode65=2**nsubmode
    npts=ntrperiod*12000
    nfft1=ntrperiod*12000
    nfft2=ntrperiod*6000
    allocate(dd(npts))
    allocate (c00(0:nfft1-1))
    allocate (c0(0:nfft1-1))

    if(ntrperiod.eq.15) then
       nsps=1800
    else if(ntrperiod.eq.30) then
       nsps=3600
    else if(ntrperiod.eq.60) then
       nsps=7200
    else if(ntrperiod.eq.120) then
       nsps=16000
    else if(ntrperiod.eq.300) then
       nsps=41472
    else
       stop 'Invalid TR period'
    endif
    baud=12000.0/nsps
    df1=12000.0/nfft1
    this%callback => callback
    if(nutc.eq.-999) print*,lapdx,nfa,nfb,nfqso  !Silence warning
    nFadingModel=1
!    call qra_params(ndepth,maxaptype,idfmax,idtmax,ibwmin,ibwmax,maxdist)
    call timer('sync_q65',0)
    call sync_q65(iwave,ntrperiod*12000,mode65,nsps,nfqso,ntol,xdt,f0,   &
         snr1,width)
    call timer('sync_q65',1)

    irc=-9
    if(snr1.lt.2.8) go to 100
    jpk0=(xdt+1.0)*6000                      !### Is this OK?
    if(ntrperiod.le.30) jpk0=(xdt+0.5)*6000  !###
    if(jpk0.lt.0) jpk0=0
    fac=1.0/32767.0
    dd=fac*iwave(1:npts)
!###
! Optionslly write noise level to LU 56
!    sq=dot_product(dd,dd)/npts
!    m=nutc
!    if(ntrperiod.ge.60) m=100*m
!    ihr=m/10000
!    imin=mod(m/100,100)
!    isec=mod(m,100)
!    hours=ihr + imin/60.0 + isec/3600.0
!    write(56,3056) m,hours,db(sq)+90.3
!3056 format(i6.6,f10.6,f10.3)
!###
    nmode=65
    call ana64(dd,npts,c00)

    call ft8apset(mycall,hiscall,ncontest,apsym0,aph10)
    where(apsym0.eq.-1) apsym0=0

    npasses=2
    if(nQSOprogress.eq.5) npasses=3
    if(lapcqonly) npasses=1
    iaptype=0
    do ipass=0,npasses
       apmask=0
       apsymbols=0
       if(ipass.ge.1) then
          call q65_ap(nQSOprogress,ipass,ncontest,lapcqonly,iaptype,   &
               apsym0,apmask1,apsymbols1)
          write(c78,1050) apmask1
1050      format(78i1)
          read(c78,1060) apmask
1060      format(13b6.6)
          write(c78,1050) apsymbols1
          read(c78,1060) apsymbols
       endif
       call timer('q65loops',0)
       call q65_loops(c00,nutc,npts/2,nsps/2,nmode,mode65,nsubmode,         &
            nFadingModel,ndepth,jpk0,xdt,f0,width,iaptype,apmask,apsymbols, &
            snr1,xdt1,f1,snr2,irc,dat4)
       call timer('q65loops',1)
       snr2=snr2 + db(6912.0/nsps)
       if(irc.ge.0) exit
    enddo

100 decoded='                                     '
    if(irc.ge.0) then
!###
       navg=irc/100
!       irc=100*navg + ipass
       irc=100*navg + iaptype
!###
       write(c77,1000) dat4(1:12),dat4(13)/2
1000   format(12b6.6,b5.5)
       call unpack77(c77,0,decoded,unpk77_success) !Unpack to get msgsent
       nsnr=nint(snr2)
       call this%callback(nutc,sync,nsnr,xdt1,f1,decoded,              &
            irc,qual,ntrperiod,fmid,w50)
    else
! Report sync, even if no decode.
       nsnr=db(snr1) - 35.0
       call this%callback(nutc,sync,nsnr,xdt1,f1,decoded,              &
            irc,qual,ntrperiod,fmid,w50)
    endif

    return
  end subroutine decode

end module q65_decode
