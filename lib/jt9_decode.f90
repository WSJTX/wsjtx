module jt9_decode
  use, intrinsic :: iso_c_binding, only: c_short, c_ptr, c_float, c_float_complex, c_null_ptr, c_associated
  use fftw3, only: fftwf_plan_dft_r2c_1d, fftwf_execute_dft_r2c, fftwf_destroy_plan, FFTW_ESTIMATE
  use jt9_downsample, only: jt9_downsample_workspace
  use jt9_soft_symbols, only: softsym
  private
  public :: jt9_decoder, jt9_spectrum_workspace

  type :: jt9_spectrum_workspace
     private
     real(c_float), allocatable :: input(:)
     complex(c_float_complex), allocatable :: output(:)
     type(c_ptr) :: plan=c_null_ptr
   contains
     procedure :: initialize => initialize_spectrum
     procedure :: prepare_spectra
     procedure :: clear => clear_spectrum
     final :: finalize_spectrum
  end type jt9_spectrum_workspace

  type :: jt9_decoder
     procedure(jt9_decode_callback), pointer :: callback=>null()
     type(jt9_downsample_workspace), private :: downsample
     type(jt9_spectrum_workspace), private :: spectrum
     real, allocatable, private :: spectra(:,:), ccfred(:), red2(:)
     logical, allocatable, private :: ccfok(:), done(:)
   contains
     procedure :: initialize
     procedure :: decode
     procedure :: decode_pcm
     procedure :: release_input
     procedure :: reset
     final :: finalize_decoder
  end type jt9_decoder

  abstract interface
     subroutine jt9_decode_callback (this, sync, snr, dt, freq, drift, &
          decoded)
       import jt9_decoder
       implicit none
       class(jt9_decoder), intent(inout) :: this
       real, intent(in) :: sync
       integer, intent(in) :: snr
       real, intent(in) :: dt
       real, intent(in) :: freq
       integer, intent(in) :: drift
       character(len=22), intent(in) :: decoded
     end subroutine jt9_decode_callback
  end interface

contains

  subroutine initialize(this)
    class(jt9_decoder), intent(inout) :: this
    if(.not.allocated(this%ccfred)) then
       allocate(this%ccfred(6827),this%red2(6827),this%ccfok(6827),this%done(6827))
    endif
  end subroutine initialize

  subroutine decode_pcm(this,callback,samples,sample_count,receive_frequency, &
       search_low,search_high,tolerance,depth,submode,repeat)
    class(jt9_decoder), intent(inout) :: this
    procedure(jt9_decode_callback) :: callback
    ! The engine owns the full analysis buffer and pads beyond sample_count.
    integer(c_short), intent(in) :: samples(60*12000)
    integer, intent(in) :: sample_count,receive_frequency,search_low,search_high,tolerance,depth,submode
    logical, intent(in) :: repeat
    integer :: count,half_symbols

    count=max(0,min(sample_count,60*12000))
    half_symbols=min(181,max(0,(count-2048)/3456))
    call this%release_input()
    if(half_symbols.lt.2) return
    if(.not.any(samples(1:count).ne.0)) return
    if(.not.allocated(this%spectra)) allocate(this%spectra(184,6827))
    call this%spectrum%prepare_spectra(samples,half_symbols,this%spectra)
    call this%decode(callback,this%spectra,samples,receive_frequency,.true.,count/8, &
         search_low,search_low,search_high,tolerance,half_symbols,repeat,depth,9,submode,0)
  end subroutine decode_pcm

  subroutine initialize_spectrum(this)
    use fftw_tsan, only: wsjt_tsan_acquire_fftw, wsjt_tsan_release_fftw
    class(jt9_spectrum_workspace), intent(inout) :: this
    if(c_associated(this%plan)) return
    allocate(this%input(16384),this%output(0:8192))
    !$omp critical(fftw)
    call wsjt_tsan_acquire_fftw()
    this%plan=fftwf_plan_dft_r2c_1d(16384,this%input,this%output,FFTW_ESTIMATE)
    call wsjt_tsan_release_fftw()
    !$omp end critical(fftw)
  end subroutine initialize_spectrum

  subroutine prepare_spectra(this,samples,half_symbols,spectra)
    class(jt9_spectrum_workspace), intent(inout) :: this
    integer(c_short), intent(in) :: samples(:)
    integer, intent(in) :: half_symbols
    real, intent(out) :: spectra(184,6827)
    integer :: row,first,last,source_first
    real, parameter :: scale=(1.0/16384)**2

    call this%initialize()
    spectra=0.
    do row=1,half_symbols
       last=row*3456
       first=last-16384+1
       source_first=max(1,first)
       this%input=0.
       this%input(source_first-first+1:16384)=0.1*samples(source_first:last)
       call fftwf_execute_dft_r2c(this%plan,this%input,this%output)
       spectra(row,:)=scale*(real(this%output(0:6826))**2+aimag(this%output(0:6826))**2)
    enddo
  end subroutine prepare_spectra

  subroutine clear_spectrum(this)
    use fftw_tsan, only: wsjt_tsan_acquire_fftw, wsjt_tsan_release_fftw
    class(jt9_spectrum_workspace), intent(inout) :: this
    !$omp critical(fftw)
    call wsjt_tsan_acquire_fftw()
    if(c_associated(this%plan)) call fftwf_destroy_plan(this%plan)
    call wsjt_tsan_release_fftw()
    !$omp end critical(fftw)
    this%plan=c_null_ptr
    if(allocated(this%input)) deallocate(this%input,this%output)
  end subroutine clear_spectrum

  subroutine finalize_spectrum(this)
    type(jt9_spectrum_workspace), intent(inout) :: this
    call this%clear()
  end subroutine finalize_spectrum

  subroutine release_input(this)
    class(jt9_decoder), intent(inout) :: this
    call this%downsample%invalidate()
    nullify(this%callback)
  end subroutine release_input

  subroutine reset(this)
    class(jt9_decoder), intent(inout) :: this
    call this%release_input()
  end subroutine reset

  subroutine finalize_decoder(this)
    type(jt9_decoder), intent(inout) :: this
    call this%downsample%clear()
    call this%spectrum%clear()
    if(allocated(this%spectra)) deallocate(this%spectra)
    if(allocated(this%ccfred)) deallocate(this%ccfred,this%red2,this%ccfok,this%done)
    nullify(this%callback)
  end subroutine finalize_decoder

  subroutine decode(this,callback,ss,id2,nfqso,newdat,npts8,nfa,    &
       nfsplit,nfb,ntol,nzhsym,nagain,ndepth,nmode,nsubmode,nexp_decode)
    use timer_module, only: timer

    include 'constants.f90'
    class(jt9_decoder), intent(inout) :: this
    procedure(jt9_decode_callback) :: callback
    real ss(184,NSMAX)
    logical, intent(in) :: newdat, nagain
    character*22 msg
    integer*2, intent(in) :: id2(*)
    integer*1 i1SoftSymbols(207)
    common/decstats/ntry65a,ntry65b,n65a,n65b,num9,numfano

    if(nexp_decode.eq.-99) stop     !Silence compiler warning
    call this%initialize()
    associate(ccfred=>this%ccfred,red2=>this%red2,ccfok=>this%ccfok,done=>this%done)
    this%callback => callback
    if(newdat) call this%downsample%invalidate()
    if(nmode.eq.9 .and. nsubmode.ge.1) then
       call decode9w(nfqso,ntol,nsubmode,ss,nzhsym,id2,sync,nsnr,xdt,freq,msg)
       if (associated(this%callback).and.len_trim(msg)>0) then
          ndrift=0
          call this%callback(sync,nsnr,xdt,freq,ndrift,msg)
       end if
       go to 999
    endif

    nsynced=0
    ndecoded=0
    nsps=6912                                   !Params for JT9-1
    df3=1500.0/2048.0

    tstep=0.5*nsps/12000.0                      !Half-symbol step (seconds)
    done=.false.

    nf0=0
    nf1=nfa
    if(nmode.eq.65+9) nf1=nfsplit
    ia=max(1,nint((nf1-nf0)/df3))
    ib=min(NSMAX,nint((nfb-nf0)/df3))
    if(ib.le.ia) go to 999
    lag1=-int(2.5/tstep + 0.9999)
    lag2=int(5.0/tstep + 0.9999)
    call timer('sync9   ',0)
    call sync9(ss,nzhsym,lag1,lag2,ia,ib,ccfred,red2,ipk)
    call timer('sync9   ',1)

    nsps8=nsps/8
    df8=1500.0/nsps8
    dblim=db(864.0/nsps8) - 26.2

    ia1=1                         !quel compiler gripe
    ib1=1                         !quel compiler gripe
    do nqd=1,0,-1
       limit=5000
       ccflim=3.0
       red2lim=1.6
       schklim=2.2
       if(iand(ndepth,7).eq.2) then
          limit=10000
          ccflim=2.7
       endif
       if(iand(ndepth,7).eq.3 .or. nqd.eq.1) then
          limit=30000
          ccflim=2.5
          schklim=2.0
       endif
       if(nagain) then
          limit=100000
          ccflim=2.4
          schklim=1.8
       endif
       ccfok=.false.

       if(nqd.eq.1) then
          nfa1=nfqso-ntol
          nfb1=nfqso+ntol
          ia=max(1,nint((nfa1-nf0)/df3))
          ib=min(NSMAX,nint((nfb1-nf0)/df3))
          ccfok(ia:ib)=(ccfred(ia:ib).gt.(ccflim-2.0)) .and.               &
               (red2(ia:ib).gt.(red2lim-1.0))
          ia1=ia
          ib1=ib
       else
          nfa1=nf1
          nfb1=nfb
          ia=max(1,nint((nfa1-nf0)/df3))
          ib=min(NSMAX,nint((nfb1-nf0)/df3))
          do i=ia,ib
             ccfok(i)=ccfred(i).gt.ccflim .and. red2(i).gt.red2lim
          enddo
          ccfok(ia1:ib1)=.false.
       endif

       fgood=0.
       do i=ia,ib
          if(done(i) .or. (.not.ccfok(i))) cycle
          f=(i-1)*df3
          if(nqd.eq.1 .or.                                                   &
               (ccfred(i).ge.ccflim .and. abs(f-fgood).gt.10.0*df8)) then

             call timer('softsym ',0)
             fpk=nf0 + df3*(i-1)
             call softsym(this%downsample,id2,npts8,nsps8,fpk,syncpk,snrdb,xdt,    &
                  freq,drift,a3,schk,i1SoftSymbols)
             call timer('softsym ',1)

             sync=(syncpk+1)/4.0
             if(nqd.eq.1 .and. ((sync.lt.0.5) .or. (schk.lt.1.0))) cycle
             if(nqd.ne.1 .and. ((sync.lt.1.0) .or. (schk.lt.1.5))) cycle

             call timer('jt9fano ',0)
             call jt9fano(i1SoftSymbols,limit,nlim,msg)
             call timer('jt9fano ',1)

             if(sync.lt.0.0 .or. snrdb.lt.dblim-2.0) sync=0.0
             nsync=int(sync)
             if(nsync.gt.10) nsync=10
             nsnr=nint(snrdb)
             ndrift=nint(drift/df3)
             num9=num9+1

             if(msg.ne.'                      ') then
                numfano=numfano+1
                if (associated(this%callback)) then
                   call this%callback(sync,nsnr,xdt,freq,ndrift,msg)
                end if
                iaa=max(1,i-1)
                ibb=min(NSMAX,i+22)
                fgood=f
                nsynced=1
                ndecoded=1
                ccfok(iaa:ibb)=.false.
                done(iaa:ibb)=.true.
             endif
          endif
       enddo
       if(nagain) exit
    enddo

999 nullify(this%callback)
    end associate
    return
  end subroutine decode
end module jt9_decode
