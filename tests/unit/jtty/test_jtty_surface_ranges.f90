program test_jtty_surface_ranges
  use iso_fortran_env, only: int16
  use fftw3, only: fftwf_cleanup
  use jtty_fec, only: is13,TOTAL_K
  use jtty_mod, only: MAX_FRAMES
  use jtty_mdec, only: npending,pending_updates,discard_pending_updates, &
       jtty_release_fft_resources
  implicit none
  integer, parameter :: rates(5)=[240,320,384,480,240]
  integer, parameter :: FULL_SURFACE_CASE=2,NARROW_HALO_CASE=4
  integer :: irate,icase
  complex :: fft_scratch(1)

  ! Change both the rate and the searched bins between successive receptions.
  do irate=1,size(rates)
     do icase=1,4
        call verify_surface_case(rates(irate),icase)
     enddo
  enddo

  call jtty_release_fft_resources()
  call four2a(fft_scratch,-1,1,1,1)
  call fftwf_cleanup()

  call verify_surface_case(480,FULL_SURFACE_CASE)
  call verify_surface_case(480,NARROW_HALO_CASE)
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
    total_samples=nlead+nsamples+frame_symbols*nsps
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
