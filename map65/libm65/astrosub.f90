module astrosub_mod
  use iso_fortran_env, only: real64
  use iso_c_binding, only: c_int, c_double, c_char
  use astro0_mod
  implicit none

contains

  ! Pure Fortran interface (internal)
  subroutine astrosub(nyear,month,nday,uth8,nfreq,mygrid,hisgrid,          &
       AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,ntsky,ndop,ndop00,  &
       RAMoon8,DecMoon8,Dgrd8,poloffset8,xnr8)
    implicit none
    integer,        intent(in)  :: nyear, month, nday, nfreq
    real(real64),   intent(inout)  :: uth8
    character(len=6), intent(in):: mygrid, hisgrid
    real(real64),   intent(out) :: AzSun8, ElSun8, AzMoon8, ElMoon8
    real(real64),   intent(out) :: AzMoonB8, ElMoonB8
    integer,        intent(out) :: ntsky, ndop, ndop00
    real(real64),   intent(out) :: RAMoon8, DecMoon8, Dgrd8
    real(real64),   intent(out) :: poloffset8, xnr8

    ! Locals for astro0 extras we don’t export
    real(real64) :: dbMoon8, HA8, sd8, dfdt, dfdt0
    real(real64) :: width1, width2, w501, w502, xlst8

    call astro0(nyear,month,nday,uth8,nfreq,mygrid,hisgrid,                &
       AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,ntsky,ndop,ndop00,  &
       dbMoon8,RAMoon8,DecMoon8,HA8,Dgrd8,sd8,poloffset8,xnr8,dfdt,dfdt0,  &
       width1,width2,w501,w502,xlst8)
  end subroutine astrosub

  subroutine astrosub_legacy(nyear,month,nday,uth8,nfreq,                    &
                            mygrid,hisgrid,                                 &
                            azsun,elsun,azmoon,elmoon,                      &
                            azmoondx,elmoondx,                              &
                            ntsky,ndop,ndop00,                              &
                            ramoon,decmoon,dgrd,poloffset,xnr,              &
                            len1,len2)                                      &
      bind(C, name='astrosub_')

    use iso_c_binding
    implicit none

    ! ===== C ABI dummy arguments (must match C++ exactly) =====
    integer(c_int),    intent(inout) :: nyear, month, nday, nfreq
    real(c_double),    intent(inout) :: uth8
    character(c_char), intent(in)    :: mygrid(*), hisgrid(*)
    real(c_double),    intent(out)   :: azsun, elsun, azmoon, elmoon
    real(c_double),    intent(out)   :: azmoondx, elmoondx
    integer(c_int),    intent(out)   :: ntsky, ndop, ndop00
    real(c_double),    intent(out)   :: ramoon, decmoon, dgrd
    real(c_double),    intent(out)   :: poloffset, xnr
    integer(c_int),    value         :: len1, len2

    ! ===== Local CHARACTER*6 buffers =====
    character(len=6) :: g1, g2

    ! ===== Local Fortran outputs =====
    real(real64) :: AzSun8, ElSun8, AzMoon8, ElMoon8
    real(real64) :: AzMoonB8, ElMoonB8
    integer      :: ntsky_loc, ndop_loc, ndop00_loc
    real(real64) :: RAMoon8, DecMoon8, Dgrd8, poloffset8, xnr8
    
    ! ===== Convert C strings to CHARACTER*6 =====
    call copy_c_to_f6(g1, mygrid, len1)
    call copy_c_to_f6(g2, hisgrid, len2)

    ! ===== Call modern Fortran routine =====
    call astrosub(nyear,month,nday,uth8,nfreq,g1,g2,                         &
        AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,                    &
        ntsky_loc,ndop_loc,ndop00_loc,                                      &
        RAMoon8,DecMoon8,Dgrd8,poloffset8,xnr8)
    ! ===== Copy results back to C =====
    azsun    = AzSun8
    elsun    = ElSun8
    azmoon   = AzMoon8
    elmoon   = ElMoon8
    azmoondx = AzMoonB8
    elmoondx = ElMoonB8

    ntsky    = ntsky_loc
    ndop     = ndop_loc
    ndop00   = ndop00_loc

    ramoon   = RAMoon8
    decmoon  = DecMoon8
    dgrd     = Dgrd8
    poloffset= poloffset8
    xnr      = xnr8
  end subroutine astrosub_legacy

  ! C-callable wrapper: astrosub00_
  subroutine astrosub00(nyear,month,nday,uth8,nfreq,mygrid,ndop00, mygrid_len) &
       bind(C, name='astrosub00_')
    use iso_c_binding
    implicit none
    ! C ABI arguments
    integer(c_int),    intent(inout) :: nyear, month, nday, nfreq, ndop00
    real(c_double),    intent(inout) :: uth8
    character(c_char), intent(in)    :: mygrid(*)
    integer(c_int),    value         :: mygrid_len

    ! Locals
    character(len=6) :: mygrid6, hisgrid6
    integer          :: ntsky, ndop
    real(real64)     :: AzSun8, ElSun8, AzMoon8, ElMoon8
    real(real64)     :: AzMoonB8, ElMoonB8
    real(real64)     :: RAMoon8, DecMoon8, Dgrd8
    real(real64)     :: poloffset8, xnr8

    ! C string → Fortran CHARACTER*6
    call copy_c_to_f6(mygrid6, mygrid, mygrid_len)
    hisgrid6 = mygrid6   ! same grid for now

    call astrosub(nyear,month,nday,uth8,nfreq,mygrid6,hisgrid6,            &
       AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,ntsky,ndop,ndop00,  &
       RAMoon8,DecMoon8,Dgrd8,poloffset8,xnr8)
       
  end subroutine astrosub00

  subroutine copy_c_to_f6(dest, src, n)
    use iso_c_binding, only: c_char, c_int, c_double, c_null_char

    character(len=6),   intent(out) :: dest
    character(c_char),  intent(in)  :: src(*)
    integer(c_int),     intent(in)  :: n
    integer :: i

    dest = '      '
    do i = 1, min(6, n)
      if (src(i) == c_null_char) exit
      dest(i:i) = src(i)
    end do
  end subroutine copy_c_to_f6

end module astrosub_mod

