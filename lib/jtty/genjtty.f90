subroutine genjtty(umsg,itone,nsym)

! Input:  character*80 umsg               !User message
! Output: integer*4 itone(1:nsym)         !Tones for channel symbols
!         integer*4 nsym                  !Number of channel symbols

  use jtty_mod
  use jtty_fec
  parameter (MAX_TONES=59*16)       !Max number of channel symbols
  character*80 umsg                 !User-formatted message
  character*34 c32(16)
  integer itone(MAX_TONES)          !Array of tone frequencies for this message
  integer payload(PAYLOAD_BITS)
  integer tone_symbols(46)

  call tbcc_init(JTTY_WAVA_NU)
  call pack_jtty(umsg,c32,nframes)
  nsym=0
  do i=1,nframes
    read(c32(i),'(34i1)') payload
    call tbcc_encode(payload,tone_symbols)
    ib=(i-1)*59+1   ! 59 tones per frame
    itone(ib:ib+12)=is13
    itone(ib+13:ib+58)=tone_symbols
    nsym=nsym+59
 enddo

 return
end subroutine genjtty
