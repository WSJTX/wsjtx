module jt4_decode
  use iso_c_binding, only: c_ptr,c_null_ptr,c_associated,c_f_pointer,c_int16_t,c_size_t
  use fftw3
  use jt4, only: nch,npr,MAXAVE

  type :: jt4_options
     integer :: utc=0,submode=0,receive_frequency_hz=1200,tolerance_hz=100,depth=1
     integer :: min_sync=0,minimum_width_profile=0
     logical :: averaging=.false.,deep_search=.false.
     character(len=12) :: mycall='',hiscall=''
     character(len=6) :: hisgrid=''
  end type
  type :: jt4_call
     character(len=12) :: call=''
     character(len=4) :: grid=''
  end type
  type :: jt4_average_entry
     integer :: utc=0,frequency_hz=0,polarity=1
     logical :: used=.false.
     real :: sync=0.,dt_seconds=0.
  end type
  type, private :: jt4_fft_buffer
     type(c_ptr) :: storage=c_null_ptr,forward=c_null_ptr,backward=c_null_ptr
     real, pointer :: x(:)=>null()
     complex, pointer :: cx(:)=>null()
  end type
  type :: jt4_decoder
     procedure(jt4_decode_callback), pointer :: decode_callback => null ()
     procedure(jt4_diagnostic_callback), pointer, nopass :: diagnostic_callback => null ()
     integer :: iutc(MAXAVE)=-1,nfsave(MAXAVE)=0,nsave=0,average_count=0
     real :: dtsave(MAXAVE)=0.,syncsave(MAXAVE)=0.,flipsave(MAXAVE)=0.
     logical :: average_used(MAXAVE)=.false.
     real, allocatable :: ppsave(:,:,:),rsymbol(:,:)
     real, allocatable, private :: dat(:),pcm(:),s2(:,:)
     integer, private :: ich1=1,ich2=1,current_submode=-1,current_minimum_profile=-1
     integer, private :: fft_flags=FFTW_ESTIMATE
     real :: sync_strength=0.
     type(jt4_fft_buffer), private :: transform(4)
     type(jt4_call), allocatable :: calls(:)
     real, allocatable, private :: code(:,:)
     character(len=22), allocatable, private :: testmsg(:)
     character(len=12), private :: mycall0='',hiscall0=''
     character(len=6), private :: hisgrid0=''
     logical, private :: templates_valid=.false.,prepared=.false.
     integer, private :: ntot=0,kernel_status=0
   contains
     procedure :: decode_pcm,set_calls,get_averages,clear_averages,release_input,reset,set_fft_flags
     procedure :: save_average,select_average
     procedure, private :: wsjt4,avg4,decode4,deep4,sync4,lpf1,ps4,getsnr,prepare_workspace,destroy_plans
     final :: finalize_jt4
  end type jt4_decoder

  abstract interface
     subroutine jt4_diagnostic_callback(line)
       character(len=*),intent(in) :: line
     end subroutine
  end interface
  abstract interface
     subroutine jt4_decode_callback (this, snr, dt, freq, have_sync,     &
          sync, is_deep, decoded, qual, ich, is_average, ave)
       import jt4_decoder
       implicit none
       class(jt4_decoder), intent(inout) :: this
       integer, intent(in) :: snr
       real, intent(in) :: dt
       integer, intent(in) :: freq
       logical, intent(in) :: have_sync
       logical, intent(in) :: is_deep
       character(len=1), intent(in) :: sync
       character(len=22), intent(in) :: decoded
       real, intent(in) :: qual
       integer, intent(in) :: ich
       logical, intent(in) :: is_average
       integer, intent(in) :: ave
     end subroutine jt4_decode_callback
  end interface


contains

  subroutine wsjt4(this,dat,npts,nutc,minsync,ntol,mode4,minw,mycall,hiscall,hisgrid,nfqso,ndepth)

! JT4 searches for one signal within the selected frequency tolerance.

    use timer_module, only: timer

    class(jt4_decoder), intent(inout) :: this
    integer, intent(in) :: npts,nutc,minsync,ntol,mode4,minw,       &
         nfqso,ndepth
    character(len=12), intent(in) :: mycall,hiscall
    character(len=6), intent(in) :: hisgrid
    real, intent(in) :: dat(npts) !Raw data
    logical prtavg,have_candidate
    character decoded*22
    character*22 avemsg,deepmsg,deepave,blank,deepmsg0,deepave1
    character csync*1

    blank=' '
    decoded=blank
    deepmsg=blank
    deepmsg0=blank
    deepave1=blank
    avemsg=blank
    csync=' '
    ich=0
    ich0=-1
    aveich=-1
    nfanoave=0
    ndeepave=0
    dtx0=0.
    dtx1=0.
    nfreq0=nfqso
    nfreq1=nfqso
    syncmin=1.0+minsync
    nq1=3
    if(iand(ndepth,7).ge.2) nq1=1

! Attempt to synchronize: look for sync pattern, get DF and DT.
    call timer('sync4   ',0)
    call this%sync4(dat,npts,ntol,nfqso,mode4,minw+1,dtx,dfx,snrsync,flip,have_candidate)
    sync=snrsync
    this%sync_strength=sync
    dtxz=dtx-0.8
    nfreqz=nint(dfx)
    call timer('sync4   ',1)

    nsnr=-26
    if(.not.have_candidate.or.sync.lt.syncmin) then
       if (associated (this%decode_callback)) then
          call this%decode_callback(nsnr,dtxz,nfreqz,.false.,csync,      &
               .false.,decoded,0.,ich,.false.,0)
       end if
       go to 990
    endif

! We have achieved sync
    nsnr=nint(snrsync - 22.9)
    decoded=blank
    deepmsg=blank
    csync='*'
    if(flip.lt.0.0) csync='#'
    qbest=0.
    qabest=0.
    prtavg=.false.

    idf=0
    do idt=-2,2
       dtx=dtxz + 0.03*idt
       nfreq=nfreqz + 2*idf

! Attempt a single-sequence decode, including deep4 if Fano fails.
       call timer('decode4 ',0)
       call this%decode4(dat,npts,dtx,nfreq,flip,mode4,ndepth,minw,           &
            mycall,hiscall,hisgrid,decoded,nfano,deepmsg,qual,ich)
       call timer('decode4 ',1)

       if(nfano.gt.0) then
          call this%getsnr(dat,npts,mode4,dtx,decoded,nsnr)
! Fano succeeded: report the message and return              !Fano OK
          if (associated (this%decode_callback)) then
             call this%decode_callback(nsnr,dtx,nfreq,.true.,csync,      &
                  .false.,decoded,99.,ich,.false.,0)
          end if
          go to 990

       else                                                  !Fano failed
          if(qual.gt.qbest) then
             dtx0=dtx
             nfreq0=nfreq
             deepmsg0=deepmsg
             ich0=ich
             qbest=qual
          endif
       endif

       if(idt.ne.0) cycle
! Single-sequence Fano decode failed, so try for an average Fano decode:
       qave=0.
! If we're doing averaging, call avg4
       if(iand(ndepth,16).eq.16 .and. (.not.prtavg)) then
          call this%save_average(nutc,sync,dtx,flip,nfreq,ntol)
          if(this%kernel_status/=0) return
          call timer('avg4    ',0)
          call this%avg4(nutc,dtx,flip,nfreq,mode4,ntol,ndepth, &
               mycall,hiscall,hisgrid,nfanoave,avemsg,qave,deepave,ich,ndeepave)
          call timer('avg4    ',1)

          if(nfanoave.gt.0) then
! Fano succeeded: report the message                       AVG FANO OK
             if (associated (this%decode_callback)) then
                call this%decode_callback(nsnr,dtx,nfreq,.true.,csync,   &
                     .false.,avemsg,99.,ich,.true.,nfanoave)
             end if
             prtavg=.true.
             cycle
          else
             if(qave.gt.qabest) then
                dtx1=dtx
                nfreq1=nfreq
                deepave1=deepave
                aveich=ich
                qabest=qave
             endif
          endif
       endif
    enddo

    dtx=dtx0
    nfreq=nfreq0
    deepmsg=deepmsg0
    ich=ich0
    qual=qbest

    if (associated (this%decode_callback)) then
       if(qual.gt.0.0) call this%getsnr(dat,npts,mode4,dtx,deepmsg,nsnr)
       if(int(qual).ge.nq1) then
          call this%decode_callback(nsnr,dtx,nfreqz,.true.,csync,.true., &
               deepmsg,qual,ich,.false.,0)
       else
          call this%decode_callback(nsnr,dtxz,nfreqz,.true.,csync,       &
               .false.,blank,0.,ich,.false.,0)
       endif
    end if

    dtx=dtx1
    nfreq=nfreq1
    deepave=deepave1
    ich=aveich
    qave=qabest

    if (associated (this%decode_callback) .and. ndeepave.ge.2) then
       if(int(qave).ge.nq1) then
          call this%decode_callback(nsnr,dtx,nfreq,.true.,csync,.true.,  &
               deepave,qave,ich,.true.,ndeepave)
       endif
    end if

990 return
  end subroutine wsjt4

  subroutine avg4(this,nutc,dtxx,flip,nfreq,mode4,ntol,ndepth, &
       mycall,hiscall,hisgrid,nfanoave,avemsg,qave,deepave,ichbest,ndeepave)

! Decodes averaged JT4 data

    class(jt4_decoder), intent(inout) :: this

    character*22 avemsg,deepave,deepbest
    character mycall*12,hiscall*12,hisgrid*6
    real sym(207,7)
    call this%select_average(nutc,dtxx,flip,nfreq,ntol,sym,nsum)
    nfanoave=0
    ndeepave=0
    avemsg=' '
    deepave=' '
    deepbest=' '
    qave=0.
    ichbest=-1
    if(nsum==0) return
    kbest=this%ich1
    do k=this%ich1,this%ich2
       call extract4(sym(1,k),ncount,avemsg)     !Do the Fano decode
       nfanoave=0
       if(ncount.ge.0) then
          ichbest=k
          nfanoave=nsum
          go to 900
       endif
       if(nch(k).ge.mode4) exit
    enddo

    deepave='                      '
    qave=0.

    if(iand(ndepth,32).eq.32) then
       flipx=1.0                     !Normal flip not relevant for ave msg
       qbest=0.
       do k=this%ich1,this%ich2
          call this%deep4(sym(2,k),flipx,mycall,hiscall,hisgrid,deepave,qave)
          if(qave.gt.qbest) then
             qbest=qave
             deepbest=deepave
             kbest=k
             ndeepave=nsum
          endif
          if(nch(k).ge.mode4) exit
       enddo

       deepave=deepbest
       qave=qbest
       ichbest=kbest
    endif

900 return
  end subroutine avg4
subroutine getsnr(this,dat,npts,mode4,dtx,decoded,nsnr)

! Compute SNR using raw data, T, DF, and decoded message.

  class(jt4_decoder),intent(inout) :: this
  parameter(NSPS=1260,NH=NSPS/2)
  real dat(npts)
  real,pointer :: x(:)
  real s(NH),savg(NH)
  complex,pointer :: cx(:)
  integer itone(206)
  integer ipk(1)
  character*22 decoded,msgsent
  x=>this%transform(4)%x
  cx=>this%transform(4)%cx

  call gen4(decoded,0,msgsent,itone,itype)  !Get itone values for this message
  dt=1.0/5512.5
  df=5512.5/NSPS
  istart=(dtx+1.08)/dt

  savg=0.
  do j=1,206                           !Loop over all symbols
     cx=0.
     do i=1,NSPS
        k=(j-1)*NSPS + i + istart
        if(k.ge.1 .and. k.le.npts) x(i)=dat(k)
     enddo
     call fftwf_execute_dft_r2c(this%transform(4)%forward,x,cx)        !r2c FFT
     do i=1,nh
        s(i)=real(cx(i))**2 + aimag(cx(i))**2
     enddo
     nshift=mode4*itone(j)
     savg=savg + cshift(s,nshift)      !Move all power into lowest tone freq
  enddo

  nskip=mode4/2
  call averms(savg,nh,nskip,ave,rms)
  if(rms<=tiny(rms)) then
     nsnr=-33
     return
  endif
  savg=(savg-ave)/rms

  do i=1,mode4/4
     call smo121(savg,nh)
  enddo

  ipk=maxloc(savg)
  psig=0.
  do i=ipk(1),1,-1
     if(savg(i).lt.0) exit
     psig=psig + savg(i)
  enddo
  do i=ipk(1),nh
     if(savg(i).lt.0) exit
     psig=psig + savg(i)
  enddo
  pnoise=2500.0*sqrt(206.0)/df
  snrdb=db(psig/pnoise)
  nsnr=nint(snrdb)


end subroutine getsnr

subroutine decode4(this,dat,npts,dtx,nfreq,flip,mode4,ndepth,minw,           &
     mycall,hiscall,hisgrid,decoded,nfano,deepbest,qbest,ichbest)

! Decodes JT4 data, assuming that DT and DF have already been determined.
! Input dat(npts) has already been downsampled by 2: rate = 11025/2.

  class(jt4_decoder),intent(inout) :: this
  real dat(npts)                        !Raw data
  character decoded*22,deepmsg*22,deepbest*22
  character*12 mycall,hiscall
  character*6 hisgrid
  real*8 dt,df,phi,f0,dphi,twopi,phi1,dphi1
  complex*16 cz,cz1,c0,c1
  real*4 sym(207)

  twopi=8*atan(1.d0)
  dt=2.d0/11025             !Sample interval (2x downsampled data)
  df=11025.d0/2520.d0       !Tone separation for JT4A mode
  nsym=206
  amp=15.0
  istart=nint((dtx+0.8)/dt)              !Start index for synced FFTs
  if(istart.lt.0) istart=0
  nchips=0
  nfano=0
  decoded=' '
  deepbest=' '
  qbest=0.
  deepmsg='                      '
  ichbest=-1
  c0=0.
  k=istart
  phi=0.d0
  phi1=0.d0

  this%ich1=minw+1
  do ich=1,7
     if(nch(ich).le.mode4) this%ich2=ich
  enddo

  do ich=this%ich1,this%ich2
     nchips=min(nch(ich),70)
     nspchip=1260/nchips
     k=istart
     phi=0.d0
     phi1=0.d0
     fac2=1.e-8 * sqrt(float(mode4))
     do j=1,nsym+1
        if(flip.gt.0.0) then
           f0=nfreq + (npr(j))*mode4*df
           f1=nfreq + (2+npr(j))*mode4*df
        else
           f0=nfreq + (1-npr(j))*mode4*df
           f1=nfreq + (3-npr(j))*mode4*df
        endif
        dphi=twopi*dt*f0
        dphi1=twopi*dt*f1
        sq0=0.
        sq1=0.
        do nc=1,nchips
           phi=0.d0
           phi1=0.d0
           c0=0.
           c1=0.
           do i=1,nspchip
              k=k+1
              phi=phi+dphi
              phi1=phi1+dphi1
              cz=dcmplx(cos(phi),-sin(phi))
              cz1=dcmplx(cos(phi1),-sin(phi1))
              if(k.le.npts) then
                 c0=c0 + dat(k)*cz
                 c1=c1 + dat(k)*cz1
              endif
           enddo
           sq0=sq0 + real(c0)**2 + aimag(c0)**2
           sq1=sq1 + real(c1)**2 + aimag(c1)**2
        enddo
        sq0=fac2*sq0
        sq1=fac2*sq1
        rsym=amp*(sq1-sq0)
        this%rsymbol(j,ich)=rsym
        sym(j)=rsym
     enddo

     call extract4(sym,ncount,decoded)          !Do the convolutional decode
     nfano=0
     if(ncount.ge.0) then
        nfano=1
        ichbest=ich
        exit
     endif

     qual=0.                                    !Now try deep search
     if(iand(ndepth,32).eq.32) then
        call this%deep4(sym(2),flip,mycall,hiscall,hisgrid,deepmsg,qual)
        if(qual.gt.qbest) then
           qbest=qual
           deepbest=deepmsg
           ichbest=ich
        endif
     endif
  enddo

end subroutine decode4

subroutine deep4(this,sym0,flip,mycall,hiscall,hisgrid,decoded,qual)

! Deep search routine for JT4

  class(jt4_decoder),intent(inout) :: this
  parameter (MAXCALLS=7000,MAXRPT=63)
  real*4 sym0(206),sym(206)
  character callsign*12,grid*4,message*22,hisgrid*6
  character*12 mycall,hiscall
  character*22 decoded
  character(len=15) :: callgrid
  character*180 line
  character*4 rpt(MAXRPT)
  integer ncode(206)
  real :: pp(2*MAXCALLS + 2 + MAXRPT)
  integer :: allocation_status
  data rpt/'-01','-02','-03','-04','-05',          &
           '-06','-07','-08','-09','-10',          &
           '-11','-12','-13','-14','-15',          &
           '-16','-17','-18','-19','-20',          &
           '-21','-22','-23','-24','-25',          &
           '-26','-27','-28','-29','-30',          &
           'R-01','R-02','R-03','R-04','R-05',     &
           'R-06','R-07','R-08','R-09','R-10',     &
           'R-11','R-12','R-13','R-14','R-15',     &
           'R-16','R-17','R-18','R-19','R-20',     &
           'R-21','R-22','R-23','R-24','R-25',     &
           'R-26','R-27','R-28','R-29','R-30',     &
           'RO','RRR','73'/

  sym=sym0
  if(mycall.eq.this%mycall0 .and. hiscall.eq.this%hiscall0 .and.         &
       hisgrid.eq.this%hisgrid0 .and. this%templates_valid) go to 30

  if(.not.allocated(this%code)) then
     allocate(this%code(206,64+2*size(this%calls)),this%testmsg(64+2*size(this%calls)),stat=allocation_status)
     if(allocation_status/=0) then
        this%kernel_status=-1
        if(allocated(this%code)) deallocate(this%code)
        if(allocated(this%testmsg)) deallocate(this%testmsg)
        decoded=' '
        qual=0.
        return
     endif
  endif
  k=0
  do n=1,1+size(this%calls)
     if(n.eq.1) then
        callsign=hiscall
        do i=4,12
           if(ichar(callsign(i:i)).eq.0) callsign(i:i)=' '
        enddo
        grid=hisgrid(1:4)
        if(ichar(grid(3:3)).eq.0) grid(3:3)=' '
        if(ichar(grid(4:4)).eq.0) grid(4:4)=' '
     else
        callsign=this%calls(n-1)%call
        grid=this%calls(n-1)%grid
     endif

     j1=index(mycall,' ') - 1
     if(j1.le.-1) j1=12
     if(j1.lt.3) j1=6
     j2=index(callsign,' ') - 1
     if(j2.le.-1) j2=12
     if(j2.lt.3) j2=6
     j3=index(mycall,'/')                 ! j3>0 means compound mycall
     j4=index(callsign,'/')               ! j4>0 means compound hiscall
     callgrid=callsign(1:j2)

     mz=1
! The current contact also supplies report and acknowledgement templates.
     if(n.eq.1 .and. j3.lt.1 .and. j4.lt.1 .and. callsign(1:6).ne.'      ') &
          mz=MAXRPT+1
     do m=1,mz
        if(m.gt.1) grid=rpt(m-1)
        if(j3.lt.1 .and.j4.lt.1) callgrid=callsign(1:j2)//' '//grid
        message=mycall(1:j1)//' '//callgrid
        k=k+1
        this%testmsg(k)=message
        call encode4(message,ncode)
        this%code(1:206,k)=2*ncode(1:206)-1
        if(n.ge.2) then
! Insert CQ message
           if(j4.lt.1) callgrid=callsign(1:j2)//' '//grid
           message='CQ '//callgrid
           k=k+1
           this%testmsg(k)=message
           call encode4(message,ncode)
        this%code(1:206,k)=2*ncode(1:206)-1
        endif
     enddo
  enddo

  this%ntot=k

30 this%mycall0=mycall
  this%hiscall0=hiscall
  this%hisgrid0=hisgrid
  this%templates_valid=.true.

  sq=0.
  do j=1,206
     sq=sq + sym(j)**2
  enddo
  rms=sqrt(sq/206.0)
  decoded=' '
  qual=0.
  if(rms<=tiny(rms)) return
  sym=sym/rms
  ip1=1

  p1=-1.e30
  p2=-1.e30
  do k=1,this%ntot
     pp(k)=0.
! Test all messages if flip=+1; skip the CQ messages if flip=-1.
     if(flip.gt.0.0 .or. this%testmsg(k)(1:3).ne.'CQ ') then
        p=0.
        do j=1,206
           p=p + this%code(j,k)*sym(j)
        enddo
        pp(k)=p
        if(p.gt.p1) then
           p1=p
           ip1=k
        endif
     endif
  enddo

  do i=1,this%ntot
     if(pp(i).gt.p2 .and. this%testmsg(i).ne.this%testmsg(ip1)) p2=pp(i)
  enddo

  qual=p1-max(1.15*p2,70.0)

  if(associated(this%diagnostic_callback)) then
     write(line,1001) p1,p2,this%ntot,rms,qual,ip1,this%testmsg(ip1)
1001 format(2f8.2,i8,2f8.2,i6,2x,a22)
     call this%diagnostic_callback(trim(line))
  endif

  if(qual.gt.1.0) then
     decoded=this%testmsg(ip1)
  else
     decoded='                      '
     qual=0.
  endif

! Make sure everything is upper case.
  do i=1,22
     if(decoded(i:i).ge.'a' .and. decoded(i:i).le.'z')                &
          decoded(i:i)=char(ichar(decoded(i:i))-32)
  enddo

end subroutine deep4

subroutine sync4(this,dat,jz,ntol,nfqso,mode4,minwidth,dtx,dfx,snrsync,flip,have_candidate)

! Synchronizes JT4 data, finding the best-fit DT and DF.

  class(jt4_decoder),intent(inout) :: this
  parameter (NFFTMAX=2520)         !Max length of FFTs
  parameter (NHMAX=NFFTMAX/2)      !Max length of power spectra
  parameter (NSMAX=525)            !Max number of half-symbol steps
  integer ntol                     !Range of DF search
  real dat(jz)
  real ccfblue(-5:540)             !CCF with pseudorandom sequence
  logical,intent(out) :: have_candidate

! Do FFTs of twice symbol length, stepped by half symbols.  Note that
! we have already downsampled the data by factor of 2.
  dtx=0.
  dfx=real(nfqso)
  snrsync=0.
  have_candidate=.false.
  flip=1.
  nsym=207
  nfft=2520
  nq=nfft/4
  nsteps=jz/nq - 1
  df=0.5*11025.0/nfft
  ftop=nfqso + 7*mode4*df
  if(ftop.gt.11025.0/4.0) then
     go to 900
  endif


  do j=1,nsteps                     !Compute spectrum for each step, get average
     k=(j-1)*nq + 1
     call this%ps4(dat(k),nfft,this%s2(1,j))
  enddo

! Set freq and lag ranges
  ia=(nfqso-ntol)/df              !Index of lowest tone, bottom of search range
  ib=(nfqso+ntol)/df              !Index of lowest tone, top of search range
  iamin=nint(100.0/df)
  if(ia.lt.iamin) ia=iamin
  ibmax=nint(2700.0/df) - 6*mode4
  if(ib.gt.ibmax) ib=ibmax

  lag1=-5
  lag2=59
  syncbest=-1.e30

  do ich=minwidth,7                       !Find best width
     kz=nch(ich)/2
     iaa=ia+kz
     ibb=ib-kz
     do i=iaa,ibb                       !Find best frequency channel for CCF
        call xcor4(this%s2,i,nsteps,nsym,lag1,lag2,ich,mode4,ccfblue,ccf0,   &
             lagpk0,flip)
        if(.not.(ccf0.gt.0.0)) cycle

! Find rms of the CCF, without main peak
        call slope(ccfblue(lag1),lag2-lag1+1,lagpk0-lag1+1.0)
        sync=abs(ccfblue(lagpk0))

! Find best sync value
        if(sync.gt.0.0 .and. sync.gt.syncbest*1.03) then
           ipk=i
           ichpk=ich
           syncbest=sync
        endif
     enddo
  enddo
  if(syncbest<=0.) go to 900

  dfx=ipk*df

! Peak up in time, at best whole-channel frequency
  call xcor4(this%s2,ipk,nsteps,nsym,lag1,lag2,ichpk,mode4,ccfblue,ccfmax,   &
       lagpk,flip)
  xlag=lagpk
  if(lagpk.gt.lag1 .and. lagpk.lt.lag2) then
     call peakup(ccfblue(lagpk-1),ccfmax,ccfblue(lagpk+1),dx2)
     xlag=lagpk+dx2
  endif

! Find rms of the CCF, without the main peak
  call slope(ccfblue(lag1),lag2-lag1+1,xlag-lag1+1.0)
  sq=0.
  nsq=0
  do lag=lag1,lag2
     if(abs(lag-xlag).gt.2.0) then
        sq=sq+ccfblue(lag)**2
        nsq=nsq+1
     endif
  enddo
  rms=sqrt(sq/nsq)
  if(rms<=tiny(rms)) return
  snrsync=max(0.0,db(abs(ccfblue(lagpk)/rms - 1.0)) - 4.5)
  dt=2.0/11025.0
  istart=xlag*nq
  dtx=istart*dt
  have_candidate=.true.

900 return
end subroutine sync4

subroutine lpf1(this,dd,jz,dat,jz2)

  class(jt4_decoder),intent(inout) :: this
  parameter (NFFT1=64*11025,NFFT2=32*11025)
  real dd(jz)
  real dat(jz/2)
  real,pointer :: x(:)
  complex,pointer :: cx(:)
  x=>this%transform(2)%x
  cx=>this%transform(2)%cx

  fac=1.0/float(NFFT1)
  x(1:jz)=fac*dd(1:jz)
  x(jz+1:NFFT1)=0.0
  call fftwf_execute_dft_r2c(this%transform(2)%forward,x,cx)                    !Forward FFT, r2c
  cx(NFFT2/2:)=0.0


  call fftwf_execute_dft_c2r(this%transform(2)%backward,cx,x)                   !Inverse FFT, c2r
  jz2=jz/2
  dat(1:jz2)=x(1:jz2)

end subroutine lpf1

subroutine ps4(this,dat,nfft,s)

  class(jt4_decoder),intent(inout) :: this
  parameter (NMAX=2520+2)
  parameter (NHMAX=NMAX/2-1)
  real dat(nfft)
  real,pointer :: dat2(:)
  real s(NHMAX)
  complex,pointer :: c(:)
  dat2=>this%transform(3)%x
  c=>this%transform(3)%cx

  nh=nfft/2
  do i=1,nh
     dat2(i)=dat(i)/128.0       !### Why 128 ??
  enddo
  do i=nh+1,nfft
     dat2(i)=0.
  enddo

  call fftwf_execute_dft_r2c(this%transform(3)%forward,dat2,c)

  fac=1.0/nfft
  do i=1,nh
     s(i)=fac*(real(c(i))**2 + aimag(c(i))**2)
  enddo

end subroutine ps4

  subroutine decode_pcm(this,options,input,sample_rate_hz,decode_callback,status,diagnostic_callback)
    class(jt4_decoder),intent(inout) :: this
    type(jt4_options),intent(in) :: options
    integer(c_int16_t),intent(in) :: input(:)
    integer,intent(in) :: sample_rate_hz
    procedure(jt4_decode_callback) :: decode_callback
    integer,intent(out) :: status
    procedure(jt4_diagnostic_callback),optional :: diagnostic_callback
    integer :: jz,jz2,depth,n
    real,pointer :: x(:)
    complex,pointer :: cx(:)

    status=-1
    if(sample_rate_hz/=11025.and.sample_rate_hz/=12000) return
    if(size(input)<1.or.size(input)>60*sample_rate_hz) return
    if(options%submode<0.or.options%submode>6) return
    if(options%minimum_width_profile<0.or.options%minimum_width_profile>options%submode) return
    call this%prepare_workspace(status,sample_rate_hz==12000)
    if(status/=0) return
    this%decode_callback=>decode_callback
    nullify(this%diagnostic_callback)
    if(present(diagnostic_callback)) this%diagnostic_callback=>diagnostic_callback
    if(this%current_submode/=options%submode.or.this%current_minimum_profile/=options%minimum_width_profile) &
         call this%clear_averages()
    this%current_submode=options%submode
    this%current_minimum_profile=options%minimum_width_profile
    if(.not.this%prepared) then
       this%pcm=0.
       jz=min(size(input),52*sample_rate_hz)
       if(sample_rate_hz==11025) then
          this%pcm(1:jz)=real(input(1:jz))
       else
          x=>this%transform(1)%x
          cx=>this%transform(1)%cx
          x=0.
          x(1:jz)=real(input(1:jz))
          call fftwf_execute_dft_r2c(this%transform(1)%forward,x,cx)
          cx(320000:)=0.
          call fftwf_execute_dft_c2r(this%transform(1)%backward,cx,x)
          n=int(jz*11025./12000.)
          this%pcm(1:n)=1.e-6*x(1:n)
       endif
       call this%lpf1(this%pcm,52*11025,this%dat,jz2)
       this%prepared=.true.
    endif
    depth=options%depth
    if(options%averaging) depth=ior(depth,16)
    if(options%deep_search) depth=ior(depth,32)
    this%kernel_status=0
    call this%wsjt4(this%dat,26*11025,options%utc,options%min_sync,options%tolerance_hz, &
         nch(options%submode+1),options%minimum_width_profile,options%mycall,options%hiscall, &
         options%hisgrid,options%receive_frequency_hz,depth)
    status=this%kernel_status
    nullify(this%decode_callback,this%diagnostic_callback)
  end subroutine

  subroutine prepare_workspace(this,status,include_conversion)
    class(jt4_decoder),intent(inout) :: this
    integer,intent(out) :: status
    logical,intent(in),optional :: include_conversion
    integer,parameter :: lengths(4)=[64*12000,64*11025,2520,1260]
    integer,parameter :: inverse_lengths(4)=[64*11025,32*11025,0,0]
    integer :: i,n,allocation_status,first
    status=-1
    if(.not.allocated(this%pcm)) then
       allocate(this%pcm(52*11025),stat=allocation_status)
       if(allocation_status/=0) return
    endif
    if(.not.allocated(this%dat)) then
       allocate(this%dat(30*11025),stat=allocation_status)
       if(allocation_status/=0) return
    endif
    if(.not.allocated(this%s2)) then
       allocate(this%s2(1260,525),stat=allocation_status)
       if(allocation_status/=0) return
    endif
    if(.not.allocated(this%ppsave)) then
       allocate(this%ppsave(207,7,MAXAVE),stat=allocation_status)
       if(allocation_status/=0) return
       this%ppsave=0.
    endif
    if(.not.allocated(this%rsymbol)) then
       allocate(this%rsymbol(207,7),stat=allocation_status)
       if(allocation_status/=0) return
       this%rsymbol=0.
    endif
    if(.not.allocated(this%calls)) then
       allocate(this%calls(0),stat=allocation_status)
       if(allocation_status/=0) return
    endif
    first=2
    if(present(include_conversion)) then
       if(include_conversion) first=1
    endif
    do i=first,4
       n=lengths(i)
       if(.not.c_associated(this%transform(i)%storage)) then
          this%transform(i)%storage=fftwf_alloc_complex(int(n/2+1,c_size_t))
          if(.not.c_associated(this%transform(i)%storage)) return
          call c_f_pointer(this%transform(i)%storage,this%transform(i)%x,[n+2])
          call c_f_pointer(this%transform(i)%storage,this%transform(i)%cx,[n/2+1])
          this%transform(i)%cx(0:n/2)=>this%transform(i)%cx
       endif
       if(.not.c_associated(this%transform(i)%forward)) then
!$omp critical(fftw)
          this%transform(i)%forward=fftwf_plan_dft_r2c_1d(n,this%transform(i)%x, &
               this%transform(i)%cx,this%fft_flags)
!$omp end critical(fftw)
       endif
       if(.not.c_associated(this%transform(i)%forward)) return
       if(inverse_lengths(i)>0.and..not.c_associated(this%transform(i)%backward)) then
!$omp critical(fftw)
          this%transform(i)%backward=fftwf_plan_dft_c2r_1d(inverse_lengths(i),this%transform(i)%cx, &
               this%transform(i)%x,this%fft_flags)
!$omp end critical(fftw)
       endif
       if(inverse_lengths(i)>0.and..not.c_associated(this%transform(i)%backward)) return
    enddo
    status=0
  end subroutine

  subroutine set_fft_flags(this,flags)
    class(jt4_decoder),intent(inout) :: this
    integer,intent(in) :: flags
    if(flags==this%fft_flags) return
    call this%destroy_plans()
    this%fft_flags=flags
    this%prepared=.false.
  end subroutine

  subroutine destroy_plans(this)
    class(jt4_decoder),intent(inout) :: this
    integer :: i
!$omp critical(fftw)
    do i=1,4
       if(c_associated(this%transform(i)%forward)) call fftwf_destroy_plan(this%transform(i)%forward)
       if(c_associated(this%transform(i)%backward)) call fftwf_destroy_plan(this%transform(i)%backward)
       this%transform(i)%forward=c_null_ptr
       this%transform(i)%backward=c_null_ptr
    enddo
!$omp end critical(fftw)
  end subroutine

  subroutine finalize_jt4(this)
    type(jt4_decoder),intent(inout) :: this
    integer :: i
    call this%destroy_plans()
    do i=1,4
       if(c_associated(this%transform(i)%storage)) call fftwf_free(this%transform(i)%storage)
       this%transform(i)%storage=c_null_ptr
       nullify(this%transform(i)%x,this%transform(i)%cx)
    enddo
  end subroutine

  subroutine release_input(this)
    class(jt4_decoder),intent(inout) :: this
    this%prepared=.false.
    this%sync_strength=0.
    nullify(this%decode_callback,this%diagnostic_callback)
  end subroutine

  subroutine reset(this)
    class(jt4_decoder),intent(inout) :: this
    call this%release_input()
    call this%clear_averages()
    if(allocated(this%calls)) deallocate(this%calls)
    if(allocated(this%code)) deallocate(this%code)
    if(allocated(this%testmsg)) deallocate(this%testmsg)
    this%templates_valid=.false.
    this%ntot=0
    this%current_submode=-1
    this%current_minimum_profile=-1
  end subroutine

  subroutine set_calls(this,calls,status)
    class(jt4_decoder),intent(inout) :: this
    type(jt4_call),intent(in) :: calls(:)
    integer,intent(out),optional :: status
    type(jt4_call),allocatable :: replacement(:)
    integer :: i,count,allocation_status
    if(present(status)) status=0
    count=min(size(calls),6999)
    if(allocated(this%calls)) then
       if(size(this%calls)==count) then
          do i=1,count
             if(this%calls(i)%call/=calls(i)%call.or.this%calls(i)%grid/=calls(i)%grid) exit
          enddo
          if(i>count) return
       endif
    endif
    allocate(replacement(count),stat=allocation_status)
    if(allocation_status/=0) then
       this%kernel_status=-1
       if(present(status)) status=-1
       return
    endif
    replacement=calls(1:count)
    call move_alloc(replacement,this%calls)
    if(allocated(this%code)) deallocate(this%code)
    if(allocated(this%testmsg)) deallocate(this%testmsg)
    this%templates_valid=.false.
  end subroutine

  subroutine clear_averages(this)
    class(jt4_decoder),intent(inout) :: this
    this%iutc=-1
    this%nfsave=0
    this%dtsave=0.
    this%syncsave=0.
    this%flipsave=0.
    this%average_used=.false.
    this%nsave=0
    this%average_count=0
    if(allocated(this%ppsave)) this%ppsave=0.
    if(allocated(this%rsymbol)) this%rsymbol=0.
  end subroutine

  subroutine save_average(this,utc,sync,dt,polarity,freq,tolerance)
    class(jt4_decoder),intent(inout) :: this
    integer,intent(in) :: utc,freq,tolerance
    real,intent(in) :: sync,dt,polarity
    integer :: i,allocation_status
    if(.not.allocated(this%ppsave)) then
       allocate(this%ppsave(207,7,MAXAVE),stat=allocation_status)
       if(allocation_status/=0) then
          this%kernel_status=-1
          return
       endif
       this%ppsave=0.
    endif
    if(.not.allocated(this%rsymbol)) then
       allocate(this%rsymbol(207,7),stat=allocation_status)
       if(allocation_status/=0) then
          this%kernel_status=-1
          return
       endif
       this%rsymbol=0.
    endif
    do i=1,MAXAVE
       if(this%iutc(i)/100==utc/100.and.this%iutc(i)>=0.and.abs(this%nfsave(i)-freq)<=tolerance) return
    enddo
    this%nsave=mod(this%nsave,MAXAVE)+1
    this%average_count=min(this%average_count+1,MAXAVE)
    i=this%nsave
    this%iutc(i)=utc
    this%nfsave(i)=freq
    this%syncsave(i)=sync
    this%dtsave(i)=dt
    this%flipsave(i)=polarity
    this%ppsave(:,:,i)=this%rsymbol
    this%average_used(i)=.false.
  end subroutine

  subroutine select_average(this,utc,dt,polarity,freq,tolerance,sym,nsum)
    class(jt4_decoder),intent(inout) :: this
    integer,intent(in) :: utc,freq,tolerance
    real,intent(in) :: dt,polarity
    real,intent(out) :: sym(207,7)
    integer,intent(out) :: nsum
    integer :: i
    sym=0.
    nsum=0
    this%average_used=.false.
    if(.not.allocated(this%ppsave)) return
    do i=1,MAXAVE
       if(this%iutc(i)<0) cycle
       if(mod(this%iutc(i)/100,2)/=mod(utc/100,2)) cycle
       if(abs(dt-this%dtsave(i))>0.2) cycle
       if(abs(freq-this%nfsave(i))>tolerance) cycle
       if(polarity/=this%flipsave(i)) cycle
       sym=sym+this%ppsave(:,:,i)
       nsum=nsum+1
       this%average_used(i)=.true.
    enddo
    if(nsum>0) sym=sym/nsum
  end subroutine

  subroutine get_averages(this,entries,count)
    class(jt4_decoder),intent(in) :: this
    type(jt4_average_entry),intent(out) :: entries(:)
    integer,intent(out) :: count
    integer :: i
    count=0
    do i=1,MAXAVE
       if(this%iutc(i)<0) cycle
       count=count+1
       if(count>size(entries)) cycle
       entries(count)%utc=this%iutc(i)
       entries(count)%frequency_hz=this%nfsave(i)
       entries(count)%polarity=1
       if(this%flipsave(i)<0.) entries(count)%polarity=-1
       entries(count)%used=this%average_used(i)
       entries(count)%sync=this%syncsave(i)
       entries(count)%dt_seconds=this%dtsave(i)
    enddo
  end subroutine
end module jt4_decode
