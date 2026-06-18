module pfxdump_mod
  implicit none
contains
  subroutine pfxdump(fname)
    use pfx_data_mod
    implicit none
    character*(*), intent(in) :: fname
  
    open(11,file=fname,status='unknown')
    write(11,1001) sfx
  1001 format('Supported Suffixes:'/(11('/',a1,2x)))
    write(11,1002) pfx
  1002 format(/'Supported Add-On DXCC Prefixes:'/(15(a5,1x)))
    close(11)
  
  end subroutine pfxdump
end module pfxdump_mod
