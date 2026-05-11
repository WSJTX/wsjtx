module astro0_state_mod
  use iso_fortran_env, only: real64
  implicit none
  real(real64) :: uth8z      = 0.0_real64
  real(real64) :: dopplerz   = 0.0_real64
  real(real64) :: doppler00z = 0.0_real64
end module astro0_state_mod


module astro0_mod
  use iso_fortran_env, only: real64
  use astro_mod
  use astro0_state_mod
  use tm2_mod
  implicit none

  real(real64), parameter :: DEGS = 57.2957795130823_real64

contains

  subroutine astro0(nyear,month,nday,uth8,nfreq,mygrid,hisgrid,              &
       AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,ntsky,ndop,ndop00,    &
       dbMoon8,RAMoon8,DecMoon8,HA8,Dgrd8,sd8,poloffset8,xnr8,dfdt,dfdt0,    &
       width1,width2,w501,w502,xlst8)

    implicit none

    ! Dummy arguments
    integer,        intent(in)    :: nyear, month, nday, nfreq
    real(real64),   intent(inout) :: uth8
    character(len=6), intent(in)  :: mygrid, hisgrid
    real(real64),   intent(out)   :: AzSun8, ElSun8, AzMoon8, ElMoon8
    real(real64),   intent(out)   :: AzMoonB8, ElMoonB8
    integer,        intent(out)   :: ntsky, ndop, ndop00
    real(real64),   intent(out)   :: dbMoon8, RAMoon8, DecMoon8, HA8
    real(real64),   intent(out)   :: Dgrd8, sd8, poloffset8, xnr8
    real(real64),   intent(out)   :: dfdt, dfdt0
    real(real64),   intent(out)   :: width1, width2, w501, w502, xlst8

       ! Locals
    real :: uth,day
    real :: doppler00_loc, ndop_loc
    real :: AzSun, ElSun, AzMoon, ElMoon
    real :: AzMoonB, ElMoonB
    integer      :: ntsky_loc
    real :: dbMoon, RAMoon, DecMoon, HA, Dgrd, sd
    real :: poloffset, xnr
    real         :: xlon1, xlat1, xlon2, xlat2
    real         :: xlst
    real :: xl1, b1, xl2, b2
    real :: xl1a, b1a, xl2a, b2a
    real(real64) :: fghz, dldt1, dbdt1, dldt2, dbdt2
    real(real64) :: rate1, rate2
    real(real64) :: fbend, a2, f50
    real(real64) :: dt

    uth = uth8

    ! Station 2 (hisgrid)
    call astro(nyear,month,nday,uth,nfreq,hisgrid,2,1.0,          &
         AzSun,ElSun,AzMoonB,ElMoonB,ntsky_loc,doppler00_loc,ndop_loc,   &
         dbMoon,RAMoon,DecMoon,HA,Dgrd,sd,poloffset,xnr,                 &
         day,xlon2,xlat2,xlst)

    AzMoonB8 = AzMoonB
    ElMoonB8 = ElMoonB

    ! Station 1 (mygrid)
    call astro(nyear,month,nday,uth,nfreq,mygrid,1,1.0,           &
         AzSun,ElSun,AzMoon,ElMoon,ntsky_loc,doppler00_loc,ndop_loc,     &
         dbMoon,RAMoon,DecMoon,HA,Dgrd,sd,poloffset,xnr,                 &
         day,xlon1,xlat1,xlst)
         
    ! Outputs from second call
    AzSun8   = AzSun
    ElSun8   = ElSun
    AzMoon8  = AzMoon
    ElMoon8  = ElMoon
    dbMoon8  = dbMoon
    RAMoon8  = RAMoon/15.0_real64
    DecMoon8 = DecMoon
    HA8      = HA
    Dgrd8    = Dgrd
    sd8      = sd
    poloffset8 = poloffset
    xnr8       = xnr
    ndop       = nint(ndop_loc)
    ndop00     = nint(doppler00_loc)
    ntsky      = ntsky_loc
    xlst8      = xlst

    ! Tracking motion for width1/width2
    call tm2(real(day,8),xlat1,xlon1,xl1,b1)
    call tm2(real(day,8),xlat2,xlon2,xl2,b2)
    call tm2(real(day,8)+1.0/1440.0,xlat1,xlon1,xl1a,b1a)
    call tm2(real(day,8)+1.0/1440.0,xlat2,xlon2,xl2a,b2a)

    fghz  = 0.001_real64*real(nfreq,real64)
    dldt1 = DEGS*(xl1a-xl1)
    dbdt1 = DEGS*(b1a-b1)
    dldt2 = DEGS*(xl2a-xl2)
    dbdt2 = DEGS*(b2a-b2)

    rate1  = 2.0_real64*sqrt(dldt1**2 + dbdt1**2)
    width1 = 0.5_real64*6741.0_real64*fghz*rate1
    rate2  = sqrt((dldt1+dldt2)**2 + (dbdt1+dbdt2)**2)
    width2 = 0.5_real64*6741.0_real64*fghz*rate2

    fbend = 0.7_real64
    a2    = 0.0045_real64*log(fghz/fbend)/log(1.05_real64)
    if (fghz < fbend) a2 = 0.0_real64
    f50 = 0.19_real64 * (fghz/fbend)**a2
    if (f50 > 1.0_real64) f50 = 1.0_real64
    w501 = f50*width1
    w502 = f50*width2

    ! df/dt using persistent state in astro0_state_mod
    if (uth8z == 0.0_real64) then
       uth8z      = uth8 - 1.0_real64/3600.0_real64
       dopplerz   = ndop_loc
       doppler00z = doppler00_loc
    end if

    dt = 60.0_real64*(uth8-uth8z)
    if (dt <= 0.0_real64) dt = 1.0_real64/60.0_real64

    dfdt  = (ndop_loc     - dopplerz  )/dt
    dfdt0 = (doppler00_loc - doppler00z)/dt

    uth8z      = uth8
    dopplerz   = ndop_loc
    doppler00z = doppler00_loc

  end subroutine astro0

end module astro0_mod
