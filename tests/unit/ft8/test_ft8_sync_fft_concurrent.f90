program test_ft8_sync_fft_concurrent
  use omp_lib, only: omp_get_thread_num, omp_get_num_threads
  implicit none
  integer, parameter :: workers=4
  integer :: failures(workers),thread,phase,actual_workers,transform_size
  integer :: npatience,nthreads
  complex :: unused(1)
  common /patience/ npatience,nthreads

  npatience=1
  nthreads=1
  do phase=1,3
    transform_size=3840
    if(phase.eq.3) then
      npatience=2
      transform_size=128
    endif
    failures=0
    actual_workers=0
    !$omp parallel num_threads(workers) private(thread)
    !$omp single
    actual_workers=omp_get_num_threads()
    !$omp end single
    thread=omp_get_thread_num()+1
    call check_worker(thread,transform_size,failures(thread))
    !$omp end parallel
    if(actual_workers.ne.workers) error stop 'four FFT workers required'
    if(any(failures.ne.0)) error stop 'concurrent borrowed FFT plan failed'
    call four2avar(unused,-1,1,1,1)
  enddo

contains

  subroutine check_worker(worker,nfft,failure)
    integer, intent(in) :: worker,nfft
    integer, intent(out) :: failure
    integer, parameter :: max_fft=3840
    real :: buffer(max_fft+2),reference_buffer(max_fft+2),input(max_fft),twopi,tolerance
    complex :: spectrum(0:max_fft/2),reference(0:max_fft/2)
    integer*8 :: handle,cached_handle
    integer :: i,iteration,tone,peak(1)
    equivalence(buffer,spectrum),(reference_buffer,reference)

    failure=0
    twopi=8.*atan(1.)
    do i=1,nfft
      input(i)=cos(twopi*worker*real(i-1)/nfft)
    enddo
    buffer(1:nfft)=input(1:nfft)
    buffer(nfft+1:)=0.
    call four2avar_get_plan(spectrum,nfft,1,-1,0,handle)
    if(handle.eq.0) then
      failure=1
      return
    endif
    if(any(buffer(1:nfft).ne.input(1:nfft))) then
      failure=5
      return
    endif
    do iteration=1,16
      tone=3*worker+iteration
      do i=1,nfft
        input(i)=cos(twopi*tone*real(i-1)/nfft)
      enddo
      buffer(1:nfft)=input(1:nfft)
      buffer(nfft+1:)=0.
      reference_buffer=buffer
      call four2avar_get_plan(spectrum,nfft,1,-1,0,cached_handle)
      if(cached_handle.ne.handle .or. any(buffer(1:nfft).ne.input(1:nfft))) then
        failure=2
        return
      endif
      call sfftw_execute_dft_r2c(handle,buffer,spectrum)
      call four2avar(reference,nfft,1,-1,0)
      tolerance=5.e-5*max(1.,maxval(abs(reference(0:nfft/2))))
      if(any(abs(spectrum(0:nfft/2)-reference(0:nfft/2)).gt.tolerance)) then
        failure=3
        return
      endif
      peak=maxloc(abs(spectrum(0:nfft/2)))-1
      if(peak(1).ne.tone .or. abs(abs(spectrum(tone))-nfft/2.).gt.tolerance) then
        failure=4
        return
      endif
    enddo
  end subroutine check_worker
end program test_ft8_sync_fft_concurrent
