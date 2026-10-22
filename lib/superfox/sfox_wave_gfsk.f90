subroutine sfox_wave_gfsk()

! Called by WSJT-X when it's time for SuperFox to transmit: the GFSK waveform
! of foxcom3's itone(1:151) into foxcom's wave.

  use sfox_tx, only: sfox_wave_for_tones
  parameter (NWAVE=(160+2)*134400*4) !Max WSJT-X waveform (FST4-1800 at 48kHz)
  character*40 cmsg2
  common/foxcom/wave(NWAVE)
  common/foxcom3/nslots2,cmsg2(5),itone3(151)

  wave=0.
  call sfox_wave_for_tones(itone3,wave)

  return
end subroutine sfox_wave_gfsk
