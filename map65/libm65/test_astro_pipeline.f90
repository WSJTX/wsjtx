program test_astro_pipeline
  use iso_fortran_env, only: real64
  use astrosub_mod
  implicit none

  integer        :: nyear, month, nday, nfreq, ndop00
  real(real64)   :: uth
  character(len=6) :: mygrid

  ! Simple test case
  nyear  = 2025
  month  = 1
  nday   = 15
  uth    = 12.0_real64      ! 12:00 UTC
  nfreq  = 1440000000/1000000  ! e.g. 1440 MHz → 1440
  mygrid = 'FN20rx'         ! or whatever grid you like
  ndop00 = 0

  call astrosub00(nyear,month,nday,uth,nfreq,mygrid,ndop00,len(mygrid))

  print *, 'Test astro pipeline:'
  print *, '  nyear  =', nyear
  print *, '  month  =', month
  print *, '  nday   =', nday
  print *, '  uth    =', uth
  print *, '  nfreq  =', nfreq
  print *, '  mygrid =', mygrid
  print *, '  ndop00 =', ndop00

end program test_astro_pipeline

!does not require end module
