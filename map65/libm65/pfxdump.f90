subroutine pfxdump(fname)
  character*(*) fname
  include 'pfx.f90'

  open(11,file=fname,status='unknown')
  write(11,1001) sfx
1001 format('Supported Suffixes:'/(11('/',a1,2x)))
  write(11,1002) pfx
1002 format(/'Supported Add-On DXCC Prefixes:'/(15(a5,1x)))
  close(11)

  return
end subroutine pfxdump
