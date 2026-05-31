module astro_mod
  use iso_fortran_env, only: real64, real32, int16
  implicit none

  ! Sky temperature lookup table (was integer*2 nt144 + DATA + SAVE)
  integer(int16), parameter :: nt144(180) = [ &
     234_int16, 246_int16, 257_int16, 267_int16, 275_int16, 280_int16, 283_int16, 286_int16, 291_int16, 298_int16, &
     305_int16, 313_int16, 322_int16, 331_int16, 341_int16, 351_int16, 361_int16, 369_int16, 376_int16, 381_int16, &
     383_int16, 382_int16, 379_int16, 374_int16, 370_int16, 366_int16, 363_int16, 361_int16, 363_int16, 368_int16, &
     376_int16, 388_int16, 401_int16, 415_int16, 428_int16, 440_int16, 453_int16, 467_int16, 487_int16, 512_int16, &
     544_int16, 579_int16, 607_int16, 618_int16, 609_int16, 588_int16, 563_int16, 539_int16, 512_int16, 482_int16, &
     450_int16, 422_int16, 398_int16, 379_int16, 363_int16, 349_int16, 334_int16, 319_int16, 302_int16, 282_int16, &
     262_int16, 242_int16, 226_int16, 213_int16, 205_int16, 200_int16, 198_int16, 197_int16, 196_int16, 197_int16, &
     200_int16, 202_int16, 204_int16, 205_int16, 204_int16, 203_int16, 202_int16, 201_int16, 203_int16, 206_int16, &
     212_int16, 218_int16, 223_int16, 227_int16, 231_int16, 236_int16, 240_int16, 243_int16, 247_int16, 257_int16, &
     276_int16, 301_int16, 324_int16, 339_int16, 346_int16, 344_int16, 339_int16, 331_int16, 323_int16, 316_int16, &
     312_int16, 310_int16, 312_int16, 317_int16, 327_int16, 341_int16, 358_int16, 375_int16, 392_int16, 407_int16, &
     422_int16, 437_int16, 451_int16, 466_int16, 480_int16, 494_int16, 511_int16, 530_int16, 552_int16, 579_int16, &
     612_int16, 653_int16, 702_int16, 768_int16, 863_int16,1008_int16,1232_int16,1557_int16,1966_int16,2385_int16, &
    2719_int16,2924_int16,3018_int16,3038_int16,2986_int16,2836_int16,2570_int16,2213_int16,1823_int16,1461_int16, &
    1163_int16, 939_int16, 783_int16, 677_int16, 602_int16, 543_int16, 494_int16, 452_int16, 419_int16, 392_int16, &
     373_int16, 360_int16, 353_int16, 350_int16, 350_int16, 350_int16, 350_int16, 350_int16, 350_int16, 348_int16, &
     344_int16, 337_int16, 329_int16, 319_int16, 307_int16, 295_int16, 284_int16, 276_int16, 272_int16, 272_int16, &
     273_int16, 274_int16, 274_int16, 271_int16, 266_int16, 260_int16, 252_int16, 245_int16, 238_int16, 231_int16 ]

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
    real(real64) :: xx, yy, poloffset1 = 0.0_real64, poloffset2 = 0.0_real64
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

    freq = real(nfreq,real64)*1.0e6_real64
    if (nfreq == 2) freq = 1.8e6_real64
    if (nfreq == 4) freq = 3.5e6_real64

    call MoonDop(nyear,month,nday,uth,lon,lat,RAMoon,DecMoon,LST,HA,   &
         AzMoon,ElMoon,vr,dist)

    ! Spatial polarization offset
    xx = sin(lat/RAD2DEG)*cos(ElMoon/RAD2DEG) - &
         cos(lat/RAD2DEG)*cos(AzMoon/RAD2DEG)*sin(ElMoon/RAD2DEG)
    yy = cos(lat/RAD2DEG)*sin(AzMoon/RAD2DEG)
    if (NStation == 1) poloffset1 = RAD2DEG*atan2(yy,xx)
    if (NStation == 2) poloffset2 = RAD2DEG*atan2(yy,xx)

    techo   = 2.0_real64 * dist / C_KM_S
    doppler = -freq*vr / C_KM_S

    call coord(0.,0.,-1.570796,1.161639, &
               RAMoon/RAD2DEG,DecMoon/RAD2DEG,el,eb)

    longecl_half = nint(RAD2DEG*el/2.0_real64)
    if (longecl_half < 1 .or. longecl_half > 180) longecl_half = 180
    t144 = nt144(longecl_half)
    tsky = (real(t144,real64)-2.7_real64)*(144.0_real64/freq*1.0e-6_real64)**2.6_real64 + 2.7_real64

    xdop(NStation) = doppler
    if (NStation == 2) then
       HisGrid = MyGrid
    else
       doppler00 = 2.0_real64*xdop(1)
       doppler   = xdop(1) + xdop(2)
       dbMoon    = -40.0_real64*log10(dist/356903.0_real64)
       sd        = 16.23_real64*370152.0_real64/dist

       if (NStation == 1 .and. MoonDX /= 0.0_real64) then
          poloffset = mod(poloffset2-poloffset1+720.0_real64,180.0_real64)
          if (poloffset > 90.0_real64) poloffset = poloffset - 180.0_real64
          x1 = abs(cos(2.0_real64*poloffset/RAD2DEG))
          if (x1 < 0.056234_real64) x1 = 0.056234_real64
          xnr = -20.0_real64*log10(x1)
          if (HisGrid(1:1) < 'A' .or. HisGrid(1:1) > 'R') xnr = 0.0_real64
       end if

       tr      = 80.0_real64
       tskymin = 13.0_real64*(408.0_real64/freq*1.0e-6_real64)**2.6_real64
       tsysmin = tskymin + tr
       tsys    = tsky + tr
       Dgrd    = -10.0_real64*log10(tsys/tsysmin) + dbMoon
    end if

    AzMoon0 = AzMoon
    ElMoon0 = ElMoon
    ntsky   = nint(tsky)

  end subroutine astro

end module astro_mod
