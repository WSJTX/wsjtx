subroutine chkstat(dd,nhsym,pdb)

  use qmap_params, only: nrate_active
  real dd(2,*)
  real pdb(4)
  integer ia(4),ib(4)
  logical*1 btx0,btx1

  btx0=.false.
  btx1=.false.
  ia(1)=23*nrate_active+1
  ib(1)=24*nrate_active
  ia(2)=27*nrate_active+1
  ib(2)=28*nrate_active
  ia(3)=53*nrate_active+1
  ib(3)=54*nrate_active
  ia(4)=57*nrate_active+1
  ib(4)=58*nrate_active

  do j=1,4
     sq=0.
     do i=ia(j),ib(j)
        sq=sq + dd(1,i)*dd(1,i) + dd(2,i)*dd(2,i)
     enddo
     pdb(j)=db(1.0 + sq/(2.0*nrate_active))
  enddo
  
  return
end subroutine chkstat
