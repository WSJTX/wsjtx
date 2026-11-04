module jt65_decode

  use jt65_mod, only: jt65_workspace,jt65_average_entry,jt65_work, &
       clear_jt65_averages,destroy_jt65_plans,jt65_ap_epoch
  use, intrinsic :: iso_c_binding, only: c_int16_t,c_int64_t

  integer, parameter :: NSZ=3413, NZMAX=60*12000

  type jt65_options
    integer :: utc=0,low_frequency=200,high_frequency=4000,receive_frequency=1500,tolerance=1000
    integer :: submode=0,min_sync=0,passes=2,trials=1000,aggressiveness=0,effort=1,qso_progress=0
    logical :: repeat=.false.,single_decode=.false.,vhf=.false.,average=.false.
    logical :: auto_clear=.false.,deep_search=.false.,ap=.false.
    character(len=12) :: mycall='',hiscall=''
    character(len=6) :: hisgrid=''
  end type

  type :: jt65_decoder
     type(jt65_workspace), pointer :: workspace=>null()
     procedure(jt65_decode_callback), pointer :: callback => null()
   contains
     procedure :: decode
     procedure :: decode_pcm
     procedure :: initialize
     procedure :: release_input
     procedure :: clear_averages
     procedure :: reset
     procedure :: destroy
     procedure :: set_calls
     procedure :: get_averages
     final :: finalize
  end type jt65_decoder

! Callback function to be called with each decode
  abstract interface
     subroutine jt65_decode_callback(this,sync,snr,dt,freq,drift,     &
          nflip,width,decoded,ft,qual,nsmo,nsum,minsync)

       import jt65_decoder
       implicit none
       class(jt65_decoder), intent(inout) :: this
       real, intent(in) :: sync
       integer, intent(in) :: snr
       real, intent(in) :: dt
       integer, intent(in) :: freq
       integer, intent(in) :: drift
       integer, intent(in) :: nflip
       real, intent(in) :: width
       character(len=22), intent(in) :: decoded
       integer, intent(in) :: ft
       integer, intent(in) :: qual
       integer, intent(in) :: nsmo
       integer, intent(in) :: nsum
       integer, intent(in) :: minsync

     end subroutine jt65_decode_callback
  end interface

contains

  subroutine initialize(this)
    class(jt65_decoder), intent(inout) :: this
    if(.not.associated(this%workspace)) allocate(this%workspace)
  end subroutine

  subroutine release_input(this)
    class(jt65_decoder), intent(inout) :: this
    if(.not.associated(this%workspace)) return
    this%workspace%afc_valid=.false.
    this%workspace%input_id=0
    this%workspace%last_average_input=0
    if(allocated(this%workspace%reception_ids)) this%workspace%reception_ids=0
  end subroutine

  subroutine clear_averages(this)
    class(jt65_decoder), intent(inout) :: this
    if(associated(this%workspace)) call clear_jt65_averages(this%workspace)
  end subroutine

  subroutine reset(this)
    class(jt65_decoder), intent(inout) :: this
    call this%release_input()
    call this%clear_averages()
    jt65_ap_epoch=jt65_ap_epoch+1
    if(.not.associated(this%workspace)) return
    this%workspace%submode=-1
    this%workspace%profile=-1
    if(allocated(this%workspace%calls)) deallocate(this%workspace%calls,this%workspace%grids)
    this%workspace%call_revision=this%workspace%call_revision+1
    this%workspace%hint_revision=-1
  end subroutine

  subroutine destroy(this)
    class(jt65_decoder), intent(inout) :: this
    if(.not.associated(this%workspace)) return
    if(associated(jt65_work,this%workspace)) nullify(jt65_work)
    call destroy_jt65_plans(this%workspace)
    deallocate(this%workspace)
    nullify(this%workspace,this%callback)
  end subroutine

  subroutine finalize(this)
    type(jt65_decoder), intent(inout) :: this
    call this%destroy()
  end subroutine

  subroutine set_calls(this,calls,grids)
    class(jt65_decoder), intent(inout) :: this
    character(len=12), intent(in) :: calls(:)
    character(len=4), intent(in) :: grids(:)
    integer :: n
    call this%initialize()
    n=min(size(calls),size(grids),10000)
    if(allocated(this%workspace%calls)) then
      if(size(this%workspace%calls)==n) then
        if(all(this%workspace%calls==calls(:n)).and.all(this%workspace%grids==grids(:n))) return
      endif
      deallocate(this%workspace%calls,this%workspace%grids)
    endif
    allocate(this%workspace%calls(n),this%workspace%grids(n))
    this%workspace%calls=calls(:n)
    this%workspace%grids=grids(:n)
    this%workspace%call_revision=this%workspace%call_revision+1
  end subroutine

  subroutine get_averages(this,entries,count)
    class(jt65_decoder), intent(in) :: this
    type(jt65_average_entry), intent(out) :: entries(:)
    integer, intent(out) :: count
    count=0
    if(.not.associated(this%workspace)) return
    if(.not.allocated(this%workspace%averages)) return
    count=min(size(entries),this%workspace%average_count)
    entries(:count)=this%workspace%averages(:count)
    entries(:count)%dt=entries(:count)%dt-1.
  end subroutine

  subroutine decode_pcm(this,callback,samples,sample_count,options,input_id)
    class(jt65_decoder), intent(inout) :: this
    procedure(jt65_decode_callback) :: callback
    integer(c_int16_t), intent(in) :: samples(:)
    integer, intent(in) :: sample_count
    type(jt65_options), intent(in) :: options
    integer(c_int64_t), intent(in) :: input_id
    integer :: n,depth,flags
    call this%initialize()
    if(.not.allocated(this%workspace%audio)) allocate(this%workspace%audio(NZMAX))
    n=min(sample_count,size(samples),NZMAX)
    if(n<=0) return
    this%workspace%audio(:n)=samples(:n)
    this%workspace%audio(n+1:)=0.
    this%workspace%input_id=input_id
    depth=options%effort
    if(options%average) depth=ior(depth,16)
    if(options%deep_search) depth=ior(depth,32)
    if(options%auto_clear) depth=ior(depth,128)
    flags=0
    if(options%single_decode) flags=ior(flags,32)
    if(options%vhf) flags=ior(flags,64)
    call decode(this,callback,this%workspace%audio,min(n,52*12000),.true.,options%utc, &
         options%low_frequency,options%high_frequency,options%receive_frequency,options%tolerance, &
         options%submode,options%min_sync,options%repeat,options%passes,.false.,options%trials, &
         options%aggressiveness,depth,0.,.false.,options%mycall,options%hiscall,options%hisgrid, &
         flags,options%qso_progress,options%ap,prepared=.true.)
  end subroutine

  subroutine decode(this,callback,dd0,npts,newdat,nutc,nf1,nf2,nfqso,     &
       ntol,nsubmode,minsync,nagain,n2pass,nrobust,ntrials,naggressive,   &
       ndepth,emedelay,clearave,mycall,hiscall,hisgrid,nexp_decode,       &
       nQSOProgress,ljt65apon,prepared)

!  Process dd0() data to find and decode JT65 signals.

    use jt65_mod
    use timer_module, only: timer

    include 'constants.f90'

    class(jt65_decoder), intent(inout) :: this
    procedure(jt65_decode_callback) :: callback
    real, intent(in) :: dd0(NZMAX),emedelay
    integer, intent(in) :: npts, nutc, nf1, nf2, nfqso, ntol     &
         , nsubmode, minsync, n2pass, ntrials, naggressive, ndepth      &
         , nexp_decode, nQSOProgress
    logical, intent(in) :: newdat, nagain, nrobust, clearave, ljt65apon
    character(len=12), intent(in) :: mycall, hiscall
    character(len=6), intent(in) :: hisgrid

    logical, optional, intent(in) :: prepared
    type(jt65_workspace), pointer :: previous_work
    real savg(NSZ)
    real a(5)
    character*22 decoded,decoded0,avemsg,deepave
    type candidate
       real freq
       real dt
       real sync
       real flip
    end type candidate
    type(candidate) ca(300)
    type accepted_decode
       real freq
       real dt
       real sync
       character*22 decoded
    end type accepted_decode
    type(accepted_decode) dec(50)
    logical :: first_time,prtavg,single_decode,bVHF

    integer h0(0:11),d0(0:11)
    real r0(0:11)
    common/decstats/ntry65a,ntry65b,n65a,n65b,num9,numfano
    common/steve/thresh0

!            0  1  2  3  4  5  6  7  8  9 10 11
    data h0/41,42,43,43,44,45,46,47,48,48,49,49/
    data d0/71,72,73,74,76,77,78,80,81,82,83,83/

!             0    1    2    3    4    5    6    7    8    9   10   11
    data r0/0.70,0.72,0.74,0.76,0.78,0.80,0.82,0.84,0.86,0.88,0.90,0.90/


    call this%initialize()
    if(.not.allocated(this%workspace%audio)) allocate(this%workspace%audio(NZMAX))
    if(.not.allocated(this%workspace%spectra)) allocate(this%workspace%spectra(552,NSZ))
    previous_work=>jt65_work
    jt65_work=>this%workspace
    associate(dd=>jt65_work%audio,ss=>jt65_work%spectra,nutc0=>jt65_work%utc0, &
         nfreq0=>jt65_work%frequency0,nsave=>jt65_work%saved,nsum=>jt65_work%nsum, &
         clear_avg65=>jt65_work%clear_average)
    this%callback => callback
    first_time=nrobust .and. (emedelay.eq.-999.9)    !Silence compiler warning
    first_time=newdat
    if(present(prepared)) then
      if(.not.prepared) dd=dd0
    else
      dd=dd0
      jt65_work%input_id=0
    endif
    neme=0
    ndeepave=0
    ndecoded=0
    ndecoded0=0
    single_decode=iand(nexp_decode,32).ne.0 .or. nagain
    bVHF=iand(nexp_decode,64).ne.0
    width=0.
    if(jt65_work%submode/=nsubmode.or.jt65_work%profile/=merge(1,0,bVHF)) call this%clear_averages()
    jt65_work%submode=nsubmode
    jt65_work%profile=merge(1,0,bVHF)
    if(clearave) call this%clear_averages()

    if(bVHF) then
      nvec=ntrials
      npass=1
      if(n2pass.gt.1) npass=2
    else
      nvec=1000
      if(iand(ndepth,3).eq.1) then
         npass=2
         nvec=100
      elseif(iand(ndepth,3).eq.2) then
         npass=2
         nvec=1000
      else 
         npass=4
         nvec=1000
      endif
    endif
    do ipass=1,npass 
       first_time=.true.
       if(ipass.eq.1) then                        !First-pass parameters
          thresh0=2.5
          nsubtract=1
          nrob=0
       elseif( ipass.eq.2 ) then                  !Second-pass parameters
          thresh0=2.0
          nsubtract=1
          nrob=0
       elseif( ipass.eq.3 ) then 
          thresh0=2.0
          nsubtract=1
          nrob=0
       elseif( ipass.eq.4 ) then 
          thresh0=2.0
          nsubtract=0
          nrob=1
       endif
       if(npass.eq.1) then
         nsubtract=0
         thresh0=2.0
       endif

       call timer('symsp65 ',0)
       ss=0.
       call symspec65(dd,npts,nqsym,savg)    !Get normalized symbol spectra
       call timer('symsp65 ',1)
       if(nqsym==0) exit
       nfa=nf1
       nfb=nf2

!### Q: should either of the next two uses of "single_decode" be "bVHF" instead?       
       if(single_decode .or. (bVHF .and. ntol.lt.1000)) then
          nfa=max(200,nfqso-ntol)
          nfb=min(4000,nfqso+ntol)
          thresh0=1.0
       endif
       df=12000.0/8192.0                     !df = 1.465 Hz
       if(bVHF) then
          ia=max(1,nint((nfa-100)/df))
          ib=min(NSZ,nint((nfb+100)/df))
          nz=ib-ia+1
          if(nz.lt.50) go to 900
          if(isnan(sum(savg(ia:ia+nz-1)))) go to 900
          call lorentzian(savg(ia),nz,a)
          baseline=a(1)
          amp=a(2)
          f0=(a(3)+ia-1)*df
          width=a(4)*df
       endif

       ncand=0
       call timer('sync65  ',0)
       call sync65(nfa,nfb,ntol,nqsym,ca,ncand,nrob,bVHF)
       ncand=min(ncand,50/ipass)
       call timer('sync65  ',1)

       mode65=2**nsubmode
       nflip=1
       nqd=0
       decoded='                      '
       decoded0=""
       freq0=0.
       prtavg=.false.
       if(.not.nagain) nsum=0


       if(bVHF) then
! Be sure to search for shorthand message at nfqso +/- ntol
          if(ncand.lt.300) ncand=ncand+1
          ca(ncand)%sync=5.0
          ca(ncand)%dt=2.5
          ca(ncand)%freq=nfqso
          ca(ncand)%flip=0
       endif
       do icand=1,ncand
          sync1=ca(icand)%sync
          dtx=ca(icand)%dt
          freq=ca(icand)%freq
          if(bVHF) then
             flip=ca(icand)%flip
             nflip=int(flip)
          else
             nflip=1
          endif
          if(sync1.lt.float(minsync)) nflip=0
          if(ipass.eq.1) ntry65a=ntry65a + 1
          if(ipass.eq.2) ntry65b=ntry65b + 1
          call timer('decod65a',0)
          nft=0
          nspecial=0
          call decode65a(dd,npts,first_time,nqd,freq,nflip,mode65,nvec,     &
               naggressive,ndepth,ntol,mycall,hiscall,hisgrid,nQSOProgress, &
               ljt65apon,bVHF,sync2,a,dtx,nft,nspecial,qual,                &
               nhist,nsmo,decoded)
          call timer('decod65a',1)

          if(.not.bVHF) then   
             if(abs(a(1)).gt.10.0/ipass) cycle
             ibad=0
             if(abs(a(1)).gt.5.0) ibad=1
             if(abs(a(2)).gt.2.0) ibad=ibad+1
             if(abs(dtx-1.0).gt.2.5) ibad=ibad+1
             if(ibad.ge.2) cycle
          endif
          
          if(nspecial.eq.0 .and. sync1.eq.5.0 .and. dtx.eq.2.5) cycle
          if(nspecial.eq.2) decoded='RO'
          if(nspecial.eq.3) decoded='RRR'
          if(nspecial.eq.4) decoded='73'
          if(sync1.lt.float(minsync) .and.                                  &
               decoded.eq.'                      ') nflip=0
          if(nft.ne.0) nsum=1
          
          nhard_min=param(1)
          nrtt1000=param(4)
          ntotal_min=param(5)
          nsmo=param(9)
          
          nfreq=nint(freq+a(1))
          ndrift=nint(2.0*a(2))
          if(bVHF) then
            xtmp=10**((sync1+16.0)/10.0) ! sync comes to us in dB
            s2db=1.1*db(xtmp)+1.4*(dB(width)-4.3)-52.0 
!             s2db=sync1 - 30.0 + db(width/3.3)       !### VHF/UHF/microwave
             if(nspecial.gt.0) s2db=sync2
          else
             s2db=10.0*log10(max(sync2,tiny(1.0))) - 35             !### Empirical (HF)
          endif
          nsnr=nint(s2db)
          if(nsnr.lt.-30) nsnr=-30
          if(nsnr.gt.-1) nsnr=-1
          nftt=0
!********* DOES THIS STILL WORK WHEN NFT INCLUDES # OF AP SYMBOLS USED??
          if(nft.ne.1 .and. iand(ndepth,16).eq.16 .and.                    &
               sync1.ge.float(minsync) .and. (.not.prtavg)) then
! Single-sequence FT decode failed, so try for an average FT decode.
             if(clear_avg65.or.abs(nfreq-nfreq0).gt.ntol.or. &
                  (jt65_work%input_id/=0.and.jt65_work%input_id/=jt65_work%last_average_input).or. &
                  (jt65_work%input_id==0.and.nutc/=nutc0)) then
! This is a new minute or a new frequency, so call avg65.
                jt65_work%last_average_input=jt65_work%input_id
                nutc0=nutc
                nfreq0=nfreq
                nsave=nsave+1
                nsave=mod(nsave-1,64)+1
                call avg65(nutc,nsave,sync1,dtx,nflip,nfreq,mode65,ntol,     &
                     ndepth,nagain,ntrials,naggressive,clear_avg65,neme,     &
                     mycall,hiscall,hisgrid,nftt,avemsg,qave,deepave,nsum,   &
                     ndeepave,nQSOProgress,ljt65apon)
                nsmo=param(9)
                nqave=int(qave)

                if (associated(this%callback) .and.nftt.ge.1 .and. nsum.ge.2) then
! Display a decoded message obtained by averaging 2 or more transmissions
                   call this%callback(sync1,nsnr,dtx-1.0,nfreq,ndrift,  &
                        nflip,width,avemsg,nftt,nqave,nsmo,nsum,minsync)
                   if (iand(ndepth,128).ne.0 .and. .not.nagain .and.    &
                        abs(nfreq-nfqso).le.ntol) clear_avg65=.true.  ! AutoClrAvg
                   prtavg=.true.
                end if

             endif
          endif

          if(nftt.eq.0) go to 5
!          if(nftt.eq.1) then
!!             nft=1
!             decoded=avemsg
!             go to 5
!          endif
          n=naggressive
          rtt=0.001*nrtt1000
          if(nft.lt.2 .and. minsync.ge.0 .and. nspecial.eq.0 .and. .not.bVHF) then
             if(nhard_min.gt.50) cycle
             if(nhard_min.gt.h0(n)) cycle
             if(ntotal_min.gt.d0(n)) cycle
             if(rtt.gt.r0(n)) cycle
          endif

5         continue
          if(decoded.eq.decoded0 .and. abs(freq-freq0).lt. 3.0 .and.    &
               minsync.ge.0) cycle                  !Don't display dupes
!          if(decoded.ne.'                      ' .or. minsync.lt.0) then
          if(decoded.ne.'                      ' .or. bVHF) then
             if(nsubtract.eq.1) then
                call timer('subtr65 ',0)
                call subtract65(dd,npts,freq,dtx)
                call timer('subtr65 ',1)
             endif

             ndupe=0 ! de-dedupe
             do i=1, ndecoded
                if(decoded==dec(i)%decoded) then
                   ndupe=1
                   exit
                endif
             enddo
             if(ndupe.ne.1 .and. ((sync1.ge.float(minsync)) .or. bVHF)) then
                if(ipass.eq.1) n65a=n65a + 1
                if(ipass.eq.2) n65b=n65b + 1
                if(ndecoded.lt.50) ndecoded=ndecoded+1
                dec(ndecoded)%freq=freq+a(1)
                dec(ndecoded)%dt=dtx
                dec(ndecoded)%sync=sync2
                dec(ndecoded)%decoded=decoded
                nqual=min(int(qual),9999)

                if(associated(this%callback)) then
                   call this%callback(sync1,nsnr,dtx-1.0,nfreq,ndrift,  &
                        nflip,width,decoded,nft,nqual,nsmo,1,minsync)
                end if
             endif
             decoded0=decoded
             freq0=freq
             if(decoded0.eq.'                      ') decoded0='*'
             if(single_decode .and. ndecoded.gt.0) go to 900
          endif
       enddo   ! icand
       if(ipass.gt.1 .and. ndecoded.eq.ndecoded0) exit
       ndecoded0=ndecoded
    enddo   ! ipass
900 continue
    end associate
    jt65_work=>previous_work
  end subroutine decode

  subroutine avg65(nutc,nsave,snrsync,dtxx,nflip,nfreq,mode65,ntol,ndepth,    &
       nagain, ntrials,naggressive,clear_avg65,neme,mycall,hiscall,hisgrid,   &
       nftt,avemsg,qave,deepave,nsum,ndeepave,nQSOProgress,ljt65apon)

! Decodes averaged JT65 data

    use jt65_mod
    parameter (MAXAVE=64)
    character*22 avemsg,deepave,deepbest
    character mycall*12,hiscall*12,hisgrid*6
    character*1 cused(64)
    logical nagain
    integer iused(64)
! Accumulated data for message averaging
    real s1b(-255:256,126)
    real s2(66,126)
    real s3b(64,63)
    real s3c(64,63)
    logical clear_avg65,ljt65apon
    call ensure_jt65_workspace()
    if(.not.allocated(jt65_work%history1)) then
      allocate(jt65_work%history1(-255:256,126,MAXAVE),jt65_work%history3(64,63,MAXAVE))
      allocate(jt65_work%averages(MAXAVE),jt65_work%reception_ids(MAXAVE))
      clear_avg65=.true.
    endif
    associate(iutc=>jt65_work%averages%utc,nfsave=>jt65_work%averages%frequency, &
         nflipsave=>jt65_work%averages%polarity,dtsave=>jt65_work%averages%dt, &
         syncsave=>jt65_work%averages%sync,s1save=>jt65_work%history1,s3save=>jt65_work%history3)

    nftt=0
    avemsg='                      '
    deepave='                      '
    qave=0.0
    deepbest='                      '
    nfttbest=0

    if(clear_avg65) then
       iutc=-1
       nfsave=0
       jt65_work%reception_ids=0
       jt65_work%average_count=0
       s3save=0.
       s1save=0.
       nsave=1           !### ???
! Silence compiler warnings
       if(nagain .and. ndeepave.eq.-99 .and. neme.eq.-99) stop
       clear_avg65=.false.
    endif

    do i=1,64
       if(iutc(i).lt.0) exit
       if(abs(nfreq-nfsave(i)).le.ntol.and. &
            ((jt65_work%input_id/=0.and.jt65_work%reception_ids(i)==jt65_work%input_id).or. &
            (jt65_work%input_id==0.and.nutc==iutc(i)))) then
          nsave=mod(nsave-2+MAXAVE,MAXAVE)+1
          go to 10
       endif
    enddo

! Save data for message averaging
    jt65_work%reception_ids(nsave)=jt65_work%input_id
    jt65_work%average_count=max(jt65_work%average_count,nsave)
    iutc(nsave)=nutc
    syncsave(nsave)=snrsync
    dtsave(nsave)=dtxx
    nfsave(nsave)=nfreq
    nflipsave(nsave)=nflip
    s1save(-255:256,1:126,nsave)=s1
    s3save(1:64,1:63,nsave)=s3a
    
10  syncsum=0.
    dtsum=0.
    nfsum=0
    nsum=0
    s1b=0.
    s3b=0.
    s3c=0.

    do i=1,MAXAVE                               !Consider all saved spectra
       cused(i)='.'
       jt65_work%averages(i)%used=.false.
       if(iutc(i).lt.0) exit
       if(mod(iutc(i),2).ne.mod(nutc,2)) cycle  !Use only same (odd/even) seq
       if(abs(dtxx-dtsave(i)).gt.0.2) cycle  !DT must match
       if(abs(nfreq-nfsave(i)).gt.ntol) cycle   !Freq must match
       if(nflipsave(i).eq.0) cycle              !No sync
       if(nflip.ne.nflipsave(i)) cycle          !Sync type (*/#) must match

       s3b=s3b + s3save(1:64,1:63,i)
       s1b=s1b + s1save(-255:256,1:126,i)
       syncsum=syncsum + syncsave(i)
       dtsum=dtsum + dtsave(i)
       nfsum=nfsum + nfsave(i)
       cused(i)='$'
       jt65_work%averages(i)%used=.true.
       nsum=nsum+1
       iused(nsum)=i
    enddo
    if(nsum.lt.64) iused(nsum+1)=0

    syncave=0.
    dtave=0.
    fave=0.
    if(nsum.gt.0) then
       syncave=syncsum/nsum
       dtave=dtsum/nsum
       fave=float(nfsum)/nsum
    endif

    if(nsum.lt.2) go to 900

    df=1378.125/512.0

! Do the smoothing loop
    qualbest=0.
    minsmo=0
    maxsmo=0
    if(mode65.ge.2) then
       minsmo=nint(width/df)
       maxsmo=2*minsmo
    endif
    nn=0
    do ismo=minsmo,maxsmo
       nftt=0
       if(ismo.gt.0) then
          do j=1,126
             call smo121(s1b(-255,j),512)
             if(j.eq.1) nn=nn+1
             if(nn.ge.4) then
                call smo121(s1b(-255,j),512)
                if(j.eq.1) nn=nn+1
             endif
          enddo
       endif

       do i=1,66
          jj=i
          if(mode65.eq.2) jj=2*i-1
          if(mode65.eq.4) then
             ff=4*(i-1)*df - 355.297852
             jj=nint(ff/df)+1
          endif
          s2(i,1:126)=s1b(jj,1:126)
       enddo

       do j=1,63
          k=mdat(j)                       !Points to data symbol
          if(nflip.lt.0) k=mdat2(j)
          do i=1,64
             s3c(i,j)=4.e-5*s2(i+2,k)
          enddo
       enddo

       nadd=nsum*ismo
       call extract(s3c,nadd,mode65,ntrials,naggressive,ndepth,nflip,mycall, &
            hiscall,hisgrid,nQSOProgress,ljt65apon,ncount,nhist, &
            avemsg,ltext,nftt,qual)
       if(nftt.eq.1) then
          nsmo=ismo
          param(9)=nsmo
          go to 900
       else if(nftt.ge.2) then
          if(qual.gt.qualbest) then
             deepbest=avemsg
             qualbest=qual
             nnbest=nn
             nsmobest=ismo
             nfttbest=nftt
          endif
       endif
    enddo
    if(nfttbest.eq.2) then
       avemsg=deepbest       !### ???
       deepave=deepbest
       qave=qualbest
       nsmo=nsmobest
       param(9)=nsmo
       nftt=nfttbest
    endif
900 continue
    end associate
    return
  end subroutine avg65

end module jt65_decode
