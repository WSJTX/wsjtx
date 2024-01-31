subroutine chkstat(dd,nhsym,pdb)

  real dd(2,5760000)
  real pdb(4)
  integer ia(4),ib(4)
  logical*1 btx0,btx1

  btx0=.false.
  btx1=.false.
  ia(1)=23*96000+1
  ib(1)=24*96000
  ia(2)=27*96000+1
  ib(2)=28*96000
  ia(3)=53*96000+1
  ib(3)=54*96000
  ia(4)=57*96000+1
  ib(4)=58*96000

  do j=1,4
     sq=0.
     do i=ia(j),ib(j)
        sq=sq + dd(1,i)*dd(1,i) + dd(2,i)*dd(2,i)
     enddo
     pdb(j)=db(1.0 + sq/(2.0*96000.0))
  enddo
  
  return
end subroutine chkstat
