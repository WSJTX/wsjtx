module astro_module
  use, intrinsic :: iso_c_binding, only : c_int, c_double, c_bool, c_char, c_ptr, c_size_t, c_f_pointer
  implicit none

  private
  public :: astrosub

contains

  subroutine astrosub(nyear,month,nday,uth8,freq8,mygrid_cp,mygrid_len,         &
       hisgrid_cp,hisgrid_len,AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,  &
       ntsky,ndop,ndop00,RAMoon8,DecMoon8,Dgrd8,poloffset8,xnr8,techo8,width1,  &
       width2,bTx,AzElFileName_cp,AzElFileName_len,jpleph_cp,jpleph_len)        &
       bind (C, name="astrosub")

    integer, parameter :: dp = selected_real_kind(15, 50)

    integer(c_int), intent(in), value :: nyear, month, nday
    real(c_double), intent(in), value :: uth8, freq8
    real(c_double), intent(out) :: AzSun8, ElSun8, AzMoon8, ElMoon8, AzMoonB8,  &
         ElMoonB8, Ramoon8, DecMoon8, Dgrd8, poloffset8, xnr8, techo8, width1,  &
         width2
    integer(c_int), intent(out) :: ntsky, ndop, ndop00
    logical(c_bool), intent(in), value :: bTx
    type(c_ptr), intent(in), value :: mygrid_cp, hisgrid_cp, AzElFileName_cp, jpleph_cp
    integer(c_size_t), intent(in), value :: mygrid_len, hisgrid_len, AzElFileName_len, jpleph_len

    character(len=6) :: mygrid, hisgrid
    character(kind=c_char, len=:), allocatable :: AzElFileName
    character(len=1) :: c1
    integer :: ih, im, imin, is, isec, nfreq, nRx
    real(dp) :: AzAux, ElAux, dbMoon8, dfdt, dfdt0, doppler, doppler00, HA8, sd8, xlst8
    character*256 jpleph_file_name
    common/jplcom/jpleph_file_name

    block
      character(kind=c_char, len=mygrid_len), pointer :: mygrid_fp
      character(kind=c_char, len=hisgrid_len), pointer :: hisgrid_fp
      character(kind=c_char, len=AzElFileName_len), pointer :: AzElFileName_fp
      character(kind=c_char, len=jpleph_len), pointer :: jpleph_fp
      call c_f_pointer(cptr=mygrid_cp, fptr=mygrid_fp)
      mygrid = mygrid_fp
      mygrid_fp => null()
      call c_f_pointer(cptr=hisgrid_cp, fptr=hisgrid_fp)
      hisgrid = hisgrid_fp
      hisgrid_fp => null()
      call c_f_pointer(cptr=AzElFileName_cp, fptr=AzElFileName_fp)
      AzElFileName = AzElFileName_fp
      AzElFileName_fp => null()
      call c_f_pointer(cptr=jpleph_cp, fptr=jpleph_fp)
      jpleph_file_name = jpleph_fp
      jpleph_fp => null()
    end block

    call astro0(nyear,month,nday,uth8,freq8,mygrid,hisgrid,                &
         AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,ntsky,ndop,ndop00,  &
         dbMoon8,RAMoon8,DecMoon8,HA8,Dgrd8,sd8,poloffset8,xnr8,dfdt,dfdt0,  &
         width1,width2,xlst8,techo8)

    if (len_trim(AzElFileName) .eq. 0) go to 999
    imin=60*uth8
    isec=3600*uth8
    ih=uth8
    im=mod(imin,60)
    is=mod(isec,60)
    open(15,file=AzElFileName,status='unknown',err=900)
    c1='R'
    nRx=1
    if(bTx) then
       c1='T'
       nRx=0
    endif
    AzAux=0.
    ElAux=0.
    nfreq=freq8/1000000
    doppler=ndop
    doppler00=ndop00
    write(15,1010,err=10) ih,im,is,AzMoon8,ElMoon8,                     &
         ih,im,is,AzSun8,ElSun8,                                        &
         ih,im,is,AzAux,ElAux,                                          &
         nfreq,doppler,dfdt,doppler00,dfdt0,c1
    !       TXFirst,TRPeriod,poloffset,Dgrd,xnr,ave,rms,nRx
1010 format(                                                          &
         i2.2,':',i2.2,':',i2.2,',',f5.1,',',f5.1,',Moon'/               &
         i2.2,':',i2.2,':',i2.2,',',f5.1,',',f5.1,',Sun'/                &
         i2.2,':',i2.2,':',i2.2,',',f5.1,',',f5.1,',Source'/             &
         i5,',',f8.1,',',f8.2,',',f8.1,',',f8.2,',Doppler, ',a1)
    !      i1,',',i3,',',f8.1,','f8.1,',',f8.1,',',f12.3,',',f12.3,',',i1,',RPol')
10  close(15)
    go to 999

900 print*,'Error opening azel.dat'

999 return
  end subroutine astrosub

end module astro_module
