module q65_decode

   type :: q65_decoder
      procedure(q65_decode_callback), pointer :: callback
   contains
      procedure :: decode
   end type q65_decoder

   abstract interface
      subroutine q65_decode_callback (this,nutc,sync,nsnr,dt,freq,    &
         decoded,nap,ntrperiod)
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
         integer, intent(in) :: ntrperiod
      end subroutine q65_decode_callback
   end interface

contains

  subroutine decode(this,callback,iwave,nutc,ntrperiod,nsubmode,nfqso,      &
       ntol,ndepth,lclearave,emedelay,mycall,hiscall,hisgrid,nQSOprogress,  &
       ncontest,lapcqonly)

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
    use q65                               !Shared variables
 
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
    integer apmask1(78),apsymbols1(78)
    integer dgen(13)
    integer codewords(63,206)
    logical lclearave,lapcqonly,unpk77_success
    complex, allocatable :: c00(:)        !Analytic signal, 6000 Sa/s
    complex, allocatable :: c0(:)         !Analytic signal, 6000 Sa/s

    id1=0
    id2=0
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
    nFadingModel=1
! Set up the codewords for full-AP list decoding    
    call q65_set_list(mycall,hiscall,hisgrid,codewords,ncw) 
    dgen=0
    call q65_enc(dgen,codewords)         !Initialize the Q65 codec
    call timer('sync_q65',0)
    call q65_sync(nutc,iwave,ntrperiod,mode65,codewords,ncw,nsps,      &
         nfqso,ntol,ndepth,lclearave,emedelay,xdt,f0,snr1,width,dat4,  &
         snr2,idec)
    call timer('sync_q65',1)
    if(idec.ge.0) then
       xdt1=xdt                          !We have a list-decode result
       f1=f0
       go to 100
    endif
    
    if(snr1.lt.2.8) then
       xdt1=0.                   !No reliable sync, abandon decoding attempt
       f1=0.
       go to 100
    endif
    
    jpk0=(xdt+1.0)*6000                      !Index of nominal start of signal
    if(ntrperiod.le.30) jpk0=(xdt+0.5)*6000  !For shortest sequences
    if(jpk0.lt.0) jpk0=0
    fac=1.0/32767.0
    dd=fac*iwave(1:npts)
    call ana64(dd,npts,c00)              !Convert to complex c00() at 6000 Sa/s

! Generate ap symbols as in FT8
    call ft8apset(mycall,hiscall,ncontest,apsym0,aph10)
    where(apsym0.eq.-1) apsym0=0

! Main decoding loop starts here
    npasses=2
    if(nQSOprogress.eq.5) npasses=3
    if(lapcqonly) npasses=1
    iaptype=0
    do ipass=0,npasses
       apmask=0                         !Try first with no AP information
       apsymbols=0

       if(ipass.ge.1) then
          ! Subsequent passes use AP information appropiate for nQSOprogress
          call q65_ap(nQSOprogress,ipass,ncontest,lapcqonly,iaptype,   &
               apsym0,apmask1,apsymbols1)
          write(c78,1050) apmask1
1050      format(78i1)
          read(c78,1060) apmask
1060      format(13b6.6)
          write(c78,1050) apsymbols1
          read(c78,1060) apsymbols
          if(iaptype.eq.4) then
             do j=1,3
                ng15=32401+j
                write(c78(60:74),'(b15.15)') ng15
                read(c78,1060) dgen
                call q65_enc(dgen,codewords(1,j))
             enddo
          endif
       endif

       call timer('q65loops',0)
       call q65_loops(c00,npts/2,nsps/2,mode65,nsubmode,ndepth,jpk0,   &
            xdt,f0,iaptype,xdt1,f1,snr2,dat4,idec)
       call timer('q65loops',1)
       if(idec.gt.0) go to 100       !Successful decode, we're done
    enddo

! No single-transmission decode.
    if(iand(ndepth,16).eq.16) then
       call q65_avg2(ntrperiod,ntol,baud,nsubmode,nQSOprogress,lapcqonly, &
            ibwa,ibwb,codewords,ncw,xdt,f0,snr1,snr2,dat4,idec)
    endif
    
100 decoded='                                     '
    if(idec.gt.0) then

! ------------------------------------------------------
! idec Meaning
! ------------------------------------------------------
! -1:  No decode
!  1:  Decode with AP for "MyCall DxCall ?"
!  2:  Decode without AP information
!  3:  Decode with AP for "CQ ? ?"
!  4:  Decode with AP for "MyCall ? ?"
! ------------------------------------------------------
! Second digit (if any) is number of sequences averaged.
! ------------------------------------------------------

! Unpack decoded message for display to user
       write(c77,1000) dat4(1:12),dat4(13)/2
1000   format(12b6.6,b5.5)
       call unpack77(c77,0,decoded,unpk77_success) !Unpack to get msgsent
       nsnr=nint(snr2)
       call this%callback(nutc,sync,nsnr,xdt1,f1,decoded,              &
            idec,ntrperiod)
    else
! Report sync, even if no decode.
       nsnr=db(snr1) - 35.0
       idec=-1
       call this%callback(nutc,sync,nsnr,xdt1,f1,decoded,              &
            idec,ntrperiod)
    endif

    return
  end subroutine decode

end module q65_decode
