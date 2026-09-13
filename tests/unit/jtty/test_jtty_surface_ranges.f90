program test_jtty_surface_ranges
  use iso_fortran_env, only: int16
  use fftw3, only: fftwf_cleanup
  use jtty_fec, only: is13,TOTAL_K
  use jtty_mod, only: MAX_FRAMES
  use jtty_mdec, only: npending,pending_updates,discard_pending_updates, &
       jtty_release_fft_resources
  implicit none
  integer, parameter :: ncases=8
  integer, parameter :: rates(ncases)=[240,320,384,240,480,480,384,240]
  integer, parameter :: scenarios(ncases)=[1,1,1,4,2,1,3,1]
  integer :: icase
  complex :: fft_scratch(1)

  ! Direct tests own the frequency-window Cartesian product. Keep integration
  ! cases for every FFT geometry and each distinct surface-search behavior.
  do icase=1,ncases
     if(icase.eq.ncases) then
        ! Prove that a populated geometry can be recreated after teardown.
        call jtty_release_fft_resources()
        call four2a(fft_scratch,-1,1,1,1)
        call fftwf_cleanup()
     endif
     call verify_surface_case(rates(icase),scenarios(icase))
  enddo

  call jtty_release_fft_resources()
  print *, 'JTTY surface range tests passed'

contains

  subroutine verify_surface_case(nsps,icase)
    integer, parameter :: frame_symbols=size(is13)+TOTAL_K
    integer, intent(in) :: nsps,icase
    integer :: nsym,nsamples,nlead,total_samples
    integer :: tones(MAX_FRAMES*frame_symbols),nfa,nfb
    integer(int16), allocatable :: pcm(:)
    real, allocatable :: wave(:)
    complex, allocatable :: cwave(:)
    real :: hz,fc,width
    character(len=80) :: message

    call discard_pending_updates()
    hz=1500.0
    fc=hz
    width=50.0
    nfa=200
    nfb=2800
    select case(icase)
    case(2)
       ! Auxiliary channels follow graph limits away from their nominal bands.
       hz=2700.0
       fc=900.0
       width=3.0
       nfa=2600
       nfb=2750
    case(3)
       ! The QSO channel remains usable with an inverted graph range.
       hz=400.0
       fc=hz
       width=3.0
       nfa=1800
       nfb=1200
    case(4)
       ! Narrow, non-bin-aligned limits still need the smoothing halo.
       hz=1504.0
       fc=hz
       width=0.5
       nfa=1504
       nfb=1505
    end select

    message='N2PPI'
    call genjtty(message,tones,nsym)
    nsamples=nsym*nsps
    nlead=1800
    ! One 1.25-frame chunk contains the lead-in and complete signal. A
    ! longer buffer only schedules empty trailing decoder windows.
    total_samples=frame_symbols*nsps+(frame_symbols*nsps)/4
    if(nlead+nsamples.gt.total_samples) &
         error stop 'surface fixture exceeds one decoder chunk'
    allocate(pcm(total_samples),wave(nsamples),cwave(nsamples))
    call gen_jttywave(tones,nsym,nsps,2.0,12000.0,hz,cwave,wave,0,nsamples)
    pcm=0_int16
    pcm(nlead+1:nlead+nsamples)=int(nint(14000.0*wave),int16)
    call rjtty_sub(pcm,1,nsps,nfa,nfb,fc,width)
    call rjtty_sub(pcm,total_samples,nsps,nfa,nfb,fc,width)
    if(npending.ne.1) then
       print *, 'Unexpected message count',nsps,icase,npending
       stop 1
    endif
    if(trim(pending_updates(1)%decoded).ne.'N2PPI' .or. &
         .not.pending_updates(1)%complete) then
       print *, 'Unexpected message',nsps,icase,pending_updates(1)%decoded
       stop 1
    endif
  end subroutine verify_surface_case
end program
