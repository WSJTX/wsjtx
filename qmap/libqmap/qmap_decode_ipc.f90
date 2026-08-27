module qmap_decode_ipc
  implicit none

  ! Keep row dimensions in sync with qmap/decode_ipc.h.
  integer, parameter :: max_decode_rows = 50
  integer, parameter :: decode_row_length = 80
  integer, parameter :: live_cq_row_length = 8

  integer ndecodes,ncand2,nQDecoderDone,nWDecoderBusy
  integer nWTransmitting,kHzRequested
  character(len=decode_row_length) result(max_decode_rows)
  character(len=live_cq_row_length) result2(max_decode_rows)

  common/decodes/ndecodes,ncand2,nQDecoderDone,nWDecoderBusy,              &
       nWTransmitting,kHzRequested,result
  common/decodes2/result2

end module qmap_decode_ipc
