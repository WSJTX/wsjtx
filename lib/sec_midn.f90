real function sec_midn()
  sec_midn=secnds(0.0)
  return
end function sec_midn

subroutine sleep_msec(n)
  use, intrinsic :: iso_fortran_env, only: INT64
  call usleep(int(1000*n, kind=INT64))
  return
end subroutine sleep_msec
