program test_jpl_setup

  implicit none

  character*256 fname,jpleph_file_name
  common/jplcom/jpleph_file_name

  fname=repeat('x',len(fname))
  call jpl_setup(fname)
  if(jpleph_file_name.ne.fname) error stop 1

  fname='short/path'//char(0)
  call jpl_setup(fname)
  if(jpleph_file_name.ne.'short/path') error stop 2

  print*,'JPLEPH path setup test passed'

end program test_jpl_setup
