subroutine encode65(message,sent)

  use packjt
  character message*22
  integer dgen(12)
  integer sent(63)

  call packmsg(message,dgen,itype)
  call rs_encode(dgen,sent)
  call interleave63(sent,1)
  call graycode(sent,63,1)

  return
end subroutine encode65
