program test_wspr_packing
  implicit none
  character(len=22) :: msg, sent
  integer(kind=1) :: data(11)
  integer :: ntype, tones(162), i
  character(len=22), parameter :: invalid(*) = [character(len=22) :: &
       'PJ4/ABC 37', 'ABC/P 37', '<K1ABC> ZZ 37', 'K1ABC FN20 X', &
       'K1ABC/P X', '<K1ABC> FN20AB X', 'K1ABC FN20 /', &
       'K1ABC FN20', '<K1ABC FN20AB 37', 'ABC', &
       '<K1ABC> ZZ00ZZ 37', '<K1ABC> FN20YZ 37', &
       'K1ABC/AB 37', 'K1ABC/! 37', 'K1ABC/09 37', &
       'K1ABC FN2037', 'K1ABC RZ73 37', 'K1ABC ZZ00 37', &
       '<...> FN20AB 37', 'K1ABC FN20 37 X']
  character(len=22), parameter :: valid(*) = [character(len=22) :: &
       'K1ABC FN20 37', 'PJ4/K1ABC 37', 'K1ABC/P 37', '<K1ABC> FN20AB 37', &
       'K1ABC RR73 37', 'K1ABC FN20 -5', 'K1ABC FN20 99', '<K1ABC> FN20 37', &
       '3DA0AB FN20 37', '3XA1AB FN20 37', 'NYN/K1ABC 37', 'K1ABC/10 0']
  integer, parameter :: types(*) = [37, 39, 39, -38, 37, 0, 60, -38, 37, 37, 39, 2]
  integer, parameter :: expected(7,12) = reshape([ &
       247,12,35,139,57,217,64, 247,12,35,129,14,153,192, &
       247,12,35,141,79,57,192, 155,204,254,67,47,38,128, &
       247,12,35,128,66,185,64, 247,12,35,139,57,208,0, &
       247,12,35,139,57,223,0, 155,209,201,19,47,38,128, &
       22,182,146,59,57,217,64, 177,152,23,75,57,217,64, &
       247,12,35,128,0,25,192, 247,12,35,141,80,144,128], [7,12])

  do i=1,size(invalid)
     msg=invalid(i)
     data=42
     ntype=123
     call wqencode(msg,ntype,data)
     if(ntype.ne.-999 .or. any(data.ne.0)) then
        print *, 'accepted invalid message: ', trim(msg), ntype
        stop 1
     endif
     call genwspr(msg,sent,tones)
     if(sent.ne.'*** bad message ***' .or. any(tones.ne.-1)) then
        print *, 'generated invalid message: ', trim(msg)
        stop 1
     endif
  enddo
  do i=1,size(valid)
     msg=valid(i)
     call wqencode(msg,ntype,data)
     if(ntype.ne.types(i) .or. any(iand(int(data(1:7)),255).ne.expected(:,i)) &
        .or. any(data(8:).ne.0)) then
        print *, 'unexpected encoding: ', trim(msg), ntype, iand(int(data(1:7)),255)
        stop 1
     endif
     call genwspr(msg,sent,tones)
     if(sent.ne.msg .or. any(tones.lt.0) .or. any(tones.gt.3)) stop 1
  enddo
end program test_wspr_packing
