! Keep the production reference-spectrum implementation in this test.  This
! bridge only supplies its flags and the compiler-specific CHARACTER argument.
subroutine receive_reference_probe(id2,ninput) bind(C)
  use iso_c_binding, only: c_int16_t,c_int
  implicit none
  integer(c_int16_t) :: id2(*)
  integer(c_int), value :: ninput
  logical*1 :: clear, measure, apply

  clear=.false.
  measure=.true.
  apply=.false.
  ! One measurement per process: refspectrum writes a file every fourth call.
  call refspectrum(id2,ninput,clear,measure,apply,'unused-receive-reference.dat')
end subroutine

! Exercise the production filter-application path.  Build a deterministic,
! non-identity reference file in the test process so the write footprint is
! observable without relying on a user's writable-data directory.
subroutine receive_reference_apply_probe(id2,ninput) bind(C)
  use iso_c_binding, only: c_int16_t,c_int
  implicit none
  integer(c_int16_t) :: id2(*)
  integer(c_int), value :: ninput
  logical*1 :: clear, measure, apply
  integer :: i
  real :: frequency
  character(len=*), parameter :: path='receive-reference-apply.dat'

  open(16,file=path,status='replace')
  write(16,'(3i5,5e25.16)') 400,2600,5,0.d0,0.d0,0.d0,0.d0,0.d0
  do i=1,3456
     frequency=12000.0*i/6912.0
     write(16,'(f10.3,e12.3,f12.6,e12.3,f12.6)') &
          frequency,1.0,0.0,0.5,20.0*log10(0.5)
  enddo
  close(16)

  clear=.false.
  measure=.false.
  apply=.true.
  call refspectrum(id2,ninput,clear,measure,apply,path)
end subroutine
