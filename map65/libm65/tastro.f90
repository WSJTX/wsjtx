program tastro

  implicit none
  use astro0_mod

  character(6)  :: grid
  character(9)  :: cauxra, cauxdec
  character(12) :: clock(3)

  integer :: nt(8)
  integer :: nyear, month, nday, ntz
  integer :: ihour, imin, isec, ims
  integer :: ih, nfreq

  real(8) :: uth8
  real(8) :: AzSun8, ElSun8, AzMoon8, ElMoon8
  real(8) :: AzMoonB8, ElMoonB8, dbMoon8
  real(8) :: RAMoon8, DecMoon8, HA8, Dgrd8, sd8
  real(8) :: poloffset8, xnr8, dfdt, dfdt0
  real(8) :: RaAux8, DecAux8, AzAux8, ElAux8
  real(8) :: width1, width2, w501, w502, xlst8
  integer :: ntsky, ndop, ndop00

  grid   = 'FN20qi'
  nfreq  = 144
  cauxra = '00:00:00'

  ! Main loop: update astronomical display once per second
do
   call date_and_time(clock(1), clock(2), clock(3), nt)

   nyear = nt(1)
   month = nt(2)
   nday  = nt(3)
   ntz   = nt(4)
   ihour = nt(5)
   imin  = nt(6)
   isec  = nt(7)
   ims   = nt(8)

   ih = ihour - ntz/60
   if (ih <= 0) then
      ih = ih + 24
      nday = nday + 1
   endif

   uth8 = ih + imin/60.d0 + isec/3600.d0 + ims/3600000.d0

   call astro0(nyear,month,nday,uth8,nfreq,grid,cauxra,cauxdec,       &
      AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,ntsky,ndop,ndop00,  &
      dbMoon8,RAMoon8,DecMoon8,HA8,Dgrd8,sd8,poloffset8,xnr8,dfdt,dfdt0,  &
      RaAux8,DecAux8,AzAux8,ElAux8,width1,width2,w501,w502,xlst8)

   write(*,1010) nyear,month,nday,ih,imin,isec,AzMoon8,ElMoon8,          &
        AzSun8,ElSun8,ndop,dgrd8,ntsky

   call system('sleep 1')
end do

end program tastro

!does not require end module
