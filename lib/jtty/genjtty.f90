subroutine genjtty(umsg,itone,nsym)

! Input:  character*80 umsg               !User message
! Output: integer*4 itone(1:nsym)         !Tones for channel symbols
!         integer*4 nsym                  !Number of channel symbols

  use jtty_mod
  use jtty_fec
  parameter (MAX_TONES=53*16)       !Max number of channel symbols
  character*80 umsg                 !User-formatted message 
  character*32 c32(16)
  integer itone(MAX_TONES)          !Array of tone frequencies for this message
  integer*1 message32(32)
  integer*1 codeword80(80)
  integer graymap(0:3)
  integer ib13(13)
  data graymap/0,1,3,2/
  data ib13/0,0,0,0,0,1,1,0,0,1,0,1,0/

  call pack_jtty(umsg,c32,nframes)
  nsym=0
  do i=1,nframes
    read(c32(i),'(32i1)') message32(1:32) 
    call encode_80_32(message32,codeword80)
    ib=(i-1)*53+1   ! 53 tones per frame
    ie=ib+52       
    itone(ib:ib+12)=ib13
    do j = 1, 40
       is=codeword80(2*j) + 2*codeword80(2*j-1)
       itone(ib+12+j) = graymap(is)
    enddo
    nsym=nsym+53
 enddo

 return
end subroutine genjtty
