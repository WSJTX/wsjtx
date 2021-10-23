module astro_module
  implicit none

  private
  public :: astrosub

  logical :: initialized = .false.
  integer :: azel_extra_lines = 0

contains

  subroutine astrosub(nyear,month,nday,uth8,freq8,mygrid_cp,                    &
       hisgrid_cp,AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,              &
       ntsky,ndop,ndop00,RAMoon8,DecMoon8,Dgrd8,poloffset8,xnr8,techo8,width1,  &
       width2,bTx,AzElFileName_cp,jpleph_file_name_cp)                          &
       bind (C, name="astrosub")

    use :: types, only: dp
    use :: C_interface_module, only: C_int, C_double, C_bool, C_ptr, C_string_value, assignment(=)

    integer(C_int), intent(in), value :: nyear, month, nday
    real(C_double), intent(in), value :: uth8, freq8
    real(C_double), intent(out) :: AzSun8, ElSun8, AzMoon8, ElMoon8, AzMoonB8,  &
         ElMoonB8, Ramoon8, DecMoon8, Dgrd8, poloffset8, xnr8, techo8, width1,  &
         width2
    integer(C_int), intent(out) :: ntsky, ndop, ndop00
    logical(C_bool), intent(in), value :: bTx
    type(C_ptr), value, intent(in) :: mygrid_cp, hisgrid_cp, AzElFileName_cp,   &
         jpleph_file_name_cp

    character(len=6) :: mygrid, hisgrid
    character(len=:), allocatable :: AzElFileName
    character(len=1) :: c1
    character(len=32) :: envvar
    integer :: ih, im, imin, is, isec, nfreq, env_status
    real(dp) :: AzAux, ElAux, dbMoon8, dfdt, dfdt0, doppler, doppler00, HA8, sd8, xlst8
    character*256 jpleph_file_name
    common/jplcom/jpleph_file_name

    if (.not.initialized) then
       call get_environment_variable ('WSJT_AZEL_EXTRA_LINES', envvar, status=env_status)
       if (env_status.eq.0) read (envvar, *, iostat=env_status) azel_extra_lines
       initialized = .true.
    end if

    mygrid = mygrid_cp
    hisgrid = hisgrid_cp
    AzElFileName = C_string_value (AzElFileName_cp)
    jpleph_file_name = jpleph_file_name_cp

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
    if(bTx) then
       c1='T'
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
    if (azel_extra_lines.ge.1) write(15, 1020, err=10) poloffset8,xnr8,Dgrd8
1010 format(                                                          &
         i2.2,':',i2.2,':',i2.2,',',f5.1,',',f5.1,',Moon'/               &
         i2.2,':',i2.2,':',i2.2,',',f5.1,',',f5.1,',Sun'/                &
         i2.2,':',i2.2,':',i2.2,',',f5.1,',',f5.1,',Source'/             &
         i5,',',f8.1,',',f8.2,',',f8.1,',',f8.2,',Doppler, ',a1)
1020 format(f8.1,','f8.1,',',f8.1,',Pol')
10  close(15)
    go to 999

900 print*,'Error opening azel.dat'

999 return
  end subroutine astrosub

end module astro_module
