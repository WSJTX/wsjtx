module astro_mod
  use iso_fortran_env, only: real64, real32, int16
  implicit none

  ! Sky temperature lookup table (was integer*2 nt144 + DATA + SAVE)
  integer(int16), parameter :: nt144(180) = [ &
      234, 246, 257, 267, 275, 280, 283, 286, 291, 298, &
      305, 313, 322, 331, 341, 351, 361, 369, 376, 381, &
      383, 382, 379, 374, 370, 366, 363, 361, 363, 368, &
      376, 388, 401, 415, 428, 440, 453, 467, 487, 512, &
      544, 579, 607, 618, 609, 588, 563, 539, 512, 482, &
      450, 422, 398, 379, 363, 349, 334, 319, 302, 282, &
      262, 242, 226, 213, 205, 200, 198, 197, 196, 197, &
      200, 202, 204, 205, 204, 203, 202, 201, 203, 206, &
      212, 218, 223, 227, 231, 236, 240, 243, 247, 257, &
      276, 301, 324, 339, 346, 344, 339, 331, 323, 316, &
      312, 310, 312, 317, 327, 341, 358, 375, 392, 407, &
      422, 437, 451, 466, 480, 494, 511, 530, 552, 579, &
      612, 653, 702, 768, 863,1008,1232,1557,1966,2385, &
     2719,2924,3018,3038,2986,2836,2570,2213,1823,1461, &
     1163, 939, 783, 677, 602, 543, 494, 452, 419, 392, &
      373, 360, 353, 350, 350, 350, 350, 350, 350, 348, &
      344, 337, 329, 319, 307, 295, 284, 276, 272, 272, &
      273, 274, 274, 271, 266, 260, 252, 245, 238, 231 ]

  real(real32), parameter :: RAD2DEG = 57.2957795130823
  real(real32), parameter :: C_KM_S  = 2.99792458e5

contains

  subroutine astro(nyear,month,nday,uth,nfreq,Mygrid,NStation,MoonDX,     &
       AzSun,ElSun,AzMoon0,ElMoon0,ntsky,doppler00,doppler,dbMoon,RAMoon, &
       DecMoon,HA,Dgrd,sd,poloffset,xnr,day,lon,lat,LST)

    use coord_mod
    use sun_mod
    use moondop_mod
    use grid2deg_mod, only: grid2deg
    implicit none

    ! Dummy arguments
    integer,      intent(in)    :: nyear, month, nday, nfreq, NStation
    real,         intent(in)    :: uth
    real,         intent(in)    :: MoonDX
    character(len=6), intent(in):: MyGrid
    integer,      intent(out)   :: ntsky
    real, intent(out) :: AzSun, ElSun, AzMoon0, ElMoon0
    real, intent(out) :: doppler00, doppler, dbMoon
    real, intent(out) :: RAMoon, DecMoon, HA, Dgrd, sd
    real, intent(out) :: poloffset, xnr
    real, intent(out) :: day
    real, intent(out) :: lon, lat, LST

    ! Locals
    character(len=6) :: HisGrid = 'UNK   '
    real(real64)     :: freq
    real     :: RASun, DecSun
    real     :: AzMoon, ElMoon, vr, dist
    real(real64) :: xx, yy, poloffset1 = 0.0, poloffset2 = 0.0
    real(real64)     :: techo
    real(real32) :: el, eb
    integer          :: longecl_half, t144
    real(real64)     :: tsky, x1, tr, tskymin, tsysmin, tsys
    real     :: elon
    real(real64)     :: xdop(2)
    integer          :: mjd

    ! Initialize intent(out) variables
    ntsky = 0
    AzSun = 0.0
    ElSun = 0.0
    AzMoon0 = 0.0
    ElMoon0 = 0.0
    doppler00 = 0.0
    doppler = 0.0
    dbMoon = 0.0
    RAMoon = 0.0
    DecMoon = 0.0
    HA = 0.0
    Dgrd = 0.0
    sd = 0.0
    poloffset = 0.0
    xnr = 0.0
    day = 0.0
    lon = 0.0
    lat = 0.0
    LST = 0.0

    ! Convert grid to lon/lat
    call grid2deg(MyGrid, elon, lat)
    lon = -elon

    call sun(nyear,month,nday,uth,lon,lat,RASun,DecSun,LST,AzSun,ElSun,mjd,day)
    freq = nfreq*1.0d6
    call MoonDop(nyear,month,nday,uth,lon,lat,RAMoon,DecMoon,LST,HA,   &
         AzMoon,ElMoon,vr,dist)

    ! Spatial polarization offset
    xx = sin(lat/RAD2DEG)*cos(ElMoon/RAD2DEG) - &
         cos(lat/RAD2DEG)*cos(AzMoon/RAD2DEG)*sin(ElMoon/RAD2DEG)
    yy = cos(lat/RAD2DEG)*sin(AzMoon/RAD2DEG)
    if (NStation == 1) poloffset1 = RAD2DEG*atan2(yy,xx)
    if (NStation == 2) poloffset2 = RAD2DEG*atan2(yy,xx)

    techo   = 2.0 * dist / C_KM_S
    doppler = -freq*vr / C_KM_S

    call coord(0.,0.,-1.570796,1.161639, &
               RAMoon/RAD2DEG,DecMoon/RAD2DEG,el,eb)

    longecl_half = nint(RAD2DEG*el/2.0)
    if (longecl_half < 1 .or. longecl_half > 180) longecl_half = 180
    t144 = nt144(longecl_half)
    tsky = (t144-2.7)*(144.0e6/freq)**2.6 + 2.7

    xdop(NStation) = doppler
    if (NStation == 2) then
       HisGrid = MyGrid
    else
       doppler00 = 2.0*xdop(1)
       doppler   = xdop(1) + xdop(2)
       dbMoon    = -40.0*log10(dist/356903.0)
       sd        = 16.23*370152.0/dist

       if (NStation == 1 .and. MoonDX /= 0.0) then
          poloffset = mod(poloffset2-poloffset1+720.d0,180.d0)
          if (poloffset > 90.0) poloffset = poloffset - 180.0
          x1 = abs(cos(2.0*poloffset/RAD2DEG))
          if (x1 < 0.056234) x1 = 0.056234
          xnr = -20.0*log10(x1)
          if (HisGrid(1:1) < 'A' .or. HisGrid(1:1) > 'R') xnr = 0.0
       end if

       tr      = 80.0
       tskymin = 13.0*(408.0e6/freq)**2.6
       tsysmin = tskymin + tr
       tsys    = tsky + tr
       Dgrd    = -10.0*log10(tsys/tsysmin) + dbMoon
    end if

    AzMoon0 = AzMoon
    ElMoon0 = ElMoon
    ntsky   = nint(tsky)

  end subroutine astro

end module astro_mod
