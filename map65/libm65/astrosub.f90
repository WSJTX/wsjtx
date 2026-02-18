subroutine astrosub(nyear,month,nday,uth8,nfreq,mygrid,hisgrid,          &
     AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,ntsky,ndop,ndop00,  &
     RAMoon8,DecMoon8,Dgrd8,poloffset8,xnr8)

  implicit real*8 (a-h,o-z)
  character*6 mygrid,hisgrid

  call astro0(nyear,month,nday,uth8,nfreq,mygrid,hisgrid,                &
     AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,ntsky,ndop,ndop00,  &
     dbMoon8,RAMoon8,DecMoon8,HA8,Dgrd8,sd8,poloffset8,xnr8,dfdt,dfdt0,  &
     width1,width2,w501,w502,xlst8)

  return  
end subroutine astrosub

subroutine astrosub00(nyear,month,nday,uth8,nfreq,mygrid,ndop00, mygrid_len) bind(C, name='astrosub00_')
  use iso_c_binding
  implicit none
  integer(c_int), intent(inout) :: nyear, month, nday, nfreq, ndop00
  real(c_double), intent(inout) :: uth8
  character(kind=c_char), intent(in) :: mygrid(*)
  integer(c_int), value :: mygrid_len  ! Explicit length

  ! Explicitly declare all variables passed to astrosub
  real(c_double) :: AzSun8, ElSun8, AzMoon8, ElMoon8, AzMoonB8, ElMoonB8
  real(c_double) :: RAMoon8, DecMoon8, Dgrd8, poloffset8, xnr8
  integer(c_int) :: ntsky, ndop

  character(kind=c_char,len=6) :: mygrid6

  ! Copy and pad mygrid to mygrid6
  mygrid6 = '      '
  call strncpy(mygrid6, mygrid, min(mygrid_len, 6_c_int))

  call astrosub(nyear,month,nday,uth8,nfreq,mygrid6,mygrid6,             &
     AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,ntsky,ndop,ndop00,  &
     RAMoon8,DecMoon8,Dgrd8,poloffset8,xnr8)

  return
  
contains

  subroutine strncpy(dest, src, n)
    character(kind=c_char), intent(out) :: dest(*)
    character(kind=c_char), intent(in)  :: src(*)
    integer(c_int), value :: n
    integer(c_int) :: i

    do i = 1, n
      if (src(i) == c_null_char) exit
      dest(i) = src(i)
    end do
  end subroutine strncpy
  
end subroutine astrosub00
