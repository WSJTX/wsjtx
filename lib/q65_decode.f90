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
!         ndepth           Optional decoding level (???)
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
    integer*2 iwave(NMAX)                 !Raw data
    real, allocatable :: dd(:)            !Raw data
    integer dat4(13)                      !Decoded message as 12 6-bit integers
    logical lapcqonly,unpk77_success
    complex, allocatable :: c00(:)        !Analytic signal, 6000 Sa/s
    complex, allocatable :: c0(:)         !Analytic signal, 6000 Sa/s
    data nc1z/-1/,nc2z/-1/,ng2z/-1/,maxaptypez/-1/,nsubmodez/-1/
    save nc1z,nc2z,ng2z,maxaptypez,nsubmodez

    mode65=2**nsubmode
    nfft1=ntrperiod*12000
    nfft2=ntrperiod*6000
    allocate(dd(NMAX))
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
    npts=ntrperiod*12000
    baud=12000.0/nsps
    df1=12000.0/nfft1
    this%callback => callback
    if(nutc.eq.-999) print*,lapdx,nfa,nfb,nfqso  !Silence warning
    nFadingModel=1

! AP control could be done differently, but this works well:
    maxaptype=0
!    if(ndepth.eq.2) maxaptype=3
!    if(ndepth.eq.3) maxaptype=5
    if(ndepth.ge.2) maxaptype=5       !###
    minsync=-2
    call qra_params(ndepth,maxaptype,idfmax,idtmax,ibwmin,ibwmax,maxdist)
    naptype=maxaptype

    call timer('sync_q65',0)
    call sync_q65(iwave,ntrperiod*12000,mode65,nsps,nfqso,ntol,xdt,f0,snr1)
    call timer('sync_q65',1)

    irc=-1
    if(snr1.ge.2.5) then
       jpk0=(xdt+1.0)*6000                      !###
       if(ntrperiod.le.30) jpk0=(xdt+0.5)*6000  !###
       if(jpk0.lt.0) jpk0=0
       fac=1.0/32767.0
       dd=fac*iwave
       nmode=65
       call ana64(dd,npts,c00)
       call timer('q65loops',0)
       call q65_loops(c00,npts/2,nsps/2,nmode,mode65,nsubmode,nFadingModel,  &
            ndepth,jpk0,xdt,f0,width,snr2,irc,dat4)
       call timer('q65loops',1)
       snr2=snr2 + db(6912.0/nsps)
    endif
    decoded='                                     '
    if(irc.ge.0) then
       irc=(irc/100) * 100                  !### TEMPORARY ??? ###
       write(c77,1000) dat4
1000   format(12b6.6,b5.5)
       call unpack77(c77,0,decoded,unpk77_success) !Unpack to get msgsent
       nsnr=nint(snr2)
       call this%callback(nutc,sync,nsnr,xdt,f0,decoded,              &
            irc,qual,ntrperiod,fmid,w50)
    else
       nsnr=db(snr1) - 35.0
!### TEMPORARY? ###       
       call this%callback(nutc,sync,nsnr,xdt,f0,decoded,              &
            irc,qual,ntrperiod,fmid,w50)
!###
    endif

!    write(61,3061) nutc,irc,xdt,f0,snr1,snr2,trim(decoded)
!3061 format(i6.6,i4,4f10.2,2x,a)

    return
  end subroutine decode

end module q65_decode
